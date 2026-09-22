#include "hss/interruption_recovery.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace hss::recovery {
namespace {

bool valid_limits(const RecoveryLimits& limits) {
  return limits.max_records != 0U && limits.max_total_payload_bytes != 0U &&
         limits.max_payload_bytes_per_record != 0U && limits.max_interruption_markers != 0U &&
         limits.max_epoch_relationships != 0U &&
         limits.max_payload_bytes_per_record <= limits.max_total_payload_bytes;
}

bool valid_relationship(const AcceptedEpochRelationship& relationship) {
  return relationship.first_epoch != 0U && relationship.second_epoch != 0U &&
         relationship.first_epoch != relationship.second_epoch &&
         relationship.relationship_version != 0U;
}

bool relationship_authorizes(const AcceptedEpochRelationship& relationship,
                             uint32_t first_epoch, uint32_t second_epoch) {
  return (relationship.first_epoch == first_epoch && relationship.second_epoch == second_epoch) ||
         (relationship.first_epoch == second_epoch && relationship.second_epoch == first_epoch);
}

bool valid_request(const RecoveryRequest& request) {
  if (request.current_epoch == 0U ||
      !std::all_of(request.accepted_epoch_relationships.begin(),
                   request.accepted_epoch_relationships.end(), valid_relationship)) {
    return false;
  }
  for (size_t index = 0; index < request.accepted_epoch_relationships.size(); ++index) {
    for (size_t other = index + 1U; other < request.accepted_epoch_relationships.size(); ++other) {
      const auto& left = request.accepted_epoch_relationships[index];
      const auto& right = request.accepted_epoch_relationships[other];
      if (relationship_authorizes(left, right.first_epoch, right.second_epoch)) return false;
    }
  }
  return true;
}

struct InterruptionEvidence {
  bool detected{false};
  bool resource_limit{false};
};

InterruptionEvidence inspect_interruption_evidence(const storage::FileSystem& file_system,
                                                   const RecoveryLimits& limits) {
  InterruptionEvidence evidence;
  std::vector<std::string> paths;
  if (file_system.list_files("temporary", &paths)) {
    if (paths.size() > limits.max_interruption_markers) {
      evidence.resource_limit = true;
      return evidence;
    }
    evidence.detected = !paths.empty();
  } else {
    // A pending manifest can still be observed through a minimal filesystem
    // adapter that does not support directory enumeration.
    evidence.detected = file_system.exists("temporary/manifest.pending") ||
                        file_system.exists("temporary/previous.pending");
  }
  return evidence;
}

RecoveryReport no_valid_report(storage::StoreStatus status,
                               NoValidStateReason reason,
                               bool interrupted) {
  RecoveryReport report;
  report.outcome = RecoveryOutcome::kNoValidState;
  report.no_valid_state_reason = reason;
  report.store_status = status;
  report.interrupted_candidate_detected = interrupted;
  return report;
}

}  // namespace

InterruptionRecoveryCoordinator::InterruptionRecoveryCoordinator(
    storage::DurableSourceStore& store, storage::FileSystem& file_system,
    RecoveryLimits limits)
    : store_(store), file_system_(file_system), limits_(limits) {}

RecoveryReport InterruptionRecoveryCoordinator::recover_and_publish(
    const RecoveryRequest& request, StatePublisher& publisher) const {
  RecoveryReport report;
  if (!valid_limits(limits_)) {
    report.outcome = RecoveryOutcome::kInvalidConfiguration;
    report.store_status = storage::StoreStatus::kInvalidArgument;
    return report;
  }
  if (request.accepted_epoch_relationships.size() > limits_.max_epoch_relationships) {
    report.outcome = RecoveryOutcome::kResourceLimit;
    report.store_status = storage::StoreStatus::kResourceLimit;
    return report;
  }
  if (!valid_request(request)) {
    report.outcome = RecoveryOutcome::kInvalidConfiguration;
    report.store_status = storage::StoreStatus::kInvalidArgument;
    return report;
  }

  const InterruptionEvidence interruption = inspect_interruption_evidence(file_system_, limits_);
  report.interrupted_candidate_detected = interruption.detected;
  if (interruption.resource_limit) {
    report.outcome = RecoveryOutcome::kResourceLimit;
    report.store_status = storage::StoreStatus::kResourceLimit;
    return report;
  }

  storage::RecoveryResult recovered = store_.recover();
  report.store_status = recovered.status;
  report.used_previous_manifest = recovered.used_previous_manifest;
  if (recovered.status == storage::StoreStatus::kCorrupt) {
    return no_valid_report(recovered.status, NoValidStateReason::kAllCandidatesInvalid,
                           interruption.detected);
  }
  if (recovered.status == storage::StoreStatus::kResourceLimit) {
    report.outcome = RecoveryOutcome::kResourceLimit;
    return report;
  }
  if (recovered.status == storage::StoreStatus::kInvalidArgument) {
    report.outcome = RecoveryOutcome::kInvalidConfiguration;
    return report;
  }
  if (recovered.status != storage::StoreStatus::kOk) {
    report.outcome = RecoveryOutcome::kStorageFailure;
    return report;
  }
  if (recovered.revision == 0U) {
    return no_valid_report(recovered.status, NoValidStateReason::kNoCheckpoint,
                           interruption.detected);
  }

  report.revision = recovered.revision;
  report.record_count = recovered.records.size();
  if (recovered.records.size() > limits_.max_records) {
    report.outcome = RecoveryOutcome::kResourceLimit;
    report.store_status = storage::StoreStatus::kResourceLimit;
    return report;
  }

  uint64_t previous_sequence = 0U;
  bool has_previous_sequence = false;
  std::vector<uint32_t> durable_epochs;
  for (const storage::StoredRecord& record : recovered.records) {
    if (record.source.session_epoch == 0U ||
        record.payload_length > limits_.max_payload_bytes_per_record ||
        record.payload_length > limits_.max_total_payload_bytes - report.total_payload_bytes ||
        (has_previous_sequence && record.source.event_sequence <= previous_sequence)) {
      report.outcome = record.payload_length > limits_.max_payload_bytes_per_record ||
                               record.payload_length > limits_.max_total_payload_bytes -
                                                           report.total_payload_bytes
                           ? RecoveryOutcome::kResourceLimit
                           : RecoveryOutcome::kStorageFailure;
      report.store_status = report.outcome == RecoveryOutcome::kResourceLimit
                                ? storage::StoreStatus::kResourceLimit
                                : storage::StoreStatus::kCorrupt;
      return report;
    }
    report.total_payload_bytes += record.payload_length;
    previous_sequence = record.source.event_sequence;
    has_previous_sequence = true;
    durable_epochs.push_back(record.source.session_epoch);
  }
  std::sort(durable_epochs.begin(), durable_epochs.end());
  durable_epochs.erase(std::unique(durable_epochs.begin(), durable_epochs.end()),
                       durable_epochs.end());

  std::vector<AcceptedEpochRelationship> authorizing_relationships;
  for (uint32_t durable_epoch : durable_epochs) {
    if (durable_epoch == request.current_epoch) continue;
    const auto relationship = std::find_if(
        request.accepted_epoch_relationships.begin(), request.accepted_epoch_relationships.end(),
        [durable_epoch, &request](const AcceptedEpochRelationship& candidate) {
          return relationship_authorizes(candidate, durable_epoch, request.current_epoch);
        });
    if (relationship == request.accepted_epoch_relationships.end()) {
      report.blocked_epochs.push_back(durable_epoch);
    } else {
      authorizing_relationships.push_back(*relationship);
    }
  }
  if (!report.blocked_epochs.empty()) {
    report.outcome = RecoveryOutcome::kRelocalizationRequired;
    return report;
  }

  ValidatedRecoveryState state;
  state.revision = recovered.revision;
  state.current_epoch = request.current_epoch;
  state.used_previous_manifest = recovered.used_previous_manifest;
  state.interrupted_candidate_detected = interruption.detected;
  state.records = std::move(recovered.records);
  state.authorizing_relationships = std::move(authorizing_relationships);

  // This is the only state-publication point. Every storage, bound, ordering,
  // and epoch check above has completed before control reaches the publisher.
  if (!publisher.publish(state)) {
    report.outcome = RecoveryOutcome::kPublicationRejected;
    return report;
  }
  if (state.used_previous_manifest) {
    report.outcome = RecoveryOutcome::kPreviousValidatedAfterInvalidLatest;
  } else if (state.interrupted_candidate_detected) {
    report.outcome = RecoveryOutcome::kValidatedAfterInterruptedCandidate;
  } else {
    report.outcome = RecoveryOutcome::kLatestValidated;
  }
  return report;
}

}  // namespace hss::recovery

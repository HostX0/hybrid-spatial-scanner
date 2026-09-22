#include "hss/interruption_recovery.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

using hss::recovery::AcceptedEpochRelationship;
using hss::recovery::InterruptionRecoveryCoordinator;
using hss::recovery::NoValidStateReason;
using hss::recovery::RecoveryLimits;
using hss::recovery::RecoveryOutcome;
using hss::recovery::RecoveryRequest;
using hss::recovery::StatePublisher;
using hss::recovery::ValidatedRecoveryState;
using hss::storage::DurableSourceStore;
using hss::storage::NativeFileSystem;
using hss::storage::RetentionLimits;
using hss::storage::SourceKind;
using hss::storage::SourceRecord;
using hss::storage::StoreStatus;

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    char template_path[] = "/tmp/hss-b038-recovery-XXXXXX";
    char* created = ::mkdtemp(template_path);
    check(created != nullptr, "create temporary recovery directory");
    if (created != nullptr) path_ = created;
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

SourceRecord record(uint32_t epoch, uint64_t sequence, SourceKind kind,
                    std::vector<uint8_t> payload) {
  SourceRecord result;
  result.kind = kind;
  result.capture_id[0] = 0x38U;
  result.capture_id[15] = static_cast<uint8_t>(sequence);
  result.session_epoch = epoch;
  result.event_sequence = sequence;
  result.monotonic_time_s = static_cast<double>(sequence) / 10.0;
  result.payload = std::move(payload);
  if (kind == SourceKind::kMesh) {
    result.anchor_id[0] = 0x83U;
    result.anchor_id[15] = static_cast<uint8_t>(sequence);
    result.source_revision = sequence;
  }
  return result;
}

class RecordingPublisher final : public StatePublisher {
 public:
  bool publish(const ValidatedRecoveryState& state) noexcept override {
    ++publish_count;
    last_revision = state.revision;
    last_current_epoch = state.current_epoch;
    last_used_previous = state.used_previous_manifest;
    last_interrupted = state.interrupted_candidate_detected;
    record_epochs.clear();
    for (const auto& stored : state.records) {
      record_epochs.push_back(stored.source.session_epoch);
    }
    relationship_count = state.authorizing_relationships.size();
    return accept;
  }

  bool accept{true};
  uint64_t publish_count{0};
  uint64_t last_revision{0};
  uint32_t last_current_epoch{0};
  bool last_used_previous{false};
  bool last_interrupted{false};
  size_t relationship_count{0};
  std::vector<uint32_t> record_epochs;
};

RetentionLimits store_limits() {
  RetentionLimits limits;
  limits.max_depth_records = 8U;
  limits.max_mesh_records = 8U;
  limits.max_retained_bytes = 1024U;
  limits.max_chunk_bytes = 512U;
  return limits;
}

void test_valid_latest_state_publishes_once() {
  TemporaryDirectory directory;
  NativeFileSystem file_system(directory.path());
  DurableSourceStore store(file_system, store_limits());
  check(store.checkpoint({record(3U, 1U, SourceKind::kDepth, {1U, 2U})}) ==
            StoreStatus::kOk,
        "create valid latest recovery state");

  RecordingPublisher publisher;
  InterruptionRecoveryCoordinator coordinator(store, file_system);
  const auto report = coordinator.recover_and_publish({.current_epoch = 3U}, publisher);
  check(report.outcome == RecoveryOutcome::kLatestValidated,
        "latest fully validated state has explicit outcome");
  check(report.revision == 1U && report.record_count == 1U &&
            report.total_payload_bytes == 2U,
        "latest report exposes bounded metadata");
  check(publisher.publish_count == 1U && publisher.last_revision == 1U &&
            publisher.record_epochs == std::vector<uint32_t>{3U},
        "valid latest state publishes exactly once after validation");
}

void test_interrupted_candidate_is_reported_and_preserved() {
  TemporaryDirectory directory;
  NativeFileSystem file_system(directory.path());
  DurableSourceStore store(file_system, store_limits());
  check(store.checkpoint({record(4U, 1U, SourceKind::kDepth, {4U})}) ==
            StoreStatus::kOk,
        "create committed state before interruption marker");
  const std::vector<uint8_t> partial_manifest{'H', 'S', 'S', '_'};
  check(file_system.write_file("temporary/manifest.pending", partial_manifest),
        "leave incomplete candidate manifest");

  RecordingPublisher publisher;
  InterruptionRecoveryCoordinator coordinator(store, file_system);
  const auto report = coordinator.recover_and_publish({.current_epoch = 4U}, publisher);
  check(report.outcome == RecoveryOutcome::kValidatedAfterInterruptedCandidate &&
            report.interrupted_candidate_detected,
        "incomplete newest candidate is visible while committed state survives");
  check(publisher.publish_count == 1U && publisher.last_interrupted,
        "only the validated committed state is published");
  std::vector<uint8_t> retained_marker;
  check(file_system.read_file_bounded("temporary/manifest.pending", 32U, &retained_marker) &&
            retained_marker == partial_manifest,
        "recovery performs no destructive cleanup of interruption evidence");
}

void test_corrupt_newest_falls_back_without_rewriting_evidence() {
  TemporaryDirectory directory;
  NativeFileSystem file_system(directory.path());
  DurableSourceStore store(file_system, store_limits());
  check(store.checkpoint({record(5U, 1U, SourceKind::kDepth, {1U})}) ==
            StoreStatus::kOk,
        "create fallback revision");
  check(store.checkpoint({record(5U, 2U, SourceKind::kMesh, {2U})}) ==
            StoreStatus::kOk,
        "create newest revision");
  const std::vector<uint8_t> corrupt_active{0x00U, 0x38U, 0xffU};
  check(file_system.write_file("active.manifest", corrupt_active),
        "truncate and corrupt newest manifest");

  RecordingPublisher publisher;
  InterruptionRecoveryCoordinator coordinator(store, file_system);
  const auto report = coordinator.recover_and_publish({.current_epoch = 5U}, publisher);
  check(report.outcome == RecoveryOutcome::kPreviousValidatedAfterInvalidLatest &&
            report.used_previous_manifest && report.revision == 1U,
        "invalid newest state returns explicit validated fallback");
  check(publisher.publish_count == 1U && publisher.last_used_previous &&
            publisher.last_revision == 1U,
        "publisher sees only the validated previous revision");
  std::vector<uint8_t> retained_corruption;
  check(file_system.read_file_bounded("active.manifest", 32U, &retained_corruption) &&
            retained_corruption == corrupt_active,
        "fallback does not rewrite corrupt newest evidence");
}

void test_no_valid_state_never_publishes() {
  TemporaryDirectory empty_directory;
  NativeFileSystem empty_file_system(empty_directory.path());
  DurableSourceStore empty_store(empty_file_system, store_limits());
  RecordingPublisher empty_publisher;
  InterruptionRecoveryCoordinator empty_coordinator(empty_store, empty_file_system);
  const auto absent =
      empty_coordinator.recover_and_publish({.current_epoch = 6U}, empty_publisher);
  check(absent.outcome == RecoveryOutcome::kNoValidState &&
            absent.no_valid_state_reason == NoValidStateReason::kNoCheckpoint &&
            empty_publisher.publish_count == 0U,
        "manifest absence is an explicit no-state result with no publication");

  TemporaryDirectory corrupt_directory;
  NativeFileSystem corrupt_file_system(corrupt_directory.path());
  DurableSourceStore corrupt_store(corrupt_file_system, store_limits());
  check(corrupt_store.checkpoint({record(6U, 1U, SourceKind::kDepth, {6U})}) ==
            StoreStatus::kOk,
        "create only candidate before total corruption");
  check(corrupt_file_system.write_file("active.manifest", {0x00U}),
        "corrupt only durable candidate");
  RecordingPublisher corrupt_publisher;
  InterruptionRecoveryCoordinator corrupt_coordinator(corrupt_store,
                                                       corrupt_file_system);
  const auto corrupt =
      corrupt_coordinator.recover_and_publish({.current_epoch = 6U}, corrupt_publisher);
  check(corrupt.outcome == RecoveryOutcome::kNoValidState &&
            corrupt.no_valid_state_reason == NoValidStateReason::kAllCandidatesInvalid &&
            corrupt.store_status == StoreStatus::kCorrupt &&
            corrupt_publisher.publish_count == 0U,
        "all-invalid candidates fail closed before callbacks");
}

void test_epoch_mismatch_blocks_until_explicit_relationship() {
  TemporaryDirectory directory;
  NativeFileSystem file_system(directory.path());
  DurableSourceStore store(file_system, store_limits());
  check(store.checkpoint({record(7U, 1U, SourceKind::kDepth, {7U})}) ==
            StoreStatus::kOk,
        "create durable state from earlier epoch");

  RecordingPublisher blocked_publisher;
  InterruptionRecoveryCoordinator coordinator(store, file_system);
  const auto blocked =
      coordinator.recover_and_publish({.current_epoch = 8U}, blocked_publisher);
  check(blocked.outcome == RecoveryOutcome::kRelocalizationRequired &&
            blocked.blocked_epochs == std::vector<uint32_t>{7U} &&
            blocked_publisher.publish_count == 0U,
        "epoch mismatch blocks merge and requests relocalization");

  RecordingPublisher authorized_publisher;
  RecoveryRequest authorized;
  authorized.current_epoch = 8U;
  authorized.accepted_epoch_relationships.push_back(
      AcceptedEpochRelationship{.first_epoch = 7U,
                                .second_epoch = 8U,
                                .relationship_version = 1U});
  const auto allowed = coordinator.recover_and_publish(authorized, authorized_publisher);
  check(allowed.outcome == RecoveryOutcome::kLatestValidated &&
            authorized_publisher.publish_count == 1U &&
            authorized_publisher.relationship_count == 1U,
        "explicit accepted relationship authorizes source publication");
  check(authorized_publisher.record_epochs == std::vector<uint32_t>{7U} &&
            authorized_publisher.last_current_epoch == 8U,
        "authorization never rewrites source epoch or invents geometry");
}

void test_order_resource_and_publication_failures_are_atomic() {
  TemporaryDirectory directory;
  NativeFileSystem file_system(directory.path());
  DurableSourceStore store(file_system, store_limits());
  check(store.checkpoint({record(9U, 1U, SourceKind::kDepth, {1U, 1U})}) ==
            StoreStatus::kOk,
        "create ordered baseline");
  check(store.checkpoint({record(9U, 3U, SourceKind::kMesh, {3U}),
                          record(9U, 2U, SourceKind::kDepth, {2U})}) ==
            StoreStatus::kInvalidArgument,
        "out-of-order newest work is rejected before commit");
  check(store.checkpoint({record(9U, 2U, SourceKind::kDepth, {2U}),
                          record(9U, 2U, SourceKind::kMesh, {2U})}) ==
            StoreStatus::kInvalidArgument,
        "duplicate event order is rejected before commit");

  RecordingPublisher ordered_publisher;
  InterruptionRecoveryCoordinator coordinator(store, file_system);
  const auto ordered =
      coordinator.recover_and_publish({.current_epoch = 9U}, ordered_publisher);
  check(ordered.outcome == RecoveryOutcome::kLatestValidated && ordered.revision == 1U &&
            ordered.record_count == 1U && ordered_publisher.publish_count == 1U,
        "order failures preserve the last committed state");

  check(store.checkpoint({record(9U, 2U, SourceKind::kMesh, {2U, 2U})}) ==
            StoreStatus::kOk,
        "create two-record state for recovery resource bound");
  RecoveryLimits tight;
  tight.max_records = 1U;
  tight.max_total_payload_bytes = 8U;
  tight.max_payload_bytes_per_record = 8U;
  RecordingPublisher bounded_publisher;
  InterruptionRecoveryCoordinator bounded(store, file_system, tight);
  const auto bounded_report =
      bounded.recover_and_publish({.current_epoch = 9U}, bounded_publisher);
  check(bounded_report.outcome == RecoveryOutcome::kResourceLimit &&
            bounded_publisher.publish_count == 0U,
        "recovery record limit fails before publication");

  RecordingPublisher rejecting_publisher;
  rejecting_publisher.accept = false;
  const auto rejected =
      coordinator.recover_and_publish({.current_epoch = 9U}, rejecting_publisher);
  check(rejected.outcome == RecoveryOutcome::kPublicationRejected &&
            rejecting_publisher.publish_count == 1U,
        "publisher rejection remains explicit after full validation");
}

}  // namespace

int main() {
  test_valid_latest_state_publishes_once();
  test_interrupted_candidate_is_reported_and_preserved();
  test_corrupt_newest_falls_back_without_rewriting_evidence();
  test_no_valid_state_never_publishes();
  test_epoch_mismatch_blocks_until_explicit_relationship();
  test_order_resource_and_publication_failures_are_atomic();
  if (failures != 0) {
    std::cerr << failures << " interruption recovery test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "interruption recovery checks passed\n";
  return EXIT_SUCCESS;
}

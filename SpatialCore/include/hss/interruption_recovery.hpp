#ifndef HSS_INTERRUPTION_RECOVERY_HPP
#define HSS_INTERRUPTION_RECOVERY_HPP

#include "hss/durable_source_store.hpp"

#include <cstdint>
#include <vector>

namespace hss::recovery {

// Recovery never estimates or applies an epoch transform. This token only
// records that the caller's B-012 policy has already accepted a relationship.
struct AcceptedEpochRelationship {
  uint32_t first_epoch{0};
  uint32_t second_epoch{0};
  uint64_t relationship_version{0};
};

struct RecoveryRequest {
  uint32_t current_epoch{0};
  std::vector<AcceptedEpochRelationship> accepted_epoch_relationships;
};

struct RecoveryLimits {
  uint64_t max_records{384};
  uint64_t max_total_payload_bytes{256U * 1024U * 1024U};
  uint64_t max_payload_bytes_per_record{256U * 1024U * 1024U};
  uint64_t max_interruption_markers{20'000};
  uint64_t max_epoch_relationships{384};
};

enum class RecoveryOutcome : uint8_t {
  kLatestValidated,
  kValidatedAfterInterruptedCandidate,
  kPreviousValidatedAfterInvalidLatest,
  kNoValidState,
  kRelocalizationRequired,
  kResourceLimit,
  kInvalidConfiguration,
  kStorageFailure,
  kPublicationRejected,
};

enum class NoValidStateReason : uint8_t {
  kNone,
  kNoCheckpoint,
  kAllCandidatesInvalid,
};

struct ValidatedRecoveryState {
  uint64_t revision{0};
  uint32_t current_epoch{0};
  bool used_previous_manifest{false};
  bool interrupted_candidate_detected{false};
  // Metadata only: DurableSourceStore has already validated every referenced
  // chunk and deliberately returns no borrowed payload buffers.
  std::vector<storage::StoredRecord> records;
  std::vector<AcceptedEpochRelationship> authorizing_relationships;
};

class StatePublisher {
 public:
  virtual ~StatePublisher() = default;
  virtual bool publish(const ValidatedRecoveryState& state) noexcept = 0;
};

struct RecoveryReport {
  RecoveryOutcome outcome{RecoveryOutcome::kStorageFailure};
  NoValidStateReason no_valid_state_reason{NoValidStateReason::kNone};
  storage::StoreStatus store_status{storage::StoreStatus::kOk};
  uint64_t revision{0};
  uint64_t record_count{0};
  uint64_t total_payload_bytes{0};
  bool used_previous_manifest{false};
  bool interrupted_candidate_detected{false};
  // Epochs listed here remain blocked. The coordinator never treats them as
  // current or creates replacement geometry.
  std::vector<uint32_t> blocked_epochs;
};

// Additive facade over B-007. It observes recovery artifacts, validates bounds
// and epoch compatibility, and only then invokes StatePublisher. It never
// removes temporary/orphan/corrupt evidence and never changes the store.
class InterruptionRecoveryCoordinator {
 public:
  InterruptionRecoveryCoordinator(storage::DurableSourceStore& store,
                                  storage::FileSystem& file_system,
                                  RecoveryLimits limits = {});

  RecoveryReport recover_and_publish(const RecoveryRequest& request,
                                     StatePublisher& publisher) const;

 private:
  storage::DurableSourceStore& store_;
  storage::FileSystem& file_system_;
  RecoveryLimits limits_;
};

}  // namespace hss::recovery

#endif

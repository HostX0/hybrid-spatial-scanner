#ifndef HSS_RESOURCE_POLICY_HPP
#define HSS_RESOURCE_POLICY_HPP

#include <cstdint>

namespace hss::resource {

constexpr uint64_t kMaximumStorageThresholdBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMaximumRecoverySamples = 1024U;

enum class Status : uint8_t {
  kOk,
  kInvalidConfiguration,
  kInvalidMeasurement,
  kOutOfOrder,
  kInvalidTransition,
  kInvalidEventKind,
};

// Status is deliberately separate from the reported value. An unmeasured
// sensor is not a nominal device reading and cannot clear a latched pressure
// state. Unmeasured values must use their canonical zero/nominal placeholder.
enum class MeasurementStatus : uint8_t { kUnmeasured, kMeasured };
enum class ThermalState : uint8_t { kNominal, kFair, kSerious, kCritical };
enum class MemoryPressure : uint8_t { kNominal, kWarning, kCritical };

struct ThermalMeasurement {
  MeasurementStatus status{MeasurementStatus::kUnmeasured};
  ThermalState state{ThermalState::kNominal};
};

struct MemoryMeasurement {
  MeasurementStatus status{MeasurementStatus::kUnmeasured};
  MemoryPressure pressure{MemoryPressure::kNominal};
};

struct StorageMeasurement {
  MeasurementStatus status{MeasurementStatus::kUnmeasured};
  uint64_t free_bytes{0};
};

struct ResourceSample {
  uint64_t sequence{0};
  double monotonic_time_s{0.0};
  ThermalMeasurement thermal;
  MemoryMeasurement memory;
  StorageMeasurement storage;
};

struct PolicyConfig {
  // These reserves are explicit tuning inputs, not device capability claims.
  uint64_t storage_stop_bytes{512ULL * 1024ULL * 1024ULL};
  uint64_t storage_warning_bytes{1024ULL * 1024ULL * 1024ULL};
  uint64_t storage_recovery_bytes{1536ULL * 1024ULL * 1024ULL};
  uint32_t recovery_samples{3U};
};

enum class PolicyMode : uint8_t {
  kNormal,
  kConserving,
  kCheckpointPending,
  kPaused,
};

enum class PressureLevel : uint8_t { kNone, kConstrained, kUnsafe };

enum class Reason : uint8_t {
  kNominal,
  kMeasurementsUnavailable,
  kThermalFair,
  kThermalSerious,
  kThermalCritical,
  kMemoryWarning,
  kMemoryCritical,
  kStorageWarning,
  kStorageStop,
  kRecoveryHysteresis,
  kCheckpointSucceeded,
  kCheckpointFailed,
  kInvalidConfiguration,
  kInvalidMeasurement,
  kOutOfOrder,
  kInvalidTransition,
};

struct Decision {
  Status status{Status::kOk};
  PolicyMode mode{PolicyMode::kNormal};
  PressureLevel pressure{PressureLevel::kNone};
  Reason reason{Reason::kNominal};
  bool monitoring_complete{false};
  bool reduce_preview_quality{false};
  bool suspend_optional_appearance{false};
  bool reduce_unprotected_display_lod{false};
  bool validity_checks_enabled{true};
  bool accept_metric_observations{true};
  bool request_checkpoint{false};
  bool pause_capture{false};
  // The policy has no source-eviction result. Existing observations remain
  // retained even when new capture must pause.
  bool retain_existing_sources{true};
};

struct Counters {
  uint64_t accepted_samples{0};
  uint64_t rejected_samples{0};
  uint64_t unmeasured_samples{0};
  uint64_t constrained_samples{0};
  uint64_t unsafe_samples{0};
  uint64_t pressure_escalations{0};
  uint64_t pressure_recoveries{0};
  uint64_t checkpoint_requests{0};
  uint64_t checkpoint_successes{0};
  uint64_t checkpoint_failures{0};
  uint64_t dropped_preview_events{0};
  uint64_t deferred_optional_events{0};
  uint64_t source_pause_events{0};
  uint64_t protected_events_accepted{0};
};

enum class CheckpointResult : uint8_t { kSucceeded, kFailed };

// Commands, epoch transitions, removals and durable completions are the
// protected B-013/D15 control path. They never receive a drop/defer action.
enum class EventKind : uint8_t {
  // Presentation-only and expendable. MetricObservation is the retained-source
  // path; dropping this never authorizes coordinator source deletion or loss.
  kPreviewFrame,
  kOptionalAppearance,
  kUnprotectedDisplayLod,
  kMetricObservation,
  kCommand,
  kEpochTransition,
  kMeshRemoval,
  kDurableCommit,
};

enum class EventAction : uint8_t {
  kProcess,
  kDropPreview,
  kDeferOptional,
  kRetainSourceAndPauseCapture,
  kAcceptProtected,
};

struct EventDecision {
  Status status{Status::kOk};
  EventAction action{EventAction::kProcess};
  bool protected_event{false};
  bool retain_existing_sources{true};
};

class ResourcePolicy {
 public:
  explicit ResourcePolicy(PolicyConfig config = {});

  [[nodiscard]] bool valid() const { return configuration_valid_; }
  [[nodiscard]] PolicyMode mode() const { return mode_; }
  [[nodiscard]] const Counters& counters() const { return counters_; }

  // Applies one fully explicit, ordered device sample. Invalid or out-of-order
  // input leaves all pressure state unchanged.
  Decision observe(const ResourceSample& sample);

  // Completes the B-038 checkpoint handshake. Either result pauses capture;
  // a failed checkpoint never by itself authorizes source eviction or resumed
  // intake.
  Decision complete_checkpoint(CheckpointResult result);

  // Returns and records the current admission policy. Calls represent actual
  // event disposition attempts, so drop/defer/protected counters advance here.
  EventDecision decide_event(EventKind kind);

 private:
  Decision decision(Status status, Reason reason, bool monitoring_complete) const;

  PolicyConfig config_;
  bool configuration_valid_{false};
  PolicyMode mode_{PolicyMode::kNormal};
  PressureLevel pressure_{PressureLevel::kNone};
  uint64_t last_sequence_{0};
  double last_time_s_{0.0};
  bool has_sample_{false};
  uint32_t recovery_count_{0};
  Counters counters_;
};

}  // namespace hss::resource

#endif

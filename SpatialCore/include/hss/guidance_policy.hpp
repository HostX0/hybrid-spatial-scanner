#pragma once

#include <cstdint>
#include <optional>

namespace hss::guidance {

// This module selects presentation policy from already-measured quality facts.
// It neither estimates geometry nor schedules capture, haptics, or rendering.
enum class TrackingState : uint8_t { kNormal, kLimited, kUnavailable, kRelocalizing };
enum class ResourceState : uint8_t { kAvailable, kConstrained, kExhausted };
enum class IssueCode : uint8_t {
  kNone,
  kStabilizeTracking,
  kPreserveResources,
  kResolveConflict,
  kAcquireDepth,
  kObserveFeatureSide,
  kChangeViewpoint,
  kRevisitUnstableArea,
  kCompleteTargetCoverage,
  kProcessingPending,
};
enum class Status : uint8_t { kOk, kInvalidInput, kInvalidTuning };

// All scalar fields are normalized quality quantities in [0, 1]. The caller
// supplies their measurement method and target-domain revision; this policy
// deliberately has no sensor thresholds or physical-distance instructions.
struct QualityVector {
  uint64_t revision{0};
  TrackingState tracking{TrackingState::kUnavailable};
  ResourceState resources{ResourceState::kAvailable};
  bool relocalization_unresolved{false};
  double valid_depth_fraction{0.0};
  double support_extent{0.0};
  double independent_view_diversity{0.0};
  double fit_quality{0.0};
  double change_stability{0.0};
  uint64_t target_domain_revision{0};
  double target_coverage{0.0};
  double occlusion_suspicion{0.0};
  bool has_conflict{false};
  bool processing_pending{false};
};

// Debounce, minimum display, haptic suppression, and enter/exit hysteresis
// are caller-declared so a UI and its policy owner can share one profile.
struct Tuning {
  double noncritical_debounce_s{0.0};
  double minimum_display_s{0.0};
  double repeated_haptic_s{0.0};
  double low_quality_enter{0.0};
  double low_quality_exit{0.0};
  double high_suspicion_enter{0.0};
  double high_suspicion_exit{0.0};
};

struct PresentedIssue {
  IssueCode code{IssueCode::kNone};
  uint64_t evidence_revision{0};
  // The issue becomes stale when the next quality revision is evaluated.
  uint64_t expires_before_revision{0};
  bool precise_overlays_hidden{false};
  bool haptic_allowed{false};
  bool operator==(const PresentedIssue&) const = default;
};

struct State {
  std::optional<PresentedIssue> displayed;
  double displayed_since_s{0.0};
  double candidate_since_s{0.0};
  IssueCode candidate{IssueCode::kNone};
  double last_haptic_s{0.0};
  bool has_haptic_history{false};
  uint64_t last_revision{0};
  bool operator==(const State&) const = default;
};

struct Result {
  Status status{Status::kInvalidInput};
  // On failure, next_state equals the supplied state and issue is absent.
  State next_state{};
  std::optional<PresentedIssue> issue;
};

const char* status_name(Status status);
// now_s is a caller-provided monotonic timestamp used only for caller-declared
// display policy. It makes no frame-time, response-time, or device claim.
Result evaluate(const QualityVector& quality, const Tuning& tuning,
                const State& previous, double now_s);

}  // namespace hss::guidance

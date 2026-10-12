#include "hss/guidance_policy.hpp"

#include <cmath>
#include <limits>

namespace hss::guidance {
namespace {
bool unit(double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }
bool nonnegative(double value) { return std::isfinite(value) && value >= 0.0; }
bool tracking_valid(TrackingState value) {
  return value == TrackingState::kNormal || value == TrackingState::kLimited ||
         value == TrackingState::kUnavailable || value == TrackingState::kRelocalizing;
}
bool resources_valid(ResourceState value) {
  return value == ResourceState::kAvailable || value == ResourceState::kConstrained ||
         value == ResourceState::kExhausted;
}
bool valid_quality(const QualityVector& q, double now_s) {
  return q.revision != 0U && tracking_valid(q.tracking) && resources_valid(q.resources) &&
         q.revision != std::numeric_limits<uint64_t>::max() && (q.measured_facts & ~kAllQualityFacts) == 0 &&
         unit(q.valid_depth_fraction) && unit(q.support_extent) &&
         unit(q.independent_view_diversity) && unit(q.fit_quality) &&
         unit(q.change_stability) && unit(q.target_coverage) &&
         unit(q.occlusion_suspicion) && std::isfinite(now_s);
}
bool valid_tuning(const Tuning& t) {
  return nonnegative(t.noncritical_debounce_s) && nonnegative(t.minimum_display_s) &&
         nonnegative(t.repeated_haptic_s) && unit(t.low_quality_enter) &&
         unit(t.low_quality_exit) && unit(t.high_suspicion_enter) &&
         unit(t.high_suspicion_exit) && t.low_quality_enter <= t.low_quality_exit &&
         t.high_suspicion_exit <= t.high_suspicion_enter;
}
bool low(double value, IssueCode current, IssueCode code, const Tuning& t) {
  return value < (current == code ? t.low_quality_exit : t.low_quality_enter);
}
bool high(double value, IssueCode current, IssueCode code, const Tuning& t) {
  return value >= (current == code ? t.high_suspicion_exit : t.high_suspicion_enter);
}
IssueCode dominant(const QualityVector& q, IssueCode current, const Tuning& t) {
  if (q.relocalization_unresolved || q.tracking != TrackingState::kNormal) return IssueCode::kStabilizeTracking;
  if (q.resources != ResourceState::kAvailable) return IssueCode::kPreserveResources;
  if (q.has_conflict) return IssueCode::kResolveConflict;
  if ((q.measured_facts & kDepthMeasured) && low(q.valid_depth_fraction, current, IssueCode::kAcquireDepth, t)) return IssueCode::kAcquireDepth;
  if ((q.measured_facts & kOcclusionMeasured) && high(q.occlusion_suspicion, current, IssueCode::kObserveFeatureSide, t)) return IssueCode::kObserveFeatureSide;
  if ((q.measured_facts & kViewsMeasured) && low(q.independent_view_diversity, current, IssueCode::kChangeViewpoint, t)) return IssueCode::kChangeViewpoint;
  if (((q.measured_facts & kSupportMeasured) && low(q.support_extent, current, IssueCode::kRevisitUnstableArea, t)) ||
      ((q.measured_facts & kFitMeasured) && low(q.fit_quality, current, IssueCode::kRevisitUnstableArea, t)) ||
      ((q.measured_facts & kStabilityMeasured) && low(q.change_stability, current, IssueCode::kRevisitUnstableArea, t))) return IssueCode::kRevisitUnstableArea;
  if ((q.measured_facts & kCoverageMeasured) && q.target_domain_revision != 0U && low(q.target_coverage, current, IssueCode::kCompleteTargetCoverage, t)) return IssueCode::kCompleteTargetCoverage;
  if (q.processing_pending) return IssueCode::kProcessingPending;
  return IssueCode::kNone;
}
PresentedIssue make_issue(IssueCode code, const QualityVector& q, bool haptic) {
  return {code, q.revision, q.revision == std::numeric_limits<uint64_t>::max() ? q.revision : q.revision + 1U,
          q.relocalization_unresolved || q.tracking != TrackingState::kNormal, haptic};
}
}  // namespace

const char* status_name(Status status) {
  switch (status) { case Status::kOk: return "ok"; case Status::kInvalidInput: return "invalid_input"; case Status::kInvalidTuning: return "invalid_tuning"; }
  return "invalid_status";
}

Result evaluate(const QualityVector& q, const Tuning& t, const State& previous, double now_s) {
  Result result; result.next_state = previous;
  if (!valid_quality(q, now_s) || q.revision <= previous.last_revision ||
      (previous.displayed && (!nonnegative(previous.displayed_since_s) || previous.displayed_since_s > now_s)) ||
      (!nonnegative(previous.candidate_since_s) || previous.candidate_since_s > now_s) ||
      (previous.has_haptic_history && (!nonnegative(previous.last_haptic_s) || previous.last_haptic_s > now_s))) {
    result.status = Status::kInvalidInput; return result;
  }
  if (!valid_tuning(t)) { result.status = Status::kInvalidTuning; return result; }
  const IssueCode current = previous.displayed ? previous.displayed->code : IssueCode::kNone;
  const IssueCode next = dominant(q, current, t);
  result.next_state.last_revision = q.revision;
  const bool critical = next == IssueCode::kStabilizeTracking ||
                        (next == IssueCode::kPreserveResources && q.resources == ResourceState::kExhausted);
  if (next == IssueCode::kNone) { result.status = Status::kOk; result.next_state.displayed.reset(); result.next_state.candidate = next; result.next_state.candidate_since_s = now_s; return result; }
  if (previous.displayed && previous.displayed->code == next) {
    const bool haptic = next == IssueCode::kStabilizeTracking && (!previous.has_haptic_history || now_s - previous.last_haptic_s >= t.repeated_haptic_s);
    result.issue = make_issue(next, q, haptic); result.next_state.displayed = result.issue;
    if (haptic) { result.next_state.last_haptic_s = now_s; result.next_state.has_haptic_history = true; }
    result.status = Status::kOk; return result;
  }
  const double candidate_since = previous.candidate == next ? previous.candidate_since_s : now_s;
  result.next_state.candidate = next; result.next_state.candidate_since_s = candidate_since;
  const bool minimum_holds = previous.displayed && now_s - previous.displayed_since_s < t.minimum_display_s;
  if (!critical && (now_s - candidate_since < t.noncritical_debounce_s || minimum_holds)) {
    // Previous evidence expires at the new quality revision. Do not prolong or
    // relabel an obsolete instruction just to satisfy presentation timing.
    result.next_state.displayed.reset(); result.status = Status::kOk; return result;
  }
  const bool haptic = next == IssueCode::kStabilizeTracking && (!previous.has_haptic_history || now_s - previous.last_haptic_s >= t.repeated_haptic_s);
  result.issue = make_issue(next, q, haptic); result.next_state.displayed = result.issue; result.next_state.displayed_since_s = now_s;
  if (haptic) { result.next_state.last_haptic_s = now_s; result.next_state.has_haptic_history = true; }
  result.status = Status::kOk; return result;
}
}  // namespace hss::guidance

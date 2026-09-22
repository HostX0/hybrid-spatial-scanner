#include "hss/guidance_policy.hpp"

#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace hss::guidance;
int failures = 0;
void check(bool condition, const std::string& message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
Tuning tuning() { return {0.8, 2.0, 8.0, 0.4, 0.5, 0.7, 0.6}; }
QualityVector quality(uint64_t revision) {
  QualityVector q; q.revision = revision; q.tracking = TrackingState::kNormal;
  q.valid_depth_fraction = q.support_extent = q.independent_view_diversity = q.fit_quality = q.change_stability = 0.9;
  q.target_domain_revision = 7; q.target_coverage = 0.9; q.occlusion_suspicion = 0.1; return q;
}
void test_priority_and_relocalization_precision() {
  State state; auto q = quality(1); q.target_coverage = 0.1; q.resources = ResourceState::kExhausted;
  auto r = evaluate(q, tuning(), state, 10.0);
  state = r.next_state; q.revision = 2;
  r = evaluate(q, tuning(), state, 10.8);
  check(r.status == Status::kOk && r.issue && r.issue->code == IssueCode::kPreserveResources,
        "resource validity outranks completeness");
  state = r.next_state; q = quality(3); q.relocalization_unresolved = true; q.resources = ResourceState::kExhausted;
  r = evaluate(q, tuning(), state, 10.9);
  check(r.issue && r.issue->code == IssueCode::kStabilizeTracking && r.issue->precise_overlays_hidden && r.issue->haptic_allowed,
        "unresolved relocalization dominates and hides stale precision");
  check(r.issue->evidence_revision == 3 && r.issue->expires_before_revision == 4,
        "issue carries evidence revision and explicit expiry revision");
}
void test_debounce_hysteresis_and_processing() {
  State state; auto q = quality(1); q.valid_depth_fraction = 0.2;
  auto r = evaluate(q, tuning(), state, 0.0);
  check(!r.issue, "noncritical issue is caller-debounced"); state = r.next_state;
  q.revision = 2; r = evaluate(q, tuning(), state, 0.8);
  check(r.issue && r.issue->code == IssueCode::kAcquireDepth, "debounced low depth becomes actionable"); state = r.next_state;
  q = quality(3); q.valid_depth_fraction = 0.45;
  r = evaluate(q, tuning(), state, 1.0);
  check(r.issue && r.issue->code == IssueCode::kAcquireDepth, "exit hysteresis prevents low-depth flapping"); state = r.next_state;
  q = quality(4); q.valid_depth_fraction = 0.6; q.processing_pending = true;
  r = evaluate(q, tuning(), state, 3.0);
  state = r.next_state; q.revision = 5;
  r = evaluate(q, tuning(), state, 3.9);
  check(r.issue && r.issue->code == IssueCode::kProcessingPending, "waiting issue occurs only for declared processing work");
}
void test_specific_actions_and_atomic_rejection() {
  State state; auto q = quality(1); q.occlusion_suspicion = 0.8;
  auto r = evaluate(q, tuning(), state, 1.0);
  state = r.next_state; q.revision = 2;
  r = evaluate(q, tuning(), state, 1.8);
  check(r.issue && r.issue->code == IssueCode::kObserveFeatureSide, "occlusion selects a feature-side action rather than a distance claim");
  state = r.next_state; const State before = state;
  q.revision = 3; q.fit_quality = std::numeric_limits<double>::quiet_NaN();
  r = evaluate(q, tuning(), state, 2.0);
  check(r.status == Status::kInvalidInput && !r.issue && r.next_state == before, "nonfinite input fails atomically");
  q = quality(3); auto bad = tuning(); bad.low_quality_enter = 0.7; bad.low_quality_exit = 0.3;
  r = evaluate(q, bad, state, 2.0);
  check(r.status == Status::kInvalidTuning && !r.issue && r.next_state == before, "invalid hysteresis tuning fails atomically");
}
}  // namespace
int main() { test_priority_and_relocalization_precision(); test_debounce_hysteresis_and_processing(); test_specific_actions_and_atomic_rejection(); return failures == 0 ? 0 : 1; }

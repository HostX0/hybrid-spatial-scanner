#include "hss/guidance_policy.hpp"
#include "hss/hss_guidance.h"
static_assert(static_cast<uint32_t>(HSS_GUIDANCE_DEPTH_MEASURED) == static_cast<uint32_t>(hss::guidance::kDepthMeasured));
static_assert(static_cast<uint32_t>(HSS_GUIDANCE_ALL_FACTS) == static_cast<uint32_t>(hss::guidance::kAllQualityFacts));

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
void test_changed_evidence_never_retains_expired_display() {
  State state;
  auto q=quality(1); q.valid_depth_fraction=.1;
  state=evaluate(q,tuning(),state,0).next_state;
  q.revision=2;
  state=evaluate(q,tuning(),state,1).next_state;
  check(state.displayed && state.displayed->code==IssueCode::kAcquireDepth, "baseline depth guidance displayed");
  q=quality(3); q.occlusion_suspicion=.9;
  const auto r=evaluate(q,tuning(),state,1.1);
  check(r.status==Status::kOk && !r.issue && !r.next_state.displayed,
        "changed quality must suppress expired depth guidance during debounce/minimum display");
}
void test_partial_measurements_and_urgent_resource_state() {
  auto q=quality(1); q.measured_facts=kDepthMeasured;
  q.support_extent=q.fit_quality=q.change_stability=q.target_coverage=q.occlusion_suspicion=q.independent_view_diversity=0;
  auto r=evaluate(q,tuning(),{},0);
  check(r.status==Status::kOk && !r.issue, "unmeasured fit/stability/views/coverage cannot invent coaching");
  q.revision=2; q.resources=ResourceState::kExhausted;
  r=evaluate(q,tuning(),r.next_state,.01);
  check(r.issue && r.issue->code==IssueCode::kPreserveResources, "unsafe resource state bypasses presentation debounce");
  q=quality(std::numeric_limits<uint64_t>::max());
  r=evaluate(q,tuning(),{},1);
  check(r.status==Status::kInvalidInput && !r.issue, "revision exhaustion cannot mint an immediately expired instruction");
}
void test_native_bridge_uses_actual_policy_and_atomic_validation() {
  HSSGuidanceTuning t{}; t.abi_version=HSS_ABI_VERSION; t.struct_size=sizeof(t);
  t.debounce_s=.8; t.minimum_display_s=2; t.repeated_haptic_s=8;
  t.low_enter=.4; t.low_exit=.5; t.high_enter=.7; t.high_exit=.6;
  HSSGuidanceOwner* owner=nullptr;
  check(hss_guidance_create(&t,&owner)==HSS_OK && owner,"native policy owner created");
  HSSGuidanceQuality q{}; q.abi_version=HSS_ABI_VERSION; q.struct_size=sizeof(q);
  q.quality_revision=1; q.monotonic_time_s=0; q.measured_facts=kDepthMeasured; q.valid_depth_fraction=.2;
  HSSGuidancePresentation p{};
  check(hss_guidance_evaluate(owner,&q,&p)==HSS_OK && !p.has_issue,"bridge debounces actual measured depth");
  q.quality_revision=2; q.monotonic_time_s=1;
  check(hss_guidance_evaluate(owner,&q,&p)==HSS_OK && p.has_issue && p.code==static_cast<uint32_t>(IssueCode::kAcquireDepth) && p.evidence_revision==2 && p.expires_before_quality_revision==3,"native bridge exposes current measured advice");
  q.quality_revision=3; q.tracking=256; // Must not narrow to normal uint8.
  const auto before=p;
  check(hss_guidance_evaluate(owner,&q,&p)==HSS_INVALID_ARGUMENT && p.evidence_revision==before.evidence_revision,"invalid wide enum cannot narrow silently or change owner/output");
  q.tracking=0; q.monotonic_time_s=2;
  check(hss_guidance_evaluate(owner,&q,&p)==HSS_OK,"invalid input did not consume revision");
  hss_guidance_reset(owner); q.quality_revision=1; q.monotonic_time_s=0;
  check(hss_guidance_evaluate(owner,&q,&p)==HSS_OK,"new capture resets private guidance evidence sequence");
  hss_guidance_destroy(owner);
}
}  // namespace
int main() { test_priority_and_relocalization_precision(); test_debounce_hysteresis_and_processing(); test_specific_actions_and_atomic_rejection(); test_changed_evidence_never_retains_expired_display(); test_partial_measurements_and_urgent_resource_state(); test_native_bridge_uses_actual_policy_and_atomic_validation(); return failures == 0 ? 0 : 1; }

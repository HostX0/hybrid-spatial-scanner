#include "hss/appearance_isolation.hpp"

#include <iostream>
#include <limits>
#include <string>

namespace {

namespace appearance = hss::appearance;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

appearance::MetricTargetRef target(uint64_t revision = 41U) {
  return {"captured-surface-7", revision};
}

appearance::KeyframeAppearance candidate(uint64_t revision = 41U) {
  appearance::KeyframeAppearance value;
  value.keyframe_id = "keyframe-12";
  value.local_asset_id = "asset-12";
  value.source_scene_revision = revision;
  value.calibration = appearance::Verification::kVerified;
  value.accepted_transform = appearance::Verification::kVerified;
  value.visibility_consistency = appearance::Verification::kVerified;
  value.visual_confidence = 0.85;
  return value;
}

void test_unknown_or_rejected_evidence_stays_explicit_and_unapplied() {
  appearance::AppearanceIsolation store;
  appearance::KeyframeAppearance unknown = candidate();
  unknown.visibility_consistency = appearance::Verification::kUnknown;
  const appearance::UpdateResult absent = store.submit(target(), unknown);
  check(absent.status == appearance::Status::kUnknownVisibility &&
            !absent.active_binding && !absent.metric_geometry_changed &&
            store.binding_count() == 0U,
        "unknown visibility has no implied appearance or metric effect");

  unknown.visibility_consistency = appearance::Verification::kRejected;
  const appearance::UpdateResult rejected = store.submit(target(), unknown);
  check(rejected.status == appearance::Status::kRejectedVisibility &&
            !rejected.active_binding && !rejected.metric_geometry_changed &&
            store.binding_count() == 0U,
        "rejected visibility remains explicit instead of generating a visual fill");

  unknown.visibility_consistency = appearance::Verification::kVerified;
  unknown.calibration = appearance::Verification::kUnknown;
  check(store.submit(target(), unknown).status ==
            appearance::Status::kUnknownCalibration,
        "unknown calibration cannot be upgraded by visual confidence");
}

void test_verified_same_revision_layer_preserves_metric_target() {
  appearance::AppearanceIsolation store;
  const appearance::MetricTargetRef measured_target = target();
  const appearance::UpdateResult applied = store.submit(measured_target, candidate());
  check(applied.status == appearance::Status::kApplied && applied.active_binding &&
            !applied.metric_geometry_changed && !applied.retained_existing_binding,
        "only complete visual verification admits an appearance binding");
  if (!applied.active_binding)
    return;
  check(applied.active_binding->target == measured_target &&
            applied.active_binding->keyframe_id == "keyframe-12" &&
            applied.active_binding->local_asset_id == "asset-12" &&
            applied.active_binding->visual_confidence == 0.85,
        "binding carries opaque revision identity and visual metadata only");
  check(store.binding_for(measured_target) == applied.active_binding &&
            store.binding_count() == 1U,
        "appearance lookup is keyed to the immutable measured revision");

  appearance::KeyframeAppearance replacement = candidate();
  replacement.keyframe_id = "keyframe-13";
  replacement.local_asset_id = "asset-13";
  replacement.visual_confidence = 0.25;
  const appearance::UpdateResult refreshed = store.submit(measured_target, replacement);
  check(refreshed.status == appearance::Status::kApplied && refreshed.active_binding &&
            refreshed.active_binding->target == measured_target &&
            !refreshed.metric_geometry_changed && store.binding_count() == 1U,
        "changing only visual appearance neither creates nor changes metric geometry");
}

void test_failure_is_atomic_and_stale_revisions_cannot_cross_bindings() {
  appearance::AppearanceIsolation store;
  const appearance::MetricTargetRef measured_target = target();
  const appearance::UpdateResult initial = store.submit(measured_target, candidate());
  check(initial.status == appearance::Status::kApplied && initial.active_binding,
        "fixture establishes an admitted visual layer");
  if (!initial.active_binding)
    return;

  appearance::KeyframeAppearance rejected = candidate();
  rejected.keyframe_id = "keyframe-rejected";
  rejected.visibility_consistency = appearance::Verification::kRejected;
  const appearance::UpdateResult rejected_result = store.submit(measured_target, rejected);
  check(rejected_result.status == appearance::Status::kRejectedVisibility &&
            rejected_result.retained_existing_binding &&
            rejected_result.active_binding == initial.active_binding &&
            store.binding_for(measured_target) == initial.active_binding,
        "failed visual update leaves the earlier appearance association intact");

  const appearance::UpdateResult stale = store.submit(measured_target, candidate(42U));
  check(stale.status == appearance::Status::kStaleSceneRevision &&
            stale.retained_existing_binding && stale.active_binding == initial.active_binding &&
            !stale.metric_geometry_changed,
        "a candidate from another metric revision cannot paint this revision");
  check(!store.binding_for(target(42U)),
        "appearance for an old revision is never reinterpreted as a new revision");
}

void test_validation_and_bounded_storage_fail_closed() {
  appearance::AppearanceIsolation invalid({.maximum_bindings = 0U});
  check(!invalid.valid() && invalid.submit(target(), candidate()).status ==
                            appearance::Status::kInvalidOptions,
        "zero visual-layer capacity is rejected explicitly");

  appearance::AppearanceIsolation store({.maximum_bindings = 1U});
  appearance::KeyframeAppearance malformed = candidate();
  malformed.visual_confidence = std::numeric_limits<double>::quiet_NaN();
  check(store.submit(target(), malformed).status == appearance::Status::kInvalidInput &&
            store.binding_count() == 0U,
        "nonfinite visual quality cannot enter an optional layer");
  malformed = candidate();
  malformed.calibration = static_cast<appearance::Verification>(255U);
  check(store.submit(target(), malformed).status == appearance::Status::kInvalidInput,
        "unknown verification enum cannot masquerade as verified evidence");

  check(store.submit(target(), candidate()).status == appearance::Status::kApplied,
        "first binding fits the configured resource budget");
  const appearance::UpdateResult limited = store.submit(target(42U), candidate(42U));
  check(limited.status == appearance::Status::kResourceLimit &&
            store.binding_count() == 1U && !limited.metric_geometry_changed,
        "optional appearance exhaustion fails without evicting or modifying metric data");
}

} // namespace

int main() {
  test_unknown_or_rejected_evidence_stays_explicit_and_unapplied();
  test_verified_same_revision_layer_preserves_metric_target();
  test_failure_is_atomic_and_stale_revisions_cannot_cross_bindings();
  test_validation_and_bounded_storage_fail_closed();
  if (failures == 0)
    std::cout << "appearance isolation tests passed\n";
  return failures == 0 ? 0 : 1;
}

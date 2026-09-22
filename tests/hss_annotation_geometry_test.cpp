#include "hss/annotation_geometry.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {
using hss::annotation_geometry::Anchor;
using hss::annotation_geometry::Annotation;
using hss::annotation_geometry::Availability;
using hss::annotation_geometry::EvidenceSource;
using hss::annotation_geometry::Input;
using hss::annotation_geometry::Kind;
using hss::annotation_geometry::Status;

int failures = 0;
void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

hss::plan_semantics::PlanLayer plan() {
  hss::plan_semantics::PlanLayer value;
  value.scene_revision = "scene-42";
  value.validity = hss::plan_semantics::Availability::kReady;
  hss::plan_semantics::Fragment fragment;
  fragment.id = "wall-a";
  fragment.source_revision = value.scene_revision;
  fragment.support_ids = {"anchor-1", "depth-7"};
  value.fragments.push_back(std::move(fragment));
  return value;
}

Annotation linear() {
  Annotation value;
  value.id = "dimension-a";
  value.kind = Kind::kLength;
  value.evidence_source = EvidenceSource::kObserved;
  value.source_revision = "scene-42";
  value.evidence_ids = {"depth-7"};
  value.anchors = {{"wall-a", {0.0, 0.0}}, {"anchor-1", {3.0, 4.0}}};
  value.uncertainty_m = 0.02;
  return value;
}

void test_revision_bound_explicit_metric_geometry() {
  Input input;
  input.plan = plan();
  input.annotations = {linear()};
  const auto result = hss::annotation_geometry::resolve(input);
  check(result.status == Status::kOk && result.layout,
        "captured length resolves against frozen plan lineage");
  if (!result.layout)
    return;
  const auto &resolved = result.layout->annotations.front();
  check(resolved.availability == Availability::kReady && resolved.metric_value &&
            std::fabs(*resolved.metric_value - 5.0) < 1.0e-12,
        "length is calculated from authored anchors only");
  check(resolved.annotation.anchors == input.annotations.front().anchors &&
            result.layout->scene_revision == "scene-42",
        "geometry and source revision round trip without repair");
}

void test_unknown_and_inferred_evidence_never_publish_measurement() {
  Input input;
  input.plan = plan();
  Annotation unknown = linear();
  unknown.id = "unknown-dimension";
  unknown.evidence_source = EvidenceSource::kUnknown;
  unknown.evidence_ids.clear();
  Annotation inferred = linear();
  inferred.id = "inferred-angle";
  inferred.kind = Kind::kAngle;
  inferred.evidence_source = EvidenceSource::kInferred;
  inferred.evidence_ids.clear();
  inferred.anchors = {{"wall-a", {0.0, 0.0}}, {"wall-a", {1.0, 0.0}},
                      {"anchor-1", {0.0, 1.0}}};
  input.annotations = {unknown, inferred};
  const auto result = hss::annotation_geometry::resolve(input);
  check(result.status == Status::kOk && result.layout,
        "unknown and inferred annotation geometry stays representable");
  if (!result.layout)
    return;
  for (const auto &value : result.layout->annotations) {
    check(value.availability == Availability::kProvisional && !value.metric_value,
          "uncertain evidence exposes no invented numeric dimension");
  }
}

void test_plan_quality_remains_visible_on_annotations() {
  Input input;
  input.plan = plan();
  input.plan.validity = hss::plan_semantics::Availability::kProvisional;
  input.annotations = {linear()};
  const auto provisional = hss::annotation_geometry::resolve(input);
  check(provisional.status == Status::kOk && provisional.layout &&
            provisional.layout->annotations.front().availability == Availability::kProvisional &&
            provisional.layout->annotations.front().metric_value,
        "provisional plan state remains visible beside its explicit dimension");

  input.plan.validity = hss::plan_semantics::Availability::kUnavailable;
  const auto unavailable = hss::annotation_geometry::resolve(input);
  check(unavailable.status == Status::kOk && unavailable.layout &&
            unavailable.layout->annotations.front().availability == Availability::kUnavailable &&
            !unavailable.layout->annotations.front().metric_value,
        "unavailable plan state blocks a clean annotation measurement");
}

void test_invalid_lineage_and_user_value_are_atomic() {
  Input stale;
  stale.plan = plan();
  stale.annotations = {linear()};
  stale.annotations.front().source_revision = "scene-41";
  const auto stale_result = hss::annotation_geometry::resolve(stale);
  check(stale_result.status == Status::kInvalidAnnotation && !stale_result.layout,
        "stale annotation revision cannot attach to a new plan");

  Input foreign;
  foreign.plan = plan();
  foreign.annotations = {linear()};
  foreign.annotations.front().anchors.front().geometry_id = "missing-wall";
  const auto foreign_result = hss::annotation_geometry::resolve(foreign);
  check(foreign_result.status == Status::kInvalidProvenance && !foreign_result.layout,
        "foreign geometry ID cannot be rendered as a plan annotation");

  Input invented;
  invented.plan = plan();
  invented.annotations = {linear()};
  invented.annotations.front().user_measured_value_m = 9.0;
  const auto invented_result = hss::annotation_geometry::resolve(invented);
  check(invented_result.status == Status::kInvalidMeasurement && !invented_result.layout,
        "observed geometry cannot be overwritten by an undeclared user value");
}

void test_user_measurement_and_angle_are_explicit() {
  Input input;
  input.plan = plan();
  Annotation user = linear();
  user.id = "tape-1";
  user.evidence_source = EvidenceSource::kUserMeasured;
  user.user_measured_value_m = 5.15;
  user.evidence_ids = {"tape-reading-1"};
  Annotation angle = linear();
  angle.id = "corner-angle";
  angle.kind = Kind::kAngle;
  angle.evidence_source = EvidenceSource::kFitted;
  angle.evidence_ids = {"depth-7"};
  angle.anchors = {{"wall-a", {0.0, 0.0}}, {"wall-a", {1.0, 0.0}},
                   {"anchor-1", {0.0, 1.0}}};
  input.annotations = {user, angle};
  const auto result = hss::annotation_geometry::resolve(input);
  check(result.status == Status::kOk && result.layout,
        "explicit user measurement and supported angle resolve");
  if (!result.layout)
    return;
  check(result.layout->annotations[0].metric_value &&
            *result.layout->annotations[0].metric_value == 5.15,
        "user value stays explicitly user-sourced");
  check(result.layout->annotations[1].metric_value &&
            std::fabs(*result.layout->annotations[1].metric_value -
                      std::acos(-1.0) / 2.0) < 1.0e-12,
        "non-right-angle annotation uses supplied ray geometry");
}

} // namespace

int main() {
  test_revision_bound_explicit_metric_geometry();
  test_unknown_and_inferred_evidence_never_publish_measurement();
  test_plan_quality_remains_visible_on_annotations();
  test_invalid_lineage_and_user_value_are_atomic();
  test_user_measurement_and_angle_are_explicit();
  if (failures != 0) {
    std::cerr << failures << " annotation geometry test(s) failed\n";
    return 1;
  }
  std::cout << "annotation geometry tests passed\n";
  return 0;
}

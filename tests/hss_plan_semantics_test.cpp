#include "hss/plan_semantics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hss::plan_semantics::Availability;
using hss::plan_semantics::BoundaryState;
using hss::plan_semantics::EvidenceKind;
using hss::plan_semantics::EvidenceState;
using hss::plan_semantics::Fragment;
using hss::plan_semantics::HeightRange;
using hss::plan_semantics::Input;
using hss::plan_semantics::IssueCode;
using hss::plan_semantics::ReferenceState;
using hss::plan_semantics::Segment2d;
using hss::plan_semantics::Status;
using hss::plan_semantics::View;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(double actual, double expected, double tolerance = 1.0e-12) {
  return std::fabs(actual - expected) <= tolerance;
}

bool has_issue(const hss::plan_semantics::PlanLayer &layer, IssueCode code,
               const std::string &target = {}) {
  return std::any_of(layer.issues.begin(), layer.issues.end(),
                     [&](const auto &issue) {
                       return issue.code == code &&
                              (target.empty() || issue.target_id == target);
                     });
}

hss::surface_proposal::IntersectionProposal
intersection(const std::string &first_source,
             const std::string &second_source) {
  hss::surface_proposal::IntersectionProposal value;
  value.epoch_id = 4U;
  value.start_m = {0.0, 0.0, 0.0};
  value.end_m = {0.0, 3.0, 0.0};
  value.direction = {0.0, 1.0, 0.0};
  value.normal_angle_rad = 1.1;
  value.conditioning_sine = 0.89;
  value.condition_number = 1.12;
  value.first.surface_id = "10000000-0000-4000-8000-000000000001";
  value.first.all_source_ids = {first_source};
  value.first.intersection_source_ids = {first_source};
  value.second.surface_id = "20000000-0000-4000-8000-000000000002";
  value.second.all_source_ids = {second_source};
  value.second.intersection_source_ids = {second_source};
  return value;
}

Fragment supported_fragment(std::string id, hss::area::Point2d start,
                            hss::area::Point2d end, double height,
                            std::string support_id) {
  const std::string first_source = id + "-surface-a";
  const std::string second_source = id + "-surface-b";
  Fragment fragment;
  fragment.id = std::move(id);
  fragment.state = EvidenceState::kSupported;
  fragment.kind = EvidenceKind::kSurfaceIntersection;
  fragment.geometry_origin = hss::area::GeometryOrigin::kObserved;
  fragment.segment = Segment2d{start, end};
  fragment.source_height_range_m = HeightRange{height, height};
  fragment.source_revision = "scene-17";
  fragment.support_ids = {std::move(support_id), first_source, second_source};
  fragment.intersection = intersection(first_source, second_source);
  return fragment;
}

hss::area::ValidatedRing
ring(std::vector<hss::area::Point2d> points, hss::area::TopologyState topology,
     hss::area::GeometryOrigin origin = hss::area::GeometryOrigin::kObserved,
     hss::area::QualityState quality = hss::area::QualityState::kReady) {
  hss::area::ValidatedRing value;
  value.id = "room-outer";
  value.vertices_m = std::move(points);
  value.topology_state = topology;
  value.provenance.geometry_origin = origin;
  value.provenance.quality_state = quality;
  value.provenance.source_revision = "scene-17";
  value.provenance.support_ids = {"outer-ring-support"};
  return value;
}

Input rectangular_plan(View view, double height, double width, double depth) {
  Input input;
  input.scene_revision = "scene-17";
  input.source_is_synthetic = true;
  input.view = view;
  input.height.cut_height_m = height;
  input.height.sampling_band_m = 0.10;
  input.height.reference_state = ReferenceState::kConfirmedFloor;
  input.height.floor_reference_id = "floor-frame-3";
  input.boundary_state = BoundaryState::kClosedAccepted;
  input.fragments = {
      supported_fragment("edge-1", {0.0, 0.0}, {width, 0.0}, height,
                         "outer-ring-support"),
      supported_fragment("edge-2", {width, 0.0}, {width, depth}, height,
                         "outer-ring-support"),
      supported_fragment("edge-3", {width, depth}, {0.0, depth}, height,
                         "outer-ring-support"),
      supported_fragment("edge-4", {0.0, depth}, {0.0, 0.0}, height,
                         "outer-ring-support"),
  };
  hss::area::Input area;
  area.policy = hss::area::AreaPolicy::kInternalFinishedFace;
  area.outer = ring({{0.0, 0.0}, {width, 0.0}, {width, depth}, {0.0, depth}},
                    hss::area::TopologyState::kAccepted);
  input.area_input = std::move(area);
  return input;
}

void expect_atomic_failure(const Input &input, Status expected,
                           const std::string &message) {
  const auto result = hss::plan_semantics::assemble(input);
  check(result.status == expected,
        message + " reports " + hss::plan_semantics::status_name(expected));
  check(!result.layer, message + " exposes no partial plan layer");
}

void test_floor_and_architectural_cuts_remain_distinct() {
  const Input floor = rectangular_plan(View::kFloorFootprint, 0.10, 3.8, 3.0);
  const Input cut = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  const auto floor_result = hss::plan_semantics::assemble(floor);
  const auto cut_result = hss::plan_semantics::assemble(cut);
  check(floor_result.status == Status::kOk && floor_result.layer &&
            floor_result.layer->area.area_m2 &&
            near(*floor_result.layer->area.area_m2, 11.4),
        "confirmed closed floor footprint publishes its own area");
  check(cut_result.status == Status::kOk && cut_result.layer &&
            cut_result.layer->area.area_m2 &&
            near(*cut_result.layer->area.area_m2, 12.0),
        "confirmed closed architectural cut publishes its own area");
  if (floor_result.layer && cut_result.layer) {
    check(floor_result.layer->view == View::kFloorFootprint &&
              cut_result.layer->view == View::kArchitecturalCut &&
              floor_result.layer->height.cut_height_m == 0.10 &&
              cut_result.layer->height.cut_height_m == 1.20,
          "floor and architectural height policies stay explicit");
    check(floor_result.layer->fragments.front().segment !=
              cut_result.layer->fragments.front().segment,
          "different submitted sloped-wall cuts are never collapsed");
    check(cut_result.layer->source_is_synthetic &&
              cut_result.layer->fragments == cut.fragments,
          "synthetic identity and B-017 lineage round-trip unchanged");
  }
}

void test_open_boundary_never_publishes_clean_area() {
  Input input = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  input.boundary_state = BoundaryState::kOpen;
  input.fragments.pop_back();
  input.area_input->outer.topology_state =
      hss::area::TopologyState::kIncomplete;
  input.area_input->outer.vertices_m.pop_back();
  const auto result = hss::plan_semantics::assemble(input);
  check(result.status == Status::kOk && result.layer,
        "open boundary is a valid explicit plan outcome");
  if (!result.layer)
    return;
  check(result.layer->validity == Availability::kProvisional &&
            result.layer->area.availability == Availability::kProvisional &&
            !result.layer->area.area_m2 &&
            result.layer->area.calculator_status ==
                hss::area::Status::kTopologyIncomplete,
        "open boundary retains B-019 provisional/null-area result");
  check(has_issue(*result.layer, IssueCode::kOpenBoundary),
        "open boundary carries a stable explicit issue");
}

void test_unknown_and_unsupported_remain_explicit() {
  Input floor_unknown =
      rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  floor_unknown.height.reference_state = ReferenceState::kFloorUnknown;
  floor_unknown.height.floor_reference_id.clear();
  const auto unknown_floor_result =
      hss::plan_semantics::assemble(floor_unknown);
  check(unknown_floor_result.status == Status::kOk &&
            unknown_floor_result.layer &&
            unknown_floor_result.layer->area.availability ==
                Availability::kUnavailable &&
            !unknown_floor_result.layer->area.area_m2 &&
            has_issue(*unknown_floor_result.layer,
                      IssueCode::kFloorReferenceUnknown),
        "unknown floor keeps submitted cut but suppresses area");

  Input unsupported = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  unsupported.boundary_state = BoundaryState::kUnsupported;
  unsupported.fragments.clear();
  Fragment fragment;
  fragment.id = "unsupported-boundary";
  fragment.state = EvidenceState::kUnsupported;
  fragment.kind = EvidenceKind::kUnsupported;
  fragment.geometry_origin = hss::area::GeometryOrigin::kUnknown;
  fragment.source_revision = "scene-17";
  fragment.support_ids = {"outer-ring-support"};
  unsupported.fragments.push_back(fragment);
  unsupported.area_input->outer.topology_state =
      hss::area::TopologyState::kUnaccepted;
  const auto unsupported_result = hss::plan_semantics::assemble(unsupported);
  check(unsupported_result.status == Status::kOk && unsupported_result.layer &&
            unsupported_result.layer->validity == Availability::kUnsupported &&
            !unsupported_result.layer->area.area_m2 &&
            has_issue(*unsupported_result.layer,
                      IssueCode::kUnsupportedBoundaryEvidence,
                      "unsupported-boundary") &&
            !unsupported_result.layer->fragments.front().segment,
        "unsupported boundary has no generated metric segment or area");
}

void test_inferred_or_unready_provenance_cannot_be_clean_area() {
  Input inferred = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  inferred.fragments.front().geometry_origin =
      hss::area::GeometryOrigin::kInferred;
  const auto inferred_result = hss::plan_semantics::assemble(inferred);
  check(inferred_result.status == Status::kOk && inferred_result.layer &&
            inferred_result.layer->area.availability ==
                Availability::kUnavailable &&
            !inferred_result.layer->area.area_m2 &&
            has_issue(*inferred_result.layer, IssueCode::kInferredGeometry),
        "inferred boundary geometry cannot become measured clean area");

  Input needs_review =
      rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  needs_review.area_input->outer.provenance.quality_state =
      hss::area::QualityState::kNeedsReview;
  const auto review_result = hss::plan_semantics::assemble(needs_review);
  check(review_result.status == Status::kOk && review_result.layer &&
            review_result.layer->validity == Availability::kProvisional &&
            !review_result.layer->area.area_m2 &&
            has_issue(*review_result.layer, IssueCode::kQualityNotReady),
        "needs-review ring provenance remains provisional with null area");

  Input invalid = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  invalid.area_input->outer.provenance.quality_state =
      hss::area::QualityState::kInvalid;
  const auto invalid_result = hss::plan_semantics::assemble(invalid);
  check(invalid_result.status == Status::kOk && invalid_result.layer &&
            invalid_result.layer->validity == Availability::kUnavailable &&
            !invalid_result.layer->area.area_m2 &&
            has_issue(*invalid_result.layer, IssueCode::kQualityNotReady),
        "invalid ring quality remains unavailable with null area");
}

void test_reflected_ceiling_keeps_local_heights_and_unknowns() {
  Input input;
  input.scene_revision = "scene-17";
  input.source_is_synthetic = true;
  input.view = View::kReflectedCeiling;
  input.height.reference_state = ReferenceState::kNotApplicable;
  input.boundary_state = BoundaryState::kUnknown;

  hss::ceiling_overhead::Model model;
  hss::ceiling_overhead::SurfacePatch slope;
  slope.patch_id = 71U;
  slope.role = hss::ceiling_overhead::Role::kCeilingPatch;
  slope.geometry_origin = hss::ceiling_overhead::GeometryOrigin::kFitted;
  slope.representation = hss::ceiling_overhead::Representation::kPlane;
  slope.plane = hss::ceiling_overhead::Plane{{0.0, 0.8, 0.6}, -2.8};
  model.ceiling_patches.push_back(slope);
  hss::ceiling_overhead::UnknownRegion unknown;
  unknown.patch_id = 72U;
  unknown.reason = hss::ceiling_overhead::UnknownReason::kOpenSky;
  model.unknown_regions.push_back(unknown);
  input.ceiling_model = model;

  Fragment ceiling;
  ceiling.id = "ceiling-slope";
  ceiling.state = EvidenceState::kSupported;
  ceiling.kind = EvidenceKind::kCeilingPatch;
  ceiling.geometry_origin = hss::area::GeometryOrigin::kFitted;
  ceiling.segment = Segment2d{{0.0, 0.0}, {3.0, 0.0}};
  ceiling.source_height_range_m = HeightRange{2.55, 3.05};
  ceiling.source_revision = "scene-17";
  ceiling.support_ids = {"ceiling-source"};
  ceiling.overhead_patch_id = 71U;

  Fragment unknown_fragment;
  unknown_fragment.id = "skylight-unknown";
  unknown_fragment.state = EvidenceState::kUnknown;
  unknown_fragment.kind = EvidenceKind::kUnknownRegion;
  unknown_fragment.geometry_origin = hss::area::GeometryOrigin::kUnknown;
  unknown_fragment.segment = Segment2d{{1.0, 1.0}, {2.0, 1.0}};
  unknown_fragment.source_revision = "scene-17";
  unknown_fragment.support_ids = {"no-depth-mask"};
  unknown_fragment.overhead_patch_id = 72U;
  input.fragments = {ceiling, unknown_fragment};

  const auto result = hss::plan_semantics::assemble(input);
  check(result.status == Status::kOk && result.layer,
        "reflected ceiling assembles from accepted B-021 identities");
  if (!result.layer)
    return;
  check(result.layer->area.availability == Availability::kNotApplicable &&
            !result.layer->area.area_m2 &&
            has_issue(*result.layer, IssueCode::kAreaNotApplicable),
        "reflected ceiling cannot be mislabeled as floor area");
  check(result.layer->validity == Availability::kProvisional &&
            has_issue(*result.layer, IssueCode::kCeilingUnknownRegion, "72") &&
            result.layer->fragments.front().source_height_range_m ==
                HeightRange{2.55, 3.05} &&
            result.layer->ceiling_model == input.ceiling_model,
        "local slope heights and open-sky unknown survive unchanged");
}

void test_contract_mismatches_fail_atomically() {
  Input mismatch = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  mismatch.fragments.front().source_height_range_m = HeightRange{0.10, 0.10};
  expect_atomic_failure(mismatch, Status::kHeightEvidenceMismatch,
                        "cut-height mismatch");

  mismatch = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  mismatch.fragments.front().intersection->first.all_source_ids = {
      "dropped-source"};
  mismatch.fragments.front().intersection->first.intersection_source_ids = {
      "dropped-source"};
  expect_atomic_failure(mismatch, Status::kInvalidProvenance,
                        "dropped B-017 source lineage");

  mismatch = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  mismatch.area_input->outer.provenance.source_revision = "other-scene";
  expect_atomic_failure(mismatch, Status::kInvalidProvenance,
                        "cross-revision area provenance");

  mismatch = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  mismatch.boundary_state = BoundaryState::kOpen;
  expect_atomic_failure(mismatch, Status::kBoundaryStateMismatch,
                        "open label with accepted closed topology");

  mismatch = rectangular_plan(View::kArchitecturalCut, 1.20, 4.0, 3.0);
  mismatch.fragments.front().segment->start_m.x_m =
      std::numeric_limits<double>::quiet_NaN();
  expect_atomic_failure(mismatch, Status::kInvalidFragment,
                        "nonfinite submitted geometry");
}

} // namespace

int main() {
  test_floor_and_architectural_cuts_remain_distinct();
  test_open_boundary_never_publishes_clean_area();
  test_unknown_and_unsupported_remain_explicit();
  test_inferred_or_unready_provenance_cannot_be_clean_area();
  test_reflected_ceiling_keeps_local_heights_and_unknowns();
  test_contract_mismatches_fail_atomically();
  if (failures != 0) {
    std::cerr << failures << " plan semantics test(s) failed\n";
    return 1;
  }
  std::cout << "cut/area plan semantics checks passed\n";
  return 0;
}

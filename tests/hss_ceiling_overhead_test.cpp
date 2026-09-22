#include "hss/ceiling_overhead.hpp"
#include "fixtures/ceilings/sloped_coffer_skylight.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hss::ceiling_overhead::GeometryOrigin;
using hss::ceiling_overhead::Options;
using hss::ceiling_overhead::PatchInput;
using hss::ceiling_overhead::Representation;
using hss::ceiling_overhead::Role;
using hss::ceiling_overhead::SourceKind;
using hss::ceiling_overhead::Status;
using hss::ceiling_overhead::UnknownReason;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_failure(const std::vector<PatchInput> &patches, const Options &options,
                   Status expected, const std::string &message) {
  const auto result = hss::ceiling_overhead::classify(patches, options);
  check(result.status == expected && !result.model, message);
}

void test_complex_ceiling_keeps_distinct_evidence() {
  const std::vector<PatchInput> fixture =
      hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  const auto result = hss::ceiling_overhead::classify(fixture, Options{});
  check(result.status == Status::kOk && result.model.has_value(),
        "source-backed sloped/coffered ceiling fixture classifies");
  if (!result.model)
    return;
  check(result.model->ceiling_patches.size() == 1U &&
            result.model->soffit_bottoms.size() == 1U &&
            result.model->soffit_returns.size() == 1U,
        "slope, coffer bottom, and vertical return remain separate patches");
  check(result.model->floating_shelves.size() == 1U &&
            result.model->unknown_regions.size() == 1U,
        "floating shelf and open skylight remain separate from ceiling faces");

  const auto &slope = result.model->ceiling_patches.front();
  check(slope.plane == fixture[0].plane &&
            slope.metric_support == fixture[0].metric_support &&
            slope.provenance == fixture[0].provenance,
        "arbitrary plane and its source lineage are copied exactly");
  check(slope.plane && std::abs(slope.plane->normal.x) > 0.1 &&
            std::abs(slope.plane->normal.z) > 0.05,
        "sloped ceiling is not axis-aligned by classification");

  const auto &return_patch = result.model->soffit_returns.front();
  check(return_patch.plane && return_patch.plane->normal.x == 1.0 &&
            return_patch.plane->normal.y == 0.0,
        "vertical soffit return is retained without ceiling-normal coercion");
  check(result.model->soffit_bottoms.front().plane &&
            result.model->soffit_bottoms.front().plane->offset_m == -2.55 &&
            slope.plane && slope.plane->offset_m == -3.0,
        "local patch planes retain different heights instead of one room height");

  const auto &shelf = result.model->floating_shelves.front();
  check(shelf.role == Role::kFloatingShelf &&
            shelf.representation == Representation::kCapturedMesh &&
            shelf.mesh_triangles.size() == 2U,
        "floating shelf persists as captured overhead mesh");

  const auto &unknown = result.model->unknown_regions.front();
  check(unknown.reason == UnknownReason::kOpenSky &&
            unknown.boundary_m == fixture[4].unknown_boundary_m &&
            unknown.provenance == fixture[4].provenance,
        "open-sky boundary and no-depth provenance remain unknown");
}

void test_unknown_cannot_be_promoted_or_filled() {
  std::vector<PatchInput> fixture =
      hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  PatchInput &unknown = fixture.back();
  unknown.geometry_origin = GeometryOrigin::kObserved;
  check_failure(fixture, Options{}, Status::kRoleEvidenceMismatch,
                "unknown region cannot be marked observed");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.back().representation = Representation::kPlane;
  fixture.back().plane = hss::ceiling_overhead::Plane{{0.0, 1.0, 0.0}, -3.0};
  check_failure(fixture, Options{}, Status::kRoleEvidenceMismatch,
                "no-depth region cannot carry a fabricated fill plane");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.back().provenance.pop_back();
  check_failure(fixture, Options{}, Status::kInvalidUnknownRegion,
                "unknown region requires explicit no-depth provenance");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.back().unknown_boundary_m[2] = fixture.back().unknown_boundary_m[1];
  fixture.back().unknown_boundary_m[3] = fixture.back().unknown_boundary_m[1];
  check_failure(fixture, Options{}, Status::kInvalidUnknownRegion,
                "unknown region requires a finite non-collinear boundary");
}

void test_surface_roles_do_not_accept_unknown_or_unsupported_geometry() {
  std::vector<PatchInput> fixture =
      hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.front().geometry_origin = GeometryOrigin::kUnknown;
  check_failure(fixture, Options{}, Status::kRoleEvidenceMismatch,
                "unknown evidence cannot become a ceiling surface");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.front().metric_support.front().point_m.y += 0.2;
  check_failure(fixture, Options{}, Status::kMetricSupportMismatch,
                "plane must agree with caller-bounded metric source support");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.front().metric_support.resize(2U);
  check_failure(fixture, Options{}, Status::kInsufficientMetricSupport,
                "plane needs non-collinear metric support");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture.front().metric_support[2].point_m =
      fixture.front().metric_support[1].point_m;
  fixture.front().metric_support[3].point_m =
      fixture.front().metric_support[1].point_m;
  check_failure(fixture, Options{}, Status::kInsufficientMetricSupport,
                "duplicate points cannot manufacture planar support");
}

void test_invalid_metric_and_mesh_inputs_fail_atomically() {
  std::vector<PatchInput> fixture =
      hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture[0].plane->normal.x = std::numeric_limits<double>::quiet_NaN();
  check_failure(fixture, Options{}, Status::kNonFiniteGeometry,
                "nonfinite plane fails without a partial model");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture[1].metric_support.front().point_m.x = 1'001.0;
  check_failure(fixture, Options{}, Status::kCoordinateOutOfBounds,
                "metric extent is bounded before classification");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture[0].metric_support.front().provenance_index = 99U;
  check_failure(fixture, Options{}, Status::kInvalidProvenance,
                "metric support must reference retained provenance");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture[3].mesh_triangles.front().c = 99U;
  check_failure(fixture, Options{}, Status::kInvalidMesh,
                "out-of-range captured mesh indices are rejected");

  fixture = hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  fixture[3].provenance.front().kind = SourceKind::kNoDepthMask;
  check_failure(fixture, Options{}, Status::kInsufficientMetricSupport,
                "no-depth masks cannot support captured shelf geometry");
}

void test_bounded_options_duplicates_and_ids_fail_atomically() {
  const std::vector<PatchInput> fixture =
      hss::ceiling_overhead::fixtures::sloped_coffer_skylight();
  Options invalid;
  invalid.max_abs_coordinate_m = 0.0;
  check_failure(fixture, invalid, Status::kInvalidOptions,
                "invalid bounds are rejected before classification");

  Options bounded;
  bounded.max_input_patches = fixture.size() - 1U;
  check_failure(fixture, bounded, Status::kInputLimitExceeded,
                "patch count is bounded before output allocation");

  std::vector<PatchInput> duplicate = fixture;
  duplicate.back().patch_id = duplicate.front().patch_id;
  check_failure(duplicate, Options{}, Status::kDuplicatePatchId,
                "duplicate patch identities cannot create ambiguous lineage");

  std::vector<PatchInput> missing_id = fixture;
  missing_id.front().patch_id = 0U;
  check_failure(missing_id, Options{}, Status::kInvalidPatchId,
                "zero patch identity is rejected");

  std::vector<PatchInput> invalid_enum = fixture;
  invalid_enum.front().role = static_cast<Role>(255U);
  check_failure(invalid_enum, Options{}, Status::kInvalidEnumValue,
                "out-of-contract role values are rejected explicitly");
}

} // namespace

int main() {
  test_complex_ceiling_keeps_distinct_evidence();
  test_unknown_cannot_be_promoted_or_filled();
  test_surface_roles_do_not_accept_unknown_or_unsupported_geometry();
  test_invalid_metric_and_mesh_inputs_fail_atomically();
  test_bounded_options_duplicates_and_ids_fail_atomically();
  if (failures == 0)
    std::cout << "ceiling and overhead classification tests passed\n";
  return failures == 0 ? 0 : 1;
}

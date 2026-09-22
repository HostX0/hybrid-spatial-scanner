#include "hss/surface_proposal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hss::surface_proposal::FitDiagnostics;
using hss::surface_proposal::Options;
using hss::surface_proposal::Point3d;
using hss::surface_proposal::Status;
using hss::surface_proposal::SupportSample;
using hss::surface_proposal::SurfaceCandidate;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Options options() {
  Options value;
  value.max_support_samples_per_surface = 512U;
  value.min_fit_support_samples = 6U;
  value.min_fit_distinct_points = 6U;
  value.min_fit_diversity_groups = 3U;
  value.min_intersection_support_samples = 6U;
  value.min_intersection_diversity_groups = 3U;
  value.unit_normal_tolerance = 1.0e-12;
  value.max_fit_residual_m = 0.005;
  value.max_support_plane_residual_m = 1.0e-10;
  value.min_intersection_sine = 0.1;
  value.max_support_distance_to_intersection_m = 0.04;
  value.min_supported_intersection_length_m = 0.5;
  value.max_abs_coordinate_m = 100.0;
  return value;
}

std::string uuid(size_t value) {
  std::ostringstream stream;
  stream << "c1700000-0000-4000-8000-" << std::hex << std::nouppercase
         << std::setw(12) << std::setfill('0') << value;
  return stream.str();
}

std::vector<std::string> split(std::string_view input, char delimiter) {
  std::vector<std::string> fields;
  size_t start = 0U;
  while (start <= input.size()) {
    const size_t end = input.find(delimiter, start);
    fields.emplace_back(input.substr(
        start, end == std::string_view::npos ? input.size() - start
                                             : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1U;
  }
  return fields;
}

std::vector<SurfaceCandidate> load_fixture(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open surface proposal fixture");
  std::vector<SurfaceCandidate> surfaces;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "SURFACE") {
      if (fields.size() != 19U)
        throw std::runtime_error("invalid SURFACE fixture row");
      SurfaceCandidate surface;
      surface.surface_id = fields[1];
      surface.epoch_id = std::stoull(fields[2]);
      surface.normal =
          {std::stod(fields[3]), std::stod(fields[4]), std::stod(fields[5])};
      surface.offset_m = std::stod(fields[6]);
      surface.fit = {
          std::stoull(fields[7]),  std::stoull(fields[8]),
          std::stoull(fields[9]),  std::stod(fields[10]),
          std::stod(fields[11]),   std::stod(fields[12]),
          std::stod(fields[13]),   std::stod(fields[14]),
          std::stod(fields[15]),   std::stoull(fields[16]),
          std::stoull(fields[17]), std::stoull(fields[18]),
      };
      surfaces.push_back(std::move(surface));
    } else if (fields.front() == "SAMPLE") {
      if (fields.size() != 8U)
        throw std::runtime_error("invalid SAMPLE fixture row");
      SurfaceCandidate *surface = nullptr;
      for (SurfaceCandidate &candidate : surfaces) {
        if (candidate.surface_id == fields[1]) {
          surface = &candidate;
          break;
        }
      }
      if (surface == nullptr)
        throw std::runtime_error("sample references unknown surface");
      surface->support.push_back(
          {{std::stod(fields[2]), std::stod(fields[3]), std::stod(fields[4])},
           std::stod(fields[5]), std::stoull(fields[6]), fields[7]});
    } else {
      throw std::runtime_error("unknown surface proposal fixture row");
    }
  }
  if (surfaces.size() != 2U)
    throw std::runtime_error("fixture must contain exactly two surfaces");
  return surfaces;
}

SurfaceCandidate make_vertical_surface(size_t id, Point3d normal,
                                       uint64_t epoch, double minimum_y,
                                       double maximum_y,
                                       double lateral_base = 0.01) {
  SurfaceCandidate surface;
  surface.surface_id = uuid(id);
  surface.epoch_id = epoch;
  surface.normal = normal;
  surface.offset_m = 0.0;
  const Point3d tangent{normal.z, 0.0, -normal.x};
  for (size_t group = 0U; group < 3U; ++group) {
    const double fraction = static_cast<double>(group) / 2.0;
    const double y = minimum_y + (maximum_y - minimum_y) * fraction;
    for (size_t sample = 0U; sample < 2U; ++sample) {
      const double lateral = lateral_base + 0.01 * static_cast<double>(sample);
      surface.support.push_back(
          {{lateral * tangent.x, y, lateral * tangent.z}, 1.0, group,
           uuid(id * 100U + group + 1U)});
    }
  }
  surface.fit = {6U, 6U, 3U, 6.0, 0.7, 0.0001, 0.0, 0.0, 0.0,
                 200U, 2U, 1'200U};
  return surface;
}

void expect_failure(const SurfaceCandidate &first,
                    const SurfaceCandidate &second, Status expected,
                    const std::string &message,
                    Options proposal_options = options()) {
  const auto result = hss::surface_proposal::propose_intersection(
      first, second, proposal_options);
  check(result.status == expected,
        message + " has explicit status " +
            hss::surface_proposal::status_name(expected));
  check(!result.proposal, message + " exposes no partial intersection");
}

void test_supported_acute_fixture_preserves_angle_and_provenance() {
  const auto surfaces = load_fixture(
      std::string(HSS_SOURCE_DIR) +
      "/fixtures/surface_proposals/acute_supported.csv");
  const auto result = hss::surface_proposal::propose_intersection(
      surfaces[0], surfaces[1], options());
  check(result.status == Status::kOk && result.proposal.has_value(),
        "supported acute oblique surfaces produce an intersection proposal");
  if (!result.proposal)
    return;
  const double expected_angle = std::acos(33.0 / 65.0);
  check(std::abs(result.proposal->normal_angle_rad - expected_angle) < 1.0e-12,
        "acute angle is retained exactly without Manhattan snapping");
  check(std::abs(result.proposal->normal_angle_rad - std::acos(0.0)) > 0.1,
        "accepted acute angle is materially nonorthogonal");
  check(result.proposal->direction == Point3d{0.0, 1.0, 0.0} &&
            std::abs(result.proposal->start_m.y - 0.2) < 1.0e-12 &&
            std::abs(result.proposal->end_m.y - 1.8) < 1.0e-12,
        "intersection endpoints are clipped to overlapping observed support");
  check(result.proposal->first.fit == surfaces[0].fit &&
            result.proposal->second.fit == surfaces[1].fit,
        "fit diagnostics survive proposal generation unchanged");
  check(result.proposal->first.all_source_ids.size() == 3U &&
            result.proposal->first.intersection_source_ids.size() == 3U &&
            result.proposal->second.all_source_ids.size() == 3U &&
            result.proposal->second.intersection_source_ids.size() == 3U,
        "complete and direct intersection provenance remain explicit per surface");
  check(result.proposal->conditioning_sine > options().min_intersection_sine &&
            result.proposal->condition_number > 1.0,
        "conditioning diagnostics are published with the accepted proposal");
}

void test_supported_obtuse_fixture_is_not_orthogonalized() {
  const auto surfaces = load_fixture(
      std::string(HSS_SOURCE_DIR) +
      "/fixtures/surface_proposals/obtuse_supported.csv");
  const auto result = hss::surface_proposal::propose_intersection(
      surfaces[0], surfaces[1], options());
  check(result.status == Status::kOk && result.proposal.has_value(),
        "supported obtuse oblique surfaces produce an intersection proposal");
  if (!result.proposal)
    return;
  const double expected_angle = std::acos(-16.0 / 65.0);
  check(std::abs(result.proposal->normal_angle_rad - expected_angle) < 1.0e-12 &&
            result.proposal->normal_angle_rad > std::acos(0.0),
        "obtuse angle remains obtuse and is not replaced with ninety degrees");
  check(result.proposal->first.surface_id == surfaces[0].surface_id &&
            result.proposal->second.surface_id == surfaces[1].surface_id &&
            result.proposal->epoch_id == 17U,
        "surface identity, order, and accepted epoch are retained");
}

void test_offset_plane_intersection_is_computed_in_metric_coordinates() {
  SurfaceCandidate first =
      make_vertical_surface(30U, {1.0, 0.0, 0.0}, 12U, 0.0, 2.0);
  SurfaceCandidate second =
      make_vertical_surface(31U, {0.0, 0.0, 1.0}, 12U, 0.0, 2.0);
  first.offset_m = -1.0;
  second.offset_m = -2.0;
  for (SupportSample &sample : first.support) {
    sample.point_m.x += 1.0;
    sample.point_m.z += 2.0;
  }
  for (SupportSample &sample : second.support) {
    sample.point_m.x += 1.0;
    sample.point_m.z += 2.0;
  }
  const auto result = hss::surface_proposal::propose_intersection(
      first, second, options());
  check(result.status == Status::kOk && result.proposal.has_value(),
        "supported offset planes produce a metric intersection");
  if (!result.proposal)
    return;
  check(std::abs(result.proposal->start_m.x - 1.0) < 1.0e-12 &&
            std::abs(result.proposal->start_m.y) < 1.0e-12 &&
            std::abs(result.proposal->start_m.z - 2.0) < 1.0e-12 &&
            std::abs(result.proposal->end_m.x - 1.0) < 1.0e-12 &&
            std::abs(result.proposal->end_m.y - 2.0) < 1.0e-12 &&
            std::abs(result.proposal->end_m.z - 2.0) < 1.0e-12,
        "nonzero plane offsets are applied without moving supported endpoints");
}

void test_nearly_parallel_nonfinite_and_cross_epoch_are_atomic() {
  const SurfaceCandidate first =
      make_vertical_surface(1U, {1.0, 0.0, 0.0}, 7U, 0.0, 2.0);
  const double small = 1.0e-5;
  const double scale = 1.0 / std::hypot(1.0, small);
  SurfaceCandidate nearly_parallel = make_vertical_surface(
      2U, {scale, 0.0, small * scale}, 7U, 0.0, 2.0);
  expect_failure(first, nearly_parallel, Status::kNearlyParallel,
                 "nearly parallel surfaces");

  SurfaceCandidate nonfinite = nearly_parallel;
  nonfinite.support[0].point_m.y =
      std::numeric_limits<double>::quiet_NaN();
  expect_failure(first, nonfinite, Status::kNonFiniteInput,
                 "nonfinite support coordinate");

  SurfaceCandidate other_epoch = make_vertical_surface(
      3U, {0.0, 0.0, 1.0}, 8U, 0.0, 2.0);
  expect_failure(first, other_epoch, Status::kCrossEpoch,
                 "surface pair from unresolved epochs");
}

void test_unsupported_and_disjoint_intersections_are_not_extrapolated() {
  const SurfaceCandidate away_from_x_plane =
      make_vertical_surface(10U, {1.0, 0.0, 0.0}, 9U, 0.0, 2.0, 1.0);
  const SurfaceCandidate away_from_z_plane =
      make_vertical_surface(11U, {0.0, 0.0, 1.0}, 9U, 0.0, 2.0, 1.0);
  expect_failure(away_from_x_plane, away_from_z_plane,
                 Status::kExtrapolatedIntersection,
                 "infinite line outside both observed patches");

  const SurfaceCandidate lower =
      make_vertical_surface(12U, {1.0, 0.0, 0.0}, 9U, 0.0, 1.0);
  const SurfaceCandidate upper =
      make_vertical_surface(13U, {0.0, 0.0, 1.0}, 9U, 2.0, 3.0);
  expect_failure(lower, upper, Status::kExtrapolatedIntersection,
                 "disjoint supported intervals along the infinite line");

  SurfaceCandidate sparse =
      make_vertical_surface(14U, {1.0, 0.0, 0.0}, 9U, 0.0, 2.0);
  for (size_t index = 2U; index < sparse.support.size(); ++index)
    sparse.support[index].point_m.z =
        0.2 + 0.01 * static_cast<double>(index % 2U);
  expect_failure(sparse,
                 make_vertical_surface(15U, {0.0, 0.0, 1.0}, 9U, 0.0, 2.0),
                 Status::kInsufficientIntersectionSupport,
                 "too few directly supporting samples");
}

void test_invalid_diagnostics_ids_resources_and_options_are_atomic() {
  const SurfaceCandidate first =
      make_vertical_surface(20U, {1.0, 0.0, 0.0}, 4U, 0.0, 2.0);
  const SurfaceCandidate second =
      make_vertical_surface(21U, {0.0, 0.0, 1.0}, 4U, 0.0, 2.0);

  SurfaceCandidate invalid_diagnostics = second;
  invalid_diagnostics.fit.sample_count = 7U;
  expect_failure(first, invalid_diagnostics, Status::kInvalidFitDiagnostics,
                 "fit count inconsistent with retained support");

  SurfaceCandidate invalid_source = second;
  invalid_source.support[0].source_id = "not-a-uuid";
  expect_failure(first, invalid_source, Status::kInvalidId,
                 "malformed provenance identity");

  SurfaceCandidate duplicate = second;
  duplicate.surface_id = first.surface_id;
  expect_failure(first, duplicate, Status::kDuplicateSurface,
                 "same surface submitted twice");

  Options resource_limited = options();
  resource_limited.max_support_samples_per_surface = 5U;
  resource_limited.min_fit_support_samples = 5U;
  resource_limited.min_fit_distinct_points = 5U;
  resource_limited.min_intersection_support_samples = 5U;
  expect_failure(first, second, Status::kResourceLimitExceeded,
                 "caller support cap", resource_limited);

  expect_failure(first, second, Status::kInvalidOptions,
                 "undeclared geometric policy", Options{});
}

} // namespace

int main() {
  test_supported_acute_fixture_preserves_angle_and_provenance();
  test_supported_obtuse_fixture_is_not_orthogonalized();
  test_offset_plane_intersection_is_computed_in_metric_coordinates();
  test_nearly_parallel_nonfinite_and_cross_epoch_are_atomic();
  test_unsupported_and_disjoint_intersections_are_not_extrapolated();
  test_invalid_diagnostics_ids_resources_and_options_are_atomic();
  if (failures == 0)
    std::cout << "surface proposal tests passed\n";
  return failures == 0 ? 0 : 1;
}

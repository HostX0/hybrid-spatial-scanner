#include "hss/robust_plane_fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using hss::plane_fit::Options;
using hss::plane_fit::Point3d;
using hss::plane_fit::Status;
using hss::plane_fit::WeightedSample;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

double dot(const Point3d &left, const Point3d &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Point3d normalize(Point3d value) {
  const double length = std::hypot(value.x, value.y, value.z);
  return {value.x / length, value.y / length, value.z / length};
}

Point3d cross(const Point3d &left, const Point3d &right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Point3d add(Point3d origin, Point3d first, double first_scale, Point3d second,
            double second_scale, Point3d normal, double noise) {
  return {origin.x + first_scale * first.x + second_scale * second.x +
              noise * normal.x,
          origin.y + first_scale * first.y + second_scale * second.y +
              noise * normal.y,
          origin.z + first_scale * first.z + second_scale * second.z +
              noise * normal.z};
}

uint64_t next_random(uint64_t *state) {
  *state ^= *state << 13U;
  *state ^= *state >> 7U;
  *state ^= *state << 17U;
  return *state;
}

double uniform(uint64_t *state, double low, double high) {
  const double unit = static_cast<double>(next_random(state) >> 11U) /
                      static_cast<double>(uint64_t{1} << 53U);
  return low + (high - low) * unit;
}

Options options(double threshold) {
  Options value;
  value.inlier_threshold_m = threshold;
  value.candidate_iterations = 320U;
  value.max_input_samples = 512U;
  value.max_refinement_iterations = 8U;
  value.max_residual_evaluations = 200'000U;
  value.min_support_samples = 6U;
  value.min_distinct_support_points = 6U;
  value.min_diversity_groups = 3U;
  return value;
}

void test_seeded_oblique_outlier_fit_is_deterministic_and_weighted() {
  const Point3d expected_normal = normalize({1.0, 2.0, 3.0});
  const Point3d u = normalize(cross(expected_normal, {0.0, 0.0, 1.0}));
  const Point3d v = cross(expected_normal, u);
  const Point3d center{2.0 * expected_normal.x, 2.0 * expected_normal.y,
                       2.0 * expected_normal.z};
  std::vector<WeightedSample> samples;
  uint64_t state = 0x5a17c9e3U;
  double expected_weight = 0.0;
  for (size_t index = 0U; index < 120U; ++index) {
    const double weight = index % 5U == 0U ? 0.25 : 1.0;
    expected_weight += weight;
    samples.push_back({add(center, u, uniform(&state, -2.5, 2.5), v,
                           uniform(&state, -1.8, 1.8), expected_normal,
                           uniform(&state, -0.001, 0.001)),
                       weight, index % 9U});
  }
  for (size_t index = 0U; index < 35U; ++index) {
    samples.push_back({{uniform(&state, -4.0, 4.0), uniform(&state, -4.0, 4.0),
                        uniform(&state, -4.0, 4.0)},
                       1.0,
                       100U + index});
  }
  Options fit_options = options(0.01);
  fit_options.seed = 0x12345678U;
  const auto first = hss::plane_fit::fit(samples, fit_options);
  const auto second = hss::plane_fit::fit(samples, fit_options);
  check(first.status == Status::kOk && first.plane.has_value(),
        "seeded robust fit accepts broad oblique support with outliers");
  check(second.status == Status::kOk && second.plane == first.plane,
        "same seed and input produce exactly identical output");
  if (!first.plane)
    return;
  check(dot(first.plane->normal, expected_normal) > 0.999999,
        "arbitrary oblique normal survives without an axis prior");
  check(first.plane->support.sample_count == 120U &&
            first.plane->support.distinct_point_count == 120U,
        "outliers are absent from explicit support indices");
  check(std::abs(first.plane->support.total_weight - expected_weight) < 1.0e-12,
        "refined support reports calibrated sample weight");
  check(first.plane->diversity.group_count == 9U,
        "correlated group diversity is reported separately from sample count");
  check(first.plane->residuals.maximum_absolute_m <=
                fit_options.inlier_threshold_m &&
            std::isfinite(first.plane->residuals.weighted_rms_m),
        "finite residual distribution respects the caller threshold");
  check(first.plane->normal.z > 0.0 &&
            std::abs(first.plane->normal.z) >= std::abs(first.plane->normal.y),
        "largest-magnitude component supplies deterministic sign");
  check(std::abs(std::hypot(first.plane->normal.x, first.plane->normal.y,
                            first.plane->normal.z) -
                 1.0) < 1.0e-12,
        "canonical normal has unit length");
}

std::vector<WeightedSample> plane_grid(double z, size_t width,
                                       uint64_t group_base) {
  std::vector<WeightedSample> samples;
  for (size_t row = 0U; row < width; ++row) {
    for (size_t column = 0U; column < width; ++column) {
      samples.push_back({{static_cast<double>(column) * 0.2,
                          static_cast<double>(row) * 0.15, z},
                         1.0,
                         group_base + row});
    }
  }
  return samples;
}

void test_distinct_parallel_planes_do_not_merge() {
  std::vector<WeightedSample> samples = plane_grid(0.0, 4U, 0U);
  const size_t primary_count = samples.size();
  std::vector<WeightedSample> parallel = plane_grid(0.25, 3U, 20U);
  samples.insert(samples.end(), parallel.begin(), parallel.end());
  Options fit_options = options(0.005);
  fit_options.seed = 77U;
  const auto result = hss::plane_fit::fit(samples, fit_options);
  check(result.status == Status::kOk && result.plane.has_value(),
        "one supported candidate is selected from two parallel faces");
  if (!result.plane)
    return;
  check(result.plane->support.sample_count == primary_count,
        "parallel face is not merged into the selected support");
  check(std::all_of(
            result.plane->inlier_indices.begin(),
            result.plane->inlier_indices.end(),
            [primary_count](size_t index) { return index < primary_count; }),
        "support lineage contains only the selected plane's samples");
  check(std::abs(result.plane->offset_m) < 1.0e-12,
        "larger zero-offset parallel face wins deterministically");
  check(!std::signbit(result.plane->normal.x) &&
            !std::signbit(result.plane->normal.y) &&
            !std::signbit(result.plane->offset_m),
        "zero-valued canonical outputs never retain negative zero");
}

void test_refinement_uses_sample_weights() {
  std::vector<WeightedSample> weighted{
      {{-1.0, -1.0, 0.0}, 1.0, 0U},   {{1.0, -1.0, 0.0}, 1.0, 1U},
      {{-1.0, 1.0, 0.0}, 1.0, 2U},    {{1.0, 1.0, 0.0}, 1.0, 3U},
      {{-0.5, 0.0, 0.008}, 0.01, 4U}, {{0.5, 0.0, 0.008}, 0.01, 5U},
  };
  std::vector<WeightedSample> equal = weighted;
  for (WeightedSample &sample : equal)
    sample.weight = 1.0;
  Options fit_options = options(0.02);
  const auto weighted_result = hss::plane_fit::fit(weighted, fit_options);
  const auto equal_result = hss::plane_fit::fit(equal, fit_options);
  check(weighted_result.status == Status::kOk &&
            equal_result.status == Status::kOk && weighted_result.plane &&
            equal_result.plane,
        "weighted and equal-weight support both refine successfully");
  if (!weighted_result.plane || !equal_result.plane)
    return;
  check(std::abs(weighted_result.plane->offset_m) <
            std::abs(equal_result.plane->offset_m),
        "weighted total least squares reduces low-weight sample influence");
  check(std::abs(weighted_result.plane->support.total_weight - 4.02) < 1.0e-12,
        "weighted refinement reports the selected weight sum");
}

void test_nearly_collinear_support_is_rank_deficient() {
  std::vector<WeightedSample> samples;
  for (size_t index = 0U; index < 30U; ++index) {
    samples.push_back({{static_cast<double>(index) * 0.1,
                        index % 2U == 0U ? 1.0e-10 : -1.0e-10, 0.0},
                       1.0,
                       index % 4U});
  }
  Options fit_options = options(0.001);
  fit_options.min_second_to_largest_ratio = 1.0e-6;
  const auto result = hss::plane_fit::fit(samples, fit_options);
  check(result.status == Status::kRankDeficient && !result.plane,
        "near-line support is rejected without a partial plane");
}

void test_exact_collinear_support_is_degenerate() {
  std::vector<WeightedSample> samples;
  for (size_t index = 0U; index < 20U; ++index) {
    const double coordinate = static_cast<double>(index);
    samples.push_back(
        {{coordinate, 2.0 * coordinate, -coordinate}, 1.0, index % 4U});
  }
  const auto result = hss::plane_fit::fit(samples, options(0.01));
  check(result.status == Status::kDegenerateInput && !result.plane,
        "exact collinear input cannot create a plane hypothesis");
}

void test_repeated_identical_points_do_not_inflate_support() {
  const std::array<Point3d, 5> distinct{{
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {1.0, 1.0, 0.0},
      {0.5, 0.2, 0.0},
  }};
  std::vector<WeightedSample> samples;
  for (size_t repeat = 0U; repeat < 20U; ++repeat) {
    for (const Point3d &point : distinct)
      samples.push_back({point, 1.0, repeat % 4U});
  }
  const auto result = hss::plane_fit::fit(samples, options(0.001));
  check(result.status == Status::kDegenerateInput && !result.plane,
        "repeated coordinates cannot satisfy distinct spatial support");
}

void test_invalid_and_bounded_inputs_are_atomic() {
  Options invalid;
  const auto undeclared_threshold =
      hss::plane_fit::fit(plane_grid(0.0, 3U, 0U), invalid);
  check(undeclared_threshold.status == Status::kInvalidOptions &&
            !undeclared_threshold.plane,
        "caller must declare a positive inlier threshold");

  std::vector<WeightedSample> too_few = plane_grid(0.0, 2U, 0U);
  const auto insufficient = hss::plane_fit::fit(too_few, options(0.01));
  check(insufficient.status == Status::kInsufficientEvidence &&
            !insufficient.plane,
        "insufficient samples produce no partial fit");

  std::vector<WeightedSample> nonfinite = plane_grid(0.0, 3U, 0U);
  nonfinite[2].point.y = std::numeric_limits<double>::infinity();
  const auto invalid_input = hss::plane_fit::fit(nonfinite, options(0.01));
  check(invalid_input.status == Status::kNonFiniteInput && !invalid_input.plane,
        "nonfinite input is rejected before candidate work");

  std::vector<WeightedSample> invalid_weight = plane_grid(0.0, 3U, 0U);
  invalid_weight[2].weight = 0.0;
  const auto rejected_weight =
      hss::plane_fit::fit(invalid_weight, options(0.01));
  check(rejected_weight.status == Status::kInvalidSampleWeight &&
            !rejected_weight.plane,
        "nonpositive sample weight is rejected before candidate work");

  Options bounded = options(0.01);
  const auto bounded_samples = plane_grid(0.0, 4U, 0U);
  bounded.max_residual_evaluations =
      (bounded.candidate_iterations + bounded.max_refinement_iterations) *
          bounded_samples.size() -
      1U;
  const auto work_limited = hss::plane_fit::fit(bounded_samples, bounded);
  check(work_limited.status == Status::kWorkLimitExceeded &&
            !work_limited.plane,
        "declared residual-work cap is checked before fitting");
}

} // namespace

int main() {
  test_seeded_oblique_outlier_fit_is_deterministic_and_weighted();
  test_distinct_parallel_planes_do_not_merge();
  test_refinement_uses_sample_weights();
  test_nearly_collinear_support_is_rank_deficient();
  test_exact_collinear_support_is_degenerate();
  test_repeated_identical_points_do_not_inflate_support();
  test_invalid_and_bounded_inputs_are_atomic();
  if (failures == 0)
    std::cout << "robust plane fitting tests passed\n";
  return failures == 0 ? 0 : 1;
}

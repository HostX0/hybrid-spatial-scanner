#include "hss/robust_plane_fit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace hss::plane_fit {
namespace {

constexpr double kEigenTolerance =
    64.0 * std::numeric_limits<double>::epsilon();
constexpr size_t kJacobiSweeps = 32U;

struct CorePlane {
  Point3d normal;
  double offset{0.0};
};

struct CovarianceFit {
  CorePlane plane;
  double major_variance{0.0};
  double minor_variance{0.0};
  double total_weight{0.0};
};

struct SupportFacts {
  size_t distinct_points{0U};
  size_t diversity_groups{0U};
};

struct Candidate {
  CorePlane plane;
  std::vector<size_t> inliers;
  size_t distinct_points{0U};
  size_t diversity_groups{0U};
  double minor_variance{0.0};
  double median_residual{0.0};
};

double canonical_zero(double value) { return value == 0.0 ? 0.0 : value; }

bool finite(const Point3d &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

Point3d subtract(const Point3d &left, const Point3d &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double dot(const Point3d &left, const Point3d &right) {
  return std::fma(left.x, right.x, std::fma(left.y, right.y, left.z * right.z));
}

Point3d cross(const Point3d &left, const Point3d &right) {
  return {
      std::fma(left.y, right.z, -left.z * right.y),
      std::fma(left.z, right.x, -left.x * right.z),
      std::fma(left.x, right.y, -left.y * right.x),
  };
}

double norm(const Point3d &value) {
  return std::hypot(value.x, value.y, value.z);
}

void canonicalize(CorePlane *plane) {
  const std::array<double, 3> components{plane->normal.x, plane->normal.y,
                                         plane->normal.z};
  size_t pivot = 0U;
  for (size_t index = 1U; index < components.size(); ++index) {
    if (std::abs(components[index]) > std::abs(components[pivot]))
      pivot = index;
  }
  if (components[pivot] < 0.0) {
    plane->normal.x = -plane->normal.x;
    plane->normal.y = -plane->normal.y;
    plane->normal.z = -plane->normal.z;
    plane->offset = -plane->offset;
  }
  plane->normal.x = canonical_zero(plane->normal.x);
  plane->normal.y = canonical_zero(plane->normal.y);
  plane->normal.z = canonical_zero(plane->normal.z);
  plane->offset = canonical_zero(plane->offset);
}

bool make_three_point_plane(const Point3d &first, const Point3d &second,
                            const Point3d &third, CorePlane *output) {
  Point3d a = subtract(second, first);
  Point3d b = subtract(third, first);
  if (!finite(a) || !finite(b))
    return false;
  const double scale = std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z),
                                 std::abs(b.x), std::abs(b.y), std::abs(b.z)});
  if (!(scale > 0.0) || !std::isfinite(scale))
    return false;
  a = {a.x / scale, a.y / scale, a.z / scale};
  b = {b.x / scale, b.y / scale, b.z / scale};
  const Point3d product = cross(a, b);
  const double length = norm(product);
  if (!(length > 32.0 * std::numeric_limits<double>::epsilon()) ||
      !std::isfinite(length)) {
    return false;
  }
  CorePlane plane{{product.x / length, product.y / length, product.z / length},
                  0.0};
  plane.offset = -dot(plane.normal, first);
  if (!finite(plane.normal) || !std::isfinite(plane.offset))
    return false;
  canonicalize(&plane);
  *output = plane;
  return true;
}

bool residual(const CorePlane &plane, const Point3d &point, double *output) {
  const double signed_distance =
      std::fma(plane.normal.x, point.x,
               std::fma(plane.normal.y, point.y,
                        std::fma(plane.normal.z, point.z, plane.offset)));
  if (!std::isfinite(signed_distance))
    return false;
  *output = std::abs(signed_distance);
  return true;
}

class DeterministicGenerator {
public:
  explicit DeterministicGenerator(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    state_ += 0x9e3779b97f4a7c15ULL;
    uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

private:
  uint64_t state_;
};

std::array<size_t, 3> sample_triplet(DeterministicGenerator *generator,
                                     size_t count) {
  const size_t first = static_cast<size_t>(generator->next() % count);
  size_t second = static_cast<size_t>(generator->next() % (count - 1U));
  if (second >= first)
    ++second;
  const size_t low = std::min(first, second);
  const size_t high = std::max(first, second);
  size_t third = static_cast<size_t>(generator->next() % (count - 2U));
  if (third >= low)
    ++third;
  if (third >= high)
    ++third;
  return {first, second, third};
}

bool point_less(const Point3d &left, const Point3d &right) {
  return std::tie(left.x, left.y, left.z) < std::tie(right.x, right.y, right.z);
}

SupportFacts support_facts(const std::vector<WeightedSample> &samples,
                           const std::vector<size_t> &indices) {
  std::vector<Point3d> points;
  std::vector<uint64_t> groups;
  points.reserve(indices.size());
  groups.reserve(indices.size());
  for (const size_t index : indices) {
    points.push_back(samples[index].point);
    groups.push_back(samples[index].diversity_group);
  }
  std::sort(points.begin(), points.end(), point_less);
  const auto point_end = std::unique(points.begin(), points.end());
  std::sort(groups.begin(), groups.end());
  const auto group_end = std::unique(groups.begin(), groups.end());
  return {static_cast<size_t>(point_end - points.begin()),
          static_cast<size_t>(group_end - groups.begin())};
}

bool symmetric_eigen(std::array<std::array<double, 3>, 3> matrix,
                     std::array<double, 3> *values,
                     std::array<std::array<double, 3>, 3> *vectors) {
  std::array<std::array<double, 3>, 3> basis{{
      {{1.0, 0.0, 0.0}},
      {{0.0, 1.0, 0.0}},
      {{0.0, 0.0, 1.0}},
  }};
  for (size_t sweep = 0U; sweep < kJacobiSweeps; ++sweep) {
    size_t p = 0U;
    size_t q = 1U;
    if (std::abs(matrix[0][2]) > std::abs(matrix[p][q])) {
      p = 0U;
      q = 2U;
    }
    if (std::abs(matrix[1][2]) > std::abs(matrix[p][q])) {
      p = 1U;
      q = 2U;
    }
    const double diagonal_scale = std::abs(matrix[0][0]) +
                                  std::abs(matrix[1][1]) +
                                  std::abs(matrix[2][2]) + 1.0;
    if (std::abs(matrix[p][q]) <= kEigenTolerance * diagonal_scale)
      break;
    const double tau = (matrix[q][q] - matrix[p][p]) / (2.0 * matrix[p][q]);
    const double tangent =
        std::copysign(1.0 / (std::abs(tau) + std::hypot(1.0, tau)), tau);
    const double cosine = 1.0 / std::hypot(1.0, tangent);
    const double sine = tangent * cosine;
    for (size_t index = 0U; index < 3U; ++index) {
      if (index == p || index == q)
        continue;
      const double mip = matrix[index][p];
      const double miq = matrix[index][q];
      matrix[index][p] = cosine * mip - sine * miq;
      matrix[p][index] = matrix[index][p];
      matrix[index][q] = sine * mip + cosine * miq;
      matrix[q][index] = matrix[index][q];
    }
    const double app = matrix[p][p];
    const double aqq = matrix[q][q];
    const double apq = matrix[p][q];
    matrix[p][p] =
        cosine * cosine * app - 2.0 * sine * cosine * apq + sine * sine * aqq;
    matrix[q][q] =
        sine * sine * app + 2.0 * sine * cosine * apq + cosine * cosine * aqq;
    matrix[p][q] = 0.0;
    matrix[q][p] = 0.0;
    for (size_t row = 0U; row < 3U; ++row) {
      const double vip = basis[row][p];
      const double viq = basis[row][q];
      basis[row][p] = cosine * vip - sine * viq;
      basis[row][q] = sine * vip + cosine * viq;
    }
  }
  std::array<size_t, 3> order{0U, 1U, 2U};
  std::sort(order.begin(), order.end(), [&](size_t left, size_t right) {
    return matrix[left][left] < matrix[right][right];
  });
  for (size_t output_index = 0U; output_index < 3U; ++output_index) {
    const size_t source_index = order[output_index];
    (*values)[output_index] = matrix[source_index][source_index];
    for (size_t row = 0U; row < 3U; ++row) {
      (*vectors)[row][output_index] = basis[row][source_index];
    }
  }
  for (const double value : *values) {
    if (!std::isfinite(value))
      return false;
  }
  return true;
}

Status covariance_fit(const std::vector<WeightedSample> &samples,
                      const std::vector<size_t> &indices,
                      const Options &options, bool weighted,
                      CovarianceFit *output) {
  double max_weight = 1.0;
  if (weighted) {
    max_weight = 0.0;
    for (const size_t index : indices) {
      max_weight = std::max(max_weight, samples[index].weight);
    }
  }
  double normalized_weight_sum = 0.0;
  Point3d centroid = samples[indices.front()].point;
  bool has_centroid = false;
  long double reported_weight_sum = 0.0L;
  for (const size_t index : indices) {
    const double normalized_weight =
        weighted ? samples[index].weight / max_weight : 1.0;
    const double next_sum = normalized_weight_sum + normalized_weight;
    if (!(next_sum > 0.0) || !std::isfinite(next_sum))
      return Status::kNumericalFailure;
    if (!has_centroid) {
      centroid = samples[index].point;
      has_centroid = true;
    } else {
      const double fraction = normalized_weight / next_sum;
      centroid.x = std::lerp(centroid.x, samples[index].point.x, fraction);
      centroid.y = std::lerp(centroid.y, samples[index].point.y, fraction);
      centroid.z = std::lerp(centroid.z, samples[index].point.z, fraction);
    }
    normalized_weight_sum = next_sum;
    reported_weight_sum +=
        weighted ? static_cast<long double>(samples[index].weight) : 1.0L;
  }
  if (!finite(centroid) ||
      reported_weight_sum >
          static_cast<long double>(std::numeric_limits<double>::max())) {
    return Status::kNumericalFailure;
  }
  double coordinate_scale = 0.0;
  for (const size_t index : indices) {
    const Point3d delta = subtract(samples[index].point, centroid);
    if (!finite(delta))
      return Status::kNumericalFailure;
    coordinate_scale = std::max({coordinate_scale, std::abs(delta.x),
                                 std::abs(delta.y), std::abs(delta.z)});
  }
  if (!(coordinate_scale > 0.0) || !std::isfinite(coordinate_scale)) {
    return Status::kRankDeficient;
  }
  std::array<std::array<double, 3>, 3> covariance{};
  for (const size_t index : indices) {
    const double sample_weight =
        weighted ? samples[index].weight / max_weight : 1.0;
    const Point3d delta = subtract(samples[index].point, centroid);
    const std::array<double, 3> scaled{delta.x / coordinate_scale,
                                       delta.y / coordinate_scale,
                                       delta.z / coordinate_scale};
    for (size_t row = 0U; row < 3U; ++row) {
      for (size_t column = row; column < 3U; ++column) {
        covariance[row][column] += sample_weight * scaled[row] * scaled[column];
      }
    }
  }
  for (size_t row = 0U; row < 3U; ++row) {
    for (size_t column = row; column < 3U; ++column) {
      covariance[row][column] /= normalized_weight_sum;
      covariance[column][row] = covariance[row][column];
    }
  }
  std::array<double, 3> eigenvalues{};
  std::array<std::array<double, 3>, 3> eigenvectors{};
  if (!symmetric_eigen(covariance, &eigenvalues, &eigenvectors)) {
    return Status::kNumericalFailure;
  }
  const double scale_squared = coordinate_scale * coordinate_scale;
  if (!std::isfinite(scale_squared))
    return Status::kNumericalFailure;
  const double scaled_major = eigenvalues[2] * scale_squared;
  if (!std::isfinite(scaled_major))
    return Status::kNumericalFailure;
  const double allowed_negative =
      kEigenTolerance * std::max(1.0, std::abs(scaled_major));
  for (double &value : eigenvalues) {
    value *= scale_squared;
    if (value < -allowed_negative || !std::isfinite(value)) {
      return Status::kNumericalFailure;
    }
    value = std::max(0.0, value);
  }
  const double major = eigenvalues[2];
  const double minor = eigenvalues[1];
  if (!(minor >= options.min_second_variance_m2) || !(major > 0.0) ||
      minor / major < options.min_second_to_largest_ratio) {
    return Status::kRankDeficient;
  }
  Point3d normal{eigenvectors[0][0], eigenvectors[1][0], eigenvectors[2][0]};
  const double normal_length = norm(normal);
  if (!(normal_length > 0.0) || !std::isfinite(normal_length)) {
    return Status::kNumericalFailure;
  }
  normal = {normal.x / normal_length, normal.y / normal_length,
            normal.z / normal_length};
  CorePlane plane{normal, -dot(normal, centroid)};
  if (!finite(normal) || !std::isfinite(plane.offset))
    return Status::kNumericalFailure;
  canonicalize(&plane);
  *output = {plane, canonical_zero(major), canonical_zero(minor),
             static_cast<double>(reported_weight_sum)};
  return Status::kOk;
}

double median(std::vector<double> values) {
  const size_t middle = values.size() / 2U;
  std::nth_element(values.begin(),
                   values.begin() + static_cast<std::ptrdiff_t>(middle),
                   values.end());
  if (values.size() % 2U != 0U)
    return values[middle];
  const double upper = values[middle];
  const double lower = *std::max_element(
      values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
  return std::midpoint(lower, upper);
}

bool better_candidate(const Candidate &left, const Candidate &right) {
  if (left.distinct_points != right.distinct_points) {
    return left.distinct_points > right.distinct_points;
  }
  if (left.inliers.size() != right.inliers.size()) {
    return left.inliers.size() > right.inliers.size();
  }
  if (left.diversity_groups != right.diversity_groups) {
    return left.diversity_groups > right.diversity_groups;
  }
  if (left.minor_variance != right.minor_variance) {
    return left.minor_variance > right.minor_variance;
  }
  if (left.median_residual != right.median_residual) {
    return left.median_residual < right.median_residual;
  }
  return std::tie(left.plane.normal.x, left.plane.normal.y, left.plane.normal.z,
                  left.plane.offset) <
         std::tie(right.plane.normal.x, right.plane.normal.y,
                  right.plane.normal.z, right.plane.offset);
}

bool valid_options(const Options &options) {
  return std::isfinite(options.inlier_threshold_m) &&
         options.inlier_threshold_m > 0.0 &&
         options.candidate_iterations > 0U &&
         options.candidate_iterations <= kHardMaxCandidateIterations &&
         options.max_input_samples >= 3U &&
         options.max_input_samples <= kHardMaxSamples &&
         options.max_refinement_iterations > 0U &&
         options.max_refinement_iterations <= kHardMaxRefinementIterations &&
         options.max_residual_evaluations > 0U &&
         options.max_residual_evaluations <= kHardMaxResidualEvaluations &&
         options.min_support_samples >= 3U &&
         options.min_support_samples <= options.max_input_samples &&
         options.min_distinct_support_points >= 3U &&
         options.min_distinct_support_points <= options.min_support_samples &&
         options.min_diversity_groups >= 1U &&
         options.min_diversity_groups <= options.min_support_samples &&
         std::isfinite(options.min_second_variance_m2) &&
         options.min_second_variance_m2 > 0.0 &&
         std::isfinite(options.min_second_to_largest_ratio) &&
         options.min_second_to_largest_ratio > 0.0 &&
         options.min_second_to_largest_ratio <= 1.0;
}

bool product_exceeds(size_t left, size_t right, size_t limit) {
  return right != 0U && left > limit / right;
}

} // namespace

Result fit(const std::vector<WeightedSample> &samples, const Options &options) {
  Result result;
  if (!valid_options(options))
    return result;
  if (samples.size() > options.max_input_samples ||
      samples.size() > kHardMaxSamples) {
    result.status = Status::kWorkLimitExceeded;
    return result;
  }
  if (samples.size() < options.min_support_samples) {
    result.status = Status::kInsufficientEvidence;
    return result;
  }
  for (const WeightedSample &sample : samples) {
    if (!finite(sample.point) || !std::isfinite(sample.weight)) {
      result.status = Status::kNonFiniteInput;
      return result;
    }
    if (!(sample.weight > 0.0)) {
      result.status = Status::kInvalidSampleWeight;
      return result;
    }
  }
  const size_t iteration_count =
      options.candidate_iterations + options.max_refinement_iterations;
  if (product_exceeds(iteration_count, samples.size(),
                      options.max_residual_evaluations)) {
    result.status = Status::kWorkLimitExceeded;
    return result;
  }

  DeterministicGenerator generator(options.seed);
  std::optional<Candidate> best;
  size_t residual_evaluations = 0U;
  bool encountered_numerical_failure = false;
  bool encountered_rank_deficiency = false;
  for (size_t iteration = 0U; iteration < options.candidate_iterations;
       ++iteration) {
    const auto indices = sample_triplet(&generator, samples.size());
    CorePlane candidate_plane;
    if (!make_three_point_plane(samples[indices[0]].point,
                                samples[indices[1]].point,
                                samples[indices[2]].point, &candidate_plane)) {
      continue;
    }
    Candidate candidate;
    candidate.plane = candidate_plane;
    std::vector<double> inlier_residuals;
    for (size_t index = 0U; index < samples.size(); ++index) {
      double distance = 0.0;
      ++residual_evaluations;
      if (!residual(candidate_plane, samples[index].point, &distance)) {
        encountered_numerical_failure = true;
        candidate.inliers.clear();
        break;
      }
      if (distance <= options.inlier_threshold_m) {
        candidate.inliers.push_back(index);
        inlier_residuals.push_back(distance);
      }
    }
    if (candidate.inliers.size() < options.min_support_samples)
      continue;
    // Distinct support cannot exceed sample count. This bound cannot prune a
    // winner under better_candidate's lexicographic ordering, even when many
    // observations share coordinates. Keep all residual evaluation and random
    // sampling unchanged; this is not probabilistic RANSAC early termination.
    if (best && candidate.inliers.size() < best->distinct_points)
      continue;
    if (best && candidate.inliers == best->inliers) {
      // Exact ordered support gives identical support facts and covariance.
      // Reuse only these support-dependent values, never the hypothesis plane
      // or its residuals. Keeping just the current best bounds cache memory.
      candidate.distinct_points = best->distinct_points;
      candidate.diversity_groups = best->diversity_groups;
      candidate.minor_variance = best->minor_variance;
    } else {
      const SupportFacts facts = support_facts(samples, candidate.inliers);
      if (facts.distinct_points < options.min_distinct_support_points ||
          facts.diversity_groups < options.min_diversity_groups) {
        continue;
      }
      candidate.distinct_points = facts.distinct_points;
      candidate.diversity_groups = facts.diversity_groups;
      // Only a strictly worse prefix may bypass the extent calculation. A tie
      // still needs extent, residual median and the canonical plane tie-break.
      if (best &&
          std::tuple{candidate.distinct_points, candidate.inliers.size(),
                     candidate.diversity_groups} <
              std::tuple{best->distinct_points, best->inliers.size(),
                         best->diversity_groups}) {
        continue;
      }
      CovarianceFit unweighted_fit;
      const Status extent_status = covariance_fit(
          samples, candidate.inliers, options, false, &unweighted_fit);
      if (extent_status != Status::kOk) {
        encountered_numerical_failure |=
            extent_status == Status::kNumericalFailure;
        encountered_rank_deficiency |= extent_status == Status::kRankDeficient;
        continue;
      }
      candidate.minor_variance = unweighted_fit.minor_variance;
    }
    candidate.median_residual = median(std::move(inlier_residuals));
    if (!best || better_candidate(candidate, *best))
      best = std::move(candidate);
  }
  if (!best) {
    result.status =
        encountered_numerical_failure
            ? Status::kNumericalFailure
            : (encountered_rank_deficiency ? Status::kRankDeficient
                                           : Status::kDegenerateInput);
    return result;
  }

  std::vector<size_t> support = best->inliers;
  CovarianceFit refined;
  std::vector<double> final_residuals;
  size_t refinements_run = 0U;
  bool converged = false;
  for (size_t iteration = 0U; iteration < options.max_refinement_iterations;
       ++iteration) {
    ++refinements_run;
    const Status refinement_status =
        covariance_fit(samples, support, options, true, &refined);
    if (refinement_status != Status::kOk) {
      result.status = refinement_status;
      return result;
    }
    std::vector<size_t> next_support;
    std::vector<double> next_residuals;
    next_support.reserve(support.size());
    next_residuals.reserve(support.size());
    for (size_t index = 0U; index < samples.size(); ++index) {
      double distance = 0.0;
      ++residual_evaluations;
      if (!residual(refined.plane, samples[index].point, &distance)) {
        result.status = Status::kNumericalFailure;
        return result;
      }
      if (distance <= options.inlier_threshold_m) {
        next_support.push_back(index);
        next_residuals.push_back(distance);
      }
    }
    if (next_support.size() < options.min_support_samples) {
      result.status = Status::kInsufficientEvidence;
      return result;
    }
    const SupportFacts facts = support_facts(samples, next_support);
    if (facts.distinct_points < options.min_distinct_support_points ||
        facts.diversity_groups < options.min_diversity_groups) {
      result.status = Status::kInsufficientEvidence;
      return result;
    }
    if (next_support == support) {
      support = std::move(next_support);
      final_residuals = std::move(next_residuals);
      converged = true;
      break;
    }
    support = std::move(next_support);
  }
  if (!converged) {
    result.status = Status::kRefinementDidNotConverge;
    return result;
  }

  const SupportFacts facts = support_facts(samples, support);
  double max_residual = 0.0;
  for (const double value : final_residuals)
    max_residual = std::max(max_residual, value);
  double normalized_squared_sum = 0.0;
  double normalized_weight_sum = 0.0;
  double max_weight = 0.0;
  for (const size_t index : support)
    max_weight = std::max(max_weight, samples[index].weight);
  if (max_residual > 0.0) {
    for (size_t offset = 0U; offset < support.size(); ++offset) {
      const double weight = samples[support[offset]].weight / max_weight;
      const double scaled_residual = final_residuals[offset] / max_residual;
      normalized_squared_sum += weight * scaled_residual * scaled_residual;
      normalized_weight_sum += weight;
    }
  }
  const double rms = max_residual == 0.0
                         ? 0.0
                         : max_residual * std::sqrt(normalized_squared_sum /
                                                    normalized_weight_sum);
  Plane plane;
  plane.normal = refined.plane.normal;
  plane.offset_m = refined.plane.offset;
  plane.residuals = {canonical_zero(rms),
                     canonical_zero(median(final_residuals)),
                     canonical_zero(max_residual)};
  plane.support = {support.size(), facts.distinct_points, refined.total_weight,
                   refined.major_variance, refined.minor_variance};
  plane.diversity = {facts.diversity_groups};
  plane.inlier_indices = std::move(support);
  plane.candidate_iterations_run = options.candidate_iterations;
  plane.refinement_iterations_run = refinements_run;
  plane.residual_evaluations = residual_evaluations;
  if (!finite(plane.normal) || !std::isfinite(plane.offset_m) ||
      !std::isfinite(plane.residuals.weighted_rms_m) ||
      !std::isfinite(plane.residuals.median_absolute_m) ||
      !std::isfinite(plane.residuals.maximum_absolute_m) ||
      !std::isfinite(plane.support.total_weight) ||
      !std::isfinite(plane.support.major_variance_m2) ||
      !std::isfinite(plane.support.minor_variance_m2)) {
    result.status = Status::kNumericalFailure;
    return result;
  }
  result.status = Status::kOk;
  result.plane = std::move(plane);
  return result;
}

} // namespace hss::plane_fit

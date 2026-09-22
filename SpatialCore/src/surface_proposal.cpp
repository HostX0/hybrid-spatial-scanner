#include "hss/surface_proposal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hss::surface_proposal {
namespace {

struct SupportedInterval {
  size_t sample_count{0U};
  size_t diversity_group_count{0U};
  double minimum_parameter_m{0.0};
  double maximum_parameter_m{0.0};
  double minimum_distance_m{std::numeric_limits<double>::infinity()};
  std::vector<std::string> source_ids;
};

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool valid_options(const Options &options) {
  return options.max_support_samples_per_surface >= 3U &&
         options.max_support_samples_per_surface <=
             kHardMaxSupportSamplesPerSurface &&
         options.min_fit_support_samples >= 3U &&
         options.min_fit_support_samples <=
             options.max_support_samples_per_surface &&
         options.min_fit_distinct_points >= 3U &&
         options.min_fit_distinct_points <= options.min_fit_support_samples &&
         options.min_fit_diversity_groups > 0U &&
         options.min_fit_diversity_groups <= options.min_fit_support_samples &&
         options.min_intersection_support_samples >= 2U &&
         options.min_intersection_support_samples <=
             options.max_support_samples_per_surface &&
         options.min_intersection_diversity_groups > 0U &&
         options.min_intersection_diversity_groups <=
             options.min_intersection_support_samples &&
         finite_positive(options.unit_normal_tolerance) &&
         options.unit_normal_tolerance < 1.0 &&
         finite_positive(options.max_fit_residual_m) &&
         finite_positive(options.max_support_plane_residual_m) &&
         finite_positive(options.min_intersection_sine) &&
         options.min_intersection_sine < 1.0 &&
         finite_positive(options.max_support_distance_to_intersection_m) &&
         finite_positive(options.min_supported_intersection_length_m) &&
         finite_positive(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m >
             options.max_support_distance_to_intersection_m &&
         options.max_abs_coordinate_m >
             options.min_supported_intersection_length_m;
}

bool lowercase_hex(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

bool valid_uuid(std::string_view id) {
  if (id.size() != 36U)
    return false;
  bool nonzero = false;
  for (size_t index = 0U; index < id.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (id[index] != '-')
        return false;
      continue;
    }
    if (!lowercase_hex(id[index]))
      return false;
    nonzero = nonzero || id[index] != '0';
  }
  const bool supported_version = id[14] >= '1' && id[14] <= '8';
  const bool rfc_variant = id[19] == '8' || id[19] == '9' ||
                           id[19] == 'a' || id[19] == 'b';
  return nonzero && supported_version && rfc_variant;
}

bool finite_point(const Point3d &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool point_in_extent(const Point3d &point, double extent) {
  return std::abs(point.x) <= extent && std::abs(point.y) <= extent &&
         std::abs(point.z) <= extent;
}

double dot(const Point3d &left, const Point3d &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Point3d cross(const Point3d &left, const Point3d &right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Point3d subtract(const Point3d &left, const Point3d &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Point3d scaled(const Point3d &value, double scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

Point3d add(const Point3d &left, const Point3d &right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

double canonical_zero(double value) { return value == 0.0 ? 0.0 : value; }

Point3d canonical_zero(Point3d point) {
  point.x = canonical_zero(point.x);
  point.y = canonical_zero(point.y);
  point.z = canonical_zero(point.z);
  return point;
}

Point3d canonical_direction(Point3d direction) {
  size_t largest = 0U;
  const std::array<double, 3> values{direction.x, direction.y, direction.z};
  for (size_t index = 1U; index < values.size(); ++index) {
    if (std::abs(values[index]) > std::abs(values[largest]))
      largest = index;
  }
  if (values[largest] < 0.0)
    direction = scaled(direction, -1.0);
  return canonical_zero(direction);
}

std::vector<std::string>
unique_sources(const std::vector<SupportSample> &samples) {
  std::vector<std::string> result;
  result.reserve(samples.size());
  std::unordered_set<std::string> seen;
  seen.reserve(samples.size());
  for (const SupportSample &sample : samples) {
    if (seen.insert(sample.source_id).second)
      result.push_back(sample.source_id);
  }
  return result;
}

Status validate_surface(const SurfaceCandidate &surface,
                        const Options &options) {
  if (!valid_uuid(surface.surface_id))
    return Status::kInvalidId;
  if (surface.support.size() > options.max_support_samples_per_surface ||
      surface.support.size() > kHardMaxSupportSamplesPerSurface)
    return Status::kResourceLimitExceeded;
  if (!finite_point(surface.normal) || !std::isfinite(surface.offset_m))
    return Status::kNonFiniteInput;

  const double normal_length =
      std::hypot(surface.normal.x, surface.normal.y, surface.normal.z);
  if (!std::isfinite(normal_length) ||
      std::abs(normal_length - 1.0) > options.unit_normal_tolerance)
    return Status::kInvalidPlane;

  const FitDiagnostics &fit = surface.fit;
  if (!std::isfinite(fit.total_weight) ||
      !std::isfinite(fit.major_variance_m2) ||
      !std::isfinite(fit.minor_variance_m2) ||
      !std::isfinite(fit.weighted_rms_m) ||
      !std::isfinite(fit.median_absolute_m) ||
      !std::isfinite(fit.maximum_absolute_m))
    return Status::kNonFiniteInput;
  if (fit.sample_count != surface.support.size() ||
      fit.sample_count < options.min_fit_support_samples ||
      fit.distinct_point_count < options.min_fit_distinct_points ||
      fit.distinct_point_count > fit.sample_count ||
      fit.diversity_group_count < options.min_fit_diversity_groups ||
      fit.diversity_group_count > fit.sample_count ||
      !(fit.total_weight > 0.0) || !(fit.major_variance_m2 > 0.0) ||
      !(fit.minor_variance_m2 > 0.0) ||
      fit.minor_variance_m2 > fit.major_variance_m2 ||
      fit.weighted_rms_m < 0.0 || fit.median_absolute_m < 0.0 ||
      fit.maximum_absolute_m < 0.0 ||
      fit.weighted_rms_m > fit.maximum_absolute_m ||
      fit.median_absolute_m > fit.maximum_absolute_m ||
      fit.maximum_absolute_m > options.max_fit_residual_m ||
      fit.candidate_iterations_run == 0U ||
      fit.refinement_iterations_run == 0U ||
      fit.residual_evaluations < fit.sample_count)
    return Status::kInvalidFitDiagnostics;

  std::set<std::array<double, 3>> distinct_points;
  std::unordered_set<uint64_t> diversity_groups;
  diversity_groups.reserve(surface.support.size());
  double total_weight = 0.0;
  for (const SupportSample &sample : surface.support) {
    if (!valid_uuid(sample.source_id))
      return Status::kInvalidId;
    if (!finite_point(sample.point_m) || !std::isfinite(sample.weight))
      return Status::kNonFiniteInput;
    if (!point_in_extent(sample.point_m, options.max_abs_coordinate_m))
      return Status::kCoordinateOutOfRange;
    if (!(sample.weight > 0.0))
      return Status::kInvalidFitDiagnostics;
    total_weight += sample.weight;
    if (!std::isfinite(total_weight))
      return Status::kNumericalFailure;
    distinct_points.insert(
        {canonical_zero(sample.point_m.x), canonical_zero(sample.point_m.y),
         canonical_zero(sample.point_m.z)});
    diversity_groups.insert(sample.diversity_group);
    const double plane_residual =
        std::abs(dot(surface.normal, sample.point_m) + surface.offset_m);
    if (!std::isfinite(plane_residual))
      return Status::kNumericalFailure;
    if (plane_residual > options.max_support_plane_residual_m)
      return Status::kInvalidPlane;
  }

  const double weight_tolerance =
      64.0 * std::numeric_limits<double>::epsilon() *
      std::max({1.0, std::abs(total_weight), std::abs(fit.total_weight)});
  if (distinct_points.size() != fit.distinct_point_count ||
      diversity_groups.size() != fit.diversity_group_count ||
      std::abs(total_weight - fit.total_weight) > weight_tolerance)
    return Status::kInvalidFitDiagnostics;
  return Status::kOk;
}

SupportedInterval supported_interval(const SurfaceCandidate &surface,
                                     const Point3d &line_anchor,
                                     const Point3d &line_direction,
                                     const Options &options) {
  SupportedInterval interval;
  std::unordered_set<uint64_t> groups;
  std::unordered_set<std::string> sources;
  groups.reserve(surface.support.size());
  sources.reserve(surface.support.size());
  bool initialized = false;
  for (const SupportSample &sample : surface.support) {
    const Point3d delta = subtract(sample.point_m, line_anchor);
    const double parameter = dot(delta, line_direction);
    const Point3d perpendicular =
        subtract(delta, scaled(line_direction, parameter));
    const double distance =
        std::hypot(perpendicular.x, perpendicular.y, perpendicular.z);
    interval.minimum_distance_m =
        std::min(interval.minimum_distance_m, distance);
    if (distance > options.max_support_distance_to_intersection_m)
      continue;
    if (!initialized) {
      interval.minimum_parameter_m = parameter;
      interval.maximum_parameter_m = parameter;
      initialized = true;
    } else {
      interval.minimum_parameter_m =
          std::min(interval.minimum_parameter_m, parameter);
      interval.maximum_parameter_m =
          std::max(interval.maximum_parameter_m, parameter);
    }
    ++interval.sample_count;
    groups.insert(sample.diversity_group);
    if (sources.insert(sample.source_id).second)
      interval.source_ids.push_back(sample.source_id);
  }
  interval.diversity_group_count = groups.size();
  return interval;
}

Result failure(Status status) { return {status, std::nullopt}; }

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kResourceLimitExceeded:
    return "resource_limit_exceeded";
  case Status::kInvalidId:
    return "invalid_id";
  case Status::kDuplicateSurface:
    return "duplicate_surface";
  case Status::kNonFiniteInput:
    return "nonfinite_input";
  case Status::kCoordinateOutOfRange:
    return "coordinate_out_of_range";
  case Status::kInvalidPlane:
    return "invalid_plane";
  case Status::kInvalidFitDiagnostics:
    return "invalid_fit_diagnostics";
  case Status::kCrossEpoch:
    return "cross_epoch";
  case Status::kNearlyParallel:
    return "nearly_parallel";
  case Status::kInsufficientIntersectionSupport:
    return "insufficient_intersection_support";
  case Status::kExtrapolatedIntersection:
    return "extrapolated_intersection";
  case Status::kNumericalFailure:
    return "numerical_failure";
  }
  return "unknown";
}

Result propose_intersection(const SurfaceCandidate &first,
                            const SurfaceCandidate &second,
                            const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  Status status = validate_surface(first, options);
  if (status != Status::kOk)
    return failure(status);
  status = validate_surface(second, options);
  if (status != Status::kOk)
    return failure(status);
  if (first.surface_id == second.surface_id)
    return failure(Status::kDuplicateSurface);
  if (first.epoch_id != second.epoch_id)
    return failure(Status::kCrossEpoch);

  const Point3d direction_cross = cross(first.normal, second.normal);
  const double sine = std::hypot(direction_cross.x, direction_cross.y,
                                 direction_cross.z);
  if (!std::isfinite(sine))
    return failure(Status::kNumericalFailure);
  if (sine <= options.min_intersection_sine)
    return failure(Status::kNearlyParallel);
  Point3d direction = canonical_direction(scaled(direction_cross, 1.0 / sine));

  const Point3d offset_combination =
      subtract(scaled(first.normal, second.offset_m),
               scaled(second.normal, first.offset_m));
  const Point3d anchor_numerator = cross(offset_combination, direction_cross);
  const double sine_squared = sine * sine;
  Point3d anchor = scaled(anchor_numerator, 1.0 / sine_squared);
  if (!finite_point(anchor) || !finite_point(direction))
    return failure(Status::kNumericalFailure);

  const SupportedInterval first_interval =
      supported_interval(first, anchor, direction, options);
  const SupportedInterval second_interval =
      supported_interval(second, anchor, direction, options);
  if (!std::isfinite(first_interval.minimum_distance_m) ||
      !std::isfinite(second_interval.minimum_distance_m))
    return failure(Status::kNumericalFailure);
  if (first_interval.minimum_distance_m >
          options.max_support_distance_to_intersection_m ||
      second_interval.minimum_distance_m >
          options.max_support_distance_to_intersection_m)
    return failure(Status::kExtrapolatedIntersection);
  if (first_interval.sample_count <
          options.min_intersection_support_samples ||
      second_interval.sample_count <
          options.min_intersection_support_samples ||
      first_interval.diversity_group_count <
          options.min_intersection_diversity_groups ||
      second_interval.diversity_group_count <
          options.min_intersection_diversity_groups)
    return failure(Status::kInsufficientIntersectionSupport);

  const double overlap_start =
      std::max(first_interval.minimum_parameter_m,
               second_interval.minimum_parameter_m);
  const double overlap_end =
      std::min(first_interval.maximum_parameter_m,
               second_interval.maximum_parameter_m);
  if (!std::isfinite(overlap_start) || !std::isfinite(overlap_end))
    return failure(Status::kNumericalFailure);
  if (overlap_end < overlap_start)
    return failure(Status::kExtrapolatedIntersection);
  if (overlap_end - overlap_start <
      options.min_supported_intersection_length_m)
    return failure(Status::kInsufficientIntersectionSupport);

  Point3d start =
      canonical_zero(add(anchor, scaled(direction, overlap_start)));
  Point3d end = canonical_zero(add(anchor, scaled(direction, overlap_end)));
  if (!finite_point(start) || !finite_point(end))
    return failure(Status::kNumericalFailure);
  if (!point_in_extent(start, options.max_abs_coordinate_m) ||
      !point_in_extent(end, options.max_abs_coordinate_m))
    return failure(Status::kCoordinateOutOfRange);

  const double normal_dot =
      std::clamp(dot(first.normal, second.normal), -1.0, 1.0);
  const double normal_angle = std::acos(normal_dot);
  const double condition_number = 1.0 / sine;
  if (!std::isfinite(normal_angle) || !std::isfinite(condition_number))
    return failure(Status::kNumericalFailure);

  SurfaceContribution first_contribution{
      first.surface_id,
      first.fit,
      unique_sources(first.support),
      first_interval.source_ids,
      first_interval.sample_count,
      first_interval.diversity_group_count,
      canonical_zero(first_interval.minimum_parameter_m),
      canonical_zero(first_interval.maximum_parameter_m),
  };
  SurfaceContribution second_contribution{
      second.surface_id,
      second.fit,
      unique_sources(second.support),
      second_interval.source_ids,
      second_interval.sample_count,
      second_interval.diversity_group_count,
      canonical_zero(second_interval.minimum_parameter_m),
      canonical_zero(second_interval.maximum_parameter_m),
  };
  IntersectionProposal proposal{
      first.epoch_id,
      start,
      end,
      direction,
      canonical_zero(normal_angle),
      canonical_zero(sine),
      canonical_zero(condition_number),
      std::move(first_contribution),
      std::move(second_contribution),
  };
  return {Status::kOk, std::move(proposal)};
}

} // namespace hss::surface_proposal

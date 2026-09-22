#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hss::plane_fit {

inline constexpr size_t kHardMaxSamples = 65'536U;
inline constexpr size_t kHardMaxCandidateIterations = 4'096U;
inline constexpr size_t kHardMaxRefinementIterations = 16U;
inline constexpr size_t kHardMaxResidualEvaluations = 50'000'000U;

struct Point3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  bool operator==(const Point3d &) const = default;
};

// diversity_group identifies correlated observations, such as samples from one
// frame group. It is used in candidate ranking and reported with the fit; it is
// not inferred from point count.
struct WeightedSample {
  Point3d point;
  double weight{1.0};
  uint64_t diversity_group{0U};
  bool operator==(const WeightedSample &) const = default;
};

struct Options {
  // Deliberately has no usable default. This is caller-declared tuning, not an
  // accuracy statement or a fixed sensor property.
  double inlier_threshold_m{0.0};
  uint64_t seed{101U};
  size_t candidate_iterations{200U};
  size_t max_input_samples{4'096U};
  size_t max_refinement_iterations{6U};
  size_t max_residual_evaluations{2'000'000U};
  size_t min_support_samples{6U};
  size_t min_distinct_support_points{6U};
  size_t min_diversity_groups{1U};
  // Numeric stability guards, expressed as weighted covariance variance.
  double min_second_variance_m2{1.0e-12};
  double min_second_to_largest_ratio{1.0e-8};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kNonFiniteInput,
  kInvalidSampleWeight,
  kInsufficientEvidence,
  kDegenerateInput,
  kRankDeficient,
  kWorkLimitExceeded,
  kRefinementDidNotConverge,
  kNumericalFailure,
};

struct ResidualSummary {
  double weighted_rms_m{0.0};
  double median_absolute_m{0.0};
  double maximum_absolute_m{0.0};
  bool operator==(const ResidualSummary &) const = default;
};

struct SupportSummary {
  size_t sample_count{0U};
  size_t distinct_point_count{0U};
  double total_weight{0.0};
  // Weighted in-plane covariance variances, largest first.
  double major_variance_m2{0.0};
  double minor_variance_m2{0.0};
  bool operator==(const SupportSummary &) const = default;
};

struct DiversitySummary {
  size_t group_count{0U};
  bool operator==(const DiversitySummary &) const = default;
};

struct Plane {
  // Canonical equation normal dot point + offset_m = 0. The normal is unit
  // length. Its largest-magnitude component is positive; ties use x, y, z.
  Point3d normal;
  double offset_m{0.0};
  ResidualSummary residuals;
  SupportSummary support;
  DiversitySummary diversity;
  std::vector<size_t> inlier_indices;
  size_t candidate_iterations_run{0U};
  size_t refinement_iterations_run{0U};
  size_t residual_evaluations{0U};
  bool operator==(const Plane &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Empty for every failure status: no partial plane is observable.
  std::optional<Plane> plane;
};

// Deterministic robust candidate selection followed by weighted total least
// squares over the selected support. No axis, gravity, or Manhattan prior is
// applied. The function never merges candidates; returned indices identify
// only the threshold-supported samples used by this fit.
Result fit(const std::vector<WeightedSample> &samples, const Options &options);

} // namespace hss::plane_fit

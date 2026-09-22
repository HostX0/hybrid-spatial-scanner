#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::surface_proposal {

inline constexpr size_t kHardMaxSupportSamplesPerSurface = 65'536U;

struct Point3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  bool operator==(const Point3d &) const = default;
};

// A retained fitted-plane support sample. source_id identifies its source
// observation and diversity_group identifies correlated observations such as
// one frame group. Neither is inferred from the number of coordinates.
struct SupportSample {
  Point3d point_m;
  double weight{1.0};
  uint64_t diversity_group{0U};
  std::string source_id;
  bool operator==(const SupportSample &) const = default;
};

// Diagnostics are supplied by the fit that produced the candidate and are
// preserved in an accepted intersection. They describe numerical support, not
// physical accuracy or a semantic surface category.
struct FitDiagnostics {
  size_t sample_count{0U};
  size_t distinct_point_count{0U};
  size_t diversity_group_count{0U};
  double total_weight{0.0};
  double major_variance_m2{0.0};
  double minor_variance_m2{0.0};
  double weighted_rms_m{0.0};
  double median_absolute_m{0.0};
  double maximum_absolute_m{0.0};
  size_t candidate_iterations_run{0U};
  size_t refinement_iterations_run{0U};
  size_t residual_evaluations{0U};
  bool operator==(const FitDiagnostics &) const = default;
};

struct SurfaceCandidate {
  // Surface and source identifiers are canonical lowercase non-nil UUIDs.
  std::string surface_id;
  uint64_t epoch_id{0U};
  // normal dot point + offset_m = 0. The normal must already be unit length;
  // this module neither re-fits nor rotates it toward an axis.
  Point3d normal;
  double offset_m{0.0};
  FitDiagnostics fit;
  std::vector<SupportSample> support;
  bool operator==(const SurfaceCandidate &) const = default;
};

struct Options {
  size_t max_support_samples_per_surface{4'096U};
  size_t min_fit_support_samples{0U};
  size_t min_fit_distinct_points{0U};
  size_t min_fit_diversity_groups{0U};
  size_t min_intersection_support_samples{0U};
  size_t min_intersection_diversity_groups{0U};

  // Deliberately unusable geometric defaults. These are caller-declared
  // numeric/evidence policy, not sensor accuracy statements.
  double unit_normal_tolerance{0.0};
  double max_fit_residual_m{0.0};
  double max_support_plane_residual_m{0.0};
  double min_intersection_sine{0.0};
  double max_support_distance_to_intersection_m{0.0};
  double min_supported_intersection_length_m{0.0};
  double max_abs_coordinate_m{0.0};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kResourceLimitExceeded,
  kInvalidId,
  kDuplicateSurface,
  kNonFiniteInput,
  kCoordinateOutOfRange,
  kInvalidPlane,
  kInvalidFitDiagnostics,
  kCrossEpoch,
  kNearlyParallel,
  kInsufficientIntersectionSupport,
  kExtrapolatedIntersection,
  kNumericalFailure,
};

struct SurfaceContribution {
  std::string surface_id;
  FitDiagnostics fit;
  // Unique IDs preserve first occurrence order. all_source_ids describes the
  // complete fit support; intersection_source_ids describes samples that
  // directly bounded the accepted line segment.
  std::vector<std::string> all_source_ids;
  std::vector<std::string> intersection_source_ids;
  size_t intersection_sample_count{0U};
  size_t intersection_diversity_group_count{0U};
  double supported_parameter_min_m{0.0};
  double supported_parameter_max_m{0.0};
  bool operator==(const SurfaceContribution &) const = default;
};

struct IntersectionProposal {
  uint64_t epoch_id{0U};
  Point3d start_m;
  Point3d end_m;
  // Canonical unit line direction: its largest-magnitude component is
  // positive (ties x, y, z). No angle is snapped or orthogonalized.
  Point3d direction;
  double normal_angle_rad{0.0};
  double conditioning_sine{0.0};
  double condition_number{0.0};
  SurfaceContribution first;
  SurfaceContribution second;
  bool operator==(const IntersectionProposal &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Empty for every failure status; no unsupported infinite line or partial
  // endpoint is observable.
  std::optional<IntersectionProposal> proposal;
};

const char *status_name(Status status);

// Proposes the overlap of the two surfaces' directly supported intervals on
// their conditioned intersection line. It never creates a boundary, merges a
// surface, crosses epochs, extrapolates a gap, or imposes a Manhattan prior.
Result propose_intersection(const SurfaceCandidate &first,
                            const SurfaceCandidate &second,
                            const Options &options);

} // namespace hss::surface_proposal

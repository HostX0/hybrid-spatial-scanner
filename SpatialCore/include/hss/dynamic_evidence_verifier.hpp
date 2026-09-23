#pragma once

#include "hss/dynamic_provenance.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::dynamic_evidence_verifier {

using Bounds3d = dynamic_provenance::Bounds3d;
using Point3d = dynamic_provenance::Point3d;
using SourceReference = dynamic_provenance::SourceReference;

inline constexpr size_t kHardMaxObservations = 64U;

enum class Verification : uint8_t {
  kUnknown,
  kAccepted,
  kRejected,
};

enum class TrackingState : uint8_t {
  kNormal,
  kLimited,
  kUnavailable,
};

enum class SupportKind : uint8_t {
  kCalibratedDepthPoint,
  kCalibratedMeshPoint,
  kSemanticOnly,
};

// One owned, retained observation of the same geometric correspondence. The
// point is expressed in its exact captured source's local coordinates. The
// verifier validates project_from_source and computes the project point; a
// caller-provided project point is deliberately not accepted.
struct Observation {
  std::string project_id;
  SourceReference source;
  SupportKind support_kind{SupportKind::kSemanticOnly};
  std::string correspondence_id;
  std::string pose_id;
  std::string calibration_id;
  // Observations with the same independence_group_id are correlated and may
  // not jointly establish motion (for example sceneDepth and a smoothed copy).
  std::string independence_group_id;
  TrackingState tracking_state{TrackingState::kUnavailable};
  Verification calibration{Verification::kUnknown};
  Verification pose{Verification::kUnknown};
  Verification epoch_relationship{Verification::kUnknown};
  Verification retained_source{Verification::kUnknown};
  uint64_t epoch_transform_revision{0U};
  double monotonic_time_s{0.0};
  // Column-major rigid transform into canonical project metres.
  std::array<double, 16U> project_from_source{};
  Point3d source_point_m;
  // Absolute radial bounds, not a probability or ordinal confidence value.
  double position_uncertainty_m{0.0};
  // Upstream retained-geometry matching residual for this correspondence.
  double correspondence_residual_m{0.0};
  // Distance to the nearest retained depth/mesh discontinuity or occlusion
  // boundary in project metres.
  double discontinuity_clearance_m{0.0};
  bool operator==(const Observation &) const = default;
};

struct Request {
  std::string evidence_id;
  std::string exclusion_command_id;
  std::string project_id;
  std::string correspondence_id;
  SourceReference target_source;
  // Exact retained support bounds for target_source and the command's bounds.
  Bounds3d target_source_bounds_m;
  Bounds3d exclusion_bounds_m;
  uint64_t start_sequence{0U};
  uint64_t end_sequence{0U};
};

struct Options {
  size_t minimum_observations{3U};
  size_t minimum_motion_confirmations{2U};
  size_t max_observations{16U};
  double max_position_uncertainty_m{0.02};
  double min_verified_displacement_m{0.05};
  double max_correspondence_residual_m{0.01};
  double max_motion_model_residual_m{0.02};
  double min_discontinuity_clearance_m{0.03};
  double min_time_separation_s{0.10};
  double max_time_span_s{10.0};
  double max_abs_coordinate_m{1'000.0};
  double rigid_tolerance{1.0e-6};
};

enum class Status : uint8_t {
  kVerified,
  kInvalidOptions,
  kInvalidRequest,
  kInputLimitExceeded,
  kInsufficientObservations,
  kInvalidObservation,
  kWrongProject,
  kWrongCapture,
  kWrongEpoch,
  kWrongSequence,
  kTargetSourceMissing,
  kDuplicateSource,
  kCorrelatedObservations,
  kSemanticOnly,
  kInvalidTracking,
  kInvalidCalibration,
  kInvalidPose,
  kUnacceptedEpoch,
  kSourceNotRetained,
  kUncertaintyTooLarge,
  kOcclusionEdge,
  kUnstableCorrespondence,
  kInsufficientMotion,
  kResourceLimit,
  kInternalFailure,
};

struct Diagnostics {
  size_t observation_count{0U};
  size_t motion_confirmation_count{0U};
  double minimum_adjusted_displacement_m{0.0};
  double maximum_adjusted_motion_residual_m{0.0};
  double time_span_s{0.0};
  bool operator==(const Diagnostics &) const = default;
};

struct Result {
  Status status{Status::kInvalidRequest};
  std::optional<dynamic_provenance::VerifiedDynamicEvidence> evidence;
  // Populated only on kVerified. Failed input never receives partial proof.
  std::optional<Diagnostics> diagnostics;
};

// Requires independently retained, calibrated geometric observations. A
// command, semantic label, classification score, or single source cannot
// produce VerifiedDynamicEvidence. Inputs are immutable and no geometry is
// rewritten. Thresholds are explicit policy values, not physical calibration.
[[nodiscard]] Result verify(
    const Request &request, const std::vector<Observation> &observations,
    const Options &options = {});

[[nodiscard]] const char *status_name(Status status);

} // namespace hss::dynamic_evidence_verifier

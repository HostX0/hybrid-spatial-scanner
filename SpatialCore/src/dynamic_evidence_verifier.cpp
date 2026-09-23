#include "hss/dynamic_evidence_verifier.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace hss::dynamic_evidence_verifier {
namespace {

bool valid_uuid(std::string_view value) {
  if (value.size() != 36U)
    return false;
  for (size_t index = 0U; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-')
        return false;
      continue;
    }
    const char character = value[index];
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool valid(Verification value) {
  switch (value) {
  case Verification::kUnknown:
  case Verification::kAccepted:
  case Verification::kRejected:
    return true;
  }
  return false;
}

bool valid(TrackingState value) {
  switch (value) {
  case TrackingState::kNormal:
  case TrackingState::kLimited:
  case TrackingState::kUnavailable:
    return true;
  }
  return false;
}

bool valid(SupportKind value) {
  switch (value) {
  case SupportKind::kCalibratedDepthPoint:
  case SupportKind::kCalibratedMeshPoint:
  case SupportKind::kSemanticOnly:
    return true;
  }
  return false;
}

bool valid(dynamic_provenance::SourceKind value) {
  switch (value) {
  case dynamic_provenance::SourceKind::kDepthFrame:
  case dynamic_provenance::SourceKind::kMeshAnchor:
    return true;
  }
  return false;
}

bool finite(const Point3d &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool finite_matrix(const std::array<double, 16U> &matrix) {
  return std::all_of(matrix.begin(), matrix.end(),
                     [](double value) { return std::isfinite(value); });
}

bool within_extent(const Point3d &point, double maximum) {
  return std::abs(point.x) <= maximum && std::abs(point.y) <= maximum &&
         std::abs(point.z) <= maximum;
}

bool ordered(const Bounds3d &bounds) {
  return bounds.min_m.x <= bounds.max_m.x &&
         bounds.min_m.y <= bounds.max_m.y &&
         bounds.min_m.z <= bounds.max_m.z;
}

bool contains_with_margin(const Bounds3d &bounds, const Point3d &point,
                          double margin) {
  return point.x - margin >= bounds.min_m.x &&
         point.x + margin <= bounds.max_m.x &&
         point.y - margin >= bounds.min_m.y &&
         point.y + margin <= bounds.max_m.y &&
         point.z - margin >= bounds.min_m.z &&
         point.z + margin <= bounds.max_m.z;
}

bool contains(const Bounds3d &outer, const Bounds3d &inner) {
  return contains_with_margin(outer, inner.min_m, 0.0) &&
         contains_with_margin(outer, inner.max_m, 0.0);
}

bool valid_bounds(const Bounds3d &bounds, double maximum) {
  return finite(bounds.min_m) && finite(bounds.max_m) && ordered(bounds) &&
         within_extent(bounds.min_m, maximum) &&
         within_extent(bounds.max_m, maximum);
}

using SourceKey =
    std::tuple<uint8_t, std::string, std::string, uint64_t, uint64_t>;

SourceKey source_key(const SourceReference &source) {
  return {static_cast<uint8_t>(source.kind), source.source_id,
          source.capture_id, source.session_epoch, source.event_sequence};
}

bool valid_source(const SourceReference &source) {
  return valid(source.kind) && valid_uuid(source.source_id) &&
         valid_uuid(source.capture_id) && source.session_epoch != 0U &&
         source.event_sequence != 0U;
}

bool valid_rigid_transform(const std::array<double, 16U> &matrix,
                           double tolerance) {
  if (!finite_matrix(matrix) || std::abs(matrix[3]) > tolerance ||
      std::abs(matrix[7]) > tolerance ||
      std::abs(matrix[11]) > tolerance ||
      std::abs(matrix[15] - 1.0) > tolerance) {
    return false;
  }
  const auto dot_column = [&](size_t left, size_t right) {
    return std::fma(matrix[left], matrix[right],
                    std::fma(matrix[left + 1U], matrix[right + 1U],
                             matrix[left + 2U] * matrix[right + 2U]));
  };
  for (size_t column = 0U; column < 3U; ++column) {
    if (std::abs(dot_column(4U * column, 4U * column) - 1.0) >
        tolerance) {
      return false;
    }
  }
  if (std::abs(dot_column(0U, 4U)) > tolerance ||
      std::abs(dot_column(0U, 8U)) > tolerance ||
      std::abs(dot_column(4U, 8U)) > tolerance) {
    return false;
  }
  const double determinant =
      matrix[0] * (matrix[5] * matrix[10] - matrix[9] * matrix[6]) -
      matrix[4] * (matrix[1] * matrix[10] - matrix[9] * matrix[2]) +
      matrix[8] * (matrix[1] * matrix[6] - matrix[5] * matrix[2]);
  return std::abs(determinant - 1.0) <= 4.0 * tolerance;
}

Point3d transform(const std::array<double, 16U> &matrix,
                  const Point3d &point) {
  return {std::fma(matrix[8], point.z,
                   std::fma(matrix[4], point.y,
                            std::fma(matrix[0], point.x, matrix[12]))),
          std::fma(matrix[9], point.z,
                   std::fma(matrix[5], point.y,
                            std::fma(matrix[1], point.x, matrix[13]))),
          std::fma(matrix[10], point.z,
                   std::fma(matrix[6], point.y,
                            std::fma(matrix[2], point.x, matrix[14])))};
}

Point3d subtract(const Point3d &left, const Point3d &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double distance(const Point3d &left, const Point3d &right) {
  const Point3d delta = subtract(left, right);
  return std::hypot(delta.x, delta.y, delta.z);
}

Point3d interpolate(const Point3d &first, const Point3d &last, double alpha) {
  return {std::lerp(first.x, last.x, alpha),
          std::lerp(first.y, last.y, alpha),
          std::lerp(first.z, last.z, alpha)};
}

bool valid_options(const Options &options) {
  return options.minimum_observations >= 3U &&
         options.max_observations >= options.minimum_observations &&
         options.max_observations <= kHardMaxObservations &&
         options.minimum_motion_confirmations >= 2U &&
         options.minimum_motion_confirmations < options.minimum_observations &&
         std::isfinite(options.max_position_uncertainty_m) &&
         options.max_position_uncertainty_m > 0.0 &&
         std::isfinite(options.min_verified_displacement_m) &&
         options.min_verified_displacement_m > 0.0 &&
         std::isfinite(options.max_correspondence_residual_m) &&
         options.max_correspondence_residual_m >= 0.0 &&
         std::isfinite(options.max_motion_model_residual_m) &&
         options.max_motion_model_residual_m >= 0.0 &&
         std::isfinite(options.min_discontinuity_clearance_m) &&
         options.min_discontinuity_clearance_m > 0.0 &&
         std::isfinite(options.min_time_separation_s) &&
         options.min_time_separation_s > 0.0 &&
         std::isfinite(options.max_time_span_s) &&
         options.max_time_span_s >=
             options.min_time_separation_s *
                 static_cast<double>(options.minimum_observations - 1U) &&
         std::isfinite(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > 0.0 &&
         std::isfinite(options.rigid_tolerance) &&
         options.rigid_tolerance > 0.0 && options.rigid_tolerance <= 1.0e-3;
}

struct EvaluatedObservation {
  const Observation *observation{nullptr};
  Point3d project_point_m;
};

Result failure(Status status) {
  Result result;
  result.status = status;
  return result;
}

} // namespace

Result verify(const Request &request,
              const std::vector<Observation> &observations,
              const Options &options) {
  try {
    if (!valid_options(options))
      return failure(Status::kInvalidOptions);
    if (!valid_uuid(request.evidence_id) ||
        !valid_uuid(request.exclusion_command_id) ||
        !valid_uuid(request.project_id) ||
        !valid_uuid(request.correspondence_id) ||
        !valid_source(request.target_source) || request.start_sequence == 0U ||
        request.end_sequence < request.start_sequence ||
        !valid_bounds(request.target_source_bounds_m,
                      options.max_abs_coordinate_m) ||
        !valid_bounds(request.exclusion_bounds_m,
                      options.max_abs_coordinate_m) ||
        !contains(request.exclusion_bounds_m,
                  request.target_source_bounds_m)) {
      return failure(Status::kInvalidRequest);
    }
    if (request.target_source.event_sequence < request.start_sequence ||
        request.target_source.event_sequence > request.end_sequence) {
      return failure(Status::kWrongSequence);
    }
    if (observations.size() < options.minimum_observations)
      return failure(Status::kInsufficientObservations);
    if (observations.size() > options.max_observations)
      return failure(Status::kInputLimitExceeded);

    std::vector<EvaluatedObservation> evaluated;
    evaluated.reserve(observations.size());
    std::set<SourceKey> source_keys;
    std::set<std::string> pose_ids;
    std::set<std::string> independence_groups;
    size_t target_count = 0U;
    uint64_t accepted_epoch_transform_revision = 0U;

    for (const Observation &observation : observations) {
      if (!valid_source(observation.source) ||
          !valid(observation.support_kind) ||
          !valid(observation.tracking_state) ||
          !valid(observation.calibration) || !valid(observation.pose) ||
          !valid(observation.epoch_relationship) ||
          !valid(observation.retained_source) ||
          !valid_uuid(observation.project_id) ||
          !valid_uuid(observation.correspondence_id) ||
          !valid_uuid(observation.pose_id) ||
          !valid_uuid(observation.calibration_id) ||
          !valid_uuid(observation.independence_group_id) ||
          !std::isfinite(observation.monotonic_time_s) ||
          observation.monotonic_time_s < 0.0 ||
          !finite(observation.source_point_m) ||
          !std::isfinite(observation.position_uncertainty_m) ||
          observation.position_uncertainty_m <= 0.0 ||
          !std::isfinite(observation.correspondence_residual_m) ||
          observation.correspondence_residual_m < 0.0 ||
          !std::isfinite(observation.discontinuity_clearance_m) ||
          observation.discontinuity_clearance_m < 0.0) {
        return failure(Status::kInvalidObservation);
      }
      if (observation.project_id != request.project_id)
        return failure(Status::kWrongProject);
      if (observation.source.capture_id != request.target_source.capture_id)
        return failure(Status::kWrongCapture);
      if (observation.source.session_epoch !=
          request.target_source.session_epoch) {
        return failure(Status::kWrongEpoch);
      }
      if (observation.source.event_sequence < request.start_sequence ||
          observation.source.event_sequence > request.end_sequence) {
        return failure(Status::kWrongSequence);
      }
      if (observation.correspondence_id != request.correspondence_id)
        return failure(Status::kUnstableCorrespondence);
      if (observation.support_kind == SupportKind::kSemanticOnly)
        return failure(Status::kSemanticOnly);
      if ((observation.support_kind == SupportKind::kCalibratedDepthPoint &&
           observation.source.kind !=
               dynamic_provenance::SourceKind::kDepthFrame) ||
          (observation.support_kind == SupportKind::kCalibratedMeshPoint &&
           observation.source.kind !=
               dynamic_provenance::SourceKind::kMeshAnchor)) {
        return failure(Status::kInvalidObservation);
      }
      if (observation.tracking_state != TrackingState::kNormal)
        return failure(Status::kInvalidTracking);
      if (observation.calibration != Verification::kAccepted)
        return failure(Status::kInvalidCalibration);
      if (observation.pose != Verification::kAccepted ||
          !valid_rigid_transform(observation.project_from_source,
                                 options.rigid_tolerance)) {
        return failure(Status::kInvalidPose);
      }
      if (observation.epoch_relationship != Verification::kAccepted ||
          observation.epoch_transform_revision == 0U) {
        return failure(Status::kUnacceptedEpoch);
      }
      if (accepted_epoch_transform_revision == 0U) {
        accepted_epoch_transform_revision =
            observation.epoch_transform_revision;
      } else if (observation.epoch_transform_revision !=
                 accepted_epoch_transform_revision) {
        return failure(Status::kWrongEpoch);
      }
      if (observation.retained_source != Verification::kAccepted)
        return failure(Status::kSourceNotRetained);
      if (observation.position_uncertainty_m >
          options.max_position_uncertainty_m) {
        return failure(Status::kUncertaintyTooLarge);
      }
      if (observation.correspondence_residual_m >
          options.max_correspondence_residual_m) {
        return failure(Status::kUnstableCorrespondence);
      }
      if (observation.discontinuity_clearance_m <
          options.min_discontinuity_clearance_m +
              observation.position_uncertainty_m) {
        return failure(Status::kOcclusionEdge);
      }
      if (!source_keys.insert(source_key(observation.source)).second)
        return failure(Status::kDuplicateSource);
      if (!pose_ids.insert(observation.pose_id).second ||
          !independence_groups.insert(observation.independence_group_id)
               .second) {
        return failure(Status::kCorrelatedObservations);
      }
      const Point3d project_point =
          transform(observation.project_from_source,
                    observation.source_point_m);
      if (!finite(project_point) ||
          !within_extent(project_point, options.max_abs_coordinate_m) ||
          !contains_with_margin(request.exclusion_bounds_m, project_point,
                                observation.position_uncertainty_m)) {
        return failure(Status::kInvalidObservation);
      }
      if (source_key(observation.source) ==
          source_key(request.target_source)) {
        ++target_count;
        if (!contains_with_margin(request.target_source_bounds_m,
                                  project_point,
                                  observation.position_uncertainty_m)) {
          return failure(Status::kInvalidRequest);
        }
      }
      evaluated.push_back({&observation, project_point});
    }
    if (target_count != 1U)
      return failure(Status::kTargetSourceMissing);

    std::sort(evaluated.begin(), evaluated.end(),
              [](const EvaluatedObservation &left,
                 const EvaluatedObservation &right) {
                if (left.observation->monotonic_time_s !=
                    right.observation->monotonic_time_s) {
                  return left.observation->monotonic_time_s <
                         right.observation->monotonic_time_s;
                }
                return left.observation->source.event_sequence <
                       right.observation->source.event_sequence;
              });
    for (size_t index = 1U; index < evaluated.size(); ++index) {
      const Observation &previous = *evaluated[index - 1U].observation;
      const Observation &current = *evaluated[index].observation;
      if (current.source.event_sequence <= previous.source.event_sequence)
        return failure(Status::kWrongSequence);
      if (current.monotonic_time_s - previous.monotonic_time_s <
          options.min_time_separation_s) {
        return failure(Status::kCorrelatedObservations);
      }
    }
    const double time_span =
        evaluated.back().observation->monotonic_time_s -
        evaluated.front().observation->monotonic_time_s;
    if (time_span > options.max_time_span_s)
      return failure(Status::kUnstableCorrespondence);

    const auto target = std::find_if(
        evaluated.begin(), evaluated.end(), [&](const auto &item) {
          return source_key(item.observation->source) ==
                 source_key(request.target_source);
        });
    size_t confirmations = 0U;
    double minimum_adjusted_displacement =
        std::numeric_limits<double>::infinity();
    for (const EvaluatedObservation &item : evaluated) {
      if (&item == &*target)
        continue;
      const double adjusted =
          std::max(0.0, distance(target->project_point_m,
                                 item.project_point_m) -
                            target->observation->position_uncertainty_m -
                            item.observation->position_uncertainty_m -
                            target->observation->correspondence_residual_m -
                            item.observation->correspondence_residual_m);
      if (adjusted >= options.min_verified_displacement_m) {
        ++confirmations;
        minimum_adjusted_displacement =
            std::min(minimum_adjusted_displacement, adjusted);
      }
    }
    if (confirmations < options.minimum_motion_confirmations)
      return failure(Status::kInsufficientMotion);

    const EvaluatedObservation &first = evaluated.front();
    const EvaluatedObservation &last = evaluated.back();
    double maximum_adjusted_residual = 0.0;
    for (const EvaluatedObservation &item : evaluated) {
      const double alpha =
          (item.observation->monotonic_time_s -
           first.observation->monotonic_time_s) /
          time_span;
      const Point3d predicted =
          interpolate(first.project_point_m, last.project_point_m, alpha);
      const double endpoint_uncertainty =
          std::lerp(first.observation->position_uncertainty_m,
                    last.observation->position_uncertainty_m, alpha);
      const double adjusted_residual =
          std::max(0.0, distance(item.project_point_m, predicted) -
                            item.observation->position_uncertainty_m -
                            endpoint_uncertainty);
      maximum_adjusted_residual =
          std::max(maximum_adjusted_residual, adjusted_residual);
      if (adjusted_residual > options.max_motion_model_residual_m)
        return failure(Status::kUnstableCorrespondence);
    }

    Result result;
    result.status = Status::kVerified;
    result.evidence = dynamic_provenance::VerifiedDynamicEvidence{
        request.evidence_id,
        request.exclusion_command_id,
        request.project_id,
        request.target_source,
        request.target_source_bounds_m,
        dynamic_provenance::EvidenceKind::kTrackedMultiFrameMotion};
    result.diagnostics = Diagnostics{
        observations.size(), confirmations, minimum_adjusted_displacement,
        maximum_adjusted_residual, time_span};
    return result;
  } catch (const std::bad_alloc &) {
    return failure(Status::kResourceLimit);
  } catch (const std::length_error &) {
    return failure(Status::kResourceLimit);
  } catch (...) {
    return failure(Status::kInternalFailure);
  }
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kVerified: return "verified";
  case Status::kInvalidOptions: return "invalid_options";
  case Status::kInvalidRequest: return "invalid_request";
  case Status::kInputLimitExceeded: return "input_limit_exceeded";
  case Status::kInsufficientObservations: return "insufficient_observations";
  case Status::kInvalidObservation: return "invalid_observation";
  case Status::kWrongProject: return "wrong_project";
  case Status::kWrongCapture: return "wrong_capture";
  case Status::kWrongEpoch: return "wrong_epoch";
  case Status::kWrongSequence: return "wrong_sequence";
  case Status::kTargetSourceMissing: return "target_source_missing";
  case Status::kDuplicateSource: return "duplicate_source";
  case Status::kCorrelatedObservations: return "correlated_observations";
  case Status::kSemanticOnly: return "semantic_only";
  case Status::kInvalidTracking: return "invalid_tracking";
  case Status::kInvalidCalibration: return "invalid_calibration";
  case Status::kInvalidPose: return "invalid_pose";
  case Status::kUnacceptedEpoch: return "unaccepted_epoch";
  case Status::kSourceNotRetained: return "source_not_retained";
  case Status::kUncertaintyTooLarge: return "uncertainty_too_large";
  case Status::kOcclusionEdge: return "occlusion_edge";
  case Status::kUnstableCorrespondence: return "unstable_correspondence";
  case Status::kInsufficientMotion: return "insufficient_motion";
  case Status::kResourceLimit: return "resource_limit";
  case Status::kInternalFailure: return "internal_failure";
  }
  return "unknown";
}

} // namespace hss::dynamic_evidence_verifier

#include "hss/ring_net_area.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hss::area {
namespace {

struct CompensatedSum {
  long double sum{0.0L};
  long double correction{0.0L};

  void add(long double value) {
    const long double next = sum + value;
    if (std::fabs(sum) >= std::fabs(value))
      correction += (sum - next) + value;
    else
      correction += (value - next) + sum;
    sum = next;
  }

  long double value() const { return sum + correction; }
};

enum class AreaStatus : uint8_t {
  kOk,
  kInvalidRing,
  kNonFiniteCoordinate,
  kNumericalFailure,
};

struct SignedArea {
  AreaStatus status{AreaStatus::kInvalidRing};
  long double value_m2{0.0L};
};

SignedArea translated_signed_area(const ValidatedRing &ring) {
  if (ring.id.empty() || ring.vertices_m.size() < 3U)
    return {};

  for (const Point2d point : ring.vertices_m) {
    if (!std::isfinite(point.x_m) || !std::isfinite(point.y_m))
      return {AreaStatus::kNonFiniteCoordinate, 0.0L};
  }

  const Point2d origin = ring.vertices_m.front();
  CompensatedSum twice_area;
  // A translated triangle fan avoids the large, nearly equal global-origin
  // products in the ordinary shoelace form. The compensated accumulator keeps
  // small concave contributions from disappearing behind larger terms.
  for (size_t index = 1U; index + 1U < ring.vertices_m.size(); ++index) {
    const long double ax =
        static_cast<long double>(ring.vertices_m[index].x_m) - origin.x_m;
    const long double ay =
        static_cast<long double>(ring.vertices_m[index].y_m) - origin.y_m;
    const long double bx =
        static_cast<long double>(ring.vertices_m[index + 1U].x_m) - origin.x_m;
    const long double by =
        static_cast<long double>(ring.vertices_m[index + 1U].y_m) - origin.y_m;
    twice_area.add(ax * by - ay * bx);
  }
  const long double area = twice_area.value() * 0.5L;
  if (!std::isfinite(area))
    return {AreaStatus::kNumericalFailure, 0.0L};
  return {AreaStatus::kOk, area};
}

Result failure(Status status, Availability availability,
               const Input &input) {
  Result result;
  result.status = status;
  result.availability = availability;
  result.policy = input.policy;
  return result;
}

Status map_area_status(AreaStatus status) {
  switch (status) {
  case AreaStatus::kOk:
    return Status::kOk;
  case AreaStatus::kInvalidRing:
    return Status::kInvalidRing;
  case AreaStatus::kNonFiniteCoordinate:
    return Status::kNonFiniteCoordinate;
  case AreaStatus::kNumericalFailure:
    return Status::kNumericalFailure;
  }
  return Status::kNumericalFailure;
}

Decision hole_decision(const Hole &hole, AreaPolicy policy) {
  if (policy == AreaPolicy::kInternalFinishedFace)
    return Decision::kIgnoredByInternalFinishedFacePolicy;
  if (hole.kind == HoleKind::kFurniture)
    return Decision::kIgnoredFurniture;
  if (hole.kind != HoleKind::kFixedObstruction)
    return Decision::kIgnoredOther;
  if (!hole.explicitly_fixed)
    return Decision::kIgnoredNotExplicitlyFixed;
  if (hole.attachment == Attachment::kAttached)
    return Decision::kIgnoredAttached;
  if (hole.attachment == Attachment::kUncertain)
    return Decision::kIgnoredUncertainAttachment;
  return Decision::kSubtractedFixedDetachedObstruction;
}

bool valid_options(const Options &options) {
  return options.max_rings > 0U && options.max_rings <= kHardMaxRings &&
         options.max_vertices >= 3U &&
         options.max_vertices <= kHardMaxVertices;
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kInvalidUnit:
    return "invalid_unit";
  case Status::kInvalidPolicy:
    return "invalid_policy";
  case Status::kResourceLimitExceeded:
    return "resource_limit_exceeded";
  case Status::kTopologyIncomplete:
    return "topology_incomplete";
  case Status::kTopologyUnaccepted:
    return "topology_unaccepted";
  case Status::kInvalidClassification:
    return "invalid_classification";
  case Status::kInvalidRing:
    return "invalid_ring";
  case Status::kNonFiniteCoordinate:
    return "nonfinite_coordinate";
  case Status::kWrongWinding:
    return "wrong_winding";
  case Status::kNumericalFailure:
    return "numerical_failure";
  }
  return "unknown";
}

const char *decision_name(Decision decision) {
  switch (decision) {
  case Decision::kOuterBase:
    return "outer_base";
  case Decision::kSubtractedFixedDetachedObstruction:
    return "subtracted_fixed_detached_obstruction";
  case Decision::kIgnoredByInternalFinishedFacePolicy:
    return "ignored_by_internal_finished_face_policy";
  case Decision::kIgnoredNotExplicitlyFixed:
    return "ignored_not_explicitly_fixed";
  case Decision::kIgnoredAttached:
    return "ignored_attached";
  case Decision::kIgnoredUncertainAttachment:
    return "ignored_uncertain_attachment";
  case Decision::kIgnoredFurniture:
    return "ignored_furniture";
  case Decision::kIgnoredOther:
    return "ignored_other";
  }
  return "unknown";
}

Result calculate(const Input &input, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions, Availability::kUnavailable, input);
  if (input.coordinate_unit != LinearUnit::kMetres)
    return failure(Status::kInvalidUnit, Availability::kUnavailable, input);
  if (input.policy != AreaPolicy::kInternalFinishedFace &&
      input.policy != AreaPolicy::kNetFixedObstructions)
    return failure(Status::kInvalidPolicy, Availability::kUnavailable, input);
  if (input.holes.size() >= options.max_rings)
    return failure(Status::kResourceLimitExceeded,
                   Availability::kUnavailable, input);

  size_t vertex_count = input.outer.vertices_m.size();
  if (vertex_count > options.max_vertices)
    return failure(Status::kResourceLimitExceeded,
                   Availability::kUnavailable, input);
  for (const Hole &hole : input.holes) {
    if (hole.ring.vertices_m.size() > options.max_vertices - vertex_count)
      return failure(Status::kResourceLimitExceeded,
                     Availability::kUnavailable, input);
    vertex_count += hole.ring.vertices_m.size();
  }

  bool incomplete = input.outer.topology_state == TopologyState::kIncomplete;
  bool unaccepted =
      input.outer.topology_state == TopologyState::kUnaccepted;
  if (input.outer.topology_state != TopologyState::kAccepted &&
      input.outer.topology_state != TopologyState::kIncomplete &&
      input.outer.topology_state != TopologyState::kUnaccepted)
    unaccepted = true;
  for (const Hole &hole : input.holes) {
    incomplete = incomplete ||
                 hole.ring.topology_state == TopologyState::kIncomplete;
    unaccepted = unaccepted ||
                 hole.ring.topology_state == TopologyState::kUnaccepted;
    if (hole.ring.topology_state != TopologyState::kAccepted &&
        hole.ring.topology_state != TopologyState::kIncomplete &&
        hole.ring.topology_state != TopologyState::kUnaccepted)
      unaccepted = true;
  }
  if (unaccepted)
    return failure(Status::kTopologyUnaccepted, Availability::kUnavailable,
                   input);
  if (incomplete)
    return failure(Status::kTopologyIncomplete, Availability::kProvisional,
                   input);

  for (const Hole &hole : input.holes) {
    const bool valid_kind = hole.kind == HoleKind::kFixedObstruction ||
                            hole.kind == HoleKind::kFurniture ||
                            hole.kind == HoleKind::kOther;
    const bool valid_attachment =
        hole.attachment == Attachment::kDetached ||
        hole.attachment == Attachment::kAttached ||
        hole.attachment == Attachment::kUncertain;
    if (!valid_kind || !valid_attachment)
      return failure(Status::kInvalidClassification,
                     Availability::kUnavailable, input);
  }

  const SignedArea outer_area = translated_signed_area(input.outer);
  if (outer_area.status != AreaStatus::kOk)
    return failure(map_area_status(outer_area.status),
                   Availability::kUnavailable, input);
  if (!(outer_area.value_m2 > 0.0L))
    return failure(Status::kWrongWinding, Availability::kUnavailable, input);

  std::vector<RingAssessment> assessments;
  assessments.reserve(input.holes.size() + 1U);
  assessments.push_back({input.outer.id,
                         static_cast<double>(outer_area.value_m2),
                         static_cast<double>(outer_area.value_m2),
                         Decision::kOuterBase,
                         input.outer.provenance});

  CompensatedSum net_area;
  net_area.add(outer_area.value_m2);
  for (const Hole &hole : input.holes) {
    const SignedArea hole_area = translated_signed_area(hole.ring);
    if (hole_area.status != AreaStatus::kOk)
      return failure(map_area_status(hole_area.status),
                     Availability::kUnavailable, input);
    if (!(hole_area.value_m2 < 0.0L))
      return failure(Status::kWrongWinding, Availability::kUnavailable, input);

    const Decision decision = hole_decision(hole, input.policy);
    const long double delta =
        decision == Decision::kSubtractedFixedDetachedObstruction
            ? hole_area.value_m2
            : 0.0L;
    net_area.add(delta);
    assessments.push_back({hole.ring.id,
                           static_cast<double>(hole_area.value_m2),
                           static_cast<double>(delta), decision,
                           hole.ring.provenance});
  }

  const long double area = net_area.value();
  if (!std::isfinite(area) || area < 0.0L ||
      area > std::numeric_limits<double>::max())
    return failure(Status::kNumericalFailure, Availability::kUnavailable,
                   input);

  Result result;
  result.status = Status::kOk;
  result.availability = Availability::kReady;
  result.policy = input.policy;
  result.area_m2 = static_cast<double>(area);
  result.assessments = std::move(assessments);
  return result;
}

} // namespace hss::area

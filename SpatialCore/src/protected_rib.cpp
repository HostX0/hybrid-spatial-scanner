#include "hss/protected_rib.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace hss::rib {
namespace {

constexpr std::array<SegmentRole, kRequiredSegmentCount> kRoleOrder{
    SegmentRole::kApproach, SegmentRole::kFirstReturn, SegmentRole::kFront,
    SegmentRole::kSecondReturn, SegmentRole::kContinuation};

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool valid_options(const Options &options) {
  return options.max_ids_per_field > 0U &&
         options.max_ids_per_field <= kHardMaxIdsPerField &&
         finite_positive(options.min_segment_length_m) &&
         finite_positive(options.geometric_epsilon_m) &&
         options.geometric_epsilon_m < options.min_segment_length_m &&
         finite_positive(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > options.min_segment_length_m;
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

Status validate_id_vector(const std::vector<std::string> &ids,
                          std::string_view owner_id, size_t maximum) {
  if (ids.size() > maximum)
    return Status::kResourceLimitExceeded;
  std::unordered_set<std::string_view> unique;
  unique.reserve(ids.size());
  for (const std::string &id : ids) {
    if (!valid_uuid(id))
      return Status::kInvalidId;
    if (id == owner_id || !unique.insert(id).second)
      return Status::kInvalidLineage;
  }
  return Status::kOk;
}

Status validate_lineage(const std::vector<std::string> &parents,
                        const std::vector<std::string> &replacements,
                        std::string_view owner_id, size_t maximum) {
  Status status = validate_id_vector(parents, owner_id, maximum);
  if (status != Status::kOk)
    return status;
  status = validate_id_vector(replacements, owner_id, maximum);
  if (status != Status::kOk)
    return status;
  std::unordered_set<std::string_view> parent_ids;
  parent_ids.reserve(parents.size());
  for (const std::string &id : parents)
    parent_ids.insert(id);
  for (const std::string &id : replacements) {
    if (parent_ids.contains(id))
      return Status::kInvalidLineage;
  }
  return Status::kOk;
}

long double squared_distance(const Point2d &first, const Point2d &second) {
  const long double dx = static_cast<long double>(first.x) - second.x;
  const long double dy = static_cast<long double>(first.y) - second.y;
  return dx * dx + dy * dy;
}

long double segment_length(const Point2d &first, const Point2d &second) {
  return std::hypotl(static_cast<long double>(second.x) - first.x,
                     static_cast<long double>(second.y) - first.y);
}

long double cross(const Point2d &a, const Point2d &b, const Point2d &point) {
  const long double abx = static_cast<long double>(b.x) - a.x;
  const long double aby = static_cast<long double>(b.y) - a.y;
  const long double apx = static_cast<long double>(point.x) - a.x;
  const long double apy = static_cast<long double>(point.y) - a.y;
  return abx * apy - aby * apx;
}

bool point_on_segment(const Point2d &point, const Point2d &a, const Point2d &b,
                      long double epsilon) {
  const long double length = segment_length(a, b);
  if (!(length > 0.0L) || !std::isfinite(length))
    return false;
  if (std::abs(cross(a, b, point)) > epsilon * length)
    return false;
  const long double px = static_cast<long double>(point.x);
  const long double py = static_cast<long double>(point.y);
  const long double ax = static_cast<long double>(a.x);
  const long double ay = static_cast<long double>(a.y);
  const long double bx = static_cast<long double>(b.x);
  const long double by = static_cast<long double>(b.y);
  const long double from_a = (px - ax) * (bx - ax) + (py - ay) * (by - ay);
  const long double from_b = (px - bx) * (ax - bx) + (py - by) * (ay - by);
  return from_a >= -epsilon * length && from_b >= -epsilon * length;
}

int orientation(const Point2d &a, const Point2d &b, const Point2d &point,
                long double epsilon) {
  const long double value = cross(a, b, point);
  const long double tolerance = epsilon * segment_length(a, b);
  if (value > tolerance)
    return 1;
  if (value < -tolerance)
    return -1;
  return 0;
}

bool segments_meet(const Point2d &a, const Point2d &b, const Point2d &c,
                   const Point2d &d, long double epsilon) {
  const int abc = orientation(a, b, c, epsilon);
  const int abd = orientation(a, b, d, epsilon);
  const int cda = orientation(c, d, a, epsilon);
  const int cdb = orientation(c, d, b, epsilon);
  if (abc * abd < 0 && cda * cdb < 0)
    return true;
  return (abc == 0 && point_on_segment(c, a, b, epsilon)) ||
         (abd == 0 && point_on_segment(d, a, b, epsilon)) ||
         (cda == 0 && point_on_segment(a, c, d, epsilon)) ||
         (cdb == 0 && point_on_segment(b, c, d, epsilon));
}

bool contains_id(const std::vector<std::string> &ids, std::string_view id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
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
  case Status::kDuplicateId:
    return "duplicate_id";
  case Status::kInvalidRevision:
    return "invalid_revision";
  case Status::kInvalidLineage:
    return "invalid_lineage";
  case Status::kMissingEvidence:
    return "missing_evidence";
  case Status::kNonFiniteCoordinate:
    return "nonfinite_coordinate";
  case Status::kCoordinateOutOfRange:
    return "coordinate_out_of_range";
  case Status::kWrongSegmentCount:
    return "wrong_segment_count";
  case Status::kWrongRoleOrder:
    return "wrong_role_order";
  case Status::kInvalidReference:
    return "invalid_reference";
  case Status::kOpenChain:
    return "open_chain";
  case Status::kAmbiguousChain:
    return "ambiguous_chain";
  case Status::kDuplicateVertex:
    return "duplicate_vertex";
  case Status::kNearZeroSegment:
    return "near_zero_segment";
  case Status::kUnprotectedSegment:
    return "unprotected_segment";
  case Status::kDegenerateFeature:
    return "degenerate_feature";
  case Status::kSelfIntersection:
    return "self_intersection";
  case Status::kTopologyChange:
    return "topology_change";
  case Status::kNumericalFailure:
    return "numerical_failure";
  }
  return "unknown";
}

Result validate(const Chain &chain, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  if (!valid_uuid(chain.feature_id))
    return failure(Status::kInvalidId);
  if (chain.revision == 0U)
    return failure(Status::kInvalidRevision);
  if (chain.vertices.size() != kRequiredVertexCount ||
      chain.ordered_segments.size() != kRequiredSegmentCount)
    return failure(Status::kWrongSegmentCount);

  std::unordered_set<std::string> entity_ids;
  entity_ids.reserve(1U + chain.vertices.size() +
                     chain.ordered_segments.size());
  entity_ids.insert(chain.feature_id);
  std::unordered_map<std::string, size_t> vertex_by_id;
  vertex_by_id.reserve(chain.vertices.size());
  for (size_t index = 0U; index < chain.vertices.size(); ++index) {
    const Vertex &vertex = chain.vertices[index];
    if (!valid_uuid(vertex.id))
      return failure(Status::kInvalidId);
    if (!entity_ids.insert(vertex.id).second)
      return failure(Status::kDuplicateId);
    Status status =
        validate_lineage(vertex.parent_vertex_ids,
                         vertex.replacement_vertex_ids, vertex.id,
                         options.max_ids_per_field);
    if (status != Status::kOk)
      return failure(status);
    status = validate_id_vector(vertex.provenance_support_ids, vertex.id,
                                options.max_ids_per_field);
    if (status != Status::kOk)
      return failure(status);
    if (vertex.provenance_support_ids.empty())
      return failure(Status::kMissingEvidence);
    if (!std::isfinite(vertex.position_m.x) ||
        !std::isfinite(vertex.position_m.y))
      return failure(Status::kNonFiniteCoordinate);
    if (std::abs(vertex.position_m.x) > options.max_abs_coordinate_m ||
        std::abs(vertex.position_m.y) > options.max_abs_coordinate_m)
      return failure(Status::kCoordinateOutOfRange);
    vertex_by_id.emplace(vertex.id, index);
  }

  std::array<size_t, kRequiredSegmentCount + 1U> ordered_vertex_indices{};
  for (size_t index = 0U; index < chain.ordered_segments.size(); ++index) {
    const Segment &segment = chain.ordered_segments[index];
    if (segment.role != kRoleOrder[index])
      return failure(Status::kWrongRoleOrder);
    if (!valid_uuid(segment.id) || !valid_uuid(segment.start_vertex_id) ||
        !valid_uuid(segment.end_vertex_id))
      return failure(Status::kInvalidId);
    if (!entity_ids.insert(segment.id).second)
      return failure(Status::kDuplicateId);
    const auto start = vertex_by_id.find(segment.start_vertex_id);
    const auto end = vertex_by_id.find(segment.end_vertex_id);
    if (segment.start_vertex_id == segment.end_vertex_id ||
        start == vertex_by_id.end() || end == vertex_by_id.end())
      return failure(Status::kInvalidReference);
    if (index != 0U &&
        chain.ordered_segments[index - 1U].end_vertex_id !=
            segment.start_vertex_id)
      return failure(Status::kOpenChain);

    Status status =
        validate_lineage(segment.parent_segment_ids,
                         segment.replacement_segment_ids, segment.id,
                         options.max_ids_per_field);
    if (status != Status::kOk)
      return failure(status);
    status = validate_id_vector(segment.supporting_surface_ids, segment.id,
                                options.max_ids_per_field);
    if (status != Status::kOk)
      return failure(status);
    status = validate_id_vector(segment.provenance_support_ids, segment.id,
                                options.max_ids_per_field);
    if (status != Status::kOk)
      return failure(status);
    if (segment.supporting_surface_ids.empty() ||
        segment.provenance_support_ids.empty())
      return failure(Status::kMissingEvidence);
    if (!segment.protected_topology)
      return failure(Status::kUnprotectedSegment);

    ordered_vertex_indices[index] = start->second;
    ordered_vertex_indices[index + 1U] = end->second;
  }

  std::unordered_set<size_t> used_vertices;
  used_vertices.reserve(ordered_vertex_indices.size());
  for (const size_t index : ordered_vertex_indices) {
    if (!used_vertices.insert(index).second)
      return failure(Status::kAmbiguousChain);
  }
  if (used_vertices.size() != chain.vertices.size())
    return failure(Status::kAmbiguousChain);

  const long double epsilon = options.geometric_epsilon_m;
  const long double duplicate_distance_squared = epsilon * epsilon;
  const long double minimum_length_squared =
      static_cast<long double>(options.min_segment_length_m) *
      options.min_segment_length_m;
  for (size_t first = 0U; first < chain.vertices.size(); ++first) {
    for (size_t second = first + 1U; second < chain.vertices.size(); ++second) {
      const long double distance = squared_distance(
          chain.vertices[first].position_m, chain.vertices[second].position_m);
      if (!std::isfinite(distance))
        return failure(Status::kNumericalFailure);
      if (distance <= duplicate_distance_squared)
        return failure(Status::kDuplicateVertex);
    }
  }

  std::array<Point2d, kRequiredVertexCount> points{};
  for (size_t index = 0U; index < points.size(); ++index)
    points[index] = chain.vertices[ordered_vertex_indices[index]].position_m;
  for (size_t index = 0U; index < kRequiredSegmentCount; ++index) {
    const long double length_squared =
        squared_distance(points[index], points[index + 1U]);
    if (!std::isfinite(length_squared))
      return failure(Status::kNumericalFailure);
    if (length_squared <= minimum_length_squared)
      return failure(Status::kNearZeroSegment);
  }

  // All four junctions must be real turns. A straight role sequence is not
  // evidence of two returns and a front and cannot replace the feature with a
  // broad subdivided wall.
  for (size_t index = 0U; index + 2U < points.size(); ++index) {
    if (orientation(points[index], points[index + 1U], points[index + 2U],
                    epsilon) == 0)
      return failure(Status::kDegenerateFeature);
  }

  for (size_t first = 0U; first < kRequiredSegmentCount; ++first) {
    for (size_t second = first + 2U; second < kRequiredSegmentCount; ++second) {
      if (segments_meet(points[first], points[first + 1U], points[second],
                        points[second + 1U], epsilon))
        return failure(Status::kSelfIntersection);
    }
  }

  return {Status::kOk, chain};
}

Result validate_update(const Chain &previous, const Chain &candidate,
                       const Options &options) {
  const Result prior_result = validate(previous, options);
  if (prior_result.status != Status::kOk)
    return failure(prior_result.status);
  const Result candidate_result = validate(candidate, options);
  if (candidate_result.status != Status::kOk)
    return candidate_result;
  if (candidate.feature_id != previous.feature_id ||
      candidate.revision <= previous.revision)
    return failure(Status::kInvalidRevision);

  for (size_t index = 0U; index < kRequiredSegmentCount; ++index) {
    const Segment &before = previous.ordered_segments[index];
    const Segment &after = candidate.ordered_segments[index];
    if (after.id != before.id &&
        !contains_id(after.parent_segment_ids, before.id))
      return failure(Status::kTopologyChange);
  }

  auto ordered_vertices = [](const Chain &chain) {
    std::unordered_map<std::string_view, const Vertex *> by_id;
    by_id.reserve(chain.vertices.size());
    for (const Vertex &vertex : chain.vertices)
      by_id.emplace(vertex.id, &vertex);
    std::array<const Vertex *, kRequiredVertexCount> result{};
    result[0] = by_id.at(chain.ordered_segments.front().start_vertex_id);
    for (size_t index = 0U; index < kRequiredSegmentCount; ++index)
      result[index + 1U] =
          by_id.at(chain.ordered_segments[index].end_vertex_id);
    return result;
  };
  const auto before_vertices = ordered_vertices(previous);
  const auto after_vertices = ordered_vertices(candidate);
  for (size_t index = 0U; index < kRequiredVertexCount; ++index) {
    if (after_vertices[index]->id != before_vertices[index]->id &&
        !contains_id(after_vertices[index]->parent_vertex_ids,
                     before_vertices[index]->id))
      return failure(Status::kTopologyChange);
  }

  return candidate_result;
}

} // namespace hss::rib

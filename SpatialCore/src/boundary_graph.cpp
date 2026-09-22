#include "hss/boundary_graph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace hss::boundary {
namespace {

struct RingView {
  std::vector<size_t> node_indices;
  std::vector<size_t> edge_indices;
  long double signed_area_m2{0.0L};
};

enum class PointLocation : uint8_t { kOutside, kInside, kBoundary };

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool valid_options(const Options &options) {
  return options.max_nodes > 0U && options.max_nodes <= kHardMaxNodes &&
         options.max_edges > 0U && options.max_edges <= kHardMaxEdges &&
         options.max_holes <= kHardMaxHoles &&
         options.max_lineage_ids_per_entity > 0U &&
         options.max_lineage_ids_per_entity <=
             kHardMaxLineageIdsPerEntity &&
         options.max_pair_tests > 0U &&
         options.max_pair_tests <= kHardMaxPairTests &&
         finite_positive(options.min_edge_length_m) &&
         finite_positive(options.geometric_epsilon_m) &&
         options.geometric_epsilon_m < options.min_edge_length_m &&
         finite_positive(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > options.min_edge_length_m;
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

bool add_pair_count(size_t count, size_t limit, size_t *total) {
  if (count < 2U)
    return true;
  const size_t left = count;
  const size_t right = count - 1U;
  const size_t half_left = left % 2U == 0U ? left / 2U : left;
  const size_t half_right = left % 2U == 0U ? right : right / 2U;
  if (half_left > limit / half_right)
    return false;
  const size_t pairs = half_left * half_right;
  if (pairs > limit - *total)
    return false;
  *total += pairs;
  return true;
}

long double squared_distance(const Point2d &first, const Point2d &second) {
  const long double dx = static_cast<long double>(first.x) - second.x;
  const long double dy = static_cast<long double>(first.y) - second.y;
  return dx * dx + dy * dy;
}

long double cross(const Point2d &a, const Point2d &b, const Point2d &c) {
  const long double abx = static_cast<long double>(b.x) - a.x;
  const long double aby = static_cast<long double>(b.y) - a.y;
  const long double acx = static_cast<long double>(c.x) - a.x;
  const long double acy = static_cast<long double>(c.y) - a.y;
  return abx * acy - aby * acx;
}

long double segment_length(const Point2d &a, const Point2d &b) {
  return std::hypotl(static_cast<long double>(b.x) - a.x,
                     static_cast<long double>(b.y) - a.y);
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
  const long double dot_from_a = (px - ax) * (bx - ax) +
                                 (py - ay) * (by - ay);
  const long double dot_from_b = (px - bx) * (ax - bx) +
                                 (py - by) * (ay - by);
  return dot_from_a >= -epsilon * length &&
         dot_from_b >= -epsilon * length;
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

bool adjacent_backtracks(const Point2d &a, const Point2d &joint,
                         const Point2d &c, long double epsilon) {
  const long double first_length = segment_length(a, joint);
  const long double second_length = segment_length(joint, c);
  const long double turn = std::abs(cross(a, joint, c));
  if (turn > epsilon * std::max(first_length, second_length))
    return false;
  const long double first_x = static_cast<long double>(joint.x) - a.x;
  const long double first_y = static_cast<long double>(joint.y) - a.y;
  const long double second_x = static_cast<long double>(c.x) - joint.x;
  const long double second_y = static_cast<long double>(c.y) - joint.y;
  return first_x * second_x + first_y * second_y < 0.0L;
}

bool compute_area_and_perimeter(const RingView &ring,
                                const std::vector<Node> &nodes,
                                long double *area,
                                long double *perimeter) {
  const Point2d origin = nodes[ring.node_indices.front()].position_m;
  long double sum = 0.0L;
  long double compensation = 0.0L;
  long double length_sum = 0.0L;
  for (size_t index = 0U; index < ring.node_indices.size(); ++index) {
    const Point2d current = nodes[ring.node_indices[index]].position_m;
    const Point2d next =
        nodes[ring.node_indices[(index + 1U) % ring.node_indices.size()]]
            .position_m;
    const long double current_x =
        static_cast<long double>(current.x) - origin.x;
    const long double current_y =
        static_cast<long double>(current.y) - origin.y;
    const long double next_x = static_cast<long double>(next.x) - origin.x;
    const long double next_y = static_cast<long double>(next.y) - origin.y;
    const long double term = current_x * next_y - current_y * next_x;
    const long double corrected = term - compensation;
    const long double updated = sum + corrected;
    compensation = (updated - sum) - corrected;
    sum = updated;
    length_sum += segment_length(current, next);
  }
  *area = sum / 2.0L;
  *perimeter = length_sum;
  return std::isfinite(*area) && std::isfinite(*perimeter);
}

PointLocation locate_point(const Point2d &point, const RingView &ring,
                           const std::vector<Node> &nodes,
                           long double epsilon) {
  bool inside = false;
  for (size_t index = 0U; index < ring.node_indices.size(); ++index) {
    const Point2d a = nodes[ring.node_indices[index]].position_m;
    const Point2d b =
        nodes[ring.node_indices[(index + 1U) % ring.node_indices.size()]]
            .position_m;
    if (point_on_segment(point, a, b, epsilon))
      return PointLocation::kBoundary;
    const bool crosses_y = (a.y > point.y) != (b.y > point.y);
    if (!crosses_y)
      continue;
    const long double intersect_x =
        static_cast<long double>(a.x) +
        (static_cast<long double>(point.y) - a.y) *
            (static_cast<long double>(b.x) - a.x) /
            (static_cast<long double>(b.y) - a.y);
    if (intersect_x > static_cast<long double>(point.x))
      inside = !inside;
  }
  return inside ? PointLocation::kInside : PointLocation::kOutside;
}

bool ring_edges_meet(const RingView &first, const RingView &second,
                     const std::vector<Node> &nodes, long double epsilon) {
  for (size_t first_index = 0U; first_index < first.node_indices.size();
       ++first_index) {
    const Point2d a = nodes[first.node_indices[first_index]].position_m;
    const Point2d b =
        nodes[first.node_indices[(first_index + 1U) %
                                 first.node_indices.size()]]
            .position_m;
    for (size_t second_index = 0U; second_index < second.node_indices.size();
         ++second_index) {
      const Point2d c = nodes[second.node_indices[second_index]].position_m;
      const Point2d d =
          nodes[second.node_indices[(second_index + 1U) %
                                    second.node_indices.size()]]
              .position_m;
      if (segments_meet(a, b, c, d, epsilon))
        return true;
    }
  }
  return false;
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
  case Status::kWorkLimitExceeded:
    return "work_limit_exceeded";
  case Status::kInvalidId:
    return "invalid_id";
  case Status::kDuplicateId:
    return "duplicate_id";
  case Status::kInvalidLineage:
    return "invalid_lineage";
  case Status::kNonFiniteCoordinate:
    return "nonfinite_coordinate";
  case Status::kCoordinateOutOfRange:
    return "coordinate_out_of_range";
  case Status::kInvalidReference:
    return "invalid_reference";
  case Status::kLoopTooShort:
    return "loop_too_short";
  case Status::kDuplicateLoopMember:
    return "duplicate_loop_member";
  case Status::kOpenChain:
    return "open_chain";
  case Status::kAmbiguousTopology:
    return "ambiguous_topology";
  case Status::kDuplicateVertex:
    return "duplicate_vertex";
  case Status::kNearZeroEdge:
    return "near_zero_edge";
  case Status::kDegenerateLoop:
    return "degenerate_loop";
  case Status::kWrongWinding:
    return "wrong_winding";
  case Status::kSelfIntersection:
    return "self_intersection";
  case Status::kLoopTouching:
    return "loop_touching";
  case Status::kHoleOutsideOuter:
    return "hole_outside_outer";
  case Status::kHoleOverlap:
    return "hole_overlap";
  case Status::kNumericalFailure:
    return "numerical_failure";
  }
  return "unknown";
}

Result validate(const Graph &graph, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  if (graph.nodes.size() > options.max_nodes ||
      graph.edges.size() > options.max_edges ||
      graph.holes.size() > options.max_holes)
    return failure(Status::kResourceLimitExceeded);

  size_t pair_tests = 0U;
  if (!add_pair_count(graph.nodes.size(), options.max_pair_tests,
                      &pair_tests) ||
      !add_pair_count(graph.edges.size(), options.max_pair_tests,
                      &pair_tests))
    return failure(Status::kWorkLimitExceeded);

  std::unordered_set<std::string> entity_ids;
  entity_ids.reserve(graph.nodes.size() + graph.edges.size() +
                     graph.holes.size() + 1U);
  std::unordered_map<std::string, size_t> node_by_id;
  node_by_id.reserve(graph.nodes.size());
  for (size_t index = 0U; index < graph.nodes.size(); ++index) {
    const Node &node = graph.nodes[index];
    if (!valid_uuid(node.id))
      return failure(Status::kInvalidId);
    if (!entity_ids.insert(node.id).second)
      return failure(Status::kDuplicateId);
    const Status lineage =
        validate_lineage(node.parent_node_ids, node.replacement_node_ids,
                         node.id, options.max_lineage_ids_per_entity);
    if (lineage != Status::kOk)
      return failure(lineage);
    const Status support = validate_id_vector(
        node.provenance_support_ids, node.id,
        options.max_lineage_ids_per_entity);
    if (support != Status::kOk)
      return failure(support);
    if (!std::isfinite(node.position_m.x) ||
        !std::isfinite(node.position_m.y))
      return failure(Status::kNonFiniteCoordinate);
    if (std::abs(node.position_m.x) > options.max_abs_coordinate_m ||
        std::abs(node.position_m.y) > options.max_abs_coordinate_m)
      return failure(Status::kCoordinateOutOfRange);
    node_by_id.emplace(node.id, index);
  }

  std::unordered_map<std::string, size_t> edge_by_id;
  edge_by_id.reserve(graph.edges.size());
  for (size_t index = 0U; index < graph.edges.size(); ++index) {
    const Edge &edge = graph.edges[index];
    if (!valid_uuid(edge.id))
      return failure(Status::kInvalidId);
    if (!entity_ids.insert(edge.id).second)
      return failure(Status::kDuplicateId);
    if (!valid_uuid(edge.start_node_id) || !valid_uuid(edge.end_node_id))
      return failure(Status::kInvalidId);
    if (edge.start_node_id == edge.end_node_id ||
        !node_by_id.contains(edge.start_node_id) ||
        !node_by_id.contains(edge.end_node_id))
      return failure(Status::kInvalidReference);
    const Status lineage =
        validate_lineage(edge.parent_edge_ids, edge.replacement_edge_ids,
                         edge.id, options.max_lineage_ids_per_entity);
    if (lineage != Status::kOk)
      return failure(lineage);
    Status references =
        validate_id_vector(edge.supporting_surface_ids, edge.id,
                           options.max_lineage_ids_per_entity);
    if (references != Status::kOk)
      return failure(references);
    references = validate_id_vector(edge.provenance_support_ids, edge.id,
                                    options.max_lineage_ids_per_entity);
    if (references != Status::kOk)
      return failure(references);
    edge_by_id.emplace(edge.id, index);
  }

  std::vector<const Loop *> loops;
  loops.reserve(graph.holes.size() + 1U);
  loops.push_back(&graph.outer);
  for (const Loop &hole : graph.holes)
    loops.push_back(&hole);

  std::vector<size_t> edge_use_count(graph.edges.size(), 0U);
  std::vector<size_t> node_use_count(graph.nodes.size(), 0U);
  std::vector<RingView> rings;
  rings.reserve(loops.size());
  for (const Loop *loop : loops) {
    if (!valid_uuid(loop->id))
      return failure(Status::kInvalidId);
    if (!entity_ids.insert(loop->id).second)
      return failure(Status::kDuplicateId);
    if (loop->edge_ids.size() < 3U)
      return failure(Status::kLoopTooShort);
    RingView ring;
    ring.edge_indices.reserve(loop->edge_ids.size());
    ring.node_indices.reserve(loop->edge_ids.size());
    for (const std::string &edge_id : loop->edge_ids) {
      if (!valid_uuid(edge_id))
        return failure(Status::kInvalidId);
      const auto found = edge_by_id.find(edge_id);
      if (found == edge_by_id.end())
        return failure(Status::kInvalidReference);
      const size_t edge_index = found->second;
      if (++edge_use_count[edge_index] != 1U)
        return failure(Status::kDuplicateLoopMember);
      ring.edge_indices.push_back(edge_index);
    }
    for (size_t index = 0U; index < ring.edge_indices.size(); ++index) {
      const Edge &edge = graph.edges[ring.edge_indices[index]];
      const Edge &next = graph.edges[ring.edge_indices[
          (index + 1U) % ring.edge_indices.size()]];
      if (edge.end_node_id != next.start_node_id)
        return failure(Status::kOpenChain);
      const size_t node_index = node_by_id.at(edge.start_node_id);
      if (++node_use_count[node_index] != 1U)
        return failure(Status::kAmbiguousTopology);
      ring.node_indices.push_back(node_index);
    }
    rings.push_back(std::move(ring));
  }

  if (std::any_of(edge_use_count.begin(), edge_use_count.end(),
                  [](size_t count) { return count != 1U; }) ||
      std::any_of(node_use_count.begin(), node_use_count.end(),
                  [](size_t count) { return count != 1U; }))
    return failure(Status::kAmbiguousTopology);

  const long double minimum_edge = options.min_edge_length_m;
  const long double epsilon = options.geometric_epsilon_m;
  const long double minimum_edge_squared = minimum_edge * minimum_edge;
  for (const Edge &edge : graph.edges) {
    const Point2d start =
        graph.nodes[node_by_id.at(edge.start_node_id)].position_m;
    const Point2d end = graph.nodes[node_by_id.at(edge.end_node_id)].position_m;
    const long double length_squared = squared_distance(start, end);
    if (!std::isfinite(length_squared))
      return failure(Status::kNumericalFailure);
    if (length_squared <= minimum_edge_squared)
      return failure(Status::kNearZeroEdge);
  }

  const long double duplicate_distance_squared = epsilon * epsilon;
  for (size_t first = 0U; first < graph.nodes.size(); ++first) {
    for (size_t second = first + 1U; second < graph.nodes.size(); ++second) {
      if (squared_distance(graph.nodes[first].position_m,
                           graph.nodes[second].position_m) <=
          duplicate_distance_squared)
        return failure(Status::kDuplicateVertex);
    }
  }

  for (size_t ring_index = 0U; ring_index < rings.size(); ++ring_index) {
    RingView &ring = rings[ring_index];
    long double perimeter = 0.0L;
    if (!compute_area_and_perimeter(ring, graph.nodes, &ring.signed_area_m2,
                                    &perimeter))
      return failure(Status::kNumericalFailure);
    if (std::abs(ring.signed_area_m2) <= epsilon * perimeter)
      return failure(Status::kDegenerateLoop);
    if ((ring_index == 0U && ring.signed_area_m2 < 0.0L) ||
        (ring_index != 0U && ring.signed_area_m2 > 0.0L))
      return failure(Status::kWrongWinding);

    const size_t count = ring.node_indices.size();
    for (size_t first = 0U; first < count; ++first) {
      const Point2d a = graph.nodes[ring.node_indices[first]].position_m;
      const Point2d b =
          graph.nodes[ring.node_indices[(first + 1U) % count]].position_m;
      const Point2d c =
          graph.nodes[ring.node_indices[(first + 2U) % count]].position_m;
      if (adjacent_backtracks(a, b, c, epsilon))
        return failure(Status::kSelfIntersection);
      for (size_t second = first + 1U; second < count; ++second) {
        const bool adjacent = second == first + 1U ||
                              (first == 0U && second == count - 1U);
        if (adjacent)
          continue;
        const Point2d d = graph.nodes[ring.node_indices[second]].position_m;
        const Point2d e =
            graph.nodes[ring.node_indices[(second + 1U) % count]].position_m;
        if (segments_meet(a, b, d, e, epsilon))
          return failure(Status::kSelfIntersection);
      }
    }
  }

  const RingView &outer = rings.front();
  for (size_t hole_index = 1U; hole_index < rings.size(); ++hole_index) {
    const RingView &hole = rings[hole_index];
    if (ring_edges_meet(outer, hole, graph.nodes, epsilon))
      return failure(Status::kLoopTouching);
    for (const size_t node_index : hole.node_indices) {
      const PointLocation location =
          locate_point(graph.nodes[node_index].position_m, outer, graph.nodes,
                       epsilon);
      if (location == PointLocation::kBoundary)
        return failure(Status::kLoopTouching);
      if (location != PointLocation::kInside)
        return failure(Status::kHoleOutsideOuter);
    }
  }

  for (size_t first = 1U; first < rings.size(); ++first) {
    for (size_t second = first + 1U; second < rings.size(); ++second) {
      if (ring_edges_meet(rings[first], rings[second], graph.nodes, epsilon))
        return failure(Status::kHoleOverlap);
      const Point2d first_point =
          graph.nodes[rings[first].node_indices.front()].position_m;
      const Point2d second_point =
          graph.nodes[rings[second].node_indices.front()].position_m;
      if (locate_point(first_point, rings[second], graph.nodes, epsilon) !=
              PointLocation::kOutside ||
          locate_point(second_point, rings[first], graph.nodes, epsilon) !=
              PointLocation::kOutside)
        return failure(Status::kHoleOverlap);
    }
  }

  return {Status::kOk, graph};
}

} // namespace hss::boundary

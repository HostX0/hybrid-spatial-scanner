#include "hss/boundary_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hss::boundary::Edge;
using hss::boundary::Graph;
using hss::boundary::Loop;
using hss::boundary::Node;
using hss::boundary::Options;
using hss::boundary::Point2d;
using hss::boundary::Status;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Options options() {
  Options value;
  value.min_edge_length_m = 0.01;
  value.geometric_epsilon_m = 1.0e-8;
  value.max_abs_coordinate_m = 1'000.0;
  return value;
}

std::string uuid(size_t value) {
  std::ostringstream stream;
  stream << "90000000-0000-4000-8000-" << std::hex << std::nouppercase
         << std::setw(12) << std::setfill('0') << value;
  return stream.str();
}

void append_loop(Graph *graph, const std::vector<Point2d> &points,
                 bool outer, size_t *next_id) {
  Loop loop;
  loop.id = uuid((*next_id)++);
  std::vector<std::string> node_ids;
  for (const Point2d point : points) {
    Node node;
    node.id = uuid((*next_id)++);
    node.position_m = point;
    node_ids.push_back(node.id);
    graph->nodes.push_back(std::move(node));
  }
  for (size_t index = 0U; index < points.size(); ++index) {
    Edge edge;
    edge.id = uuid((*next_id)++);
    edge.start_node_id = node_ids[index];
    edge.end_node_id = node_ids[(index + 1U) % node_ids.size()];
    loop.edge_ids.push_back(edge.id);
    graph->edges.push_back(std::move(edge));
  }
  if (outer)
    graph->outer = std::move(loop);
  else
    graph->holes.push_back(std::move(loop));
}

Graph graph_from(const std::vector<Point2d> &outer,
                 const std::vector<std::vector<Point2d>> &holes = {}) {
  Graph graph;
  size_t next_id = 1U;
  append_loop(&graph, outer, true, &next_id);
  for (const auto &hole : holes)
    append_loop(&graph, hole, false, &next_id);
  return graph;
}

std::vector<std::string> split(std::string_view input, char delimiter) {
  std::vector<std::string> fields;
  size_t start = 0U;
  while (start <= input.size()) {
    const size_t end = input.find(delimiter, start);
    fields.emplace_back(input.substr(
        start, end == std::string_view::npos ? input.size() - start
                                             : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1U;
  }
  return fields;
}

std::vector<std::string> ids(std::string_view field) {
  return field == "-" ? std::vector<std::string>{} : split(field, ';');
}

Graph load_graph_fixture(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open boundary graph fixture");
  Graph graph;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "NODE") {
      if (fields.size() != 8U)
        throw std::runtime_error("invalid NODE fixture row");
      graph.nodes.push_back(
          {fields[1],
           {std::stod(fields[2]), std::stod(fields[3])},
           ids(fields[4]),
           ids(fields[5]),
           ids(fields[6]),
           fields[7] == "1"});
    } else if (fields.front() == "EDGE") {
      if (fields.size() != 9U)
        throw std::runtime_error("invalid EDGE fixture row");
      graph.edges.push_back({fields[1], fields[2], fields[3], ids(fields[4]),
                             ids(fields[5]), ids(fields[6]), ids(fields[7]),
                             fields[8] == "1"});
    } else if (fields.front() == "OUTER" || fields.front() == "HOLE") {
      if (fields.size() != 3U)
        throw std::runtime_error("invalid loop fixture row");
      Loop loop{fields[1], ids(fields[2])};
      if (fields.front() == "OUTER")
        graph.outer = std::move(loop);
      else
        graph.holes.push_back(std::move(loop));
    } else {
      throw std::runtime_error("unknown boundary graph fixture row");
    }
  }
  return graph;
}

void expect_failure(const Graph &graph, Status expected,
                    const std::string &message, Options validation = options()) {
  const auto result = hss::boundary::validate(graph, validation);
  check(result.status == expected, message + " has explicit status " +
                                       hss::boundary::status_name(expected));
  check(!result.graph, message + " exposes no partial accepted graph");
}

void test_fixture_preserves_identity_lineage_and_protected_topology() {
  const Graph graph = load_graph_fixture(
      std::string(HSS_SOURCE_DIR) +
      "/fixtures/boundary_graph/protected_rib_with_hole.csv");
  const auto result = hss::boundary::validate(graph, options());
  check(result.status == Status::kOk && result.graph.has_value(),
        "protected concave graph with contained CW hole is accepted");
  check(result.graph == graph,
        "accepted graph preserves every identity, lineage, support, flag, and coordinate");
  if (!result.graph)
    return;
  check(result.graph->nodes.size() == 13U && result.graph->edges.size() == 13U,
        "validator neither hulls, simplifies, merges, nor invents topology");
  check(result.graph->edges[0].protected_topology &&
            result.graph->edges[1].protected_topology,
        "forward collinear protected edges remain distinct");
  check(!result.graph->nodes.front().parent_node_ids.empty() &&
            !result.graph->edges[1].parent_edge_ids.empty() &&
            !result.graph->edges[1].supporting_surface_ids.empty(),
        "node and edge lineage/support fields survive acceptance");
}

void test_arbitrary_angles_concavity_and_holes() {
  Graph graph = graph_from(
      {{0.0, 0.0}, {3.5, 0.4}, {4.7, 2.6}, {2.8, 2.0},
       {1.7, 4.1}, {-0.4, 2.7}},
      {{{1.0, 1.2}, {1.1, 1.8}, {1.8, 1.7}, {1.7, 1.1}}});
  graph.edges[1].protected_topology = true;
  graph.edges[1].parent_edge_ids = {uuid(10'001U)};
  graph.edges[1].replacement_edge_ids = {uuid(10'002U)};
  const auto result = hss::boundary::validate(graph, options());
  check(result.status == Status::kOk && result.graph == graph,
        "oblique concave topology and a nonrectangular contained hole are unchanged");
}

void test_orientation_containment_and_hole_separation() {
  expect_failure(graph_from({{0, 0}, {0, 4}, {4, 4}, {4, 0}}),
                 Status::kWrongWinding, "clockwise outer loop");
  expect_failure(
      graph_from({{0, 0}, {5, 0}, {5, 5}, {0, 5}},
                 {{{1, 1}, {2, 1}, {2, 2}, {1, 2}}}),
      Status::kWrongWinding, "counterclockwise hole");
  expect_failure(
      graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}},
                 {{{5, 1}, {5, 2}, {6, 2}, {6, 1}}}),
      Status::kHoleOutsideOuter, "outside hole");
  expect_failure(
      graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}},
                 {{{0, 1}, {0, 2}, {1, 2}, {1, 1}}}),
      Status::kLoopTouching, "hole touching outer edge");
  expect_failure(
      graph_from({{0, 0}, {6, 0}, {6, 5}, {0, 5}},
                 {{{1, 1}, {1, 3}, {3, 3}, {3, 1}},
                  {{2, 2}, {2, 4}, {4, 4}, {4, 2}}}),
      Status::kHoleOverlap, "overlapping holes");
}

void test_crossings_touching_duplicates_and_short_edges() {
  expect_failure(
      graph_from({{0, 0}, {4, 0}, {1, 3}, {3, -1}, {4, 4}, {0, 4}}),
      Status::kSelfIntersection, "self-crossing outer loop");
  expect_failure(graph_from({{0, 0}, {4, 0}, {4, 4}, {2, 0}, {0, 4}}),
                 Status::kSelfIntersection, "self-touching outer loop");
  expect_failure(
      graph_from({{0, 0}, {3, 0}, {2, 0}, {4, 3}, {0, 3}}),
      Status::kSelfIntersection, "adjacent backtracking overlap");
  expect_failure(
      graph_from({{0, 0}, {4, 0}, {4, 4}, {1, 1}, {0, 4}, {1, 1}}),
      Status::kDuplicateVertex, "duplicate loop vertex coordinates");
  expect_failure(graph_from({{0, 0}, {0.005, 0}, {4, 0}, {4, 4}, {0, 4}}),
                 Status::kNearZeroEdge, "near-zero edge");
  expect_failure(graph_from({{0, 0}, {2, 0}, {4, 0}}),
                 Status::kDegenerateLoop, "zero-area collinear loop");
}

void test_identity_lineage_coordinate_and_reference_failures() {
  Graph invalid_id = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  invalid_id.nodes[0].id = "not-a-uuid";
  expect_failure(invalid_id, Status::kInvalidId, "malformed entity ID");

  Graph invalid_uuid_variant =
      graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  invalid_uuid_variant.nodes[0].id =
      "90000000-0000-4000-7000-000000000001";
  expect_failure(invalid_uuid_variant, Status::kInvalidId,
                 "non-RFC UUID variant");

  Graph duplicate_id = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  duplicate_id.edges[1].id = duplicate_id.edges[0].id;
  expect_failure(duplicate_id, Status::kDuplicateId, "duplicate entity ID");

  Graph self_lineage = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  self_lineage.edges[0].parent_edge_ids = {self_lineage.edges[0].id};
  expect_failure(self_lineage, Status::kInvalidLineage,
                 "self-referential edge lineage");

  Graph conflicting_lineage =
      graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  conflicting_lineage.edges[0].parent_edge_ids = {uuid(20'001U)};
  conflicting_lineage.edges[0].replacement_edge_ids = {uuid(20'001U)};
  expect_failure(conflicting_lineage, Status::kInvalidLineage,
                 "same lineage identity used as parent and replacement");

  Graph nonfinite = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  nonfinite.nodes[1].position_m.x =
      std::numeric_limits<double>::quiet_NaN();
  expect_failure(nonfinite, Status::kNonFiniteCoordinate,
                 "nonfinite coordinate");

  Graph out_of_range = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  out_of_range.nodes[1].position_m.x = 1'001.0;
  expect_failure(out_of_range, Status::kCoordinateOutOfRange,
                 "coordinate outside declared extent");

  Graph invalid_reference = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  invalid_reference.edges[0].start_node_id = uuid(999'999U);
  expect_failure(invalid_reference, Status::kInvalidReference,
                 "edge endpoint with no current node");

  Graph malformed_loop_reference =
      graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  malformed_loop_reference.outer.edge_ids[0] = "bad-edge-id";
  expect_failure(malformed_loop_reference, Status::kInvalidId,
                 "malformed loop edge ID");
}

void test_open_ambiguous_and_bounded_inputs_are_atomic() {
  Graph open = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  open.edges[0].end_node_id = open.edges[2].start_node_id;
  expect_failure(open, Status::kOpenChain,
                 "open chain is not closed by an invented segment");

  Graph duplicate_member = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  duplicate_member.outer.edge_ids[1] = duplicate_member.outer.edge_ids[0];
  expect_failure(duplicate_member, Status::kDuplicateLoopMember,
                 "edge repeated in an ordered loop");

  Graph ambiguous = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  Edge unused;
  unused.id = uuid(999U);
  unused.start_node_id = ambiguous.nodes[0].id;
  unused.end_node_id = ambiguous.nodes[2].id;
  ambiguous.edges.push_back(std::move(unused));
  expect_failure(ambiguous, Status::kAmbiguousTopology,
                 "unassigned alternate boundary edge");

  const Graph valid = graph_from({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
  Options invalid_options;
  expect_failure(valid, Status::kInvalidOptions, "undeclared tolerances",
                 invalid_options);
  Options too_small = options();
  too_small.max_nodes = 3U;
  expect_failure(valid, Status::kResourceLimitExceeded, "node cap",
                 too_small);
  Options no_pair_budget = options();
  no_pair_budget.max_pair_tests = 5U;
  expect_failure(valid, Status::kWorkLimitExceeded,
                 "declared pair-test budget", no_pair_budget);
}

} // namespace

int main() {
  test_fixture_preserves_identity_lineage_and_protected_topology();
  test_arbitrary_angles_concavity_and_holes();
  test_orientation_containment_and_hole_separation();
  test_crossings_touching_duplicates_and_short_edges();
  test_identity_lineage_coordinate_and_reference_failures();
  test_open_ambiguous_and_bounded_inputs_are_atomic();
  if (failures == 0)
    std::cout << "boundary graph validation tests passed\n";
  return failures == 0 ? 0 : 1;
}

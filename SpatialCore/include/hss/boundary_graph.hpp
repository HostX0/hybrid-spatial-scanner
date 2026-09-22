#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::boundary {

inline constexpr size_t kHardMaxNodes = 65'536U;
inline constexpr size_t kHardMaxEdges = 65'536U;
inline constexpr size_t kHardMaxHoles = 4'096U;
inline constexpr size_t kHardMaxLineageIdsPerEntity = 4'096U;
inline constexpr size_t kHardMaxPairTests = 50'000'000U;

struct Point2d {
  double x{0.0};
  double y{0.0};
  bool operator==(const Point2d &) const = default;
};

// Entity identifiers are canonical, lowercase, non-nil UUID strings. Lineage
// identifiers may name historical entities that are not in the submitted
// graph, but they are still syntax checked and preserved verbatim.
struct Node {
  std::string id;
  Point2d position_m;
  std::vector<std::string> parent_node_ids;
  std::vector<std::string> replacement_node_ids;
  std::vector<std::string> provenance_support_ids;
  bool position_locked{false};
  bool operator==(const Node &) const = default;
};

struct Edge {
  std::string id;
  std::string start_node_id;
  std::string end_node_id;
  std::vector<std::string> parent_edge_ids;
  std::vector<std::string> replacement_edge_ids;
  std::vector<std::string> supporting_surface_ids;
  std::vector<std::string> provenance_support_ids;
  bool protected_topology{false};
  bool operator==(const Edge &) const = default;
};

// edge_ids are directed and ordered. The validator never reverses, reorders,
// merges, closes, simplifies, or otherwise repairs this chain.
struct Loop {
  std::string id;
  std::vector<std::string> edge_ids;
  bool operator==(const Loop &) const = default;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  Loop outer;
  std::vector<Loop> holes;
  bool operator==(const Graph &) const = default;
};

struct Options {
  size_t max_nodes{2'048U};
  size_t max_edges{2'048U};
  size_t max_holes{256U};
  size_t max_lineage_ids_per_entity{256U};
  size_t max_pair_tests{5'000'000U};

  // Deliberately unusable defaults. These are dataset/numeric policy, not
  // sensor-accuracy statements. geometric_epsilon_m must be smaller than the
  // declared near-zero edge threshold.
  double min_edge_length_m{0.0};
  double geometric_epsilon_m{0.0};
  double max_abs_coordinate_m{0.0};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kResourceLimitExceeded,
  kWorkLimitExceeded,
  kInvalidId,
  kDuplicateId,
  kInvalidLineage,
  kNonFiniteCoordinate,
  kCoordinateOutOfRange,
  kInvalidReference,
  kLoopTooShort,
  kDuplicateLoopMember,
  kOpenChain,
  kAmbiguousTopology,
  kDuplicateVertex,
  kNearZeroEdge,
  kDegenerateLoop,
  kWrongWinding,
  kSelfIntersection,
  kLoopTouching,
  kHoleOutsideOuter,
  kHoleOverlap,
  kNumericalFailure,
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Present only for kOk. On success this is an exact copy of the submitted
  // graph so stable identities, lineage, protected collinear edges, arbitrary
  // angles, and concavities are observable without topology repair.
  std::optional<Graph> graph;
};

const char *status_name(Status status);

// Validates one outer CCW loop and zero or more contained, mutually disjoint
// CW holes. Failure is atomic and never exposes a partially accepted graph.
Result validate(const Graph &graph, const Options &options);

} // namespace hss::boundary

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::topology_protection {

inline constexpr size_t kHardMaxVertices = 65'536U;
inline constexpr size_t kHardMaxSegments = 65'536U;
inline constexpr size_t kHardMaxIdsPerField = 4'096U;
inline constexpr size_t kHardMaxPairTests = 50'000'000U;

struct Point2d {
  double x{0.0};
  double y{0.0};
  bool operator==(const Point2d &) const = default;
};

// Source support IDs identify retained observations. They are required before
// a candidate coordinate can be accepted; they are not confidence scores.
struct Vertex {
  std::string id;
  Point2d position_m;
  std::vector<std::string> parent_vertex_ids;
  std::vector<std::string> provenance_support_ids;
  bool position_locked{false};
  bool operator==(const Vertex &) const = default;
};

// ordered_segments is the complete directed protected feature chain. A parent
// segment ID may name a historical entity outside this snapshot.
struct Segment {
  std::string id;
  std::string start_vertex_id;
  std::string end_vertex_id;
  std::vector<std::string> parent_segment_ids;
  std::vector<std::string> provenance_support_ids;
  bool protected_topology{false};
  bool operator==(const Segment &) const = default;
};

struct Feature {
  std::string id;
  uint64_t revision{0U};
  std::vector<Vertex> vertices;
  std::vector<Segment> ordered_segments;
  bool operator==(const Feature &) const = default;
};

struct Options {
  size_t max_vertices{2'048U};
  size_t max_segments{2'048U};
  size_t max_ids_per_field{256U};
  size_t max_pair_tests{5'000'000U};

  // Mandatory policy values, not sensing-accuracy statements.
  double min_segment_length_m{0.0};
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
  kInvalidRevision,
  kInvalidLineage,
  kMissingEvidence,
  kNonFiniteCoordinate,
  kCoordinateOutOfRange,
  kInvalidReference,
  kWrongTopologySize,
  kOpenChain,
  kAmbiguousTopology,
  kDuplicateVertex,
  kNearZeroSegment,
  kUnprotectedSegment,
  kSelfIntersection,
  kFeatureIdentityChange,
  kTopologyChange,
  kPositionLockChange,
  kPositionLockViolation,
  kNumericalFailure,
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Present only for kOk, and then an exact copy of the candidate. The guard
  // never unlocks, repairs, reorders, removes, or invents geometry.
  std::optional<Feature> feature;
};

const char *status_name(Status status);

// Checks one submitted protected feature. This is validation only and does
// not infer coordinates, evidence, lineage, or a missing segment.
Result validate(const Feature &feature, const Options &options);

// Compares an accepted protected feature with a candidate update. Each
// corresponding protected vertex and segment must retain its identity or name
// the exact predecessor in its parent lineage. Coordinates may refine when
// evidence remains present, except for separately position-locked vertices.
// Any rejected update returns no candidate snapshot.
Result validate_refinement(const Feature &previous, const Feature &candidate,
                           const Options &options);

} // namespace hss::topology_protection

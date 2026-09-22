#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::rib {

inline constexpr size_t kRequiredVertexCount = 6U;
inline constexpr size_t kRequiredSegmentCount = 5U;
inline constexpr size_t kHardMaxIdsPerField = 4'096U;

struct Point2d {
  double x{0.0};
  double y{0.0};
  bool operator==(const Point2d &) const = default;
};

enum class SegmentRole : uint8_t {
  kApproach,
  kFirstReturn,
  kFront,
  kSecondReturn,
  kContinuation,
};

// Lineage identifiers may refer to historical vertices outside this snapshot.
// Provenance identifiers name retained source observations, not confidence
// scores and not semantic evidence promoted into measurements.
struct Vertex {
  std::string id;
  Point2d position_m;
  std::vector<std::string> parent_vertex_ids;
  std::vector<std::string> replacement_vertex_ids;
  std::vector<std::string> provenance_support_ids;
  bool position_locked{false};
  bool operator==(const Vertex &) const = default;
};

struct Segment {
  std::string id;
  SegmentRole role{SegmentRole::kApproach};
  std::string start_vertex_id;
  std::string end_vertex_id;
  std::vector<std::string> parent_segment_ids;
  std::vector<std::string> replacement_segment_ids;
  std::vector<std::string> supporting_surface_ids;
  std::vector<std::string> provenance_support_ids;
  bool protected_topology{false};
  bool operator==(const Segment &) const = default;
};

// ordered_segments is the complete directed open chain. A validator never
// inserts a missing return, reorders a role, merges the narrow front into a
// broad wall, or changes coordinates to make the feature symmetric.
struct Chain {
  std::string feature_id;
  uint64_t revision{0U};
  std::vector<Vertex> vertices;
  std::vector<Segment> ordered_segments;
  bool operator==(const Chain &) const = default;
};

struct Options {
  size_t max_ids_per_field{256U};

  // These are validation policy, not sensor-accuracy claims. The geometric
  // epsilon must be smaller than the declared minimum segment length.
  double min_segment_length_m{0.0};
  double geometric_epsilon_m{0.0};
  double max_abs_coordinate_m{0.0};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kResourceLimitExceeded,
  kInvalidId,
  kDuplicateId,
  kInvalidRevision,
  kInvalidLineage,
  kMissingEvidence,
  kNonFiniteCoordinate,
  kCoordinateOutOfRange,
  kWrongSegmentCount,
  kWrongRoleOrder,
  kInvalidReference,
  kOpenChain,
  kAmbiguousChain,
  kDuplicateVertex,
  kNearZeroSegment,
  kUnprotectedSegment,
  kDegenerateFeature,
  kSelfIntersection,
  kTopologyChange,
  kNumericalFailure,
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Present only for kOk and always an exact copy of the submitted candidate.
  std::optional<Chain> chain;
};

const char *status_name(Status status);

// Validates one complete approach/first-return/front/second-return/
// continuation chain. Every segment must carry surface evidence, provenance
// evidence, and topology protection. Failure is atomic.
Result validate(const Chain &chain, const Options &options);

// Additionally validates an update from an already accepted chain. Stable
// identities are allowed; a replaced vertex or segment must name the
// corresponding prior entity as a parent. Geometry may otherwise refine to
// any finite, non-self-intersecting dimensions and angles.
Result validate_update(const Chain &previous, const Chain &candidate,
                       const Options &options);

} // namespace hss::rib

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::surface_policy {

inline constexpr size_t kHardMaxEntities = 4'096U;
inline constexpr size_t kHardMaxSurfaces = 65'536U;
inline constexpr size_t kHardMaxSupportIdsPerSurface = 4'096U;
inline constexpr size_t kHardMaxGeometryReferenceBytes = 4'096U;

enum class SurfaceKind : uint8_t { kPlane, kMeshPatch };

// This role comes from accepted metric evidence, not from an object label.
// The policy never changes it.
enum class EvidenceRole : uint8_t { kStructural, kObject };

enum class GeometryOrigin : uint8_t {
  kObserved,
  kFitted,
  kUserMeasured,
  kInferred,
  kUnknown,
};

enum class SemanticClass : uint8_t {
  kUnknown,
  kWall,
  kFloor,
  kCeiling,
  kShelf,
  kFurniture,
};

enum class SemanticOrigin : uint8_t {
  kPlatform,
  kLocalModel,
  kUser,
  kImported,
  kUnknown,
};

enum class Face : uint8_t {
  kUnspecified,
  kTop,
  kFront,
  kUnderside,
  kRear,
  kSupport,
};

enum class FloorSupport : uint8_t {
  kNone,
  kObserved,
  kFitted,
  kUserMeasured,
  kAmbiguous,
};

enum class AreaPolicy : uint8_t {
  kUnspecified,
  kPreserveFloorFootprint,
  kNetFixedObstructions,
};

enum class PlanningLayer : uint8_t {
  kStructural,
  kFurniture,
  kOverhead,
  kFixedObstruction,
};

enum class FixedObstructionDisposition : uint8_t {
  kNotRequested,
  kMissingFloorSupport,
  kExcludedByAreaPolicy,
  kAccepted,
};

struct Plane {
  std::array<double, 3> normal_unit{0.0, 0.0, 0.0};
  double offset_m{0.0};
  bool operator==(const Plane &) const = default;
};

// geometry_reference is an opaque stable reference to the already-existing
// plane or captured mesh patch. It is copied verbatim; this policy neither
// creates geometry nor changes a plane, mesh, support list, or provenance.
struct SurfaceEvidence {
  std::string id;
  SurfaceKind kind{SurfaceKind::kMeshPatch};
  EvidenceRole evidence_role{EvidenceRole::kObject};
  GeometryOrigin geometry_origin{GeometryOrigin::kUnknown};
  Face face{Face::kUnspecified};
  std::string geometry_reference;
  std::optional<Plane> plane;
  std::vector<std::string> support_ids;
  bool operator==(const SurfaceEvidence &) const = default;
};

struct EntityCandidate {
  std::string id;
  SemanticClass semantic_class{SemanticClass::kUnknown};
  SemanticOrigin semantic_origin{SemanticOrigin::kUnknown};
  FloorSupport floor_support{FloorSupport::kNone};
  // This is an explicit planning classification. It does not claim that the
  // entity is load-bearing and it cannot replace floor-contact evidence.
  bool explicitly_fixed{false};
  std::vector<SurfaceEvidence> surfaces;
  bool operator==(const EntityCandidate &) const = default;
};

struct Request {
  AreaPolicy area_policy{AreaPolicy::kUnspecified};
  std::vector<EntityCandidate> entities;
};

struct Options {
  size_t max_entities{1'024U};
  size_t max_surfaces{16'384U};
  size_t max_support_ids_per_surface{1'024U};
  size_t max_geometry_reference_bytes{1'024U};
  // Numeric validation tolerance only. It is not a sensor-accuracy claim.
  // Deliberately unusable until the caller declares it.
  double unit_normal_tolerance{0.0};
};

struct EntityDecision {
  std::string entity_id;
  SemanticClass semantic_class{SemanticClass::kUnknown};
  SemanticOrigin semantic_origin{SemanticOrigin::kUnknown};
  PlanningLayer planning_layer{PlanningLayer::kFurniture};
  FixedObstructionDisposition fixed_obstruction{
      FixedObstructionDisposition::kNotRequested};
  // An exact copy of the submitted metric evidence. Parallel or nearby
  // surfaces are never merged, and semantic labels cannot change geometry.
  std::vector<SurfaceEvidence> surfaces;
  // Object rear and underside faces stay explicitly unknown unless a submitted
  // supported surface identifies that face.
  std::vector<Face> unknown_faces;
  bool operator==(const EntityDecision &) const = default;
};

struct Decision {
  AreaPolicy area_policy{AreaPolicy::kUnspecified};
  std::vector<EntityDecision> entities;
  bool operator==(const Decision &) const = default;
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kResourceLimitExceeded,
  kInvalidId,
  kDuplicateId,
  kUnsupportedValue,
  kNonFiniteInput,
  kInvalidGeometry,
  kMissingEvidence,
  kAmbiguousInput,
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Present only for kOk. Every failure is atomic and exposes no partial
  // classification or partially copied evidence.
  std::optional<Decision> decision;
};

const char *status_name(Status status);

// Routes already-supported surfaces to structural, furniture, overhead, or
// fixed-obstruction planning layers. It does not fit, merge, synthesize,
// reclassify, or otherwise mutate metric geometry.
Result separate(const Request &request, const Options &options);

} // namespace hss::surface_policy

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hss::ceiling_overhead {

inline constexpr size_t kHardMaxPatches = 4'096U;
inline constexpr size_t kHardMaxProvenanceRecords = 65'536U;
inline constexpr size_t kHardMaxMetricSupports = 262'144U;
inline constexpr size_t kHardMaxMeshVertices = 262'144U;
inline constexpr size_t kHardMaxMeshTriangles = 524'288U;
inline constexpr size_t kHardMaxUnknownBoundaryVertices = 65'536U;

struct Point3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  bool operator==(const Point3d &) const = default;
};

struct Plane {
  // The sign and arbitrary orientation supplied by the caller are retained.
  // No gravity, axis, or single-height ceiling prior is applied.
  Point3d normal;
  double offset_m{0.0};
  bool operator==(const Plane &) const = default;
};

enum class Role : uint8_t {
  kCeilingPatch,
  kSoffitBottom,
  kSoffitReturn,
  kFloatingShelf,
  kUnknownRegion,
};

enum class GeometryOrigin : uint8_t {
  kObserved,
  kFitted,
  kUnknown,
};

enum class Representation : uint8_t {
  kNone,
  kPlane,
  kCapturedMesh,
};

enum class SourceKind : uint8_t {
  kDepthFrame,
  kMeshAnchor,
  kNoDepthMask,
};

enum class UnknownReason : uint8_t {
  kNone,
  kNoDepth,
  kOpenSky,
  kOccluded,
};

struct SourceReference {
  SourceKind kind{SourceKind::kDepthFrame};
  uint64_t source_id{0U};
  uint64_t session_epoch{0U};
  uint64_t sequence{0U};
  bool operator==(const SourceReference &) const = default;
};

struct MetricSupport {
  Point3d point_m;
  // Index into PatchInput::provenance. The referenced record must be a metric
  // depth or mesh source; a no-depth mask is not geometric support.
  size_t provenance_index{0U};
  bool operator==(const MetricSupport &) const = default;
};

struct Triangle {
  uint32_t a{0U};
  uint32_t b{0U};
  uint32_t c{0U};
  bool operator==(const Triangle &) const = default;
};

struct PatchInput {
  uint64_t patch_id{0U};
  Role role{Role::kUnknownRegion};
  GeometryOrigin geometry_origin{GeometryOrigin::kUnknown};
  Representation representation{Representation::kNone};
  std::optional<Plane> plane;
  std::vector<Point3d> mesh_vertices_m;
  std::vector<Triangle> mesh_triangles;
  std::vector<MetricSupport> metric_support;
  // Boundary only: it does not describe or fill the unknown interior.
  std::vector<Point3d> unknown_boundary_m;
  UnknownReason unknown_reason{UnknownReason::kNone};
  std::vector<SourceReference> provenance;
  bool operator==(const PatchInput &) const = default;
};

struct Options {
  size_t max_input_patches{256U};
  size_t max_provenance_records{8'192U};
  size_t max_metric_supports{65'536U};
  size_t max_mesh_vertices{65'536U};
  size_t max_mesh_triangles{131'072U};
  size_t max_unknown_boundary_vertices{16'384U};
  // Coordinates outside the declared project-local metric extent are
  // rejected before output allocation.
  double max_abs_coordinate_m{1'000.0};
  double unit_normal_tolerance{1.0e-9};
  // A caller-owned consistency tolerance, not a sensor-accuracy claim.
  double max_plane_support_distance_m{0.03};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInputLimitExceeded,
  kInvalidPatchId,
  kInvalidEnumValue,
  kDuplicatePatchId,
  kMissingProvenance,
  kInvalidProvenance,
  kNonFiniteGeometry,
  kCoordinateOutOfBounds,
  kInvalidPlane,
  kInsufficientMetricSupport,
  kMetricSupportMismatch,
  kInvalidMesh,
  kRoleEvidenceMismatch,
  kInvalidUnknownRegion,
  kResourceLimit,
  kInternalFailure,
};

struct SurfacePatch {
  uint64_t patch_id{0U};
  Role role{Role::kCeilingPatch};
  GeometryOrigin geometry_origin{GeometryOrigin::kObserved};
  Representation representation{Representation::kPlane};
  std::optional<Plane> plane;
  std::vector<Point3d> mesh_vertices_m;
  std::vector<Triangle> mesh_triangles;
  std::vector<MetricSupport> metric_support;
  std::vector<SourceReference> provenance;
  bool operator==(const SurfacePatch &) const = default;
};

struct UnknownRegion {
  uint64_t patch_id{0U};
  UnknownReason reason{UnknownReason::kNoDepth};
  std::vector<Point3d> boundary_m;
  std::vector<SourceReference> provenance;
  bool operator==(const UnknownRegion &) const = default;
};

struct Model {
  std::vector<SurfacePatch> ceiling_patches;
  std::vector<SurfacePatch> soffit_bottoms;
  std::vector<SurfacePatch> soffit_returns;
  std::vector<SurfacePatch> floating_shelves;
  std::vector<UnknownRegion> unknown_regions;
  bool operator==(const Model &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Empty for every failure. Classification is all-or-nothing, so callers
  // cannot accidentally publish a partially validated ceiling model.
  std::optional<Model> model;
};

// Validates caller-declared, source-backed roles and copies them into distinct
// ceiling, soffit, shelf, and unknown collections. It does not infer semantic
// roles, fit planes, fill unknown regions, or create floor obstructions.
Result classify(const std::vector<PatchInput> &patches,
                const Options &options);

} // namespace hss::ceiling_overhead

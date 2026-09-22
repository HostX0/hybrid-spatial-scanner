#pragma once

#include "hss/ceiling_overhead.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace hss::ceiling_overhead::fixtures {

inline SourceReference depth_source(uint64_t id, uint64_t sequence) {
  return {SourceKind::kDepthFrame, id, 3U, sequence};
}

inline SourceReference mesh_source(uint64_t id, uint64_t sequence) {
  return {SourceKind::kMeshAnchor, id, 3U, sequence};
}

inline PatchInput plane_patch(uint64_t id, Role role, Point3d normal,
                              double offset_m, uint64_t source_id,
                              double x0, double x1, double z0, double z1) {
  PatchInput patch;
  patch.patch_id = id;
  patch.role = role;
  patch.geometry_origin = GeometryOrigin::kFitted;
  patch.representation = Representation::kPlane;
  patch.plane = Plane{normal, offset_m};
  patch.provenance = {depth_source(source_id, 100U + id)};
  const auto point = [normal, offset_m](double x, double z) {
    return Point3d{x, (-offset_m - normal.x * x - normal.z * z) / normal.y,
                   z};
  };
  patch.metric_support = {{point(x0, z0), 0U}, {point(x1, z0), 0U},
                          {point(x1, z1), 0U}, {point(x0, z1), 0U}};
  return patch;
}

// Synthetic source-backed fixture only. It is not a sensor capture or physical
// metrology result.
inline std::vector<PatchInput> sloped_coffer_skylight() {
  const double slope_length = std::hypot(1.0, std::hypot(0.2, 0.1));
  const Point3d slope{0.2 / slope_length, 1.0 / slope_length,
                      0.1 / slope_length};

  std::vector<PatchInput> patches;
  patches.push_back(plane_patch(11U, Role::kCeilingPatch, slope, -3.0, 501U,
                                -2.0, 2.0, -1.5, 1.5));
  patches.push_back(plane_patch(12U, Role::kSoffitBottom, {0.0, 1.0, 0.0},
                                -2.55, 502U, -0.8, 0.8, -0.6, 0.6));

  PatchInput vertical_return;
  vertical_return.patch_id = 13U;
  vertical_return.role = Role::kSoffitReturn;
  vertical_return.geometry_origin = GeometryOrigin::kFitted;
  vertical_return.representation = Representation::kPlane;
  vertical_return.plane = Plane{{1.0, 0.0, 0.0}, -0.8};
  vertical_return.provenance = {depth_source(503U, 113U)};
  vertical_return.metric_support = {
      {{0.8, 2.55, -0.6}, 0U}, {{0.8, 2.95, -0.6}, 0U},
      {{0.8, 2.95, 0.6}, 0U},  {{0.8, 2.55, 0.6}, 0U}};
  patches.push_back(vertical_return);

  PatchInput shelf;
  shelf.patch_id = 14U;
  shelf.role = Role::kFloatingShelf;
  shelf.geometry_origin = GeometryOrigin::kObserved;
  shelf.representation = Representation::kCapturedMesh;
  shelf.provenance = {mesh_source(601U, 114U)};
  shelf.mesh_vertices_m = {{-0.7, 1.45, -0.25}, {0.7, 1.45, -0.25},
                           {0.7, 1.45, 0.25}, {-0.7, 1.45, 0.25}};
  shelf.mesh_triangles = {{0U, 1U, 2U}, {0U, 2U, 3U}};
  patches.push_back(shelf);

  PatchInput skylight;
  skylight.patch_id = 15U;
  skylight.role = Role::kUnknownRegion;
  skylight.geometry_origin = GeometryOrigin::kUnknown;
  skylight.representation = Representation::kNone;
  skylight.unknown_reason = UnknownReason::kOpenSky;
  skylight.unknown_boundary_m = {{-0.45, 3.08, -0.35},
                                 {0.45, 2.90, -0.35},
                                 {0.45, 2.83, 0.35},
                                 {-0.45, 3.01, 0.35}};
  skylight.provenance = {
      depth_source(504U, 115U),
      {SourceKind::kNoDepthMask, 701U, 3U, 115U},
  };
  patches.push_back(skylight);
  return patches;
}

} // namespace hss::ceiling_overhead::fixtures

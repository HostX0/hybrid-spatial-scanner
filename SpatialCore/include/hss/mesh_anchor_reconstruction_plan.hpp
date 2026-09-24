#pragma once

#include "hss/mesh_anchor_patch_plan.hpp"
#include "hss/mesh_anchor_project_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>
#include <vector>

namespace hss::mesh_anchor_reconstruction {

enum class CaptureStatus : uint8_t {
  kOk,
  kResourceLimit,
  kPayloadSizeMismatch,
  kInvalidGeometry,
  kInvalidCoordinateFrame,
  kRawIndexOverflow,
};

struct Prepared {
  CaptureStatus capture_status{CaptureStatus::kOk};
  std::vector<HSSVec3d> project_vertices;
  std::vector<mesh_patch::Triangle> local_triangles;
  std::vector<mesh_patch::Triangle> raw_triangles;
  mesh_anchor_patch::Plan metric_plan;
  size_t vertex_bytes{};
  size_t triangle_bytes{};
};

namespace detail {

inline CaptureStatus map_batch_status(mesh_anchor_project_batch::Status status) {
  switch (status) {
  case mesh_anchor_project_batch::Status::kOk:
    return CaptureStatus::kOk;
  case mesh_anchor_project_batch::Status::kResourceLimit:
    return CaptureStatus::kResourceLimit;
  case mesh_anchor_project_batch::Status::kPayloadSizeMismatch:
    return CaptureStatus::kPayloadSizeMismatch;
  case mesh_anchor_project_batch::Status::kInvalidGeometry:
    return CaptureStatus::kInvalidGeometry;
  case mesh_anchor_project_batch::Status::kInvalidCoordinateFrame:
    return CaptureStatus::kInvalidCoordinateFrame;
  }
  return CaptureStatus::kInvalidGeometry;
}

inline void clear_capture(Prepared &out) {
  out.project_vertices.clear();
  out.local_triangles.clear();
  out.raw_triangles.clear();
  out.metric_plan = {};
}

} // namespace detail

// Prepares one accepted mesh-anchor revision for the existing live runtime.
// Capture truth and metric derivation are deliberately separate:
//
// 1. Decode and transform the complete owned payload once.
// 2. Rebase a byte/topology-equivalent raw triangle copy for snapshot storage.
// 3. Build the order-invariant metric plan from anchor-local topology.
//
// A metric-plan failure never discards a valid raw/project capture batch. A
// capture/transform/rebase failure is atomic and exposes no partial geometry.
// Surface identity remains anchor-local; callers may hash component surface_key
// values into scene IDs without allowing global vertex placement to rename a
// surface.
inline Prepared prepare(std::span<const uint8_t> payload, uint64_t vertex_count,
                        uint64_t triangle_count,
                        const HSSMat4d &T_world_anchor,
                        const HSSMat4d &T_project_world,
                        std::string_view support_identity,
                        uint64_t diversity_group, uint32_t raw_vertex_base,
                        const mesh_patch::Options &options = {}) {
  Prepared out;
  auto batch = mesh_anchor_project_batch::prepare(
      payload, vertex_count, triangle_count, T_world_anchor, T_project_world);
  out.vertex_bytes = batch.vertex_bytes;
  out.triangle_bytes = batch.triangle_bytes;
  out.capture_status = detail::map_batch_status(batch.status);
  if (out.capture_status != CaptureStatus::kOk)
    return out;

  out.project_vertices = std::move(batch.project_vertices);
  out.local_triangles = std::move(batch.triangles);

  try {
    out.raw_triangles.reserve(out.local_triangles.size());
    for (const auto &triangle : out.local_triangles) {
      const auto rebased =
          mesh_anchor_patch::rebase_raw_triangle(triangle, raw_vertex_base);
      if (!rebased) {
        detail::clear_capture(out);
        out.capture_status = CaptureStatus::kRawIndexOverflow;
        return out;
      }
      out.raw_triangles.push_back(*rebased);
    }
  } catch (const std::bad_alloc &) {
    detail::clear_capture(out);
    out.capture_status = CaptureStatus::kResourceLimit;
    return out;
  }

  // mesh_patch::Vec3 intentionally remains a separate internal type from the
  // C ABI math struct. Copy rather than aliasing/reinterpreting their storage.
  try {
    std::vector<mesh_patch::Vec3> metric_vertices;
    metric_vertices.reserve(out.project_vertices.size());
    for (const auto point : out.project_vertices)
      metric_vertices.push_back({point.x, point.y, point.z});

    out.metric_plan = mesh_anchor_patch::prepare(
        metric_vertices, out.local_triangles, support_identity, diversity_group,
        options);
  } catch (const std::bad_alloc &) {
    // Raw/project evidence is already valid and retained. Only the derived
    // metric plan becomes unavailable under allocation pressure.
    out.metric_plan = {};
    out.metric_plan.status = mesh_anchor_patch::Status::kResourceLimit;
  }

  return out;
}

inline const mesh_anchor_patch::ComponentPlan *
component_for_triangle(const Prepared &prepared, size_t triangle_index) {
  if (prepared.capture_status != CaptureStatus::kOk)
    return nullptr;
  return mesh_anchor_patch::component_for_triangle(prepared.metric_plan,
                                                   triangle_index);
}

} // namespace hss::mesh_anchor_reconstruction

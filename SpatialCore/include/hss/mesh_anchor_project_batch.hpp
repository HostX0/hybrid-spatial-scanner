#pragma once

#include "hss/coordinate_basis.hpp"
#include "hss/mesh_event_payload.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

namespace hss::mesh_anchor_project_batch {

constexpr double kMaxAbsProjectCoordinateM = 10'000.0;

using Triangle = mesh_event_payload::Triangle;

enum class Status : uint8_t {
  kOk,
  kResourceLimit,
  kPayloadSizeMismatch,
  kInvalidGeometry,
  kInvalidCoordinateFrame,
};

struct Prepared {
  Status status{Status::kOk};
  std::vector<HSSVec3d> project_vertices;
  std::vector<Triangle> triangles;
  size_t vertex_bytes{};
  size_t triangle_bytes{};
};

namespace detail {

inline bool bounded_project_point(HSSVec3d point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z) &&
         std::abs(point.x) <= kMaxAbsProjectCoordinateM &&
         std::abs(point.y) <= kMaxAbsProjectCoordinateM &&
         std::abs(point.z) <= kMaxAbsProjectCoordinateM;
}

inline Status map_payload_status(mesh_event_payload::Status status) {
  switch (status) {
  case mesh_event_payload::Status::kOk:
    return Status::kOk;
  case mesh_event_payload::Status::kResourceLimit:
    return Status::kResourceLimit;
  case mesh_event_payload::Status::kPayloadSizeMismatch:
    return Status::kPayloadSizeMismatch;
  case mesh_event_payload::Status::kInvalidGeometry:
    return Status::kInvalidGeometry;
  }
  return Status::kInvalidGeometry;
}

} // namespace detail

// Converts one already-owned HSSMeshEvent payload into a complete anchor-local
// indexed batch in canonical project metres. The input byte span remains
// caller-owned and is never modified or retained. Transform composition matches
// the live reconstruction path exactly: anchor-local -> world -> project.
// Triangle topology stays anchor-local; global snapshot rebasing is a separate
// operation so global vertex-buffer placement cannot rename or reshape a face.
inline Prepared prepare(std::span<const uint8_t> payload, uint64_t vertex_count,
                        uint64_t triangle_count,
                        const HSSMat4d &T_world_anchor,
                        const HSSMat4d &T_project_world) {
  Prepared out;
  const mesh_event_payload::Decoded decoded =
      mesh_event_payload::decode(payload, vertex_count, triangle_count);
  out.vertex_bytes = decoded.vertex_bytes;
  out.triangle_bytes = decoded.triangle_bytes;
  if (decoded.status != mesh_event_payload::Status::kOk) {
    out.status = detail::map_payload_status(decoded.status);
    return out;
  }

  if (!coordinate::is_rigid_transform(T_world_anchor) ||
      !coordinate::is_rigid_transform(T_project_world)) {
    out.status = Status::kInvalidCoordinateFrame;
    return out;
  }

  try {
    out.project_vertices.reserve(decoded.vertices.size());
    out.triangles.reserve(decoded.triangles.size());
    for (const mesh_event_payload::LocalVertex local : decoded.vertices) {
      HSSVec3d point{static_cast<double>(local.x),
                     static_cast<double>(local.y),
                     static_cast<double>(local.z)};
      point = coordinate::transform_point(T_world_anchor, point);
      point = coordinate::transform_point(T_project_world, point);
      if (!detail::bounded_project_point(point)) {
        out.project_vertices.clear();
        out.triangles.clear();
        out.status = Status::kInvalidCoordinateFrame;
        return out;
      }
      out.project_vertices.push_back(point);
    }

    for (const Triangle &triangle : decoded.triangles) {
      if (triangle[0] >= out.project_vertices.size() ||
          triangle[1] >= out.project_vertices.size() ||
          triangle[2] >= out.project_vertices.size()) {
        out.project_vertices.clear();
        out.triangles.clear();
        out.status = Status::kInvalidGeometry;
        return out;
      }
      out.triangles.push_back(triangle);
    }
  } catch (const std::bad_alloc &) {
    out.project_vertices.clear();
    out.triangles.clear();
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_anchor_project_batch

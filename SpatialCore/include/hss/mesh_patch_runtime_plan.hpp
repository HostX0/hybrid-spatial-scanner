#pragma once

#include "hss/mesh_patch_components.hpp"
#include "hss/robust_plane_fit.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hss::mesh_patch_runtime {

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kInvalidGeometry,
  kResourceLimit,
};

struct ComponentPlan {
  mesh_patch::Triangle canonical_triangle{};
  size_t triangle_count{};
  std::vector<uint32_t> sample_vertex_indices;
  std::vector<plane_fit::WeightedSample> fit_samples;
};

struct Plan {
  Status status{Status::kOk};
  std::vector<ComponentPlan> components;
  std::vector<size_t> component_of_triangle;
  size_t degenerate_triangles{};
  size_t non_manifold_edges{};
  static constexpr size_t npos = mesh_patch::Result::npos;
};

inline Status map_status(mesh_patch::Status status) {
  switch (status) {
  case mesh_patch::Status::kOk:
    return Status::kOk;
  case mesh_patch::Status::kInvalidArgument:
    return Status::kInvalidArgument;
  case mesh_patch::Status::kInvalidGeometry:
    return Status::kInvalidGeometry;
  case mesh_patch::Status::kResourceLimit:
    return Status::kResourceLimit;
  }
  return Status::kInvalidGeometry;
}

inline std::string canonical_component_suffix(
    const mesh_patch::Triangle &triangle) {
  return std::to_string(triangle[0]) + ":" + std::to_string(triangle[1]) +
         ":" + std::to_string(triangle[2]);
}

inline Plan prepare(std::span<const mesh_patch::Vec3> project_vertices,
                    std::span<const mesh_patch::Triangle> triangles,
                    uint64_t diversity_group,
                    const mesh_patch::Options &options = {}) {
  Plan out;
  auto grouped = mesh_patch::group(project_vertices, triangles, options);
  out.status = map_status(grouped.status);
  out.degenerate_triangles = grouped.degenerate_triangles;
  out.non_manifold_edges = grouped.non_manifold_edges;
  out.component_of_triangle = std::move(grouped.component_of_triangle);
  if (out.status != Status::kOk)
    return out;

  out.components.reserve(grouped.components.size());
  for (auto &component : grouped.components) {
    ComponentPlan plan;
    plan.canonical_triangle = component.canonical_triangle;
    plan.triangle_count = component.triangle_count;
    plan.sample_vertex_indices = std::move(component.sample_vertex_indices);
    plan.fit_samples.reserve(plan.sample_vertex_indices.size());
    for (const auto vertex_index : plan.sample_vertex_indices) {
      const auto &p = project_vertices[vertex_index];
      plan.fit_samples.push_back(
          {{p.x, p.y, p.z}, 1.0, diversity_group});
    }
    out.components.push_back(std::move(plan));
  }
  return out;
}

} // namespace hss::mesh_patch_runtime

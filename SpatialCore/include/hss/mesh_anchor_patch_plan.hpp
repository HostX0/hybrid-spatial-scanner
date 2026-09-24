#pragma once

#include "hss/mesh_patch_surface_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_patch {

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kInvalidGeometry,
  kResourceLimit,
  kInvalidPlan,
};

struct ComponentPlan {
  mesh_patch::Triangle canonical_triangle{};
  size_t triangle_count{};
  std::string surface_key;
  std::vector<uint32_t> sample_vertex_indices;
  std::vector<plane_fit::WeightedSample> fit_samples;
};

struct Plan {
  Status status{Status::kOk};
  std::vector<ComponentPlan> components;
  std::vector<size_t> component_of_triangle;
  size_t degenerate_triangles{};
  size_t non_manifold_edges{};
  static constexpr size_t npos = mesh_patch_runtime::Plan::npos;
};

inline Status map_runtime_status(mesh_patch_runtime::Status status) {
  switch (status) {
  case mesh_patch_runtime::Status::kOk:
    return Status::kOk;
  case mesh_patch_runtime::Status::kInvalidArgument:
    return Status::kInvalidArgument;
  case mesh_patch_runtime::Status::kInvalidGeometry:
    return Status::kInvalidGeometry;
  case mesh_patch_runtime::Status::kResourceLimit:
    return Status::kResourceLimit;
  }
  return Status::kInvalidPlan;
}

// Prepares one accepted anchor revision as a complete local unit. Canonical
// component identity is derived from anchor-local indexed geometry, while fit
// samples use the already transformed project-coordinate vertex buffer.
inline Plan prepare(std::span<const mesh_patch::Vec3> project_vertices,
                    std::span<const mesh_patch::Triangle> local_triangles,
                    std::string_view support_identity,
                    uint64_t diversity_group,
                    const mesh_patch::Options &options = {}) {
  Plan out;
  if (support_identity.empty()) {
    out.status = Status::kInvalidArgument;
    return out;
  }

  auto runtime = mesh_patch_runtime::prepare(project_vertices, local_triangles,
                                             diversity_group, options);
  out.status = map_runtime_status(runtime.status);
  out.degenerate_triangles = runtime.degenerate_triangles;
  out.non_manifold_edges = runtime.non_manifold_edges;
  if (out.status != Status::kOk)
    return out;

  const auto binding = mesh_patch_binding::bind(runtime);
  if (binding.status != mesh_patch_binding::Status::kOk ||
      binding.components.size() != runtime.components.size() ||
      binding.component_of_triangle.size() != local_triangles.size()) {
    out.status = binding.status == mesh_patch_binding::Status::kUpstreamFailure
                     ? map_runtime_status(binding.upstream_status)
                     : Status::kInvalidPlan;
    return out;
  }

  out.component_of_triangle = binding.component_of_triangle;
  out.components.reserve(runtime.components.size());
  for (size_t i = 0; i < runtime.components.size(); ++i) {
    auto &source = runtime.components[i];
    const auto &bound = binding.components[i];
    std::string surface_key;
    surface_key.reserve(support_identity.size() + bound.suffix.size() + 7U);
    surface_key.append(support_identity);
    surface_key.append(":patch:");
    surface_key.append(bound.suffix);
    out.components.push_back({source.canonical_triangle,
                              source.triangle_count,
                              std::move(surface_key),
                              std::move(source.sample_vertex_indices),
                              std::move(source.fit_samples)});
  }
  return out;
}

inline const ComponentPlan *component_for_triangle(const Plan &plan,
                                                    size_t triangle_index) {
  if (plan.status != Status::kOk ||
      triangle_index >= plan.component_of_triangle.size())
    return nullptr;
  const size_t component = plan.component_of_triangle[triangle_index];
  if (component == Plan::npos || component >= plan.components.size())
    return nullptr;
  return &plan.components[component];
}

// The live snapshot stores indices in one global vertex array, while component
// identity must remain anchor-local so adding earlier anchors cannot rename a
// later anchor's surfaces. Rebase only the raw captured triangle copy and fail
// atomically if uint32 storage would overflow.
inline std::optional<mesh_patch::Triangle>
rebase_raw_triangle(const mesh_patch::Triangle &local_triangle,
                    uint32_t vertex_base) {
  mesh_patch::Triangle rebased{};
  for (size_t i = 0; i < local_triangle.size(); ++i) {
    if (local_triangle[i] >
        std::numeric_limits<uint32_t>::max() - vertex_base)
      return std::nullopt;
    rebased[i] = local_triangle[i] + vertex_base;
  }
  return rebased;
}

} // namespace hss::mesh_anchor_patch

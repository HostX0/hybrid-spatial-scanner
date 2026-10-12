#pragma once

#include "hss/mesh_patch_runtime_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hss::mesh_patch_binding {

enum class Status : uint8_t {
  kOk,
  kUpstreamFailure,
  kInvalidPlan,
};

struct ComponentBinding {
  mesh_patch::Triangle canonical_triangle{};
  size_t triangle_count{};
  std::string suffix;
};

struct Result {
  Status status{Status::kOk};
  mesh_patch_runtime::Status upstream_status{mesh_patch_runtime::Status::kOk};
  std::vector<ComponentBinding> components;
  std::vector<size_t> component_of_triangle;
  size_t degenerate_triangles{};
  size_t non_manifold_edges{};
  static constexpr size_t npos = mesh_patch_runtime::Plan::npos;
};

inline Result bind(const mesh_patch_runtime::Plan &plan) {
  Result out;
  out.upstream_status = plan.status;
  out.degenerate_triangles = plan.degenerate_triangles;
  out.non_manifold_edges = plan.non_manifold_edges;
  if (plan.status != mesh_patch_runtime::Status::kOk) {
    out.status = Status::kUpstreamFailure;
    return out;
  }

  std::vector<size_t> mapped_counts(plan.components.size(), 0U);
  size_t mapped_degenerate = 0U;
  for (const auto component : plan.component_of_triangle) {
    if (component == mesh_patch_runtime::Plan::npos) {
      ++mapped_degenerate;
      continue;
    }
    if (component >= plan.components.size()) {
      out.status = Status::kInvalidPlan;
      return out;
    }
    ++mapped_counts[component];
  }
  if (mapped_degenerate != plan.degenerate_triangles) {
    out.status = Status::kInvalidPlan;
    return out;
  }

  out.components.reserve(plan.components.size());
  for (size_t i = 0; i < plan.components.size(); ++i) {
    const auto &component = plan.components[i];
    if (component.triangle_count == 0U ||
        mapped_counts[i] != component.triangle_count ||
        component.sample_vertex_indices.size() != component.fit_samples.size() ||
        (i != 0U && !(plan.components[i - 1U].canonical_triangle <
                      component.canonical_triangle))) {
      out.status = Status::kInvalidPlan;
      out.components.clear();
      return out;
    }
    out.components.push_back({component.canonical_triangle,
                              component.triangle_count,
                              mesh_patch_runtime::canonical_component_suffix(
                                  component.canonical_triangle)});
  }

  out.component_of_triangle = plan.component_of_triangle;
  return out;
}

inline const ComponentBinding *component_for_triangle(const Result &binding,
                                                       size_t triangle_index) {
  if (binding.status != Status::kOk ||
      triangle_index >= binding.component_of_triangle.size())
    return nullptr;
  const size_t component = binding.component_of_triangle[triangle_index];
  if (component == Result::npos || component >= binding.components.size())
    return nullptr;
  return &binding.components[component];
}

inline std::optional<std::string>
surface_key_for_triangle(const Result &binding, std::string_view support_identity,
                         size_t triangle_index) {
  if (support_identity.empty())
    return std::nullopt;
  const auto *component = component_for_triangle(binding, triangle_index);
  if (component == nullptr)
    return std::nullopt;
  std::string key;
  key.reserve(support_identity.size() + component->suffix.size() + 7U);
  key.append(support_identity);
  key.append(":patch:");
  key.append(component->suffix);
  return key;
}

} // namespace hss::mesh_patch_binding

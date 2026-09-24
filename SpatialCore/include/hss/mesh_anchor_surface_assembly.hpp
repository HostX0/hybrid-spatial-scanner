#pragma once

#include "hss/mesh_anchor_cut_fragments.hpp"
#include "hss/mesh_anchor_reconstruction_plan.hpp"
#include "hss/robust_plane_fit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_surface {

constexpr size_t kDefaultMaxSurfaces = 2'048U;
constexpr size_t kLiveMaxFitSamples = 96U;

enum class Status : uint8_t {
  kOk,
  kCaptureUnavailable,
  kMetricUnavailable,
  kCutUnavailable,
  kInvalidArgument,
  kInvalidPlan,
  kResourceLimit,
};

struct Options {
  uint64_t deterministic_seed{101U};
  size_t max_surfaces{kDefaultMaxSurfaces};
};

struct Surface {
  std::string surface_key;
  std::string support_identity;
  mesh_patch::Triangle canonical_triangle{};
  size_t triangle_count{};
  plane_fit::Status fit_status{plane_fit::Status::kInvalidOptions};
  std::optional<plane_fit::Plane> plane;
  std::vector<size_t> cut_fragment_indices;
};

struct Result {
  Status status{Status::kOk};
  std::vector<Surface> surfaces;
  size_t fitted_surface_count{};
  size_t fallback_surface_count{};
};

inline plane_fit::Options live_plane_options(uint64_t deterministic_seed) {
  plane_fit::Options options;
  options.inlier_threshold_m = 0.002;
  options.seed = deterministic_seed;
  options.max_input_samples = kLiveMaxFitSamples;
  options.candidate_iterations = 24U;
  options.max_refinement_iterations = 4U;
  options.max_residual_evaluations = 4'096U;
  options.min_support_samples = 3U;
  options.min_distinct_support_points = 3U;
  return options;
}

namespace detail {

inline bool triangle_less(const mesh_patch::Triangle &a,
                          const mesh_patch::Triangle &b) {
  return std::tie(a[0], a[1], a[2]) < std::tie(b[0], b[1], b[2]);
}

inline void clear_derived(Result &out) {
  out.surfaces.clear();
  out.fitted_surface_count = 0U;
  out.fallback_surface_count = 0U;
}

inline bool valid_surface_key(std::string_view surface_key,
                              std::string_view support_identity) {
  if (support_identity.empty())
    return false;
  std::string prefix;
  try {
    prefix.reserve(support_identity.size() + 7U);
    prefix.append(support_identity);
    prefix.append(":patch:");
  } catch (const std::bad_alloc &) {
    return false;
  }
  return surface_key.size() > prefix.size() && surface_key.starts_with(prefix);
}

} // namespace detail

// Converts one fully prepared anchor plus its deterministic architectural cut
// fragments into stable scene-surface assembly records. This is deliberately
// downstream of capture normalization/grouping and upstream of scene JSON/ID
// generation: callers may hash `surface_key` through the existing runtime
// identity function without allowing triangle arrival order or global vertex
// placement to rename a surface.
//
// Plane fitting uses the current live runtime's bounded options exactly. A fit
// failure preserves the stable component as a mesh-patch fallback; it is not an
// excuse to drop raw evidence or close topology. Cut provenance is cross-checked
// against the finalized triangle->component assignment before it is exposed.
inline Result assemble(const mesh_anchor_reconstruction::Prepared &prepared,
                       const mesh_anchor_cut::Result &cuts,
                       std::string_view support_identity,
                       const Options &options = {}) {
  Result out;
  if (prepared.capture_status !=
      mesh_anchor_reconstruction::CaptureStatus::kOk) {
    out.status = Status::kCaptureUnavailable;
    return out;
  }
  if (prepared.metric_plan.status != mesh_anchor_patch::Status::kOk) {
    out.status = Status::kMetricUnavailable;
    return out;
  }
  if (cuts.status != mesh_anchor_cut::Status::kOk) {
    out.status = Status::kCutUnavailable;
    return out;
  }
  if (support_identity.empty() || options.max_surfaces == 0U ||
      options.max_surfaces > kDefaultMaxSurfaces ||
      cuts.support_identity != support_identity) {
    out.status = Status::kInvalidArgument;
    return out;
  }

  const auto &plan = prepared.metric_plan;
  if (plan.components.size() > options.max_surfaces ||
      plan.component_of_triangle.size() != prepared.local_triangles.size()) {
    out.status = plan.components.size() > options.max_surfaces
                     ? Status::kResourceLimit
                     : Status::kInvalidPlan;
    return out;
  }

  try {
    std::map<std::string, size_t, std::less<>> component_by_key;
    std::vector<size_t> mapped_triangle_counts(plan.components.size(), 0U);
    std::vector<std::vector<size_t>> fragments_by_component(
        plan.components.size());

    for (size_t component_index = 0U;
         component_index < plan.components.size(); ++component_index) {
      const auto &component = plan.components[component_index];
      if (!detail::valid_surface_key(component.surface_key, support_identity) ||
          component.triangle_count == 0U ||
          component.fit_samples.size() > kLiveMaxFitSamples ||
          component.sample_vertex_indices.size() != component.fit_samples.size() ||
          (component_index > 0U &&
           !detail::triangle_less(plan.components[component_index - 1U]
                                      .canonical_triangle,
                                  component.canonical_triangle)) ||
          !component_by_key.emplace(component.surface_key, component_index)
               .second) {
        out.status = Status::kInvalidPlan;
        return out;
      }
    }

    for (size_t triangle_index = 0U;
         triangle_index < plan.component_of_triangle.size(); ++triangle_index) {
      const size_t component_index = plan.component_of_triangle[triangle_index];
      if (component_index == mesh_anchor_patch::Plan::npos)
        continue;
      if (component_index >= plan.components.size()) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      ++mapped_triangle_counts[component_index];
    }
    for (size_t component_index = 0U;
         component_index < plan.components.size(); ++component_index) {
      if (mapped_triangle_counts[component_index] !=
          plan.components[component_index].triangle_count) {
        out.status = Status::kInvalidPlan;
        return out;
      }
    }

    std::set<size_t> fragment_source_triangles;
    for (size_t fragment_index = 0U; fragment_index < cuts.fragments.size();
         ++fragment_index) {
      const auto &fragment = cuts.fragments[fragment_index];
      if (fragment.source_triangle_index >= prepared.local_triangles.size() ||
          prepared.local_triangles[fragment.source_triangle_index] !=
              fragment.local_triangle ||
          !fragment_source_triangles.insert(fragment.source_triangle_index)
               .second) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      const size_t component_index =
          plan.component_of_triangle[fragment.source_triangle_index];
      if (component_index == mesh_anchor_patch::Plan::npos ||
          component_index >= plan.components.size() ||
          plan.components[component_index].surface_key != fragment.surface_key) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      const auto key_it = component_by_key.find(fragment.surface_key);
      if (key_it == component_by_key.end() || key_it->second != component_index) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      fragments_by_component[component_index].push_back(fragment_index);
    }

    const plane_fit::Options fit_options =
        live_plane_options(options.deterministic_seed);
    out.surfaces.reserve(plan.components.size());
    for (size_t component_index = 0U;
         component_index < plan.components.size(); ++component_index) {
      const auto &component = plan.components[component_index];
      auto fit = plane_fit::fit(component.fit_samples, fit_options);
      Surface surface;
      surface.surface_key = component.surface_key;
      surface.support_identity.assign(support_identity);
      surface.canonical_triangle = component.canonical_triangle;
      surface.triangle_count = component.triangle_count;
      surface.fit_status = fit.status;
      surface.plane = std::move(fit.plane);
      surface.cut_fragment_indices = std::move(fragments_by_component[component_index]);
      if (surface.plane)
        ++out.fitted_surface_count;
      else
        ++out.fallback_surface_count;
      out.surfaces.push_back(std::move(surface));
    }
  } catch (const std::bad_alloc &) {
    detail::clear_derived(out);
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_anchor_surface

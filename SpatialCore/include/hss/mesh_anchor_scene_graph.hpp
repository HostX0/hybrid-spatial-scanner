#pragma once

#include "hss/mesh_anchor_cut_fragments.hpp"
#include "hss/mesh_anchor_surface_assembly.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_scene_graph {

constexpr double kLiveJoinToleranceM = 1.0e-7;
constexpr double kMaxAbsProjectCoordinateM = 10'000.0;
constexpr size_t kDefaultMaxNodes = 4'096U;
constexpr size_t kDefaultMaxEdges = 2'048U;

enum class Status : uint8_t {
  kOk,
  kCutUnavailable,
  kSurfaceUnavailable,
  kInvalidArgument,
  kInvalidPlan,
  kInvalidGeometry,
  kResourceLimit,
};

struct Options {
  size_t max_nodes{kDefaultMaxNodes};
  size_t max_edges{kDefaultMaxEdges};
};

struct JoinCell {
  int64_t x{};
  int64_t y{};
  int64_t z{};
  bool operator==(const JoinCell &) const = default;
};

struct Node {
  JoinCell cell{};
  HSSVec3d point_m{};
  coordinate::PlanPoint uv{};
  std::string node_key;
  std::vector<std::string> surface_keys;
  std::vector<size_t> edge_indices;
};

struct Edge {
  size_t a{};
  size_t b{};
  std::string edge_key;
  std::vector<std::string> surface_keys;
  std::vector<size_t> source_fragment_indices;
  std::vector<size_t> source_triangle_indices;
};

struct Result {
  Status status{Status::kOk};
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  size_t merged_endpoint_count{};
  size_t merged_edge_fragment_count{};
  size_t multi_surface_edge_count{};
};

namespace detail {

inline bool finite(HSSVec3d p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

inline bool bounded(HSSVec3d p) {
  return finite(p) && std::abs(p.x) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.y) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.z) <= kMaxAbsProjectCoordinateM;
}

inline bool finite(coordinate::PlanPoint p) {
  return std::isfinite(p.u_m) && std::isfinite(p.v_m);
}

inline bool valid_surface_key(std::string_view surface_key,
                              std::string_view support_identity) {
  constexpr std::string_view marker = ":patch:";
  if (support_identity.empty() ||
      surface_key.size() <= support_identity.size() + marker.size())
    return false;
  return surface_key.substr(0U, support_identity.size()) == support_identity &&
         surface_key.substr(support_identity.size(), marker.size()) == marker;
}

inline JoinCell quantize(HSSVec3d p) {
  return {std::llround(p.x / kLiveJoinToleranceM),
          std::llround(p.y / kLiveJoinToleranceM),
          std::llround(p.z / kLiveJoinToleranceM)};
}

inline bool cell_less(const JoinCell &a, const JoinCell &b) {
  return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
}

inline std::string node_key(const JoinCell &cell) {
  return "node-cell:" + std::to_string(cell.x) + ":" +
         std::to_string(cell.y) + ":" + std::to_string(cell.z);
}

inline std::string edge_key(std::string_view a, std::string_view b) {
  return "cut-edge:" + std::string(a) + "|" + std::string(b);
}

template <class T>
inline void sort_unique(std::vector<T> &values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

inline void clear_derived(Result &out) {
  out.nodes.clear();
  out.edges.clear();
  out.merged_endpoint_count = 0U;
  out.merged_edge_fragment_count = 0U;
  out.multi_surface_edge_count = 0U;
}

struct Endpoint {
  JoinCell cell{};
  HSSVec3d point_m{};
  coordinate::PlanPoint uv{};
  size_t fragment_index{};
  uint8_t endpoint{};
  std::string_view surface_key;
};

inline bool endpoint_less(const Endpoint &a, const Endpoint &b) {
  if (cell_less(a.cell, b.cell))
    return true;
  if (cell_less(b.cell, a.cell))
    return false;
  return std::tie(a.point_m.x, a.point_m.y, a.point_m.z, a.uv.u_m, a.uv.v_m,
                  a.surface_key, a.fragment_index, a.endpoint) <
         std::tie(b.point_m.x, b.point_m.y, b.point_m.z, b.uv.u_m, b.uv.v_m,
                  b.surface_key, b.fragment_index, b.endpoint);
}

struct EdgeCandidate {
  size_t a{};
  size_t b{};
  size_t fragment_index{};
  size_t source_triangle_index{};
  std::string_view surface_key;
};

inline bool edge_candidate_less(const EdgeCandidate &x,
                                const EdgeCandidate &y) {
  return std::tie(x.a, x.b, x.surface_key, x.source_triangle_index,
                  x.fragment_index) <
         std::tie(y.a, y.b, y.surface_key, y.source_triangle_index,
                  y.fragment_index);
}

} // namespace detail

// Deterministically welds finalized architectural-cut endpoints into the node
// and edge graph that the live runtime currently builds incrementally. The
// existing 1e-7 m join cell is preserved exactly, but the representative of a
// populated cell is chosen from the lexicographically smallest *existing* cut
// endpoint instead of whichever triangle happened to arrive first. No averaging
// or synthetic point is introduced.
//
// Coincident fragments become one undirected edge. All contributing stable
// surface keys and source indices are retained, so a second surface cannot be
// silently discarded by first-wins edge insertion. This layer deliberately
// emits stable string keys rather than final UUIDs; production integration may
// pass each key through the existing internal `id(...)` function once.
inline Result assemble(const mesh_anchor_cut::Result &cuts,
                       const mesh_anchor_surface::Result &surfaces,
                       std::string_view support_identity,
                       const Options &options = {}) {
  Result out;
  if (cuts.status != mesh_anchor_cut::Status::kOk) {
    out.status = Status::kCutUnavailable;
    return out;
  }
  if (surfaces.status != mesh_anchor_surface::Status::kOk) {
    out.status = Status::kSurfaceUnavailable;
    return out;
  }
  if (support_identity.empty() || cuts.support_identity != support_identity ||
      options.max_nodes == 0U || options.max_edges == 0U ||
      options.max_nodes > kDefaultMaxNodes ||
      options.max_edges > kDefaultMaxEdges) {
    out.status = Status::kInvalidArgument;
    return out;
  }
  if (cuts.fragments.size() > kDefaultMaxEdges ||
      surfaces.surfaces.size() > kDefaultMaxEdges) {
    out.status = Status::kResourceLimit;
    return out;
  }

  try {
    std::map<std::string, size_t, std::less<>> surface_by_key;
    std::vector<size_t> fragment_owner(cuts.fragments.size(),
                                       std::numeric_limits<size_t>::max());
    for (size_t surface_index = 0U; surface_index < surfaces.surfaces.size();
         ++surface_index) {
      const auto &surface = surfaces.surfaces[surface_index];
      if (surface.support_identity != support_identity ||
          !detail::valid_surface_key(surface.surface_key, support_identity) ||
          !surface_by_key.emplace(surface.surface_key, surface_index).second) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      for (const size_t fragment_index : surface.cut_fragment_indices) {
        if (fragment_index >= cuts.fragments.size() ||
            fragment_owner[fragment_index] !=
                std::numeric_limits<size_t>::max() ||
            cuts.fragments[fragment_index].surface_key != surface.surface_key) {
          out.status = Status::kInvalidPlan;
          return out;
        }
        fragment_owner[fragment_index] = surface_index;
      }
    }
    if (std::any_of(fragment_owner.begin(), fragment_owner.end(), [](size_t owner) {
          return owner == std::numeric_limits<size_t>::max();
        })) {
      out.status = Status::kInvalidPlan;
      return out;
    }

    std::vector<detail::Endpoint> endpoints;
    endpoints.reserve(cuts.fragments.size() * 2U);
    for (size_t fragment_index = 0U; fragment_index < cuts.fragments.size();
         ++fragment_index) {
      const auto &fragment = cuts.fragments[fragment_index];
      if (!detail::valid_surface_key(fragment.surface_key, support_identity) ||
          !detail::bounded(fragment.start_m) ||
          !detail::bounded(fragment.end_m) || !detail::finite(fragment.start_uv) ||
          !detail::finite(fragment.end_uv)) {
        out.status = Status::kInvalidGeometry;
        return out;
      }
      endpoints.push_back({detail::quantize(fragment.start_m), fragment.start_m,
                           fragment.start_uv, fragment_index, 0U,
                           fragment.surface_key});
      endpoints.push_back({detail::quantize(fragment.end_m), fragment.end_m,
                           fragment.end_uv, fragment_index, 1U,
                           fragment.surface_key});
    }
    std::sort(endpoints.begin(), endpoints.end(), detail::endpoint_less);

    std::vector<size_t> start_node(cuts.fragments.size(),
                                   std::numeric_limits<size_t>::max());
    std::vector<size_t> end_node(cuts.fragments.size(),
                                 std::numeric_limits<size_t>::max());
    for (const auto &endpoint : endpoints) {
      size_t node_index = out.nodes.size();
      if (!out.nodes.empty() && out.nodes.back().cell == endpoint.cell) {
        node_index = out.nodes.size() - 1U;
        ++out.merged_endpoint_count;
      } else {
        if (out.nodes.size() >= options.max_nodes) {
          detail::clear_derived(out);
          out.status = Status::kResourceLimit;
          return out;
        }
        Node node;
        node.cell = endpoint.cell;
        node.point_m = endpoint.point_m;
        node.uv = endpoint.uv;
        node.node_key = detail::node_key(endpoint.cell);
        out.nodes.push_back(std::move(node));
      }
      out.nodes[node_index].surface_keys.emplace_back(endpoint.surface_key);
      if (endpoint.endpoint == 0U)
        start_node[endpoint.fragment_index] = node_index;
      else
        end_node[endpoint.fragment_index] = node_index;
    }
    for (auto &node : out.nodes)
      detail::sort_unique(node.surface_keys);

    std::vector<detail::EdgeCandidate> candidates;
    candidates.reserve(cuts.fragments.size());
    for (size_t fragment_index = 0U; fragment_index < cuts.fragments.size();
         ++fragment_index) {
      size_t a = start_node[fragment_index], b = end_node[fragment_index];
      if (a == std::numeric_limits<size_t>::max() ||
          b == std::numeric_limits<size_t>::max() || a == b) {
        detail::clear_derived(out);
        out.status = Status::kInvalidGeometry;
        return out;
      }
      if (b < a)
        std::swap(a, b);
      candidates.push_back({a, b, fragment_index,
                            cuts.fragments[fragment_index].source_triangle_index,
                            cuts.fragments[fragment_index].surface_key});
    }
    std::sort(candidates.begin(), candidates.end(), detail::edge_candidate_less);

    for (const auto &candidate : candidates) {
      if (out.edges.empty() || out.edges.back().a != candidate.a ||
          out.edges.back().b != candidate.b) {
        if (out.edges.size() >= options.max_edges) {
          detail::clear_derived(out);
          out.status = Status::kResourceLimit;
          return out;
        }
        Edge edge;
        edge.a = candidate.a;
        edge.b = candidate.b;
        edge.edge_key = detail::edge_key(out.nodes[candidate.a].node_key,
                                         out.nodes[candidate.b].node_key);
        out.edges.push_back(std::move(edge));
      } else {
        ++out.merged_edge_fragment_count;
      }
      auto &edge = out.edges.back();
      edge.surface_keys.emplace_back(candidate.surface_key);
      edge.source_fragment_indices.push_back(candidate.fragment_index);
      edge.source_triangle_indices.push_back(candidate.source_triangle_index);
    }

    for (size_t edge_index = 0U; edge_index < out.edges.size(); ++edge_index) {
      auto &edge = out.edges[edge_index];
      detail::sort_unique(edge.surface_keys);
      detail::sort_unique(edge.source_fragment_indices);
      detail::sort_unique(edge.source_triangle_indices);
      if (edge.surface_keys.size() > 1U)
        ++out.multi_surface_edge_count;
      out.nodes[edge.a].edge_indices.push_back(edge_index);
      out.nodes[edge.b].edge_indices.push_back(edge_index);
    }
    for (auto &node : out.nodes)
      detail::sort_unique(node.edge_indices);
  } catch (const std::bad_alloc &) {
    detail::clear_derived(out);
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_anchor_scene_graph

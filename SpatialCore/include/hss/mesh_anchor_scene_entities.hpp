#pragma once

#include "hss/mesh_anchor_scene_graph.hpp"
#include "hss/mesh_anchor_surface_assembly.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_scene_entities {

constexpr size_t kDefaultMaxNodes = 4'096U;
constexpr size_t kDefaultMaxEdges = 2'048U;
constexpr size_t kDefaultMaxSurfaces = 2'048U;
constexpr size_t kDefaultMaxJsonBytes = 16U * 1024U * 1024U;
constexpr double kMaxAbsProjectCoordinateM = 10'000.0;

enum class Status : uint8_t {
  kOk,
  kGraphUnavailable,
  kSurfaceUnavailable,
  kInvalidArgument,
  kInvalidPlan,
  kInvalidGeometry,
  kIdentityFailure,
  kResourceLimit,
};

struct Options {
  size_t max_nodes{kDefaultMaxNodes};
  size_t max_edges{kDefaultMaxEdges};
  size_t max_surfaces{kDefaultMaxSurfaces};
  size_t max_json_bytes{kDefaultMaxJsonBytes};
};

struct NodeRecord {
  std::string node_key;
  std::string id;
  HSSVec3d point_m{};
  std::vector<std::string> surface_ids;
  std::vector<size_t> source_edge_indices;
};

struct EdgeRecord {
  std::string edge_key;
  std::string id;
  std::string start_node_id;
  std::string end_node_id;
  std::vector<std::string> surface_ids;
  std::vector<size_t> source_fragment_indices;
  std::vector<size_t> source_triangle_indices;
};

struct SurfaceRecord {
  std::string surface_key;
  std::string id;
  std::string support_id;
  plane_fit::Status fit_status{plane_fit::Status::kInvalidOptions};
  std::optional<plane_fit::Plane> plane;
  std::vector<size_t> cut_fragment_indices;
};

struct Result {
  Status status{Status::kOk};
  std::vector<NodeRecord> nodes;
  std::vector<EdgeRecord> edges;
  std::vector<SurfaceRecord> surfaces;
  std::string nodes_json;
  std::string edges_json;
  std::string surfaces_json;
  size_t emitted_json_bytes{};
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

inline bool is_lower_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

inline bool canonical_uuid(std::string_view value) {
  if (value.size() != 36U)
    return false;
  for (size_t i = 0U; i < value.size(); ++i) {
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      if (value[i] != '-')
        return false;
    } else if (!is_lower_hex(value[i])) {
      return false;
    }
  }
  return true;
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

template <class T>
inline bool sorted_unique(const std::vector<T> &values) {
  return std::adjacent_find(values.begin(), values.end(),
                            [](const T &a, const T &b) { return !(a < b); }) ==
         values.end();
}

inline std::string num(double d) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17) << d;
  return stream.str();
}

inline std::string vec(HSSVec3d p) {
  return "[" + num(p.x) + "," + num(p.y) + "," + num(p.z) + "]";
}

inline std::string provenance(std::string_view support_id,
                              std::string_view geometry_origin) {
  return "{\"geometry_origin\":\"" + std::string(geometry_origin) +
         "\",\"semantic_origin\":\"unknown\",\"support_ids\":[\"" +
         std::string(support_id) +
         "\"],\"uncertainty_m\":null,\"quality_state\":\"provisional\"}";
}

inline void clear_derived(Result &out) {
  out.nodes.clear();
  out.edges.clear();
  out.surfaces.clear();
  out.nodes_json.clear();
  out.edges_json.clear();
  out.surfaces_json.clear();
  out.emitted_json_bytes = 0U;
}

inline bool valid_plane(const plane_fit::Plane &plane) {
  const auto &n = plane.normal;
  if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
      !std::isfinite(plane.offset_m) ||
      !std::isfinite(plane.residuals.weighted_rms_m) ||
      plane.residuals.weighted_rms_m < 0.0)
    return false;
  const double length2 = n.x * n.x + n.y * n.y + n.z * n.z;
  return std::isfinite(length2) && std::abs(length2 - 1.0) <= 1.0e-6;
}

inline bool contains(const std::vector<std::string> &values,
                     std::string_view value) {
  return std::binary_search(values.begin(), values.end(), value,
                            [](const auto &lhs, const auto &rhs) {
                              return std::string_view(lhs) < std::string_view(rhs);
                            });
}

inline bool contains(const std::vector<size_t> &values, size_t value) {
  return std::binary_search(values.begin(), values.end(), value);
}

} // namespace detail

// Builds schema-shaped node/edge/surface arrays from the deterministic surface
// and cut-graph layers. Stable keys are mapped to final scene UUIDs exactly once
// through the caller-supplied identity function; identity collisions or invalid
// UUIDs fail closed instead of silently aliasing scene entities.
//
// This bridge intentionally emits only the three arrays owned by the mesh
// topology path. Epochs, regions, issues, features, objects and constraints
// remain owned by the surrounding live reconstruction snapshot builder.
template <class IdentityFn>
inline Result assemble(const mesh_anchor_scene_graph::Result &graph,
                       const mesh_anchor_surface::Result &surfaces,
                       std::string_view support_identity,
                       IdentityFn &&identity_fn,
                       const Options &options = {}) {
  Result out;
  if (graph.status != mesh_anchor_scene_graph::Status::kOk) {
    out.status = Status::kGraphUnavailable;
    return out;
  }
  if (surfaces.status != mesh_anchor_surface::Status::kOk) {
    out.status = Status::kSurfaceUnavailable;
    return out;
  }
  if (!detail::canonical_uuid(support_identity) || options.max_nodes == 0U ||
      options.max_edges == 0U || options.max_surfaces == 0U ||
      options.max_json_bytes == 0U || options.max_nodes > kDefaultMaxNodes ||
      options.max_edges > kDefaultMaxEdges ||
      options.max_surfaces > kDefaultMaxSurfaces ||
      options.max_json_bytes > kDefaultMaxJsonBytes) {
    out.status = Status::kInvalidArgument;
    return out;
  }
  if (graph.nodes.size() > options.max_nodes ||
      graph.edges.size() > options.max_edges ||
      surfaces.surfaces.size() > options.max_surfaces) {
    out.status = Status::kResourceLimit;
    return out;
  }

  try {
    std::vector<size_t> surface_order(surfaces.surfaces.size());
    for (size_t i = 0U; i < surface_order.size(); ++i)
      surface_order[i] = i;
    std::sort(surface_order.begin(), surface_order.end(), [&](size_t a, size_t b) {
      return surfaces.surfaces[a].surface_key < surfaces.surfaces[b].surface_key;
    });

    std::map<std::string, size_t, std::less<>> surface_index_by_key;
    for (size_t order_index = 0U; order_index < surface_order.size(); ++order_index) {
      const auto &surface = surfaces.surfaces[surface_order[order_index]];
      if (surface.support_identity != support_identity ||
          !detail::valid_surface_key(surface.surface_key, support_identity) ||
          surface.triangle_count == 0U ||
          !detail::sorted_unique(surface.cut_fragment_indices) ||
          (surface.plane && surface.fit_status != plane_fit::Status::kOk) ||
          (!surface.plane && surface.fit_status == plane_fit::Status::kOk) ||
          (surface.plane && !detail::valid_plane(*surface.plane)) ||
          !surface_index_by_key.emplace(surface.surface_key, order_index).second) {
        out.status = surface.plane && !detail::valid_plane(*surface.plane)
                         ? Status::kInvalidGeometry
                         : Status::kInvalidPlan;
        return out;
      }
    }

    std::vector<size_t> node_order(graph.nodes.size());
    for (size_t i = 0U; i < node_order.size(); ++i)
      node_order[i] = i;
    std::sort(node_order.begin(), node_order.end(), [&](size_t a, size_t b) {
      return graph.nodes[a].node_key < graph.nodes[b].node_key;
    });
    for (size_t i = 1U; i < node_order.size(); ++i) {
      if (graph.nodes[node_order[i - 1U]].node_key ==
          graph.nodes[node_order[i]].node_key) {
        out.status = Status::kInvalidPlan;
        return out;
      }
    }

    std::vector<size_t> edge_order(graph.edges.size());
    for (size_t i = 0U; i < edge_order.size(); ++i)
      edge_order[i] = i;
    std::sort(edge_order.begin(), edge_order.end(), [&](size_t a, size_t b) {
      return graph.edges[a].edge_key < graph.edges[b].edge_key;
    });
    for (size_t i = 1U; i < edge_order.size(); ++i) {
      if (graph.edges[edge_order[i - 1U]].edge_key ==
          graph.edges[edge_order[i]].edge_key) {
        out.status = Status::kInvalidPlan;
        return out;
      }
    }

    std::vector<size_t> canonical_edge_index(graph.edges.size(),
                                             std::numeric_limits<size_t>::max());
    for (size_t canonical = 0U; canonical < edge_order.size(); ++canonical)
      canonical_edge_index[edge_order[canonical]] = canonical;

    for (size_t source_index = 0U; source_index < graph.nodes.size(); ++source_index) {
      const auto &node = graph.nodes[source_index];
      if (node.node_key.empty() || !detail::bounded(node.point_m) ||
          !detail::sorted_unique(node.surface_keys) ||
          !detail::sorted_unique(node.edge_indices)) {
        out.status = !detail::bounded(node.point_m) ? Status::kInvalidGeometry
                                                    : Status::kInvalidPlan;
        return out;
      }
      for (const auto &surface_key : node.surface_keys) {
        if (!surface_index_by_key.contains(surface_key)) {
          out.status = Status::kInvalidPlan;
          return out;
        }
      }
      for (const size_t edge_index : node.edge_indices) {
        if (edge_index >= graph.edges.size()) {
          out.status = Status::kInvalidPlan;
          return out;
        }
      }
    }

    for (size_t source_index = 0U; source_index < graph.edges.size(); ++source_index) {
      const auto &edge = graph.edges[source_index];
      if (edge.edge_key.empty() || edge.a >= graph.nodes.size() ||
          edge.b >= graph.nodes.size() || edge.a == edge.b ||
          edge.surface_keys.empty() || !detail::sorted_unique(edge.surface_keys) ||
          edge.source_fragment_indices.empty() ||
          !detail::sorted_unique(edge.source_fragment_indices) ||
          edge.source_triangle_indices.empty() ||
          !detail::sorted_unique(edge.source_triangle_indices)) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      for (const auto &surface_key : edge.surface_keys) {
        if (!surface_index_by_key.contains(surface_key)) {
          out.status = Status::kInvalidPlan;
          return out;
        }
      }
      if (!detail::contains(graph.nodes[edge.a].edge_indices, source_index) ||
          !detail::contains(graph.nodes[edge.b].edge_indices, source_index)) {
        out.status = Status::kInvalidPlan;
        return out;
      }
      for (const size_t fragment_index : edge.source_fragment_indices) {
        bool attributable = false;
        for (const auto &surface_key : edge.surface_keys) {
          const auto &surface = surfaces.surfaces[
              surface_order[surface_index_by_key.at(surface_key)]];
          if (detail::contains(surface.cut_fragment_indices, fragment_index)) {
            attributable = true;
            break;
          }
        }
        if (!attributable) {
          out.status = Status::kInvalidPlan;
          return out;
        }
      }
    }

    for (size_t source_index = 0U; source_index < graph.nodes.size(); ++source_index) {
      std::vector<std::string> incident_surfaces;
      for (const size_t edge_index : graph.nodes[source_index].edge_indices) {
        const auto &edge = graph.edges[edge_index];
        incident_surfaces.insert(incident_surfaces.end(), edge.surface_keys.begin(),
                                 edge.surface_keys.end());
      }
      std::sort(incident_surfaces.begin(), incident_surfaces.end());
      incident_surfaces.erase(
          std::unique(incident_surfaces.begin(), incident_surfaces.end()),
          incident_surfaces.end());
      if (incident_surfaces != graph.nodes[source_index].surface_keys) {
        out.status = Status::kInvalidPlan;
        return out;
      }
    }

    for (const size_t surface_source_index : surface_order) {
      const auto &surface = surfaces.surfaces[surface_source_index];
      for (const size_t fragment_index : surface.cut_fragment_indices) {
        bool found = false;
        for (const auto &edge : graph.edges) {
          if (detail::contains(edge.surface_keys, surface.surface_key) &&
              detail::contains(edge.source_fragment_indices, fragment_index)) {
            found = true;
            break;
          }
        }
        if (!found) {
          out.status = Status::kInvalidPlan;
          return out;
        }
      }
    }

    std::map<std::string, std::string, std::less<>> ids;
    std::set<std::string, std::less<>> used_ids;
    auto map_identity = [&](const std::string &stable_key) -> bool {
      if (ids.contains(stable_key))
        return false;
      std::string mapped;
      try {
        mapped = identity_fn(stable_key);
      } catch (const std::bad_alloc &) {
        throw;
      } catch (...) {
        return false;
      }
      if (!detail::canonical_uuid(mapped) || !used_ids.insert(mapped).second)
        return false;
      ids.emplace(stable_key, std::move(mapped));
      return true;
    };

    for (const size_t index : surface_order)
      if (!map_identity(surfaces.surfaces[index].surface_key)) {
        out.status = Status::kIdentityFailure;
        return out;
      }
    for (const size_t index : node_order)
      if (!map_identity(graph.nodes[index].node_key)) {
        out.status = Status::kIdentityFailure;
        return out;
      }
    for (const size_t index : edge_order)
      if (!map_identity(graph.edges[index].edge_key)) {
        out.status = Status::kIdentityFailure;
        return out;
      }

    out.surfaces.reserve(surface_order.size());
    for (const size_t index : surface_order) {
      const auto &source = surfaces.surfaces[index];
      SurfaceRecord record;
      record.surface_key = source.surface_key;
      record.id = ids.at(source.surface_key);
      record.support_id.assign(support_identity);
      record.fit_status = source.fit_status;
      record.plane = source.plane;
      record.cut_fragment_indices = source.cut_fragment_indices;
      out.surfaces.push_back(std::move(record));
    }

    out.nodes.reserve(node_order.size());
    for (const size_t source_index : node_order) {
      const auto &source = graph.nodes[source_index];
      NodeRecord record;
      record.node_key = source.node_key;
      record.id = ids.at(source.node_key);
      record.point_m = source.point_m;
      record.source_edge_indices.reserve(source.edge_indices.size());
      for (const size_t edge_index : source.edge_indices)
        record.source_edge_indices.push_back(canonical_edge_index[edge_index]);
      std::sort(record.source_edge_indices.begin(), record.source_edge_indices.end());
      record.surface_ids.reserve(source.surface_keys.size());
      for (const auto &surface_key : source.surface_keys)
        record.surface_ids.push_back(ids.at(surface_key));
      std::sort(record.surface_ids.begin(), record.surface_ids.end());
      out.nodes.push_back(std::move(record));
    }

    out.edges.reserve(edge_order.size());
    for (const size_t source_index : edge_order) {
      const auto &source = graph.edges[source_index];
      EdgeRecord record;
      record.edge_key = source.edge_key;
      record.id = ids.at(source.edge_key);
      record.start_node_id = ids.at(graph.nodes[source.a].node_key);
      record.end_node_id = ids.at(graph.nodes[source.b].node_key);
      if (record.end_node_id < record.start_node_id)
        std::swap(record.start_node_id, record.end_node_id);
      record.surface_ids.reserve(source.surface_keys.size());
      for (const auto &surface_key : source.surface_keys)
        record.surface_ids.push_back(ids.at(surface_key));
      std::sort(record.surface_ids.begin(), record.surface_ids.end());
      record.source_fragment_indices = source.source_fragment_indices;
      record.source_triangle_indices = source.source_triangle_indices;
      out.edges.push_back(std::move(record));
    }

    out.nodes_json = "[";
    for (size_t i = 0U; i < out.nodes.size(); ++i) {
      if (i)
        out.nodes_json += ',';
      const auto &node = out.nodes[i];
      out.nodes_json += "{\"id\":\"" + node.id +
                        "\",\"position_m\":" + detail::vec(node.point_m) +
                        ",\"position_locked\":false,\"provenance\":" +
                        detail::provenance(support_identity, "fitted") + "}";
    }
    out.nodes_json += ']';

    out.edges_json = "[";
    for (size_t i = 0U; i < out.edges.size(); ++i) {
      if (i)
        out.edges_json += ',';
      const auto &edge = out.edges[i];
      out.edges_json += "{\"id\":\"" + edge.id +
                        "\",\"start_node_id\":\"" + edge.start_node_id +
                        "\",\"end_node_id\":\"" + edge.end_node_id +
                        "\",\"surface_ids\":[";
      for (size_t j = 0U; j < edge.surface_ids.size(); ++j) {
        if (j)
          out.edges_json += ',';
        out.edges_json += "\"" + edge.surface_ids[j] + "\"";
      }
      out.edges_json +=
          "],\"parent_edge_ids\":[],\"protected_topology\":true,\"provenance\":" +
          detail::provenance(support_identity, "fitted") + "}";
    }
    out.edges_json += ']';

    out.surfaces_json = "[";
    for (size_t i = 0U; i < out.surfaces.size(); ++i) {
      if (i)
        out.surfaces_json += ',';
      const auto &surface = out.surfaces[i];
      out.surfaces_json += "{\"id\":\"" + surface.id + "\"";
      if (surface.plane) {
        const auto &plane = *surface.plane;
        out.surfaces_json +=
            ",\"kind\":\"plane\",\"normal_unit\":" +
            detail::vec({plane.normal.x, plane.normal.y, plane.normal.z}) +
            ",\"offset_m\":" + detail::num(plane.offset_m) +
            ",\"boundary_node_ids\":[],\"mesh_asset\":null,\"fit_residual_m\":" +
            detail::num(plane.residuals.weighted_rms_m) +
            ",\"provenance\":" +
            detail::provenance(support_identity, "fitted") + "}";
      } else {
        out.surfaces_json +=
            ",\"kind\":\"mesh_patch\",\"normal_unit\":null,\"offset_m\":null,"
            "\"boundary_node_ids\":[],\"mesh_asset\":null,"
            "\"fit_residual_m\":null,\"provenance\":" +
            detail::provenance(support_identity, "observed") + "}";
      }
    }
    out.surfaces_json += ']';

    out.emitted_json_bytes = out.nodes_json.size() + out.edges_json.size() +
                             out.surfaces_json.size();
    if (out.emitted_json_bytes > options.max_json_bytes) {
      detail::clear_derived(out);
      out.status = Status::kResourceLimit;
      return out;
    }
  } catch (const std::bad_alloc &) {
    detail::clear_derived(out);
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_anchor_scene_entities

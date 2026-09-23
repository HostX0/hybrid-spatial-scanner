#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_patch {

struct Vec3 {
  double x{};
  double y{};
  double z{};
};

using Triangle = std::array<uint32_t, 3>;

struct Options {
  // Match the current live-reconstruction local angular gate. This module does
  // not widen it to make noisy fixtures look better.
  double min_abs_normal_dot = 0.99999;
  // Match the current 2 mm local point-to-plane gate, applied symmetrically to
  // the two opposite vertices of adjacent triangles.
  double max_cross_plane_distance_m = 0.002;
  size_t max_triangles = 65536;
  size_t max_components = 2048;
  size_t max_samples_per_component = 96;
  // More than two faces on one indexed edge is retained as non-manifold
  // evidence and is never used as a component merge bridge.
  size_t max_manifold_edge_incidence = 2;
  uint64_t deterministic_seed = 0x9e3779b97f4a7c15ULL;
};

enum class Status {
  kOk,
  kInvalidArgument,
  kInvalidGeometry,
  kResourceLimit,
};

struct Component {
  // Lexicographically smallest sorted indexed triangle in the component.
  // Stable under triangle-list permutations while anchor vertex identities stay
  // unchanged, so callers can derive stable internal surface identities.
  Triangle canonical_triangle{};
  size_t triangle_count{};
  // Unique anchor vertex indices chosen deterministically, independent of input
  // triangle order, and bounded for the downstream robust plane fit.
  std::vector<uint32_t> sample_vertex_indices;
};

struct Result {
  Status status{Status::kOk};
  std::vector<Component> components;
  // For each input triangle, component index or npos for a degenerate triangle.
  std::vector<size_t> component_of_triangle;
  size_t degenerate_triangles{};
  size_t non_manifold_edges{};
  static constexpr size_t npos = std::numeric_limits<size_t>::max();
};

namespace detail {

inline Vec3 sub(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
inline double dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 mul(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline bool finite(Vec3 p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31U);
}
inline Triangle canonical_triangle(Triangle indices) {
  std::sort(indices.begin(), indices.end());
  return indices;
}

struct Face {
  Triangle canonical{};
  Vec3 normal{};
  double offset{};
  bool valid{};
};

struct EdgeRecord {
  uint32_t a{};
  uint32_t b{};
  size_t triangle{};
};

class Dsu {
public:
  explicit Dsu(const std::vector<Face> &faces)
      : parent_(faces.size()), key_(faces.size()) {
    for (size_t i = 0; i < faces.size(); ++i) {
      parent_[i] = i;
      key_[i] = faces[i].canonical;
    }
  }

  size_t find(size_t x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];
      x = parent_[x];
    }
    return x;
  }

  void unite(size_t a, size_t b) {
    a = find(a);
    b = find(b);
    if (a == b)
      return;
    // Root selection follows geometry identity, not ingestion order.
    if (key_[b] < key_[a])
      std::swap(a, b);
    parent_[b] = a;
  }

private:
  std::vector<size_t> parent_;
  std::vector<Triangle> key_;
};

inline int opposite_vertex(const Triangle &triangle, uint32_t e0, uint32_t e1) {
  for (int i = 0; i < 3; ++i) {
    const auto v = triangle[static_cast<size_t>(i)];
    if (v != e0 && v != e1)
      return i;
  }
  return -1;
}

inline bool compatible(const Triangle &a, const Face &fa, const Triangle &b,
                       const Face &fb, std::span<const Vec3> vertices,
                       uint32_t e0, uint32_t e1, const Options &o) {
  if (!fa.valid || !fb.valid ||
      std::abs(dot(fa.normal, fb.normal)) < o.min_abs_normal_dot)
    return false;
  const int ao = opposite_vertex(a, e0, e1);
  const int bo = opposite_vertex(b, e0, e1);
  if (ao < 0 || bo < 0)
    return false;
  // Testing a shared edge point is vacuous because it lies on both local
  // triangle planes. Use the opposite vertices in both directions instead.
  const Vec3 pa = vertices[a[static_cast<size_t>(ao)]];
  const Vec3 pb = vertices[b[static_cast<size_t>(bo)]];
  const double da = std::abs(dot(fb.normal, pa) + fb.offset);
  const double db = std::abs(dot(fa.normal, pb) + fa.offset);
  return std::max(da, db) <= o.max_cross_plane_distance_m;
}

} // namespace detail

inline Result group(std::span<const Vec3> vertices,
                    std::span<const Triangle> triangles,
                    const Options &options = {}) {
  Result out;
  out.component_of_triangle.assign(triangles.size(), Result::npos);
  if (!std::isfinite(options.min_abs_normal_dot) ||
      options.min_abs_normal_dot < 0.0 || options.min_abs_normal_dot > 1.0 ||
      !std::isfinite(options.max_cross_plane_distance_m) ||
      options.max_cross_plane_distance_m < 0.0 || options.max_triangles == 0 ||
      options.max_components == 0 || options.max_samples_per_component < 3 ||
      options.max_manifold_edge_incidence != 2) {
    out.status = Status::kInvalidArgument;
    return out;
  }
  if (triangles.size() > options.max_triangles) {
    out.status = Status::kResourceLimit;
    return out;
  }
  for (const auto &p : vertices)
    if (!detail::finite(p)) {
      out.status = Status::kInvalidGeometry;
      return out;
    }

  std::vector<detail::Face> faces(triangles.size());
  std::vector<detail::EdgeRecord> edges;
  if (triangles.size() > std::numeric_limits<size_t>::max() / 3U) {
    out.status = Status::kResourceLimit;
    return out;
  }
  edges.reserve(triangles.size() * 3U);
  for (size_t i = 0; i < triangles.size(); ++i) {
    const auto &t = triangles[i];
    if (t[0] >= vertices.size() || t[1] >= vertices.size() ||
        t[2] >= vertices.size()) {
      out.status = Status::kInvalidGeometry;
      return out;
    }
    if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) {
      ++out.degenerate_triangles;
      continue;
    }
    const auto p0 = vertices[t[0]], p1 = vertices[t[1]], p2 = vertices[t[2]];
    auto n = detail::cross(detail::sub(p1, p0), detail::sub(p2, p0));
    const double length = std::sqrt(detail::dot(n, n));
    if (!(length > 1e-10)) {
      ++out.degenerate_triangles;
      continue;
    }
    n = detail::mul(n, 1.0 / length);
    faces[i] = {detail::canonical_triangle(t), n, -detail::dot(n, p0), true};
    for (size_t j = 0; j < 3; ++j) {
      const auto e = std::minmax(t[j], t[(j + 1) % 3]);
      edges.push_back({e.first, e.second, i});
    }
  }

  std::sort(edges.begin(), edges.end(), [](const auto &a, const auto &b) {
    return std::tie(a.a, a.b, a.triangle) < std::tie(b.a, b.b, b.triangle);
  });
  detail::Dsu dsu(faces);
  for (size_t begin = 0; begin < edges.size();) {
    size_t end = begin + 1;
    while (end < edges.size() && edges[end].a == edges[begin].a &&
           edges[end].b == edges[begin].b)
      ++end;
    const size_t incidence = end - begin;
    if (incidence > options.max_manifold_edge_incidence) {
      ++out.non_manifold_edges;
    } else if (incidence == 2) {
      const auto ta = edges[begin].triangle, tb = edges[begin + 1].triangle;
      if (detail::compatible(triangles[ta], faces[ta], triangles[tb], faces[tb],
                             vertices, edges[begin].a, edges[begin].b, options))
        dsu.unite(ta, tb);
    }
    begin = end;
  }

  std::vector<Triangle> canonical_by_root(triangles.size(),
                                          {UINT32_MAX, UINT32_MAX, UINT32_MAX});
  std::vector<size_t> triangle_count_by_root(triangles.size(), 0);
  for (size_t i = 0; i < triangles.size(); ++i) {
    if (!faces[i].valid)
      continue;
    const size_t root = dsu.find(i);
    canonical_by_root[root] = std::min(canonical_by_root[root], faces[i].canonical);
    ++triangle_count_by_root[root];
  }
  std::vector<size_t> roots;
  for (size_t i = 0; i < triangles.size(); ++i)
    if (triangle_count_by_root[i] != 0)
      roots.push_back(i);
  if (roots.size() > options.max_components) {
    out.status = Status::kResourceLimit;
    return out;
  }
  std::sort(roots.begin(), roots.end(), [&](size_t a, size_t b) {
    return canonical_by_root[a] < canonical_by_root[b];
  });

  std::vector<size_t> root_to_component(triangles.size(), Result::npos);
  out.components.resize(roots.size());
  std::vector<std::vector<uint32_t>> candidate_vertices(roots.size());
  for (size_t ci = 0; ci < roots.size(); ++ci) {
    const size_t root = roots[ci];
    root_to_component[root] = ci;
    out.components[ci].canonical_triangle = canonical_by_root[root];
    out.components[ci].triangle_count = triangle_count_by_root[root];
    candidate_vertices[ci].reserve(
        std::min<size_t>(triangle_count_by_root[root] * 3U,
                         options.max_samples_per_component * 2U));
  }

  for (size_t i = 0; i < triangles.size(); ++i) {
    if (!faces[i].valid)
      continue;
    const size_t ci = root_to_component[dsu.find(i)];
    out.component_of_triangle[i] = ci;
    auto &candidates = candidate_vertices[ci];
    candidates.insert(candidates.end(), triangles[i].begin(), triangles[i].end());
  }

  for (size_t ci = 0; ci < out.components.size(); ++ci) {
    auto &vertices_in_component = candidate_vertices[ci];
    std::sort(vertices_in_component.begin(), vertices_in_component.end());
    vertices_in_component.erase(
        std::unique(vertices_in_component.begin(), vertices_in_component.end()),
        vertices_in_component.end());
    if (vertices_in_component.size() > options.max_samples_per_component) {
      std::vector<std::pair<uint64_t, uint32_t>> ranked;
      ranked.reserve(vertices_in_component.size());
      for (auto vertex : vertices_in_component)
        ranked.push_back({detail::splitmix64(options.deterministic_seed ^
                                             uint64_t(vertex)),
                          vertex});
      const auto nth = ranked.begin() + static_cast<std::ptrdiff_t>(
                                            options.max_samples_per_component);
      std::nth_element(ranked.begin(), nth, ranked.end());
      ranked.resize(options.max_samples_per_component);
      vertices_in_component.clear();
      vertices_in_component.reserve(ranked.size());
      for (const auto &[rank, vertex] : ranked) {
        (void)rank;
        vertices_in_component.push_back(vertex);
      }
      std::sort(vertices_in_component.begin(), vertices_in_component.end());
    }
    out.components[ci].sample_vertex_indices = std::move(vertices_in_component);
  }
  return out;
}

} // namespace hss::mesh_patch

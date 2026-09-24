#pragma once

#include "hss/coordinate_basis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hss::mesh_anchor_cut {

using Triangle = std::array<uint32_t, 3>;

constexpr double kMaxAbsProjectCoordinateM = 10'000.0;
constexpr size_t kMaxVertices = 65'536U;
constexpr size_t kMaxTriangles = 65'536U;
constexpr double kMaxCutHeightM = 100.0;
constexpr double kMinSegmentLengthSquaredM2 = 1.0e-12;
constexpr size_t kDefaultMaxFragments = 2'048U;

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kInvalidCoordinateFrame,
  kInvalidGeometry,
  kResourceLimit,
};

struct Fragment {
  size_t source_triangle_index{};
  Triangle local_triangle{};
  HSSVec3d start_m{};
  HSSVec3d end_m{};
  coordinate::PlanPoint start_uv{};
  coordinate::PlanPoint end_uv{};
  std::string surface_key;
};

struct Result {
  Status status{Status::kOk};
  std::string support_identity;
  std::vector<Fragment> fragments;
  size_t skipped_unbound{};
  size_t skipped_degenerate{};
  size_t skipped_no_crossing{};
};

namespace detail {

inline HSSVec3d sub(HSSVec3d a, HSSVec3d b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline HSSVec3d add(HSSVec3d a, HSSVec3d b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline HSSVec3d mul(HSSVec3d a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}

inline double dot(HSSVec3d a, HSSVec3d b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline HSSVec3d cross(HSSVec3d a, HSSVec3d b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

inline bool bounded(HSSVec3d p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::abs(p.x) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.y) <= kMaxAbsProjectCoordinateM &&
         std::abs(p.z) <= kMaxAbsProjectCoordinateM;
}

inline bool plan_point_less(coordinate::PlanPoint a, coordinate::PlanPoint b) {
  return std::tie(a.u_m, a.v_m) < std::tie(b.u_m, b.v_m);
}

inline Triangle canonical_triangle(Triangle triangle) {
  std::sort(triangle.begin(), triangle.end());
  return triangle;
}

inline bool fragment_less(const Fragment &a, const Fragment &b) {
  const auto ca = canonical_triangle(a.local_triangle);
  const auto cb = canonical_triangle(b.local_triangle);
  return std::tie(a.start_uv.u_m, a.start_uv.v_m, a.end_uv.u_m,
                  a.end_uv.v_m, a.surface_key, ca) <
         std::tie(b.start_uv.u_m, b.start_uv.v_m, b.end_uv.u_m,
                  b.end_uv.v_m, b.surface_key, cb);
}

inline void clear_derived(Result &out) {
  out.fragments.clear();
}

} // namespace detail

// Produces deterministic architectural-cut fragments from one complete
// anchor-local triangle batch. `surface_keys_by_triangle` is the finalized
// per-triangle metric binding supplied by the anchor patch plan; an empty key
// means the triangle is deliberately unbound from metric topology.
//
// The cut uses the current live half-open crossing rule exactly. No hidden
// surface is inferred, coplanar triangles do not become wall cuts, and no raw
// source geometry is mutated or retained. Output fragments are sorted by their
// canonical plan-space segment and stable surface key so later scene assembly
// cannot inherit triangle arrival order through iteration order.
inline Result prepare(std::span<const HSSVec3d> project_vertices,
                      std::span<const Triangle> local_triangles,
                      std::span<const std::string_view> surface_keys_by_triangle,
                      std::string_view support_identity,
                      const coordinate::FloorFrame &floor,
                      double cut_height_m,
                      size_t max_fragments = kDefaultMaxFragments) {
  Result out;
  if (support_identity.empty() ||
      surface_keys_by_triangle.size() != local_triangles.size() ||
      !std::isfinite(cut_height_m) || cut_height_m <= 0.0 ||
      cut_height_m > kMaxCutHeightM || max_fragments == 0U ||
      max_fragments > kDefaultMaxFragments) {
    out.status = Status::kInvalidArgument;
    return out;
  }
  if (!coordinate::is_valid_floor_frame(floor) ||
      !detail::bounded(floor.origin_m)) {
    out.status = Status::kInvalidCoordinateFrame;
    return out;
  }
  if (project_vertices.size() > kMaxVertices ||
      local_triangles.size() > kMaxTriangles) {
    out.status = Status::kResourceLimit;
    return out;
  }
  for (const auto vertex : project_vertices) {
    if (!detail::bounded(vertex)) {
      out.status = Status::kInvalidGeometry;
      return out;
    }
  }

  try {
    out.support_identity.assign(support_identity);
    std::string surface_prefix;
    surface_prefix.reserve(support_identity.size() + 7U);
    surface_prefix.append(support_identity);
    surface_prefix.append(":patch:");
    out.fragments.reserve(std::min(local_triangles.size(), max_fragments));
    for (size_t triangle_index = 0U; triangle_index < local_triangles.size();
         ++triangle_index) {
      const Triangle triangle = local_triangles[triangle_index];
      if (triangle[0] >= project_vertices.size() ||
          triangle[1] >= project_vertices.size() ||
          triangle[2] >= project_vertices.size()) {
        detail::clear_derived(out);
        out.status = Status::kInvalidGeometry;
        return out;
      }

      const HSSVec3d p0 = project_vertices[triangle[0]];
      const HSSVec3d p1 = project_vertices[triangle[1]];
      const HSSVec3d p2 = project_vertices[triangle[2]];
      const std::string_view surface_key = surface_keys_by_triangle[triangle_index];
      const HSSVec3d normal =
          detail::cross(detail::sub(p1, p0), detail::sub(p2, p0));
      if (!(detail::dot(normal, normal) > 1.0e-20)) {
        if (!surface_key.empty()) {
          detail::clear_derived(out);
          out.status = Status::kInvalidArgument;
          return out;
        }
        ++out.skipped_degenerate;
        continue;
      }

      if (surface_key.empty()) {
        ++out.skipped_unbound;
        continue;
      }
      if (surface_key.size() <= surface_prefix.size() ||
          !surface_key.starts_with(surface_prefix)) {
        detail::clear_derived(out);
        out.status = Status::kInvalidArgument;
        return out;
      }

      const std::array<HSSVec3d, 3> points{p0, p1, p2};
      std::array<HSSVec3d, 2> hits{};
      size_t hit_count = 0U;
      for (size_t edge = 0U; edge < points.size(); ++edge) {
        const HSSVec3d a = points[edge];
        const HSSVec3d b = points[(edge + 1U) % points.size()];
        const double da = detail::dot(detail::sub(a, floor.origin_m), floor.n) -
                          cut_height_m;
        const double db = detail::dot(detail::sub(b, floor.origin_m), floor.n) -
                          cut_height_m;
        if ((da <= 0.0 && db > 0.0) || (db <= 0.0 && da > 0.0)) {
          if (hit_count < hits.size()) {
            hits[hit_count++] = detail::add(
                a, detail::mul(detail::sub(b, a), da / (da - db)));
          } else {
            detail::clear_derived(out);
            out.status = Status::kInvalidGeometry;
            return out;
          }
        }
      }

      if (hit_count != 2U ||
          detail::dot(detail::sub(hits[0], hits[1]),
                      detail::sub(hits[0], hits[1])) <
              kMinSegmentLengthSquaredM2) {
        ++out.skipped_no_crossing;
        continue;
      }

      coordinate::PlanPoint uv0 = coordinate::project_to_floor(floor, hits[0]);
      coordinate::PlanPoint uv1 = coordinate::project_to_floor(floor, hits[1]);
      if (detail::plan_point_less(uv1, uv0)) {
        std::swap(hits[0], hits[1]);
        std::swap(uv0, uv1);
      }

      if (out.fragments.size() >= max_fragments) {
        detail::clear_derived(out);
        out.status = Status::kResourceLimit;
        return out;
      }
      out.fragments.push_back({triangle_index, triangle, hits[0], hits[1], uv0,
                               uv1, std::string(surface_key)});
    }
    std::sort(out.fragments.begin(), out.fragments.end(), detail::fragment_less);
  } catch (const std::bad_alloc &) {
    detail::clear_derived(out);
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_anchor_cut

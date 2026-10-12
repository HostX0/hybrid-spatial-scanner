#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace hss::mesh_event_payload {

static_assert(sizeof(float) == 4U, "HSSMeshEvent requires Float32 vertices");
static_assert(sizeof(uint32_t) == 4U, "HSSMeshEvent requires UInt32 indices");

constexpr uint64_t kMaxVertices = 65'536U;
constexpr uint64_t kMaxTriangles = 65'536U;
constexpr float kMaxAbsCoordinateM = 10'000.0F;

struct LocalVertex {
  float x{};
  float y{};
  float z{};
};

using Triangle = std::array<uint32_t, 3>;

enum class Status : uint8_t {
  kOk,
  kResourceLimit,
  kPayloadSizeMismatch,
  kInvalidGeometry,
};

struct Decoded {
  Status status{Status::kOk};
  std::vector<LocalVertex> vertices;
  std::vector<Triangle> triangles;
  size_t vertex_bytes{};
  size_t triangle_bytes{};
};

namespace detail {

inline bool checked_bytes(uint64_t count, uint64_t scalars_per_element,
                          size_t scalar_size, size_t *out) {
  if (out == nullptr)
    return false;
  if (count > std::numeric_limits<size_t>::max() / scalars_per_element)
    return false;
  const size_t scalar_count =
      static_cast<size_t>(count) * static_cast<size_t>(scalars_per_element);
  if (scalar_count > std::numeric_limits<size_t>::max() / scalar_size)
    return false;
  *out = scalar_count * scalar_size;
  return true;
}

inline bool finite_in_range(float value) {
  return std::isfinite(value) && std::abs(value) <= kMaxAbsCoordinateM;
}

} // namespace detail

// Decodes the exact owned HSSMeshEvent payload layout used by BoundedIngress:
// tightly packed anchor-local XYZ Float32 values followed immediately by
// tightly packed UInt32 triangle indices. Host scalar representation is
// intentional because this is an in-process ABI payload, not a persisted file.
// The input bytes remain caller-owned and are never modified or retained.
inline Decoded decode(std::span<const uint8_t> payload, uint64_t vertex_count,
                      uint64_t triangle_count) {
  Decoded out;
  if (vertex_count > kMaxVertices || triangle_count > kMaxTriangles) {
    out.status = Status::kResourceLimit;
    return out;
  }

  if (!detail::checked_bytes(vertex_count, 3U, sizeof(float),
                             &out.vertex_bytes) ||
      !detail::checked_bytes(triangle_count, 3U, sizeof(uint32_t),
                             &out.triangle_bytes) ||
      out.triangle_bytes >
          std::numeric_limits<size_t>::max() - out.vertex_bytes) {
    out.status = Status::kResourceLimit;
    return out;
  }

  const size_t expected = out.vertex_bytes + out.triangle_bytes;
  if (payload.size() != expected) {
    out.status = Status::kPayloadSizeMismatch;
    return out;
  }

  try {
    out.vertices.reserve(static_cast<size_t>(vertex_count));
    out.triangles.reserve(static_cast<size_t>(triangle_count));

    size_t offset = 0U;
    for (uint64_t vertex = 0U; vertex < vertex_count; ++vertex) {
      float xyz[3]{};
      std::memcpy(xyz, payload.data() + offset, sizeof(xyz));
      offset += sizeof(xyz);
      if (!detail::finite_in_range(xyz[0]) ||
          !detail::finite_in_range(xyz[1]) ||
          !detail::finite_in_range(xyz[2])) {
        out.vertices.clear();
        out.triangles.clear();
        out.status = Status::kInvalidGeometry;
        return out;
      }
      out.vertices.push_back({xyz[0], xyz[1], xyz[2]});
    }

    for (uint64_t triangle = 0U; triangle < triangle_count; ++triangle) {
      Triangle indices{};
      std::memcpy(indices.data(), payload.data() + offset,
                  sizeof(uint32_t) * indices.size());
      offset += sizeof(uint32_t) * indices.size();
      if (indices[0] >= vertex_count || indices[1] >= vertex_count ||
          indices[2] >= vertex_count) {
        out.vertices.clear();
        out.triangles.clear();
        out.status = Status::kInvalidGeometry;
        return out;
      }
      out.triangles.push_back(indices);
    }
  } catch (const std::bad_alloc &) {
    out.vertices.clear();
    out.triangles.clear();
    out.status = Status::kResourceLimit;
    return out;
  }

  out.status = Status::kOk;
  return out;
}

} // namespace hss::mesh_event_payload

#include "hss/mesh_event_payload.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace mp = hss::mesh_event_payload;

static int failures = 0;
static int assertions = 0;

#define CHECK(expr)                                                            \
  do {                                                                         \
    ++assertions;                                                              \
    if (!(expr)) {                                                             \
      ++failures;                                                              \
      std::cerr << "FAIL " << __LINE__ << ": " #expr "\n";                  \
    }                                                                          \
  } while (false)

static std::vector<uint8_t>
pack(const std::vector<std::array<float, 3>> &vertices,
     const std::vector<std::array<uint32_t, 3>> &triangles) {
  const size_t vertex_bytes = vertices.size() * 3U * sizeof(float);
  const size_t triangle_bytes = triangles.size() * 3U * sizeof(uint32_t);
  std::vector<uint8_t> bytes(vertex_bytes + triangle_bytes);
  size_t offset = 0U;
  for (const auto &vertex : vertices) {
    std::memcpy(bytes.data() + offset, vertex.data(), 3U * sizeof(float));
    offset += 3U * sizeof(float);
  }
  for (const auto &triangle : triangles) {
    std::memcpy(bytes.data() + offset, triangle.data(),
                3U * sizeof(uint32_t));
    offset += 3U * sizeof(uint32_t);
  }
  return bytes;
}

static void valid_payload_round_trip() {
  const std::vector<std::array<float, 3>> vertices = {
      {-1.25F, 0.0F, 2.5F}, {4.0F, -3.5F, 6.25F}, {0.125F, 1.5F, -2.0F},
      {9.0F, 8.0F, 7.0F}};
  const std::vector<std::array<uint32_t, 3>> triangles = {
      {0U, 1U, 2U}, {2U, 1U, 3U}};
  const auto bytes = pack(vertices, triangles);
  const auto before = bytes;
  const auto decoded = mp::decode(bytes, vertices.size(), triangles.size());

  CHECK(decoded.status == mp::Status::kOk);
  CHECK(decoded.vertex_bytes == vertices.size() * 3U * sizeof(float));
  CHECK(decoded.triangle_bytes == triangles.size() * 3U * sizeof(uint32_t));
  CHECK(decoded.vertices.size() == vertices.size());
  CHECK(decoded.triangles.size() == triangles.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    CHECK(decoded.vertices[i].x == vertices[i][0]);
    CHECK(decoded.vertices[i].y == vertices[i][1]);
    CHECK(decoded.vertices[i].z == vertices[i][2]);
  }
  for (size_t i = 0; i < triangles.size(); ++i)
    CHECK(decoded.triangles[i] == triangles[i]);
  CHECK(bytes == before);
}

static void empty_and_vertex_only_payloads() {
  const auto empty = mp::decode({}, 0U, 0U);
  CHECK(empty.status == mp::Status::kOk);
  CHECK(empty.vertices.empty());
  CHECK(empty.triangles.empty());
  CHECK(empty.vertex_bytes == 0U);
  CHECK(empty.triangle_bytes == 0U);

  const std::vector<std::array<float, 3>> vertices = {
      {1.0F, 2.0F, 3.0F}, {4.0F, 5.0F, 6.0F}};
  const auto bytes = pack(vertices, {});
  const auto decoded = mp::decode(bytes, vertices.size(), 0U);
  CHECK(decoded.status == mp::Status::kOk);
  CHECK(decoded.vertices.size() == 2U);
  CHECK(decoded.triangles.empty());
}

static void size_mismatch_fails_closed() {
  const std::vector<std::array<float, 3>> vertices = {
      {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  const std::vector<std::array<uint32_t, 3>> triangles = {{0U, 1U, 2U}};
  auto bytes = pack(vertices, triangles);

  auto truncated_vertex = bytes;
  truncated_vertex.resize(3U * sizeof(float) - 1U);
  CHECK(mp::decode(truncated_vertex, 3U, 1U).status ==
        mp::Status::kPayloadSizeMismatch);

  auto truncated_triangle = bytes;
  truncated_triangle.pop_back();
  CHECK(mp::decode(truncated_triangle, 3U, 1U).status ==
        mp::Status::kPayloadSizeMismatch);

  auto trailing = bytes;
  trailing.push_back(0x7fU);
  CHECK(mp::decode(trailing, 3U, 1U).status ==
        mp::Status::kPayloadSizeMismatch);

  CHECK(mp::decode(bytes, 2U, 1U).status ==
        mp::Status::kPayloadSizeMismatch);
}

static void invalid_geometry_fails_closed() {
  std::vector<std::array<float, 3>> vertices = {
      {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  std::vector<std::array<uint32_t, 3>> triangles = {{0U, 1U, 3U}};
  auto invalid_index = mp::decode(pack(vertices, triangles), 3U, 1U);
  CHECK(invalid_index.status == mp::Status::kInvalidGeometry);
  CHECK(invalid_index.vertices.empty());
  CHECK(invalid_index.triangles.empty());

  triangles = {{0U, 1U, 2U}};
  vertices[1][0] = std::numeric_limits<float>::infinity();
  auto nonfinite = mp::decode(pack(vertices, triangles), 3U, 1U);
  CHECK(nonfinite.status == mp::Status::kInvalidGeometry);
  CHECK(nonfinite.vertices.empty());
  CHECK(nonfinite.triangles.empty());

  vertices[1][0] = 10'000.25F;
  auto out_of_range = mp::decode(pack(vertices, triangles), 3U, 1U);
  CHECK(out_of_range.status == mp::Status::kInvalidGeometry);
  CHECK(out_of_range.vertices.empty());
  CHECK(out_of_range.triangles.empty());

  const std::vector<uint8_t> one_triangle(3U * sizeof(uint32_t), 0U);
  auto no_vertices = mp::decode(one_triangle, 0U, 1U);
  CHECK(no_vertices.status == mp::Status::kInvalidGeometry);
  CHECK(no_vertices.vertices.empty());
  CHECK(no_vertices.triangles.empty());
}

static void resource_limits_fail_before_decode() {
  CHECK(mp::decode({}, mp::kMaxVertices + 1U, 0U).status ==
        mp::Status::kResourceLimit);
  CHECK(mp::decode({}, 0U, mp::kMaxTriangles + 1U).status ==
        mp::Status::kResourceLimit);
}

static void boundary_coordinates_are_accepted() {
  const std::vector<std::array<float, 3>> vertices = {
      {mp::kMaxAbsCoordinateM, -mp::kMaxAbsCoordinateM, 0.0F}};
  const auto decoded = mp::decode(pack(vertices, {}), 1U, 0U);
  CHECK(decoded.status == mp::Status::kOk);
  CHECK(decoded.vertices.size() == 1U);
  CHECK(decoded.vertices[0].x == mp::kMaxAbsCoordinateM);
  CHECK(decoded.vertices[0].y == -mp::kMaxAbsCoordinateM);
}

int main() {
  valid_payload_round_trip();
  empty_and_vertex_only_payloads();
  size_mismatch_fails_closed();
  invalid_geometry_fails_closed();
  resource_limits_fail_before_decode();
  boundary_coordinates_are_accepted();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return 1;
  }
  std::cout << "PASS " << assertions << "/" << assertions << " assertions\n";
  return 0;
}

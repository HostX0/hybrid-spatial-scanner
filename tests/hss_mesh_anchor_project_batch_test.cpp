#include "hss/mesh_anchor_project_batch.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using hss::mesh_anchor_project_batch::Prepared;
using hss::mesh_anchor_project_batch::Status;
using Triangle = hss::mesh_anchor_project_batch::Triangle;

int assertions = 0;
int failures = 0;

void expect(bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool close(double a, double b, double eps = 1e-12) {
  return std::abs(a - b) <= eps;
}

HSSMat4d identity() {
  HSSMat4d t{};
  t.column_major[0] = 1.0;
  t.column_major[5] = 1.0;
  t.column_major[10] = 1.0;
  t.column_major[15] = 1.0;
  return t;
}

HSSMat4d translate(double x, double y, double z) {
  HSSMat4d t = identity();
  t.column_major[12] = x;
  t.column_major[13] = y;
  t.column_major[14] = z;
  return t;
}

HSSMat4d rotate_z_90(double tx = 0.0, double ty = 0.0,
                     double tz = 0.0) {
  HSSMat4d t = identity();
  t.column_major[0] = 0.0;
  t.column_major[1] = 1.0;
  t.column_major[4] = -1.0;
  t.column_major[5] = 0.0;
  t.column_major[12] = tx;
  t.column_major[13] = ty;
  t.column_major[14] = tz;
  return t;
}

HSSMat4d rotate_y_90(double tx = 0.0, double ty = 0.0,
                     double tz = 0.0) {
  HSSMat4d t = identity();
  t.column_major[0] = 0.0;
  t.column_major[2] = -1.0;
  t.column_major[8] = 1.0;
  t.column_major[10] = 0.0;
  t.column_major[12] = tx;
  t.column_major[13] = ty;
  t.column_major[14] = tz;
  return t;
}

std::vector<uint8_t> payload(const std::vector<std::array<float, 3>> &vertices,
                             const std::vector<Triangle> &triangles) {
  std::vector<uint8_t> bytes(vertices.size() * 3U * sizeof(float) +
                             triangles.size() * 3U * sizeof(uint32_t));
  size_t offset = 0U;
  for (const auto &vertex : vertices) {
    std::memcpy(bytes.data() + offset, vertex.data(), 3U * sizeof(float));
    offset += 3U * sizeof(float);
  }
  for (const Triangle &triangle : triangles) {
    std::memcpy(bytes.data() + offset, triangle.data(),
                3U * sizeof(uint32_t));
    offset += 3U * sizeof(uint32_t);
  }
  return bytes;
}

void expect_point(const HSSVec3d &point, double x, double y, double z,
                  const std::string &label) {
  expect(close(point.x, x), label + " x");
  expect(close(point.y, y), label + " y");
  expect(close(point.z, z), label + " z");
}

void identity_round_trip() {
  const std::vector<std::array<float, 3>> vertices{{{1.0F, 2.0F, 3.0F}},
                                                   {{-4.0F, 5.0F, 6.0F}},
                                                   {{7.0F, -8.0F, 9.0F}}};
  const std::vector<Triangle> triangles{{{0U, 1U, 2U}}};
  const std::vector<uint8_t> bytes = payload(vertices, triangles);
  const std::vector<uint8_t> before = bytes;
  const Prepared out = hss::mesh_anchor_project_batch::prepare(
      bytes, vertices.size(), triangles.size(), identity(), identity());
  expect(out.status == Status::kOk, "identity status");
  expect(out.project_vertices.size() == 3U, "identity vertex count");
  expect(out.triangles == triangles, "identity triangle topology exact");
  expect(out.vertex_bytes == 36U, "identity vertex bytes");
  expect(out.triangle_bytes == 12U, "identity triangle bytes");
  expect(bytes == before, "input payload remains immutable");
  expect_point(out.project_vertices[0], 1.0, 2.0, 3.0, "identity v0");
  expect_point(out.project_vertices[1], -4.0, 5.0, 6.0, "identity v1");
  expect_point(out.project_vertices[2], 7.0, -8.0, 9.0, "identity v2");
}

void two_stage_transform() {
  const std::vector<std::array<float, 3>> vertices{{{1.0F, 2.0F, 3.0F}},
                                                   {{-2.0F, 4.0F, 0.5F}},
                                                   {{0.0F, -1.0F, 2.0F}}};
  const std::vector<Triangle> triangles{{{2U, 0U, 1U}}};
  const auto bytes = payload(vertices, triangles);
  const HSSMat4d world_anchor = rotate_z_90(10.0, 20.0, 30.0);
  const HSSMat4d project_world = rotate_y_90(-5.0, 4.0, 2.0);
  const Prepared out = hss::mesh_anchor_project_batch::prepare(
      bytes, vertices.size(), triangles.size(), world_anchor, project_world);
  expect(out.status == Status::kOk, "two-stage status");
  expect(out.triangles == triangles, "two-stage topology exact");
  // v0: local (1,2,3) -> world (8,21,33) -> project (28,25,-6)
  expect_point(out.project_vertices[0], 28.0, 25.0, -6.0,
               "two-stage v0");
  // v1: local (-2,4,.5) -> world (6,18,30.5) -> project (25.5,22,-4)
  expect_point(out.project_vertices[1], 25.5, 22.0, -4.0,
               "two-stage v1");
  // v2: local (0,-1,2) -> world (11,20,32) -> project (27,24,-9)
  expect_point(out.project_vertices[2], 27.0, 24.0, -9.0,
               "two-stage v2");
}

void empty_and_vertex_only() {
  const Prepared empty = hss::mesh_anchor_project_batch::prepare(
      {}, 0U, 0U, identity(), identity());
  expect(empty.status == Status::kOk, "empty payload accepted");
  expect(empty.project_vertices.empty(), "empty vertices");
  expect(empty.triangles.empty(), "empty triangles");

  const std::vector<std::array<float, 3>> vertices{{{1.0F, 2.0F, 3.0F}}};
  const auto bytes = payload(vertices, {});
  const Prepared vertex_only = hss::mesh_anchor_project_batch::prepare(
      bytes, 1U, 0U, translate(2.0, -3.0, 4.0), identity());
  expect(vertex_only.status == Status::kOk, "vertex-only status");
  expect(vertex_only.project_vertices.size() == 1U,
         "vertex-only vertex count");
  expect(vertex_only.triangles.empty(), "vertex-only no triangles");
  expect_point(vertex_only.project_vertices[0], 3.0, -1.0, 7.0,
               "vertex-only transformed");
}

void invalid_transforms_fail_closed() {
  const auto bytes = payload({{{1.0F, 2.0F, 3.0F}}}, {});

  HSSMat4d scale = identity();
  scale.column_major[0] = 2.0;
  Prepared out = hss::mesh_anchor_project_batch::prepare(
      bytes, 1U, 0U, scale, identity());
  expect(out.status == Status::kInvalidCoordinateFrame,
         "scale rejected");
  expect(out.project_vertices.empty(), "scale clears vertices");
  expect(out.triangles.empty(), "scale clears triangles");

  HSSMat4d reflection = identity();
  reflection.column_major[0] = -1.0;
  out = hss::mesh_anchor_project_batch::prepare(bytes, 1U, 0U, identity(),
                                                reflection);
  expect(out.status == Status::kInvalidCoordinateFrame,
         "reflection rejected");
  expect(out.project_vertices.empty(), "reflection clears vertices");

  HSSMat4d nonfinite = identity();
  nonfinite.column_major[12] = std::numeric_limits<double>::infinity();
  out = hss::mesh_anchor_project_batch::prepare(bytes, 1U, 0U, nonfinite,
                                                identity());
  expect(out.status == Status::kInvalidCoordinateFrame,
         "nonfinite transform rejected");
}

void project_extent_guard() {
  const auto zero = payload({{{0.0F, 0.0F, 0.0F}}}, {});
  Prepared out = hss::mesh_anchor_project_batch::prepare(
      zero, 1U, 0U, identity(), translate(10'000.0, -10'000.0, 10'000.0));
  expect(out.status == Status::kOk, "exact project extent accepted");
  expect_point(out.project_vertices[0], 10'000.0, -10'000.0, 10'000.0,
               "extent edge");

  out = hss::mesh_anchor_project_batch::prepare(
      zero, 1U, 0U, identity(), translate(10'000.0001, 0.0, 0.0));
  expect(out.status == Status::kInvalidCoordinateFrame,
         "project extent overflow rejected");
  expect(out.project_vertices.empty(), "extent failure clears vertices");
  expect(out.triangles.empty(), "extent failure clears triangles");

  const auto local_limit = payload({{{10'000.0F, 0.0F, 0.0F}}}, {});
  out = hss::mesh_anchor_project_batch::prepare(
      local_limit, 1U, 0U, translate(1.0, 0.0, 0.0), identity());
  expect(out.status == Status::kInvalidCoordinateFrame,
         "valid local point transformed outside project bound rejected");
}

void payload_failures_propagate() {
  const auto valid = payload({{{0.0F, 0.0F, 0.0F}},
                              {{1.0F, 0.0F, 0.0F}},
                              {{0.0F, 1.0F, 0.0F}}},
                             {{{0U, 1U, 2U}}});
  std::vector<uint8_t> truncated = valid;
  truncated.pop_back();
  Prepared out = hss::mesh_anchor_project_batch::prepare(
      truncated, 3U, 1U, identity(), identity());
  expect(out.status == Status::kPayloadSizeMismatch,
         "truncation propagated");
  expect(out.project_vertices.empty(), "truncation no vertices");

  auto invalid_index = valid;
  const uint32_t bad = 3U;
  std::memcpy(invalid_index.data() + 36U, &bad, sizeof(bad));
  out = hss::mesh_anchor_project_batch::prepare(
      invalid_index, 3U, 1U, identity(), identity());
  expect(out.status == Status::kInvalidGeometry,
         "invalid index propagated");
  expect(out.project_vertices.empty(), "invalid index no vertices");
  expect(out.triangles.empty(), "invalid index no triangles");

  out = hss::mesh_anchor_project_batch::prepare(
      {}, hss::mesh_event_payload::kMaxVertices + 1U, 0U, identity(),
      identity());
  expect(out.status == Status::kResourceLimit, "vertex ceiling propagated");
}

void topology_independent_of_transform() {
  const std::vector<std::array<float, 3>> vertices{{{0.0F, 0.0F, 0.0F}},
                                                   {{1.0F, 0.0F, 0.0F}},
                                                   {{1.0F, 1.0F, 0.0F}},
                                                   {{0.0F, 1.0F, 0.0F}}};
  const std::vector<Triangle> triangles{{{0U, 1U, 2U}}, {{0U, 2U, 3U}}};
  const auto bytes = payload(vertices, triangles);
  const Prepared a = hss::mesh_anchor_project_batch::prepare(
      bytes, vertices.size(), triangles.size(), identity(), identity());
  const Prepared b = hss::mesh_anchor_project_batch::prepare(
      bytes, vertices.size(), triangles.size(), rotate_z_90(3.0, 4.0, 5.0),
      translate(-7.0, 8.0, -9.0));
  expect(a.status == Status::kOk && b.status == Status::kOk,
         "topology comparison statuses");
  expect(a.triangles == triangles, "identity topology retained");
  expect(b.triangles == triangles, "transformed topology retained");
  expect(a.triangles == b.triangles, "transform cannot rename local indices");
  expect(a.project_vertices.size() == b.project_vertices.size(),
         "transform preserves vertex count");
}

} // namespace

int main() {
  identity_round_trip();
  two_stage_transform();
  empty_and_vertex_only();
  invalid_transforms_fail_closed();
  project_extent_guard();
  payload_failures_propagate();
  topology_independent_of_transform();
  std::cout << "assertions=" << assertions << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}

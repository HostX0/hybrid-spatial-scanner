#include "hss/mesh_anchor_reconstruction_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {
using Triangle = hss::mesh_patch::Triangle;
using Vec = std::array<float, 3>;
int failures = 0;
int assertions = 0;

void expect(bool value, const char *message) {
  ++assertions;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

HSSMat4d identity() {
  HSSMat4d m{};
  m.column_major[0] = m.column_major[5] = m.column_major[10] =
      m.column_major[15] = 1.0;
  return m;
}

HSSMat4d rigid_z90(double tx, double ty, double tz) {
  HSSMat4d m{};
  m.column_major[0] = 0.0;
  m.column_major[1] = 1.0;
  m.column_major[4] = -1.0;
  m.column_major[5] = 0.0;
  m.column_major[10] = 1.0;
  m.column_major[12] = tx;
  m.column_major[13] = ty;
  m.column_major[14] = tz;
  m.column_major[15] = 1.0;
  return m;
}

std::vector<Vec> protrusion_vertices() {
  // Profile in X/Z: approach, 0.50 m return, 0.15 m front, 0.50 m return,
  // continuation. Extrusion along +Y reuses each vertical edge between faces.
  const std::array<std::array<float, 2>, 6> profile{{
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 0.5F},
      {1.15F, 0.5F}, {1.15F, 0.0F}, {2.0F, 0.0F}}};
  std::vector<Vec> out;
  for (const auto p : profile) {
    out.push_back({p[0], 0.0F, p[1]});
    out.push_back({p[0], 2.0F, p[1]});
  }
  return out;
}

std::vector<Triangle> protrusion_triangles() {
  std::vector<Triangle> out;
  for (uint32_t face = 0; face < 5; ++face) {
    const uint32_t a = face * 2U;
    const uint32_t b = a + 1U;
    const uint32_t c = a + 2U;
    const uint32_t d = a + 3U;
    out.push_back({a, c, b});
    out.push_back({b, c, d});
  }
  return out;
}

std::vector<uint8_t> payload(const std::vector<Vec> &vertices,
                             const std::vector<Triangle> &triangles) {
  std::vector<uint8_t> bytes(vertices.size() * 12U + triangles.size() * 12U);
  size_t offset = 0;
  for (const auto &v : vertices) {
    std::memcpy(bytes.data() + offset, v.data(), 12U);
    offset += 12U;
  }
  for (const auto &t : triangles) {
    std::memcpy(bytes.data() + offset, t.data(), 12U);
    offset += 12U;
  }
  return bytes;
}

Triangle canonical(Triangle t) {
  std::sort(t.begin(), t.end());
  return t;
}

std::map<Triangle, std::string> geometry_surface_map(
    const hss::mesh_anchor_reconstruction::Prepared &prepared) {
  std::map<Triangle, std::string> out;
  for (size_t i = 0; i < prepared.local_triangles.size(); ++i) {
    const auto *component =
        hss::mesh_anchor_reconstruction::component_for_triangle(prepared, i);
    if (component != nullptr)
      out.emplace(canonical(prepared.local_triangles[i]), component->surface_key);
  }
  return out;
}

void test_complete_anchor_and_permutations() {
  const auto vertices = protrusion_vertices();
  const auto original = protrusion_triangles();
  const auto base_payload = payload(vertices, original);
  const auto I = identity();
  const auto baseline = hss::mesh_anchor_reconstruction::prepare(
      base_payload, vertices.size(), original.size(), I, I, "anchor-support", 77U,
      0U);
  expect(baseline.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kOk,
         "baseline capture prepares");
  expect(baseline.metric_plan.status == hss::mesh_anchor_patch::Status::kOk,
         "baseline metric plan prepares");
  expect(baseline.metric_plan.components.size() == 5U,
         "protected protrusion keeps five surfaces");
  expect(baseline.raw_triangles == original,
         "zero-base raw triangles preserve exact topology/order");
  expect(baseline.project_vertices.size() == vertices.size(),
         "all project vertices retained");
  for (const auto &component : baseline.metric_plan.components)
    for (const auto &sample : component.fit_samples)
      expect(sample.diversity_group == 77U,
             "fit samples retain mesh-event diversity group");

  const auto expected_map = geometry_surface_map(baseline);
  expect(expected_map.size() == original.size(),
         "every nondegenerate triangle has a stable surface key");

  for (uint32_t seed = 0; seed < 32U; ++seed) {
    auto shuffled = original;
    std::mt19937 rng(seed + 901U);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    const auto bytes = payload(vertices, shuffled);
    const auto prepared = hss::mesh_anchor_reconstruction::prepare(
        bytes, vertices.size(), shuffled.size(), I, I, "anchor-support", 77U,
        500U);
    expect(prepared.capture_status ==
               hss::mesh_anchor_reconstruction::CaptureStatus::kOk,
           "permutation capture prepares");
    expect(prepared.metric_plan.status == hss::mesh_anchor_patch::Status::kOk,
           "permutation metric plan prepares");
    expect(prepared.metric_plan.components.size() == 5U,
           "permutation preserves five components");
    expect(geometry_surface_map(prepared) == expected_map,
           "geometry-to-surface binding is permutation invariant");
    expect(prepared.raw_triangles.size() == shuffled.size(),
           "rebased raw triangle count preserved");
    for (size_t i = 0; i < shuffled.size(); ++i) {
      Triangle expected = shuffled[i];
      for (auto &index : expected)
        index += 500U;
      expect(prepared.raw_triangles[i] == expected,
             "raw rebasing preserves input triangle order and indices");
    }
  }
}

void test_frame_and_global_base_independence() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto bytes = payload(vertices, triangles);
  const auto I = identity();
  const auto transformed = rigid_z90(3.0, -2.0, 1.0);
  const auto a = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), I, I, "same-anchor", 5U, 0U);
  const auto b = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), transformed, I, "same-anchor", 5U,
      4096U);
  expect(a.capture_status == hss::mesh_anchor_reconstruction::CaptureStatus::kOk &&
             b.capture_status == hss::mesh_anchor_reconstruction::CaptureStatus::kOk,
         "valid rigid frames prepare");
  expect(geometry_surface_map(a) == geometry_surface_map(b),
         "rigid frame/global base cannot rename anchor-local surfaces");
  expect(a.project_vertices.front().x != b.project_vertices.front().x ||
             a.project_vertices.front().y != b.project_vertices.front().y,
         "rigid transform changes derived coordinates");
  for (size_t i = 0; i < triangles.size(); ++i) {
    Triangle expected = triangles[i];
    for (auto &index : expected)
      index += 4096U;
    expect(b.raw_triangles[i] == expected,
           "global base affects only raw snapshot indices");
  }
}

void test_metric_failure_preserves_capture() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto bytes = payload(vertices, triangles);
  auto options = hss::mesh_patch::Options{};
  options.max_components = 1U;
  const auto I = identity();
  const auto prepared = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), I, I, "anchor-support", 8U, 9U,
      options);
  expect(prepared.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kOk,
         "metric resource failure does not invalidate capture");
  expect(prepared.metric_plan.status == hss::mesh_anchor_patch::Status::kResourceLimit,
         "metric component cap fails explicitly");
  expect(prepared.project_vertices.size() == vertices.size(),
         "project evidence retained after metric failure");
  expect(prepared.raw_triangles.size() == triangles.size(),
         "raw topology retained after metric failure");
  expect(hss::mesh_anchor_reconstruction::component_for_triangle(prepared, 0U) ==
             nullptr,
         "failed metric plan exposes no derived component");

  const auto empty_support = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), I, I, "", 8U, 9U);
  expect(empty_support.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kOk,
         "empty metric support does not erase valid capture");
  expect(empty_support.metric_plan.status ==
             hss::mesh_anchor_patch::Status::kInvalidArgument,
         "empty support fails metric plan explicitly");
  expect(empty_support.raw_triangles.size() == triangles.size(),
         "raw evidence survives invalid metric support identity");
}

void test_atomic_capture_failures() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto bytes = payload(vertices, triangles);
  const auto I = identity();

  auto truncated = bytes;
  truncated.pop_back();
  auto bad = hss::mesh_anchor_reconstruction::prepare(
      truncated, vertices.size(), triangles.size(), I, I, "support", 1U, 0U);
  expect(bad.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kPayloadSizeMismatch,
         "truncated payload fails before derived geometry");
  expect(bad.project_vertices.empty() && bad.local_triangles.empty() &&
             bad.raw_triangles.empty(),
         "payload failure exposes no partial geometry");

  HSSMat4d scaled = I;
  scaled.column_major[0] = 2.0;
  bad = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), scaled, I, "support", 1U, 0U);
  expect(bad.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kInvalidCoordinateFrame,
         "nonrigid transform fails capture preparation");
  expect(bad.raw_triangles.empty(),
         "invalid frame exposes no raw derived snapshot copy");

  bad = hss::mesh_anchor_reconstruction::prepare(
      bytes, vertices.size(), triangles.size(), I, I, "support", 1U,
      std::numeric_limits<uint32_t>::max() - 5U);
  expect(bad.capture_status ==
             hss::mesh_anchor_reconstruction::CaptureStatus::kRawIndexOverflow,
         "global raw-index overflow fails atomically");
  expect(bad.project_vertices.empty() && bad.local_triangles.empty() &&
             bad.raw_triangles.empty(),
         "raw-index overflow exposes no partial prepared anchor");
}

} // namespace

int main() {
  test_complete_anchor_and_permutations();
  test_frame_and_global_base_independence();
  test_metric_failure_preserves_capture();
  test_atomic_capture_failures();
  std::cout << "assertions=" << assertions << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}

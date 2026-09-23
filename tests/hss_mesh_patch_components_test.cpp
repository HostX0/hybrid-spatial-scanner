#include "hss/mesh_patch_components.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using hss::mesh_patch::Result;
using hss::mesh_patch::Status;
using hss::mesh_patch::Triangle;
using hss::mesh_patch::Vec3;

namespace {

struct Mesh {
  std::vector<Vec3> vertices;
  std::vector<Triangle> triangles;
};

Mesh grid(size_t nx, size_t ny, double cell, double noise_amplitude_m = 0.0) {
  Mesh mesh;
  mesh.vertices.resize((nx + 1) * (ny + 1));
  auto vid = [nx](size_t x, size_t y) {
    return static_cast<uint32_t>(y * (nx + 1) + x);
  };
  for (size_t y = 0; y <= ny; ++y)
    for (size_t x = 0; x <= nx; ++x) {
      const double z = noise_amplitude_m == 0.0
                           ? 0.0
                           : noise_amplitude_m * std::sin(double(x * 17 + y * 31));
      mesh.vertices[vid(x, y)] = {double(x) * cell, double(y) * cell, z};
    }
  for (size_t y = 0; y < ny; ++y)
    for (size_t x = 0; x < nx; ++x) {
      const auto a = vid(x, y), b = vid(x + 1, y), c = vid(x, y + 1),
                 d = vid(x + 1, y + 1);
      mesh.triangles.push_back({a, b, d});
      mesh.triangles.push_back({a, d, c});
    }
  return mesh;
}

std::vector<std::array<uint32_t, 3>> component_signature(const Result &r) {
  std::vector<std::array<uint32_t, 3>> out;
  for (const auto &c : r.components)
    out.push_back(c.canonical_triangle);
  return out;
}

std::vector<std::vector<uint32_t>> sample_signature(const Result &r) {
  std::vector<std::vector<uint32_t>> out;
  for (const auto &c : r.components)
    out.push_back(c.sample_vertex_indices);
  return out;
}

std::vector<std::vector<std::array<uint32_t, 3>>>
partition_signature(const Mesh &mesh, const Result &r) {
  std::vector<std::vector<std::array<uint32_t, 3>>> groups(r.components.size());
  for (size_t i = 0; i < mesh.triangles.size(); ++i) {
    if (r.component_of_triangle[i] == Result::npos)
      continue;
    auto t = mesh.triangles[i];
    std::sort(t.begin(), t.end());
    groups[r.component_of_triangle[i]].push_back(t);
  }
  for (auto &g : groups)
    std::sort(g.begin(), g.end());
  return groups;
}

Result grouped(const Mesh &mesh) {
  return hss::mesh_patch::group(mesh.vertices, mesh.triangles);
}

Mesh shuffled(Mesh mesh, uint32_t seed) {
  std::mt19937 rng(seed);
  std::shuffle(mesh.triangles.begin(), mesh.triangles.end(), rng);
  return mesh;
}

void expect_permutation_invariant(const Mesh &mesh, size_t expected_components) {
  const auto baseline = grouped(mesh);
  assert(baseline.status == Status::kOk);
  assert(baseline.components.size() == expected_components);
  const auto components = component_signature(baseline);
  const auto samples = sample_signature(baseline);
  const auto partition = partition_signature(mesh, baseline);
  for (uint32_t seed = 1; seed <= 32; ++seed) {
    const auto permuted_mesh = shuffled(mesh, seed);
    const auto permuted = grouped(permuted_mesh);
    assert(permuted.status == Status::kOk);
    assert(component_signature(permuted) == components);
    assert(sample_signature(permuted) == samples);
    assert(partition_signature(permuted_mesh, permuted) == partition);
  }
}

void test_coplanar_and_noisy_grid() {
  expect_permutation_invariant(grid(20, 10, 0.05), 1);
  // 0.10 mm deterministic vertex perturbation over a 50 mm cell stays within
  // the unchanged local normal gate and symmetric 2 mm cross-plane gate.
  expect_permutation_invariant(grid(20, 10, 0.05, 0.0001), 1);
}

Mesh two_face_crease(double degrees) {
  constexpr double pi = 3.14159265358979323846;
  const double a = degrees * pi / 180.0;
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                   {0, std::cos(a), std::sin(a)}};
  mesh.triangles = {{0, 1, 2}, {0, 3, 1}};
  return mesh;
}

void test_shallow_angle_is_stable() {
  // No angular threshold widening. The symmetric point-to-plane check also
  // keeps a one-metre 0.20 degree synthetic crease separate instead of letting
  // triangle order decide whether it merges.
  expect_permutation_invariant(two_face_crease(0.20), 2);
  expect_permutation_invariant(two_face_crease(0.50), 2);
}

Mesh five_face_protrusion() {
  // Connected continuation / 0.50 m return / 0.15 m front / 0.50 m return /
  // continuation chain, extruded vertically. Adjacent faces are distinct.
  Mesh mesh;
  const std::vector<Vec3> lower{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                {1.0, 0.0, 0.50}, {1.15, 0.0, 0.50},
                                {1.15, 0.0, 0.0}, {2.15, 0.0, 0.0}};
  for (const auto &p : lower) {
    mesh.vertices.push_back(p);
    mesh.vertices.push_back({p.x, 2.4, p.z});
  }
  for (uint32_t i = 0; i + 1 < lower.size(); ++i) {
    const uint32_t a = 2 * i, d = a + 1, b = a + 2, c = a + 3;
    mesh.triangles.push_back({a, b, c});
    mesh.triangles.push_back({a, c, d});
  }
  return mesh;
}

void test_protrusion_topology() {
  expect_permutation_invariant(five_face_protrusion(), 5);
}

void test_disconnected_and_offset_parallel_faces() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                   {2, 0, 0}, {3, 0, 0}, {2, 1, 0},
                   {0, 0, 0.02}, {1, 0, 0.02}, {0, 1, 0.02}};
  mesh.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
  expect_permutation_invariant(mesh, 3);
}

void test_non_manifold_edge_does_not_bridge() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                   {0, -1, 0}, {0, 0, 1}};
  mesh.triangles = {{0, 1, 2}, {1, 0, 3}, {0, 1, 4}};
  const auto r = grouped(mesh);
  assert(r.status == Status::kOk);
  assert(r.non_manifold_edges == 1);
  assert(r.components.size() == 3);
}

void test_sample_cap_is_stable() {
  const auto mesh = grid(20, 10, 0.05);
  const auto baseline = grouped(mesh);
  assert(baseline.status == Status::kOk);
  assert(baseline.components.size() == 1);
  assert(baseline.components[0].sample_vertex_indices.size() == 96);
  const auto expected = baseline.components[0].sample_vertex_indices;
  for (uint32_t seed = 1; seed <= 32; ++seed) {
    const auto permuted = grouped(shuffled(mesh, seed));
    assert(permuted.status == Status::kOk);
    assert(permuted.components[0].sample_vertex_indices == expected);
  }
}

void test_invalid_index_is_rejected() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh.triangles = {{0, 1, 3}};
  assert(grouped(mesh).status == Status::kInvalidGeometry);
}

void test_resource_limits_are_explicit() {
  const auto mesh = grid(2, 1, 0.05);
  hss::mesh_patch::Options options;
  options.max_triangles = 1;
  assert(hss::mesh_patch::group(mesh.vertices, mesh.triangles, options).status ==
         Status::kResourceLimit);
  options.max_triangles = 16;
  options.max_components = 1;
  const auto separated = two_face_crease(0.5);
  assert(hss::mesh_patch::group(separated.vertices, separated.triangles, options)
             .status == Status::kResourceLimit);
}

} // namespace

int main() {
  test_coplanar_and_noisy_grid();
  test_shallow_angle_is_stable();
  test_protrusion_topology();
  test_disconnected_and_offset_parallel_faces();
  test_non_manifold_edge_does_not_bridge();
  test_sample_cap_is_stable();
  test_invalid_index_is_rejected();
  test_resource_limits_are_explicit();
  std::cout << "mesh patch component tests: PASS\n";
  return 0;
}

#include "hss/mesh_patch_runtime_plan.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using hss::mesh_patch::Triangle;
using hss::mesh_patch::Vec3;
using hss::mesh_patch_runtime::Plan;
using hss::mesh_patch_runtime::Status;

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

Mesh five_face_protrusion() {
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

Mesh two_face_crease(double degrees) {
  constexpr double pi = 3.14159265358979323846;
  const double angle = degrees * pi / 180.0;
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                   {0, std::cos(angle), std::sin(angle)}};
  mesh.triangles = {{0, 1, 2}, {0, 3, 1}};
  return mesh;
}

Mesh shuffled(Mesh mesh, uint32_t seed) {
  std::mt19937 rng(seed);
  std::shuffle(mesh.triangles.begin(), mesh.triangles.end(), rng);
  return mesh;
}

std::vector<std::array<uint32_t, 3>> component_keys(const Plan &p) {
  std::vector<std::array<uint32_t, 3>> out;
  for (const auto &component : p.components)
    out.push_back(component.canonical_triangle);
  return out;
}

std::vector<std::vector<uint32_t>> samples(const Plan &p) {
  std::vector<std::vector<uint32_t>> out;
  for (const auto &component : p.components)
    out.push_back(component.sample_vertex_indices);
  return out;
}

std::vector<std::vector<std::array<uint32_t, 3>>>
partition(const Mesh &mesh, const Plan &p) {
  std::vector<std::vector<std::array<uint32_t, 3>>> out(p.components.size());
  for (size_t i = 0; i < mesh.triangles.size(); ++i) {
    const auto component = p.component_of_triangle[i];
    if (component == Plan::npos)
      continue;
    auto triangle = mesh.triangles[i];
    std::sort(triangle.begin(), triangle.end());
    out[component].push_back(triangle);
  }
  for (auto &group : out)
    std::sort(group.begin(), group.end());
  return out;
}

void assert_fit_samples_match(const Mesh &mesh, const Plan &p,
                              uint64_t diversity_group) {
  for (const auto &component : p.components) {
    assert(component.fit_samples.size() == component.sample_vertex_indices.size());
    for (size_t i = 0; i < component.fit_samples.size(); ++i) {
      const auto index = component.sample_vertex_indices[i];
      const auto &expected = mesh.vertices[index];
      const auto &actual = component.fit_samples[i];
      assert(actual.point.x == expected.x);
      assert(actual.point.y == expected.y);
      assert(actual.point.z == expected.z);
      assert(actual.weight == 1.0);
      assert(actual.diversity_group == diversity_group);
    }
  }
}

void expect_permutation_invariant(const Mesh &mesh, size_t expected_components,
                                  uint64_t diversity_group) {
  const auto baseline = hss::mesh_patch_runtime::prepare(
      mesh.vertices, mesh.triangles, diversity_group);
  assert(baseline.status == Status::kOk);
  assert(baseline.components.size() == expected_components);
  assert_fit_samples_match(mesh, baseline, diversity_group);
  const auto expected_keys = component_keys(baseline);
  const auto expected_samples = samples(baseline);
  const auto expected_partition = partition(mesh, baseline);
  for (uint32_t seed = 1; seed <= 32; ++seed) {
    const auto permuted_mesh = shuffled(mesh, seed);
    const auto plan = hss::mesh_patch_runtime::prepare(
        permuted_mesh.vertices, permuted_mesh.triangles, diversity_group);
    assert(plan.status == Status::kOk);
    assert(component_keys(plan) == expected_keys);
    assert(samples(plan) == expected_samples);
    assert(partition(permuted_mesh, plan) == expected_partition);
    assert_fit_samples_match(permuted_mesh, plan, diversity_group);
  }
}

void test_noisy_wall_and_sample_cap() {
  const auto mesh = grid(20, 10, 0.05, 0.0001);
  expect_permutation_invariant(mesh, 1, 73);
  const auto plan = hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 73);
  assert(plan.components[0].sample_vertex_indices.size() == 96);
  assert(plan.components[0].fit_samples.size() == 96);
}

void test_shallow_angle_mapping_is_stable() {
  expect_permutation_invariant(two_face_crease(0.20), 2, 83);
  expect_permutation_invariant(two_face_crease(0.50), 2, 84);
}

void test_protrusion_mapping_is_stable() {
  const auto mesh = five_face_protrusion();
  expect_permutation_invariant(mesh, 5, 91);
  const auto plan = hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 91);
  for (size_t triangle = 0; triangle < mesh.triangles.size(); ++triangle) {
    const size_t component = plan.component_of_triangle[triangle];
    assert(component != Plan::npos);
    const std::string suffix = hss::mesh_patch_runtime::canonical_component_suffix(
        plan.components[component].canonical_triangle);
    assert(!suffix.empty());
  }
}

void test_disconnected_and_offset_parallel_faces() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                   {2, 0, 0}, {3, 0, 0}, {2, 1, 0},
                   {0, 0, 0.02}, {1, 0, 0.02}, {0, 1, 0.02}};
  mesh.triangles = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
  expect_permutation_invariant(mesh, 3, 101);
}

void test_degenerate_mapping_is_explicit() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh.triangles = {{0, 1, 2}, {0, 0, 1}};
  const auto plan = hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 7);
  assert(plan.status == Status::kOk);
  assert(plan.degenerate_triangles == 1);
  assert(plan.component_of_triangle.size() == 2);
  assert(plan.component_of_triangle[0] != Plan::npos);
  assert(plan.component_of_triangle[1] == Plan::npos);
}

void test_fail_closed_status_mapping() {
  Mesh mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh.triangles = {{0, 1, 3}};
  assert(hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 1).status ==
         Status::kInvalidGeometry);

  mesh.triangles = {{0, 1, 2}};
  hss::mesh_patch::Options options;
  options.max_triangles = 0;
  assert(hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 1, options)
             .status == Status::kInvalidArgument);
  options.max_triangles = 1;
  options.max_components = 0;
  assert(hss::mesh_patch_runtime::prepare(mesh.vertices, mesh.triangles, 1, options)
             .status == Status::kInvalidArgument);
}

} // namespace

int main() {
  test_noisy_wall_and_sample_cap();
  test_shallow_angle_mapping_is_stable();
  test_protrusion_mapping_is_stable();
  test_disconnected_and_offset_parallel_faces();
  test_degenerate_mapping_is_explicit();
  test_fail_closed_status_mapping();
  std::cout << "mesh patch runtime plan tests: PASS\n";
  return 0;
}

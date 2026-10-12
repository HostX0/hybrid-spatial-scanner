#include "hss/mesh_anchor_patch_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace anchor_plan = hss::mesh_anchor_patch;
namespace mesh = hss::mesh_patch;

int failures = 0;
size_t checks = 0;

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct Fixture {
  std::vector<mesh::Vec3> vertices;
  std::vector<mesh::Triangle> triangles;
};

mesh::Triangle canonical(mesh::Triangle t) {
  std::sort(t.begin(), t.end());
  return t;
}

Fixture protrusion_chain() {
  // Plan chain lengths: 1.00 m approach, 0.50 m return, 0.15 m front,
  // 0.50 m return, 1.00 m continuation. Every segment is extruded 3 m high.
  const std::array<std::pair<double, double>, 6> plan{{
      {0.00, 0.00}, {1.00, 0.00}, {1.00, 0.50},
      {1.15, 0.50}, {1.15, 0.00}, {2.15, 0.00},
  }};
  Fixture f;
  for (const auto &[x, z] : plan) {
    f.vertices.push_back({x, 0.0, z});
    f.vertices.push_back({x, 3.0, z});
  }
  for (uint32_t i = 0; i + 1U < plan.size(); ++i) {
    const uint32_t b0 = i * 2U, t0 = b0 + 1U;
    const uint32_t b1 = (i + 1U) * 2U, t1 = b1 + 1U;
    f.triangles.push_back({b0, b1, t1});
    f.triangles.push_back({b0, t1, t0});
  }
  return f;
}

Fixture noisy_grid() {
  constexpr uint32_t cells = 12U;
  constexpr double step = 0.05;
  Fixture f;
  for (uint32_t y = 0; y <= cells; ++y) {
    for (uint32_t x = 0; x <= cells; ++x) {
      const int pattern = int((x * 17U + y * 31U) % 5U) - 2;
      const double z = double(pattern) * 0.000025; // +/- 0.05 mm.
      f.vertices.push_back({double(x) * step, double(y) * step, z});
    }
  }
  const uint32_t stride = cells + 1U;
  for (uint32_t y = 0; y < cells; ++y) {
    for (uint32_t x = 0; x < cells; ++x) {
      const uint32_t a = y * stride + x;
      const uint32_t b = a + 1U;
      const uint32_t c = a + stride;
      const uint32_t d = c + 1U;
      f.triangles.push_back({a, b, d});
      f.triangles.push_back({a, d, c});
    }
  }
  return f;
}

Fixture shallow_crease(double degrees) {
  const double radians = degrees * 3.14159265358979323846 / 180.0;
  Fixture f;
  // Shared vertical edge is vertices 1 and 2. The second quad rotates around
  // that edge, so point-to-plane separation grows with the one-metre width.
  f.vertices = {{-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
                {-1.0, 1.0, 0.0}, {std::cos(radians), 0.0, std::sin(radians)},
                {std::cos(radians), 1.0, std::sin(radians)}};
  f.triangles = {{0, 1, 2}, {0, 2, 3}, {1, 4, 5}, {1, 5, 2}};
  return f;
}

Fixture disconnected_parallel() {
  Fixture f;
  f.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                {0, 0, 0.02}, {1, 0, 0.02}, {1, 1, 0.02}, {0, 1, 0.02}};
  f.triangles = {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}};
  return f;
}

std::map<mesh::Triangle, std::string>
geometry_surface_map(const Fixture &f, const anchor_plan::Plan &plan) {
  std::map<mesh::Triangle, std::string> out;
  for (size_t i = 0; i < f.triangles.size(); ++i) {
    const auto *component = anchor_plan::component_for_triangle(plan, i);
    if (component != nullptr)
      out.emplace(canonical(f.triangles[i]), component->surface_key);
  }
  return out;
}

void verify_samples(const Fixture &f, const anchor_plan::Plan &plan,
                    uint64_t diversity_group) {
  for (const auto &component : plan.components) {
    check(component.sample_vertex_indices.size() == component.fit_samples.size(),
          "sample index and weighted-sample counts match");
    for (size_t i = 0; i < component.sample_vertex_indices.size(); ++i) {
      const auto vertex = component.sample_vertex_indices[i];
      const auto &expected = f.vertices[vertex];
      const auto &sample = component.fit_samples[i];
      check(sample.point.x == expected.x && sample.point.y == expected.y &&
                sample.point.z == expected.z,
            "fit sample preserves transformed project coordinate");
      check(sample.weight == 1.0 && sample.diversity_group == diversity_group,
            "fit sample preserves unit weight and mesh-event diversity group");
    }
  }
}

void verify_permutations(const Fixture &base, std::string support,
                         uint64_t diversity_group,
                         size_t expected_components,
                         const std::string &label) {
  const auto baseline = anchor_plan::prepare(base.vertices, base.triangles,
                                             support, diversity_group);
  check(baseline.status == anchor_plan::Status::kOk,
        label + " baseline prepares");
  check(baseline.components.size() == expected_components,
        label + " baseline component count");
  const auto expected = geometry_surface_map(base, baseline);
  for (uint64_t seed = 0; seed < 32U; ++seed) {
    Fixture permuted = base;
    std::mt19937_64 rng(0x5041544348000000ULL + seed);
    std::shuffle(permuted.triangles.begin(), permuted.triangles.end(), rng);
    const auto plan = anchor_plan::prepare(permuted.vertices, permuted.triangles,
                                           support, diversity_group);
    check(plan.status == anchor_plan::Status::kOk,
          label + " permutation prepares");
    check(plan.components.size() == expected_components,
          label + " permutation component count");
    check(geometry_surface_map(permuted, plan) == expected,
          label + " geometry-to-surface binding is order invariant");
  }
}

void permutation_regression() {
  const Fixture base = protrusion_chain();
  const uint64_t group = 77U;
  const auto baseline = anchor_plan::prepare(base.vertices, base.triangles,
                                             "support-A", group);
  check(baseline.status == anchor_plan::Status::kOk,
        "protrusion baseline prepares");
  check(baseline.components.size() == 5U,
        "0.15 m front and 0.50 m returns stay five faces");
  verify_samples(base, baseline, group);
  const auto expected = geometry_surface_map(base, baseline);

  for (uint64_t seed = 0; seed < 32U; ++seed) {
    Fixture permuted = base;
    std::mt19937_64 rng(0x4843535300000000ULL + seed);
    std::shuffle(permuted.triangles.begin(), permuted.triangles.end(), rng);
    const auto plan = anchor_plan::prepare(permuted.vertices, permuted.triangles,
                                           "support-A", group);
    check(plan.status == anchor_plan::Status::kOk,
          "permuted protrusion prepares");
    check(plan.components.size() == 5U,
          "permuted protrusion keeps five faces");
    check(geometry_surface_map(permuted, plan) == expected,
          "triangle order does not rename geometry surfaces");
  }
}

void anchor_local_identity_and_rebase() {
  const Fixture f = protrusion_chain();
  const auto plan = anchor_plan::prepare(f.vertices, f.triangles,
                                         "support-B", 99U);
  check(plan.status == anchor_plan::Status::kOk, "rebase plan prepares");
  const auto before = geometry_surface_map(f, plan);
  for (const auto &triangle : f.triangles) {
    const auto rebased = anchor_plan::rebase_raw_triangle(triangle, 1000U);
    check(rebased.has_value(), "bounded raw triangle rebases");
    if (rebased) {
      for (size_t i = 0; i < 3U; ++i)
        check((*rebased)[i] == triangle[i] + 1000U,
              "raw triangle rebase changes only snapshot vertex index");
    }
  }
  const auto after = geometry_surface_map(f, plan);
  check(before == after,
        "global snapshot vertex base cannot rename anchor-local surfaces");

  mesh::Triangle overflow{0U, 1U, 9U};
  const auto failed = anchor_plan::rebase_raw_triangle(
      overflow, std::numeric_limits<uint32_t>::max() - 4U);
  check(!failed.has_value(), "raw triangle rebase fails atomically on overflow");
}

void topology_regressions() {
  const Fixture wall = noisy_grid();
  const auto wall_plan = anchor_plan::prepare(wall.vertices, wall.triangles,
                                              "wall", 5U);
  check(wall_plan.status == anchor_plan::Status::kOk,
        "slightly noisy coplanar wall prepares");
  check(wall_plan.components.size() == 1U,
        "slightly noisy coplanar wall remains one connected component");
  check(wall_plan.components.size() == 1U &&
            wall_plan.components[0].sample_vertex_indices.size() == 96U,
        "large wall fit input remains deterministically capped at 96 vertices");
  verify_samples(wall, wall_plan, 5U);
  verify_permutations(wall, "wall", 5U, 1U, "noisy coplanar wall");

  for (const double degrees : {0.20, 0.50}) {
    const Fixture crease = shallow_crease(degrees);
    const auto plan = anchor_plan::prepare(crease.vertices, crease.triangles,
                                           "crease", 6U);
    check(plan.status == anchor_plan::Status::kOk,
          "shallow crease prepares");
    check(plan.components.size() == 2U,
          "supported shallow crease remains two surfaces");
    verify_permutations(crease, "crease", 6U, 2U,
                        "supported shallow crease");
  }

  const Fixture separate = disconnected_parallel();
  const auto separate_plan = anchor_plan::prepare(
      separate.vertices, separate.triangles, "parallel", 7U);
  check(separate_plan.status == anchor_plan::Status::kOk,
        "parallel fixture prepares");
  check(separate_plan.components.size() == 2U,
        "disconnected offset parallel faces never merge from normals alone");
  verify_permutations(separate, "parallel", 7U, 2U,
                      "disconnected offset parallel faces");
}

void fail_closed_regressions() {
  const Fixture f = protrusion_chain();
  const auto empty_support =
      anchor_plan::prepare(f.vertices, f.triangles, "", 1U);
  check(empty_support.status == anchor_plan::Status::kInvalidArgument &&
            empty_support.components.empty(),
        "empty support identity fails closed");

  Fixture invalid = f;
  invalid.triangles[0][0] = 9999U;
  const auto invalid_plan = anchor_plan::prepare(
      invalid.vertices, invalid.triangles, "invalid", 1U);
  check(invalid_plan.status == anchor_plan::Status::kInvalidGeometry &&
            invalid_plan.components.empty(),
        "out-of-range local index fails closed");

  mesh::Options limited;
  limited.max_components = 4U;
  const auto limited_plan = anchor_plan::prepare(
      f.vertices, f.triangles, "limited", 1U, limited);
  check(limited_plan.status == anchor_plan::Status::kResourceLimit &&
            limited_plan.components.empty(),
        "component resource limit fails closed");

  Fixture degenerate;
  degenerate.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  degenerate.triangles = {{0, 0, 1}, {0, 1, 2}};
  const auto degenerate_plan = anchor_plan::prepare(
      degenerate.vertices, degenerate.triangles, "degenerate", 2U);
  check(degenerate_plan.status == anchor_plan::Status::kOk,
        "degenerate evidence does not poison valid sibling geometry");
  check(degenerate_plan.degenerate_triangles == 1U &&
            anchor_plan::component_for_triangle(degenerate_plan, 0U) == nullptr &&
            anchor_plan::component_for_triangle(degenerate_plan, 1U) != nullptr,
        "degenerate triangle remains explicitly unbound");
  check(anchor_plan::rebase_raw_triangle(degenerate.triangles[0], 12U).has_value(),
        "degenerate but index-valid raw evidence can still be retained");
}

} // namespace

int main() {
  permutation_regression();
  anchor_local_identity_and_rebase();
  topology_regressions();
  fail_closed_regressions();
  if (failures != 0)
    return EXIT_FAILURE;
  std::cout << "mesh anchor patch plan tests passed: " << checks << " checks\n";
  return EXIT_SUCCESS;
}

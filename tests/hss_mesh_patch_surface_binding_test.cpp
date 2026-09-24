#include "hss/mesh_patch_surface_binding.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <map>
#include <numeric>
#include <string>
#include <vector>

using hss::mesh_patch::Triangle;
using hss::mesh_patch_binding::Status;
using hss::mesh_patch_runtime::ComponentPlan;
using hss::mesh_patch_runtime::Plan;

namespace {
ComponentPlan component(Triangle canonical, size_t count, uint32_t sample_base) {
  ComponentPlan out;
  out.canonical_triangle = canonical;
  out.triangle_count = count;
  for (uint32_t i = 0; i < 3; ++i) {
    out.sample_vertex_indices.push_back(sample_base + i);
    out.fit_samples.push_back({{double(sample_base + i), 0.0, 0.0}, 1.0, 17U});
  }
  return out;
}

Plan five_face_plan(const std::vector<size_t> &permutation) {
  Plan plan;
  plan.components = {
      component({0, 1, 2}, 2, 0),   component({3, 4, 5}, 2, 3),
      component({6, 7, 8}, 2, 6),   component({9, 10, 11}, 2, 9),
      component({12, 13, 14}, 2, 12)};
  const std::array<size_t, 10> base{0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
  plan.component_of_triangle.resize(permutation.size());
  for (size_t i = 0; i < permutation.size(); ++i)
    plan.component_of_triangle[i] = base[permutation[i]];
  return plan;
}

std::map<size_t, std::string> geometry_binding(const std::vector<size_t> &permutation,
                                               const Plan &plan) {
  const auto binding = hss::mesh_patch_binding::bind(plan);
  assert(binding.status == Status::kOk);
  std::map<size_t, std::string> out;
  for (size_t i = 0; i < permutation.size(); ++i) {
    auto key = hss::mesh_patch_binding::surface_key_for_triangle(
        binding, "anchor-support", i);
    assert(key.has_value());
    out.emplace(permutation[i], *key);
  }
  return out;
}
} // namespace

int main() {
  std::vector<size_t> canonical_order(10);
  std::iota(canonical_order.begin(), canonical_order.end(), 0U);
  const auto baseline = geometry_binding(canonical_order, five_face_plan(canonical_order));
  assert(baseline.size() == 10U);
  // The two triangles on each face bind to one stable surface, preserving all
  // five approach/return/front/return/continuation faces.
  assert(baseline.at(0) == baseline.at(1));
  assert(baseline.at(2) == baseline.at(3));
  assert(baseline.at(4) == baseline.at(5));
  assert(baseline.at(6) == baseline.at(7));
  assert(baseline.at(8) == baseline.at(9));
  assert(baseline.at(0) != baseline.at(2));
  assert(baseline.at(2) != baseline.at(4));
  assert(baseline.at(4) != baseline.at(6));
  assert(baseline.at(6) != baseline.at(8));

  // Exercise 32 deterministic triangle-list permutations. Geometry identity,
  // not arrival position, must select the same surface key.
  std::vector<size_t> order = canonical_order;
  for (size_t iteration = 0; iteration < 32U; ++iteration) {
    std::rotate(order.begin(), order.begin() +
                  static_cast<std::ptrdiff_t>((iteration * 3U) % order.size()),
                order.end());
    if ((iteration & 1U) != 0U)
      std::reverse(order.begin(), order.end());
    assert(geometry_binding(order, five_face_plan(order)) == baseline);
  }

  // Degenerate triangles remain intentionally unbound rather than inheriting a
  // neighboring surface identity.
  Plan with_degenerate;
  with_degenerate.components = {component({2, 4, 6}, 2, 2)};
  with_degenerate.component_of_triangle = {0, Plan::npos, 0};
  with_degenerate.degenerate_triangles = 1;
  auto degenerate = hss::mesh_patch_binding::bind(with_degenerate);
  assert(degenerate.status == Status::kOk);
  assert(!hss::mesh_patch_binding::surface_key_for_triangle(
              degenerate, "support", 1).has_value());
  assert(hss::mesh_patch_binding::surface_key_for_triangle(
             degenerate, "support", 0) ==
         std::optional<std::string>{"support:patch:2:4:6"});

  // Empty support identities cannot manufacture a surface key.
  assert(!hss::mesh_patch_binding::surface_key_for_triangle(
              degenerate, "", 0).has_value());

  // Fail closed if a corrupted adapter plan could associate a triangle with a
  // non-existent component.
  Plan bad_index = with_degenerate;
  bad_index.component_of_triangle = {1, Plan::npos, 0};
  assert(hss::mesh_patch_binding::bind(bad_index).status == Status::kInvalidPlan);

  Plan bad_count = with_degenerate;
  bad_count.components[0].triangle_count = 1;
  assert(hss::mesh_patch_binding::bind(bad_count).status == Status::kInvalidPlan);

  Plan bad_degenerate = with_degenerate;
  bad_degenerate.degenerate_triangles = 0;
  assert(hss::mesh_patch_binding::bind(bad_degenerate).status == Status::kInvalidPlan);

  Plan duplicate_identity;
  duplicate_identity.components = {component({1, 2, 3}, 1, 1),
                                   component({1, 2, 3}, 1, 4)};
  duplicate_identity.component_of_triangle = {0, 1};
  assert(hss::mesh_patch_binding::bind(duplicate_identity).status == Status::kInvalidPlan);

  Plan mismatched_samples = with_degenerate;
  mismatched_samples.components[0].fit_samples.pop_back();
  assert(hss::mesh_patch_binding::bind(mismatched_samples).status == Status::kInvalidPlan);

  Plan upstream;
  upstream.status = hss::mesh_patch_runtime::Status::kResourceLimit;
  upstream.component_of_triangle = {0};
  assert(hss::mesh_patch_binding::bind(upstream).status == Status::kUpstreamFailure);

  return 0;
}

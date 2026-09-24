#include "hss/mesh_anchor_cut_fragments.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
using Triangle = hss::mesh_anchor_cut::Triangle;
int assertions = 0;
int failures = 0;

void expect(bool value, const char *message) {
  ++assertions;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::vector<HSSVec3d> protrusion_vertices() {
  const std::array<std::array<double, 2>, 6> profile{{
      {0.0, 0.0}, {1.0, 0.0}, {1.0, 0.5},
      {1.15, 0.5}, {1.15, 0.0}, {2.0, 0.0}}};
  std::vector<HSSVec3d> out;
  for (const auto p : profile) {
    out.push_back({p[0], 0.0, p[1]});
    out.push_back({p[0], 2.0, p[1]});
  }
  return out;
}

std::vector<Triangle> protrusion_triangles() {
  std::vector<Triangle> out;
  for (uint32_t face = 0U; face < 5U; ++face) {
    const uint32_t a = face * 2U;
    const uint32_t b = a + 1U;
    const uint32_t c = a + 2U;
    const uint32_t d = a + 3U;
    out.push_back({a, c, b});
    out.push_back({b, c, d});
  }
  return out;
}

std::vector<std::string> protrusion_keys() {
  std::vector<std::string> out;
  for (uint32_t face = 0U; face < 5U; ++face) {
    const std::string key = "anchor-support:patch:face:" + std::to_string(face);
    out.push_back(key);
    out.push_back(key);
  }
  return out;
}

std::vector<std::string_view> views(const std::vector<std::string> &keys) {
  std::vector<std::string_view> out;
  out.reserve(keys.size());
  for (const auto &key : keys)
    out.push_back(key);
  return out;
}

using Signature = std::tuple<long long, long long, long long, long long, std::string>;
long long q(double value) { return std::llround(value * 1.0e12); }
std::vector<Signature> signature(const hss::mesh_anchor_cut::Result &result) {
  std::vector<Signature> out;
  for (const auto &fragment : result.fragments)
    out.emplace_back(q(fragment.start_uv.u_m), q(fragment.start_uv.v_m),
                     q(fragment.end_uv.u_m), q(fragment.end_uv.v_m),
                     fragment.surface_key);
  return out;
}

bool same_vertices(const std::vector<HSSVec3d> &a,
                   const std::vector<HSSVec3d> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z)
      return false;
  return true;
}

void test_protected_chain_and_permutations() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto keys = protrusion_keys();
  const auto key_views = views(keys);
  const auto floor = hss::coordinate::default_floor_frame();
  const auto baseline = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", floor, 1.20);
  expect(baseline.status == hss::mesh_anchor_cut::Status::kOk,
         "baseline cut prepares");
  expect(baseline.fragments.size() == 10U,
         "five two-triangle faces produce ten attributed cut fragments");
  expect(baseline.support_identity == "anchor-support",
         "support identity retained once for provenance");
  expect(baseline.skipped_unbound == 0U && baseline.skipped_degenerate == 0U &&
             baseline.skipped_no_crossing == 0U,
         "protected chain has no skipped metric triangles");
  std::set<std::string> surfaces;
  for (const auto &fragment : baseline.fragments) {
    surfaces.insert(fragment.surface_key);
    expect(std::abs(fragment.start_m.y - 1.20) < 1e-12 &&
               std::abs(fragment.end_m.y - 1.20) < 1e-12,
           "every fragment lies exactly on the requested project cut plane");
    expect(std::tie(fragment.start_uv.u_m, fragment.start_uv.v_m) <=
               std::tie(fragment.end_uv.u_m, fragment.end_uv.v_m),
           "fragment endpoint orientation is canonical in plan coordinates");
  }
  expect(surfaces.size() == 5U,
         "protected approach/return/front/return/continuation keep five surface keys");
  const auto expected = signature(baseline);

  for (uint32_t seed = 0U; seed < 32U; ++seed) {
    std::vector<size_t> order(triangles.size());
    for (size_t i = 0; i < order.size(); ++i)
      order[i] = i;
    std::mt19937 rng(seed + 20260924U);
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<Triangle> shuffled_triangles;
    std::vector<std::string> shuffled_keys;
    for (const size_t i : order) {
      shuffled_triangles.push_back(triangles[i]);
      shuffled_keys.push_back(keys[i]);
    }
    const auto shuffled_views = views(shuffled_keys);
    const auto cut = hss::mesh_anchor_cut::prepare(
        vertices, shuffled_triangles, shuffled_views, "anchor-support", floor,
        1.20);
    expect(cut.status == hss::mesh_anchor_cut::Status::kOk,
           "permuted cut prepares");
    expect(cut.fragments.size() == baseline.fragments.size(),
           "permutation preserves fragment count");
    expect(signature(cut) == expected,
           "sorted geometry-to-surface cut output is triangle-order invariant");
    for (const auto &fragment : cut.fragments)
      expect(fragment.source_triangle_index < shuffled_triangles.size() &&
                 fragment.local_triangle ==
                     shuffled_triangles[fragment.source_triangle_index],
             "fragment retains source triangle attribution after deterministic sorting");
  }
}

void test_frame_translation_invariance() {
  auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto keys = protrusion_keys();
  const auto key_views = views(keys);
  const auto floor = hss::coordinate::default_floor_frame();
  const auto baseline = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", floor, 1.20);
  for (auto &p : vertices) {
    p.x += 3.0;
    p.y += 4.0;
    p.z -= 2.0;
  }
  auto translated_floor = floor;
  translated_floor.origin_m = {3.0, 4.0, -2.0};
  const auto translated = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", translated_floor, 1.20);
  expect(translated.status == hss::mesh_anchor_cut::Status::kOk,
         "translated project frame prepares");
  expect(signature(translated) == signature(baseline),
         "moving geometry and floor frame together preserves canonical plan cut");
}

void test_half_open_and_unbound_rules() {
  const auto floor = hss::coordinate::default_floor_frame();
  {
    std::vector<HSSVec3d> vertices{{0.0, 1.20, 0.0},
                                   {1.0, 1.20, 0.0},
                                   {0.0, 1.20, 1.0}};
    std::vector<Triangle> triangles{{0, 1, 2}};
    std::vector<std::string> keys{"support:patch:surface"};
    const auto key_views = views(keys);
    const auto result = hss::mesh_anchor_cut::prepare(
        vertices, triangles, key_views, "support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kOk,
           "coplanar cut fixture accepted");
    expect(result.fragments.empty() && result.skipped_no_crossing == 1U,
           "coplanar triangle does not invent a wall cut");
  }
  {
    std::vector<HSSVec3d> vertices{{0.0, 0.0, 0.0},
                                   {1.0, 2.0, 0.0},
                                   {0.0, 1.20, 1.0}};
    std::vector<Triangle> triangles{{0, 1, 2}};
    std::vector<std::string> keys{"support:patch:surface"};
    const auto key_views = views(keys);
    const auto result = hss::mesh_anchor_cut::prepare(
        vertices, triangles, key_views, "support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kOk &&
               result.fragments.size() == 1U,
           "half-open rule keeps a real crossing with one vertex on the plane");
  }
  {
    const auto vertices = protrusion_vertices();
    const auto triangles = protrusion_triangles();
    std::vector<std::string> keys(triangles.size());
    const auto key_views = views(keys);
    const auto result = hss::mesh_anchor_cut::prepare(
        vertices, triangles, key_views, "support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kOk,
           "unbound fixture accepted");
    expect(result.fragments.empty() &&
               result.skipped_unbound == triangles.size(),
           "unbound triangles cannot create metric cut fragments");
  }
  {
    std::vector<HSSVec3d> vertices{{0.0, 0.0, 0.0},
                                   {0.0, 2.0, 0.0},
                                   {1.0, 2.0, 0.0}};
    std::vector<Triangle> triangles{{0, 0, 1}};
    std::vector<std::string> keys{""};
    const auto key_views = views(keys);
    const auto result = hss::mesh_anchor_cut::prepare(
        vertices, triangles, key_views, "support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kOk,
           "degenerate captured triangle remains a valid raw input shape");
    expect(result.fragments.empty() && result.skipped_degenerate == 1U,
           "degenerate triangle remains excluded from metric cut topology");
  }
}

void test_fail_closed_validation_and_resource_bound() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto keys = protrusion_keys();
  const auto key_views = views(keys);
  const auto floor = hss::coordinate::default_floor_frame();

  auto result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "empty support identity rejected");

  auto short_views = key_views;
  short_views.pop_back();
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, short_views, "support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "triangle-to-surface binding count mismatch rejected");

  auto foreign_keys = keys;
  foreign_keys[0] = "other-support:patch:face:0";
  const auto foreign_views = views(foreign_keys);
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, foreign_views, "anchor-support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument &&
             result.fragments.empty(),
         "foreign support surface key cannot cross-bind into this anchor cut");

  auto empty_suffix_keys = keys;
  empty_suffix_keys[0] = "anchor-support:patch:";
  const auto empty_suffix_views = views(empty_suffix_keys);
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, empty_suffix_views, "anchor-support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument &&
             result.fragments.empty(),
         "empty finalized surface suffix is rejected");

  {
    std::vector<HSSVec3d> degenerate_vertices{{0.0, 0.0, 0.0},
                                              {0.0, 2.0, 0.0}};
    std::vector<Triangle> degenerate_triangles{{0, 0, 1}};
    std::vector<std::string> incorrectly_bound{"anchor-support:patch:0:0:1"};
    const auto incorrectly_bound_views = views(incorrectly_bound);
    result = hss::mesh_anchor_cut::prepare(
        degenerate_vertices, degenerate_triangles, incorrectly_bound_views,
        "anchor-support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument &&
               result.fragments.empty(),
           "degenerate triangle cannot carry finalized metric surface binding");
  }

  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "support", floor, 0.0);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "nonpositive cut rejected");
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "support", floor, 100.0001);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "cut above live contract bound rejected");
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", floor, 1.20,
      hss::mesh_anchor_cut::kDefaultMaxFragments + 1U);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "caller cannot raise the live metric fragment ceiling");
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "support", floor,
      std::numeric_limits<double>::quiet_NaN());
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidArgument,
         "nonfinite cut rejected");

  auto bad_floor = floor;
  bad_floor.u = {2.0, 0.0, 0.0};
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "support", bad_floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidCoordinateFrame,
         "nonorthonormal floor frame rejected");

  bad_floor = floor;
  bad_floor.origin_m.x = 10'000.1;
  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", bad_floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidCoordinateFrame,
         "floor origin outside existing project extent rejected");

  auto bad_triangles = triangles;
  bad_triangles[0][0] = 999U;
  result = hss::mesh_anchor_cut::prepare(
      vertices, bad_triangles, key_views, "support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidGeometry &&
             result.fragments.empty(),
         "invalid triangle index fails closed without partial fragments");

  auto bad_vertices = vertices;
  bad_vertices[0].x = 10'000.1;
  result = hss::mesh_anchor_cut::prepare(
      bad_vertices, triangles, key_views, "support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidGeometry,
         "project vertex outside existing extent rejected");
  bad_vertices = vertices;
  bad_vertices[0].z = std::numeric_limits<double>::infinity();
  result = hss::mesh_anchor_cut::prepare(
      bad_vertices, triangles, key_views, "support", floor, 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kInvalidGeometry,
         "nonfinite project vertex rejected");

  {
    std::vector<HSSVec3d> too_many_vertices(
        hss::mesh_anchor_cut::kMaxVertices + 1U);
    std::vector<Triangle> no_triangles;
    std::vector<std::string_view> no_keys;
    result = hss::mesh_anchor_cut::prepare(
        too_many_vertices, no_triangles, no_keys, "anchor-support", floor, 1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kResourceLimit,
           "vertex ceiling fails before unbounded cut work");
  }
  {
    std::vector<Triangle> too_many_triangles(
        hss::mesh_anchor_cut::kMaxTriangles + 1U, Triangle{0U, 0U, 0U});
    std::vector<std::string_view> too_many_keys(too_many_triangles.size());
    result = hss::mesh_anchor_cut::prepare(
        vertices, too_many_triangles, too_many_keys, "anchor-support", floor,
        1.20);
    expect(result.status == hss::mesh_anchor_cut::Status::kResourceLimit,
           "triangle ceiling fails before unbounded cut work");
  }

  result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support", floor, 1.20, 1U);
  expect(result.status == hss::mesh_anchor_cut::Status::kResourceLimit &&
             result.fragments.empty(),
         "fragment cap fails closed without exposing a partial derived cut");
}

void test_input_immutability() {
  const auto vertices = protrusion_vertices();
  const auto triangles = protrusion_triangles();
  const auto keys = protrusion_keys();
  const auto vertex_copy = vertices;
  const auto triangle_copy = triangles;
  const auto key_copy = keys;
  const auto key_views = views(keys);
  const auto result = hss::mesh_anchor_cut::prepare(
      vertices, triangles, key_views, "anchor-support",
      hss::coordinate::default_floor_frame(), 1.20);
  expect(result.status == hss::mesh_anchor_cut::Status::kOk,
         "immutability fixture prepares");
  expect(same_vertices(vertices, vertex_copy) && triangles == triangle_copy &&
             keys == key_copy,
         "cut preparation does not mutate source geometry or binding data");
}

} // namespace

int main() {
  test_protected_chain_and_permutations();
  test_frame_translation_invariance();
  test_half_open_and_unbound_rules();
  test_fail_closed_validation_and_resource_bound();
  test_input_immutability();
  std::cout << "assertions=" << assertions << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}

#include "hss/mesh_anchor_scene_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace graph = hss::mesh_anchor_scene_graph;
namespace cut = hss::mesh_anchor_cut;
namespace surface = hss::mesh_anchor_surface;

static size_t assertions = 0;
static size_t failures = 0;

static void check(bool condition, const std::string &message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

static HSSVec3d p(double u, double v, double y = 1.2) {
  return {u, y, -v};
}

static hss::coordinate::PlanPoint uv(double u, double v) { return {u, v}; }

struct Fixture {
  cut::Result cuts;
  surface::Result surfaces;
};

static std::string key(size_t logical) {
  return "support-A:patch:" + std::to_string(logical + 10U) + ":" +
         std::to_string(logical + 20U) + ":" + std::to_string(logical + 30U);
}

static cut::Fragment fragment(size_t logical) {
  // approach -> 0.50m return -> 0.15m front -> 0.50m return -> continuation
  static const std::pair<double, double> a[] = {
      {0.0, 0.0}, {1.0, 0.0}, {1.0, 0.50}, {1.15, 0.50}, {1.15, 0.0}};
  static const std::pair<double, double> b[] = {
      {1.0, 0.0}, {1.0, 0.50}, {1.15, 0.50}, {1.15, 0.0}, {2.0, 0.0}};
  cut::Fragment f;
  f.source_triangle_index = 100U + logical;
  f.local_triangle = {uint32_t(logical * 3U), uint32_t(logical * 3U + 1U),
                      uint32_t(logical * 3U + 2U)};
  f.start_m = p(a[logical].first, a[logical].second);
  f.end_m = p(b[logical].first, b[logical].second);
  f.start_uv = uv(a[logical].first, a[logical].second);
  f.end_uv = uv(b[logical].first, b[logical].second);
  f.surface_key = key(logical);
  return f;
}

static Fixture make_fixture(const std::vector<size_t> &order,
                            bool reverse_surfaces = false) {
  Fixture f;
  f.cuts.status = cut::Status::kOk;
  f.cuts.support_identity = "support-A";
  for (size_t logical : order)
    f.cuts.fragments.push_back(fragment(logical));

  f.surfaces.status = surface::Status::kOk;
  std::vector<size_t> surface_order{0, 1, 2, 3, 4};
  if (reverse_surfaces)
    std::reverse(surface_order.begin(), surface_order.end());
  for (size_t logical : surface_order) {
    surface::Surface s;
    s.surface_key = key(logical);
    s.support_identity = "support-A";
    s.canonical_triangle = {uint32_t(logical * 3U),
                            uint32_t(logical * 3U + 1U),
                            uint32_t(logical * 3U + 2U)};
    s.triangle_count = 1U;
    auto it = std::find(order.begin(), order.end(), logical);
    s.cut_fragment_indices = {size_t(std::distance(order.begin(), it))};
    f.surfaces.surfaces.push_back(std::move(s));
  }
  return f;
}

static std::string graph_signature(const graph::Result &r) {
  std::ostringstream os;
  os << std::setprecision(17);
  os << "nodes=" << r.nodes.size() << ';';
  for (const auto &n : r.nodes) {
    os << n.node_key << '@' << n.point_m.x << ',' << n.point_m.y << ','
       << n.point_m.z << '/' << n.uv.u_m << ',' << n.uv.v_m << '[';
    for (const auto &s : n.surface_keys)
      os << s << ',';
    os << "]deg=" << n.edge_indices.size() << ';';
  }
  os << "edges=" << r.edges.size() << ';';
  for (const auto &e : r.edges) {
    os << e.edge_key << ':' << e.a << '-' << e.b << '[';
    for (const auto &s : e.surface_keys)
      os << s << ',';
    os << "];";
  }
  return os.str();
}

static void check_fixture_graph(const graph::Result &r,
                                const std::string &prefix) {
  check(r.status == graph::Status::kOk, prefix + " status ok");
  check(r.nodes.size() == 6U, prefix + " six protected-chain nodes");
  check(r.edges.size() == 5U, prefix + " five protected-chain edges");
  check(r.merged_endpoint_count == 4U,
        prefix + " four shared endpoints welded");
  check(r.merged_edge_fragment_count == 0U,
        prefix + " no distinct protected edge collapsed");
  check(r.multi_surface_edge_count == 0U,
        prefix + " no protected edge loses surface identity");
  for (size_t i = 0; i < r.edges.size(); ++i) {
    const auto &e = r.edges[i];
    check(e.a < e.b, prefix + " canonical edge orientation " + std::to_string(i));
    check(e.surface_keys.size() == 1U,
          prefix + " exactly one surface per fixture edge " + std::to_string(i));
    check(e.source_fragment_indices.size() == 1U,
          prefix + " exactly one fragment per fixture edge " + std::to_string(i));
    check(e.source_triangle_indices.size() == 1U,
          prefix + " source triangle retained " + std::to_string(i));
    check(r.nodes[e.a].edge_indices.end() !=
              std::find(r.nodes[e.a].edge_indices.begin(),
                        r.nodes[e.a].edge_indices.end(), i),
          prefix + " start incidence retained " + std::to_string(i));
    check(r.nodes[e.b].edge_indices.end() !=
              std::find(r.nodes[e.b].edge_indices.begin(),
                        r.nodes[e.b].edge_indices.end(), i),
          prefix + " end incidence retained " + std::to_string(i));
  }
  check(r.nodes.front().edge_indices.size() == 1U,
        prefix + " approach endpoint degree one");
  check(r.nodes.back().edge_indices.size() == 1U,
        prefix + " continuation endpoint degree one");
  size_t degree_two = 0U;
  for (const auto &n : r.nodes)
    degree_two += n.edge_indices.size() == 2U ? 1U : 0U;
  check(degree_two == 4U, prefix + " four interior degree-two nodes");
}

static void permutation_regression() {
  const std::vector<size_t> identity{0, 1, 2, 3, 4};
  auto baseline_fixture = make_fixture(identity);
  const auto baseline = graph::assemble(baseline_fixture.cuts,
                                        baseline_fixture.surfaces, "support-A");
  check_fixture_graph(baseline, "baseline");
  const auto signature = graph_signature(baseline);

  std::mt19937 rng(0x51A7E123U);
  for (size_t iteration = 0U; iteration < 32U; ++iteration) {
    auto order = identity;
    std::shuffle(order.begin(), order.end(), rng);
    auto f = make_fixture(order, (iteration & 1U) != 0U);
    const auto result = graph::assemble(f.cuts, f.surfaces, "support-A");
    check_fixture_graph(result, "perm " + std::to_string(iteration));
    check(graph_signature(result) == signature,
          "permutation preserves stable geometry/surface graph " +
              std::to_string(iteration));
    for (const auto &edge : result.edges) {
      check(std::is_sorted(edge.surface_keys.begin(), edge.surface_keys.end()),
            "surface keys sorted under permutation " + std::to_string(iteration));
      check(std::is_sorted(edge.source_fragment_indices.begin(),
                           edge.source_fragment_indices.end()),
            "fragment provenance sorted under permutation " +
                std::to_string(iteration));
      check(std::is_sorted(edge.source_triangle_indices.begin(),
                           edge.source_triangle_indices.end()),
            "triangle provenance sorted under permutation " +
                std::to_string(iteration));
    }
  }
}

static Fixture jitter_join_fixture(bool reverse) {
  Fixture f;
  f.cuts.status = cut::Status::kOk;
  f.cuts.support_identity = "support-A";
  cut::Fragment a;
  a.source_triangle_index = 7U;
  a.local_triangle = {0, 1, 2};
  a.start_m = p(0, 0);
  a.end_m = p(1.0 + 4.0e-8, 0);
  a.start_uv = uv(0, 0);
  a.end_uv = uv(1.0 + 4.0e-8, 0);
  a.surface_key = key(0);
  cut::Fragment b;
  b.source_triangle_index = 8U;
  b.local_triangle = {3, 4, 5};
  b.start_m = p(1.0 - 4.0e-8, 0);
  b.end_m = p(2, 0);
  b.start_uv = uv(1.0 - 4.0e-8, 0);
  b.end_uv = uv(2, 0);
  b.surface_key = key(1);
  if (reverse)
    f.cuts.fragments = {b, a};
  else
    f.cuts.fragments = {a, b};
  f.surfaces.status = surface::Status::kOk;
  for (size_t logical = 0; logical < 2; ++logical) {
    surface::Surface s;
    s.surface_key = key(logical);
    s.support_identity = "support-A";
    const size_t idx = reverse ? 1U - logical : logical;
    s.cut_fragment_indices = {idx};
    f.surfaces.surfaces.push_back(std::move(s));
  }
  return f;
}

static void canonical_join_regression() {
  auto f1 = jitter_join_fixture(false);
  auto f2 = jitter_join_fixture(true);
  auto a = graph::assemble(f1.cuts, f1.surfaces, "support-A");
  auto b = graph::assemble(f2.cuts, f2.surfaces, "support-A");
  check(a.status == graph::Status::kOk, "jitter forward accepted");
  check(b.status == graph::Status::kOk, "jitter reverse accepted");
  check(a.nodes.size() == 3U && b.nodes.size() == 3U,
        "join cell welds near-identical endpoint");
  check(a.edges.size() == 2U && b.edges.size() == 2U,
        "join cell keeps both incident edges");
  check(graph_signature(a) == graph_signature(b),
        "representative is order-invariant");
  const auto it = std::find_if(a.nodes.begin(), a.nodes.end(), [](const auto &n) {
    return n.edge_indices.size() == 2U;
  });
  check(it != a.nodes.end(), "joined interior node found");
  if (it != a.nodes.end()) {
    check(std::abs(it->point_m.x - (1.0 - 4.0e-8)) < 1e-15,
          "joined node uses lexicographically smallest observed endpoint");
    check(std::abs(it->uv.u_m - (1.0 - 4.0e-8)) < 1e-15,
          "joined plan point matches selected observed endpoint");
    check(it->surface_keys.size() == 2U,
          "joined node retains both contributing surfaces");
  }
}

static void coincident_surface_regression() {
  Fixture f;
  f.cuts.status = cut::Status::kOk;
  f.cuts.support_identity = "support-A";
  auto a = fragment(0);
  auto b = a;
  b.source_triangle_index = 901U;
  b.local_triangle = {9, 10, 11};
  b.surface_key = key(1);
  f.cuts.fragments = {a, b};
  f.surfaces.status = surface::Status::kOk;
  for (size_t i = 0; i < 2; ++i) {
    surface::Surface s;
    s.surface_key = key(i);
    s.support_identity = "support-A";
    s.cut_fragment_indices = {i};
    f.surfaces.surfaces.push_back(std::move(s));
  }
  auto r = graph::assemble(f.cuts, f.surfaces, "support-A");
  check(r.status == graph::Status::kOk, "coincident multi-surface edge accepted");
  check(r.nodes.size() == 2U, "coincident fragments share endpoints");
  check(r.edges.size() == 1U, "coincident fragments aggregate to one edge");
  check(r.merged_edge_fragment_count == 1U,
        "coincident edge reports merged fragment");
  check(r.multi_surface_edge_count == 1U,
        "coincident edge reports multiple supporting surfaces");
  check(r.edges[0].surface_keys.size() == 2U,
        "coincident edge preserves both surface keys");
  check(r.edges[0].source_triangle_indices.size() == 2U,
        "coincident edge preserves both source triangles");
  check(r.edges[0].source_fragment_indices.size() == 2U,
        "coincident edge preserves both fragment indices");
  check(r.nodes[0].surface_keys.size() == 2U &&
            r.nodes[1].surface_keys.size() == 2U,
        "coincident nodes preserve both supporting surfaces");
}

static void failure_regressions() {
  const std::vector<size_t> identity{0, 1, 2, 3, 4};
  auto good = make_fixture(identity);

  auto cuts = good.cuts;
  cuts.status = cut::Status::kInvalidGeometry;
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kCutUnavailable,
        "cut failure propagates");

  auto surfaces = good.surfaces;
  surfaces.status = surface::Status::kInvalidPlan;
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kSurfaceUnavailable,
        "surface failure propagates");

  check(graph::assemble(good.cuts, good.surfaces, "").status ==
            graph::Status::kInvalidArgument,
        "empty support rejected");
  check(graph::assemble(good.cuts, good.surfaces, "support-B").status ==
            graph::Status::kInvalidArgument,
        "cut support mismatch rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].support_identity = "support-B";
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "surface support mismatch rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].surface_key = "support-A:wrong:1";
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "malformed stable surface key rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[1].surface_key = surfaces.surfaces[0].surface_key;
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "duplicate surface identity rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].cut_fragment_indices.clear();
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "unowned cut fragment rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].cut_fragment_indices.push_back(1U);
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "duplicate cut ownership rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].cut_fragment_indices = {99U};
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "out-of-range cut ownership rejected");

  surfaces = good.surfaces;
  surfaces.surfaces[0].cut_fragment_indices = {1U};
  surfaces.surfaces[1].cut_fragment_indices = {0U};
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "surface-to-fragment identity mismatch rejected");

  cuts = good.cuts;
  cuts.fragments[0].surface_key = "support-B:patch:1";
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kInvalidPlan,
        "cut key corruption caught by ownership validation");

  cuts = good.cuts;
  cuts.fragments[0].start_m.x = std::numeric_limits<double>::quiet_NaN();
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kInvalidGeometry,
        "nonfinite endpoint rejected");

  cuts = good.cuts;
  cuts.fragments[0].start_m.x = 10000.000001;
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kInvalidGeometry,
        "out-of-range endpoint rejected");

  cuts = good.cuts;
  cuts.fragments[0].start_uv.u_m = std::numeric_limits<double>::infinity();
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kInvalidGeometry,
        "nonfinite plan coordinate rejected");

  graph::Options options;
  options.max_nodes = 1U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kResourceLimit,
        "node resource limit fail closed");

  options = {};
  options.max_edges = 4U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kResourceLimit,
        "edge resource limit fail closed");

  options = {};
  options.max_nodes = graph::kDefaultMaxNodes + 1U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kInvalidArgument,
        "node cap cannot exceed runtime bound");

  options = {};
  options.max_edges = graph::kDefaultMaxEdges + 1U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kInvalidArgument,
        "edge cap cannot exceed runtime bound");

  options = {};
  options.max_nodes = 0U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kInvalidArgument,
        "zero node capacity rejected");

  options = {};
  options.max_edges = 0U;
  check(graph::assemble(good.cuts, good.surfaces, "support-A", options).status ==
            graph::Status::kInvalidArgument,
        "zero edge capacity rejected");

  cuts = good.cuts;
  cuts.fragments.resize(graph::kDefaultMaxEdges + 1U, cuts.fragments.front());
  check(graph::assemble(cuts, good.surfaces, "support-A").status ==
            graph::Status::kResourceLimit,
        "corrupt over-cap cut result rejected before allocation");

  surfaces = good.surfaces;
  surfaces.surfaces.resize(graph::kDefaultMaxEdges + 1U,
                           surfaces.surfaces.front());
  check(graph::assemble(good.cuts, surfaces, "support-A").status ==
            graph::Status::kResourceLimit,
        "corrupt over-cap surface result rejected before allocation");

  Fixture collapsed;
  collapsed.cuts.status = cut::Status::kOk;
  collapsed.cuts.support_identity = "support-A";
  cut::Fragment tiny;
  tiny.source_triangle_index = 1;
  tiny.local_triangle = {0, 1, 2};
  tiny.start_m = p(0, 0);
  tiny.end_m = p(4e-8, 0);
  tiny.start_uv = uv(0, 0);
  tiny.end_uv = uv(4e-8, 0);
  tiny.surface_key = key(0);
  collapsed.cuts.fragments = {tiny};
  collapsed.surfaces.status = surface::Status::kOk;
  surface::Surface ss;
  ss.surface_key = key(0);
  ss.support_identity = "support-A";
  ss.cut_fragment_indices = {0};
  collapsed.surfaces.surfaces = {ss};
  check(graph::assemble(collapsed.cuts, collapsed.surfaces, "support-A").status ==
            graph::Status::kInvalidGeometry,
        "same join-cell edge rejected instead of inventing topology");
}

static void immutability_regression() {
  const std::vector<size_t> identity{0, 1, 2, 3, 4};
  auto f = make_fixture(identity);
  const auto cuts_before = f.cuts.fragments;
  std::vector<std::string> keys_before;
  std::vector<std::vector<size_t>> fragment_map_before;
  for (const auto &s : f.surfaces.surfaces) {
    keys_before.push_back(s.surface_key);
    fragment_map_before.push_back(s.cut_fragment_indices);
  }
  auto r = graph::assemble(f.cuts, f.surfaces, "support-A");
  check(r.status == graph::Status::kOk, "immutability fixture assembles");
  check(f.cuts.fragments.size() == cuts_before.size(), "cut size unchanged");
  for (size_t i = 0; i < cuts_before.size(); ++i) {
    check(f.cuts.fragments[i].surface_key == cuts_before[i].surface_key,
          "cut surface key unchanged " + std::to_string(i));
    check(f.cuts.fragments[i].source_triangle_index ==
              cuts_before[i].source_triangle_index,
          "cut source triangle unchanged " + std::to_string(i));
    check(f.cuts.fragments[i].start_m.x == cuts_before[i].start_m.x &&
              f.cuts.fragments[i].start_m.y == cuts_before[i].start_m.y &&
              f.cuts.fragments[i].start_m.z == cuts_before[i].start_m.z &&
              f.cuts.fragments[i].end_m.x == cuts_before[i].end_m.x &&
              f.cuts.fragments[i].end_m.y == cuts_before[i].end_m.y &&
              f.cuts.fragments[i].end_m.z == cuts_before[i].end_m.z,
          "cut geometry unchanged " + std::to_string(i));
  }
  for (size_t i = 0; i < f.surfaces.surfaces.size(); ++i) {
    check(f.surfaces.surfaces[i].surface_key == keys_before[i],
          "surface key unchanged " + std::to_string(i));
    check(f.surfaces.surfaces[i].cut_fragment_indices == fragment_map_before[i],
          "surface cut map unchanged " + std::to_string(i));
  }
}

int main() {
  permutation_regression();
  canonical_join_regression();
  coincident_surface_regression();
  failure_regressions();
  immutability_regression();
  std::cout << "assertions=" << assertions << " failures=" << failures << '\n';
  return failures == 0U ? 0 : 1;
}

#include "hss/mesh_anchor_scene_entities.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace scene = hss::mesh_anchor_scene_entities;
namespace graph = hss::mesh_anchor_scene_graph;
namespace surface = hss::mesh_anchor_surface;
namespace fit = hss::plane_fit;

static size_t assertions = 0U;
static size_t failures = 0U;
#define CHECK(expr) do { ++assertions; if (!(expr)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #expr "\n"; } } while (false)

static std::string live_uuid(const std::array<uint8_t,16> &bytes) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < bytes.size(); ++i) {
    s += hex[bytes[i] >> 4U];
    s += hex[bytes[i] & 15U];
    if (i == 3U || i == 5U || i == 7U || i == 9U) s += '-';
  }
  return s;
}

static std::string live_id(const std::string &key) {
  uint64_t a = 1469598103934665603ULL, b = 1099511628211ULL;
  for (unsigned char c : key) {
    a = (a ^ c) * 1099511628211ULL;
    b = (b + c + 17U) * 14029467366897019727ULL;
  }
  std::array<uint8_t,16> bytes{};
  for (size_t i = 0; i < 8U; ++i) {
    bytes[i] = static_cast<uint8_t>(a >> (8U * i));
    bytes[i + 8U] = static_cast<uint8_t>(b >> (8U * i));
  }
  bytes[6] = static_cast<uint8_t>((bytes[6] & 15U) | 128U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 63U) | 128U);
  return live_uuid(bytes);
}

static const std::string kSupport = "11111111-1111-8111-8111-111111111111";

static std::string surface_key(size_t i) {
  return kSupport + ":patch:" + std::to_string(i * 3U) + ":" +
         std::to_string(i * 3U + 1U) + ":" + std::to_string(i * 3U + 2U);
}

struct Fixture { graph::Result graph; surface::Result surfaces; };

static Fixture fixture() {
  Fixture f;
  f.graph.status = graph::Status::kOk;
  f.surfaces.status = surface::Status::kOk;
  const std::array<HSSVec3d,6> points{{
    {0.0,1.2,0.0}, {1.0,1.2,0.0}, {1.0,1.2,0.5},
    {1.15,1.2,0.5}, {1.15,1.2,0.0}, {2.0,1.2,0.0}
  }};
  for (size_t i = 0; i < points.size(); ++i) {
    graph::Node n;
    n.cell = {static_cast<int64_t>(i * 10U), 12, 0};
    n.point_m = points[i];
    n.uv = {points[i].x, points[i].z};
    n.node_key = "node-cell:" + std::to_string(i * 10U) + ":12:0";
    f.graph.nodes.push_back(std::move(n));
  }
  for (size_t i = 0; i < 5U; ++i) {
    graph::Edge e;
    e.a = i;
    e.b = i + 1U;
    e.edge_key = "cut-edge:" + f.graph.nodes[e.a].node_key + "|" + f.graph.nodes[e.b].node_key;
    e.surface_keys = {surface_key(i)};
    e.source_fragment_indices = {i};
    e.source_triangle_indices = {i * 2U};
    f.graph.edges.push_back(e);
    f.graph.nodes[e.a].edge_indices.push_back(i);
    f.graph.nodes[e.b].edge_indices.push_back(i);
    f.graph.nodes[e.a].surface_keys.push_back(surface_key(i));
    f.graph.nodes[e.b].surface_keys.push_back(surface_key(i));

    surface::Surface s;
    s.surface_key = surface_key(i);
    s.support_identity = kSupport;
    s.canonical_triangle = {static_cast<uint32_t>(i * 3U), static_cast<uint32_t>(i * 3U + 1U), static_cast<uint32_t>(i * 3U + 2U)};
    s.triangle_count = 2U;
    s.fit_status = i == 4U ? fit::Status::kInsufficientEvidence : fit::Status::kOk;
    if (i != 4U) {
      fit::Plane p;
      p.normal = {0.0, 0.0, 1.0};
      p.offset_m = -0.1 * static_cast<double>(i);
      p.residuals.weighted_rms_m = 0.0005 + 0.0001 * static_cast<double>(i);
      s.plane = p;
      ++f.surfaces.fitted_surface_count;
    } else {
      ++f.surfaces.fallback_surface_count;
    }
    s.cut_fragment_indices = {i};
    f.surfaces.surfaces.push_back(std::move(s));
  }
  for (auto &node : f.graph.nodes) {
    std::sort(node.surface_keys.begin(), node.surface_keys.end());
    node.surface_keys.erase(std::unique(node.surface_keys.begin(), node.surface_keys.end()), node.surface_keys.end());
  }
  return f;
}

static graph::Result permute_graph(const graph::Result &source, size_t seed) {
  graph::Result out = source;
  std::vector<size_t> np(source.nodes.size()), ep(source.edges.size());
  for (size_t i = 0; i < np.size(); ++i) np[i] = i;
  for (size_t i = 0; i < ep.size(); ++i) ep[i] = i;
  std::rotate(np.begin(), np.begin() + static_cast<std::ptrdiff_t>(seed % np.size()), np.end());
  std::rotate(ep.begin(), ep.begin() + static_cast<std::ptrdiff_t>((seed * 3U) % ep.size()), ep.end());
  if (seed & 1U) std::reverse(np.begin(), np.end());
  if (seed & 2U) std::reverse(ep.begin(), ep.end());
  std::vector<size_t> old_to_new_node(np.size()), old_to_new_edge(ep.size());
  for (size_t i = 0; i < np.size(); ++i) old_to_new_node[np[i]] = i;
  for (size_t i = 0; i < ep.size(); ++i) old_to_new_edge[ep[i]] = i;
  out.nodes.clear(); out.nodes.reserve(np.size());
  for (size_t old : np) {
    auto n = source.nodes[old];
    for (auto &edge_index : n.edge_indices) edge_index = old_to_new_edge[edge_index];
    std::sort(n.edge_indices.begin(), n.edge_indices.end());
    out.nodes.push_back(std::move(n));
  }
  out.edges.clear(); out.edges.reserve(ep.size());
  for (size_t old : ep) {
    auto e = source.edges[old];
    e.a = old_to_new_node[e.a];
    e.b = old_to_new_node[e.b];
    out.edges.push_back(std::move(e));
  }
  return out;
}

static surface::Result permute_surfaces(const surface::Result &source, size_t seed) {
  surface::Result out = source;
  std::rotate(out.surfaces.begin(), out.surfaces.begin() + static_cast<std::ptrdiff_t>(seed % out.surfaces.size()), out.surfaces.end());
  if (seed & 1U) std::reverse(out.surfaces.begin(), out.surfaces.end());
  return out;
}

static void check_success_and_permutations() {
  const Fixture f = fixture();
  std::map<std::string,size_t> calls;
  auto mapper = [&](const std::string &key) { ++calls[key]; return live_id(key); };
  auto baseline = scene::assemble(f.graph, f.surfaces, kSupport, mapper);
  CHECK(baseline.status == scene::Status::kOk);
  CHECK(baseline.nodes.size() == 6U);
  CHECK(baseline.edges.size() == 5U);
  CHECK(baseline.surfaces.size() == 5U);
  CHECK(calls.size() == 16U);
  for (const auto &[key,count] : calls) { (void)key; CHECK(count == 1U); }
  CHECK(baseline.nodes_json.find("\"position_locked\":false") != std::string::npos);
  CHECK(baseline.edges_json.find("\"protected_topology\":true") != std::string::npos);
  CHECK(baseline.surfaces_json.find("\"kind\":\"plane\"") != std::string::npos);
  CHECK(baseline.surfaces_json.find("\"kind\":\"mesh_patch\"") != std::string::npos);
  CHECK(baseline.surfaces_json.find("\"boundary_node_ids\":[]") != std::string::npos);
  CHECK(baseline.surfaces_json.find("\"mesh_asset\":null") != std::string::npos);
  CHECK(baseline.nodes_json.find(":patch:") == std::string::npos);
  CHECK(baseline.edges_json.find(":patch:") == std::string::npos);
  CHECK(baseline.surfaces_json.find(":patch:") == std::string::npos);
  CHECK(baseline.emitted_json_bytes == baseline.nodes_json.size() + baseline.edges_json.size() + baseline.surfaces_json.size());
  for (size_t seed = 0; seed < 32U; ++seed) {
    auto g = permute_graph(f.graph, seed);
    auto s = permute_surfaces(f.surfaces, seed);
    auto r = scene::assemble(g, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kOk);
    CHECK(r.nodes_json == baseline.nodes_json);
    CHECK(r.edges_json == baseline.edges_json);
    CHECK(r.surfaces_json == baseline.surfaces_json);
    CHECK(r.nodes.size() == baseline.nodes.size());
    CHECK(r.edges.size() == baseline.edges.size());
    CHECK(r.surfaces.size() == baseline.surfaces.size());
  }
}

static void check_multi_surface_edge() {
  Fixture f = fixture();
  const std::string second = f.surfaces.surfaces[1].surface_key;
  f.graph.edges[0].surface_keys.push_back(second);
  std::sort(f.graph.edges[0].surface_keys.begin(), f.graph.edges[0].surface_keys.end());
  f.graph.edges[0].source_fragment_indices.push_back(1U);
  std::sort(f.graph.edges[0].source_fragment_indices.begin(), f.graph.edges[0].source_fragment_indices.end());
  for (size_t n : {0U,1U}) {
    f.graph.nodes[n].surface_keys.push_back(second);
    std::sort(f.graph.nodes[n].surface_keys.begin(), f.graph.nodes[n].surface_keys.end());
    f.graph.nodes[n].surface_keys.erase(std::unique(f.graph.nodes[n].surface_keys.begin(), f.graph.nodes[n].surface_keys.end()), f.graph.nodes[n].surface_keys.end());
  }
  auto r = scene::assemble(f.graph, f.surfaces, kSupport, live_id);
  CHECK(r.status == scene::Status::kOk);
  CHECK(r.edges[0].surface_ids.size() == 2U);
  CHECK(r.edges_json.find(live_id(surface_key(0))) != std::string::npos);
  CHECK(r.edges_json.find(live_id(surface_key(1))) != std::string::npos);
}

static void check_fail_closed() {
  const Fixture base = fixture();
  {
    auto r = scene::assemble(base.graph, base.surfaces, "NOT-A-UUID", live_id);
    CHECK(r.status == scene::Status::kInvalidArgument);
    CHECK(r.nodes.empty() && r.nodes_json.empty());
  }
  {
    auto s = base.surfaces; s.status = surface::Status::kInvalidPlan;
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kSurfaceUnavailable);
  }
  {
    auto g = base.graph; g.status = graph::Status::kInvalidPlan;
    auto r = scene::assemble(g, base.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kGraphUnavailable);
  }
  {
    auto s = base.surfaces; s.surfaces[0].surface_key = kSupport + ":wrong:0";
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto g = base.graph; g.nodes[0].surface_keys.push_back("unknown"); std::sort(g.nodes[0].surface_keys.begin(), g.nodes[0].surface_keys.end());
    auto r = scene::assemble(g, base.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto g = base.graph; g.edges[0].a = 99U;
    auto r = scene::assemble(g, base.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto g = base.graph; g.nodes[0].edge_indices.clear();
    auto r = scene::assemble(g, base.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto g = base.graph; g.edges[0].source_fragment_indices = {99U};
    auto r = scene::assemble(g, base.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto s = base.surfaces; s.surfaces[0].cut_fragment_indices = {99U};
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto s = base.surfaces; s.surfaces[0].plane->normal = {2.0,0.0,0.0};
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidGeometry);
  }
  {
    auto s = base.surfaces; s.surfaces[0].plane->residuals.weighted_rms_m = -1.0;
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidGeometry);
  }
  {
    auto s = base.surfaces; s.surfaces[0].triangle_count = 0U;
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto s = base.surfaces; s.surfaces[0].fit_status = fit::Status::kInsufficientEvidence;
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto s = base.surfaces; s.surfaces[4].fit_status = fit::Status::kOk;
    auto r = scene::assemble(base.graph, s, kSupport, live_id);
    CHECK(r.status == scene::Status::kInvalidPlan);
  }
  {
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, [](const std::string&) { return std::string("aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa"); });
    CHECK(r.status == scene::Status::kIdentityFailure);
    CHECK(r.nodes.empty() && r.edges.empty() && r.surfaces.empty());
  }
  {
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, [](const std::string&) { return std::string("BAD-ID"); });
    CHECK(r.status == scene::Status::kIdentityFailure);
  }
  {
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, [](const std::string&) -> std::string { throw 7; });
    CHECK(r.status == scene::Status::kIdentityFailure);
  }
  {
    bool threw = false;
    try {
      (void)scene::assemble(base.graph, base.surfaces, kSupport, [](const std::string&) -> std::string { throw std::bad_alloc(); });
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, [](const std::string&) -> std::string { throw std::bad_alloc(); });
    CHECK(r.status == scene::Status::kResourceLimit);
  }
  {
    scene::Options o; o.max_nodes = 5U;
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, live_id, o);
    CHECK(r.status == scene::Status::kResourceLimit);
  }
  {
    scene::Options o; o.max_json_bytes = 64U;
    auto r = scene::assemble(base.graph, base.surfaces, kSupport, live_id, o);
    CHECK(r.status == scene::Status::kResourceLimit);
    CHECK(r.nodes.empty() && r.edges.empty() && r.surfaces.empty());
    CHECK(r.nodes_json.empty() && r.edges_json.empty() && r.surfaces_json.empty());
  }
}

static bool same_point(const HSSVec3d &a, const HSSVec3d &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

static bool same_plan_point(const hss::coordinate::PlanPoint &a,
                            const hss::coordinate::PlanPoint &b) {
  return a.u_m == b.u_m && a.v_m == b.v_m;
}

static bool same_graph_input(const graph::Result &a, const graph::Result &b) {
  if (a.status != b.status || a.merged_endpoint_count != b.merged_endpoint_count ||
      a.merged_edge_fragment_count != b.merged_edge_fragment_count ||
      a.multi_surface_edge_count != b.multi_surface_edge_count ||
      a.nodes.size() != b.nodes.size() || a.edges.size() != b.edges.size())
    return false;
  for (size_t i = 0; i < a.nodes.size(); ++i) {
    const auto &x = a.nodes[i];
    const auto &y = b.nodes[i];
    if (!(x.cell == y.cell) || !same_point(x.point_m, y.point_m) ||
        !same_plan_point(x.uv, y.uv) || x.node_key != y.node_key ||
        x.surface_keys != y.surface_keys || x.edge_indices != y.edge_indices)
      return false;
  }
  for (size_t i = 0; i < a.edges.size(); ++i) {
    const auto &x = a.edges[i];
    const auto &y = b.edges[i];
    if (x.a != y.a || x.b != y.b || x.edge_key != y.edge_key ||
        x.surface_keys != y.surface_keys ||
        x.source_fragment_indices != y.source_fragment_indices ||
        x.source_triangle_indices != y.source_triangle_indices)
      return false;
  }
  return true;
}

static bool same_surface_inputs(const std::vector<surface::Surface> &a,
                                const std::vector<surface::Surface> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto &x = a[i];
    const auto &y = b[i];
    if (x.surface_key != y.surface_key ||
        x.support_identity != y.support_identity ||
        x.canonical_triangle != y.canonical_triangle ||
        x.triangle_count != y.triangle_count || x.fit_status != y.fit_status ||
        x.plane != y.plane ||
        x.cut_fragment_indices != y.cut_fragment_indices)
      return false;
  }
  return true;
}

static void check_input_immutability() {
  Fixture f = fixture();
  const auto graph_before = f.graph;
  const auto surfaces_before = f.surfaces.surfaces;
  auto r = scene::assemble(f.graph, f.surfaces, kSupport, live_id);
  CHECK(r.status == scene::Status::kOk);
  CHECK(same_graph_input(f.graph, graph_before));
  CHECK(same_surface_inputs(f.surfaces.surfaces, surfaces_before));
}

int main(int argc, char **argv) {
  check_success_and_permutations();
  check_multi_surface_edge();
  check_fail_closed();
  check_input_immutability();
  if (argc == 2 && std::string(argv[1]) == "--emit-json") {
    const Fixture f = fixture();
    const auto r = scene::assemble(f.graph, f.surfaces, kSupport, live_id);
    CHECK(r.status == scene::Status::kOk);
    std::cout << "NODES_JSON=" << r.nodes_json << "\n";
    std::cout << "EDGES_JSON=" << r.edges_json << "\n";
    std::cout << "SURFACES_JSON=" << r.surfaces_json << "\n";
  }
  std::cout << "assertions=" << assertions << " failures=" << failures << "\n";
  return failures == 0U ? 0 : 1;
}

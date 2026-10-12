#include "hss/live_reconstruction.hpp"
#include "hss/boundary_graph.hpp"
#include "hss/coordinate_basis.hpp"
#include "hss/ring_net_area.hpp"
#include "hss/robust_plane_fit.hpp"
#include "hss/topology_protection.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace hss::live {
namespace {
constexpr size_t kMaxAnchors = 128, kMaxEpochs = 32, kMaxFrames = 256;
constexpr size_t kMaxVertices = 65536, kMaxTriangles = 65536, kMaxMetric = 2048;
constexpr double kExtent = 10000, kJoin = 1e-7;
constexpr double kPlaneSupportM = 0.002;
using Key = std::string;
std::string uuid(const HSSId &id) {
  const char *hex = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < 16; ++i) {
    s += hex[id.bytes[i] >> 4];
    s += hex[id.bytes[i] & 15];
    if (i == 3 || i == 5 || i == 7 || i == 9)
      s += '-';
  }
  return s;
}
// Stable deterministic derived identity, not a content-integrity digest.
std::string id(const std::string &key) {
  uint64_t a = 1469598103934665603ULL, b = 1099511628211ULL;
  for (unsigned char c : key) {
    a = (a ^ c) * 1099511628211ULL;
    b = (b + c + 17) * 14029467366897019727ULL;
  }
  HSSId v{};
  for (size_t i = 0; i < 8; ++i) {
    v.bytes[i] = uint8_t(a >> (8 * i));
    v.bytes[i + 8] = uint8_t(b >> (8 * i));
  }
  v.bytes[6] = (v.bytes[6] & 15) | 128;
  v.bytes[8] = (v.bytes[8] & 63) | 128;
  return uuid(v);
}
std::string quote(const std::string &s) { return "\"" + s + "\""; }
std::string num(double d) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  s << std::setprecision(17) << d;
  return s.str();
}
std::string vec(HSSVec3d p) {
  return "[" + num(p.x) + "," + num(p.y) + "," + num(p.z) + "]";
}
HSSVec3d sub(HSSVec3d a, HSSVec3d b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
HSSVec3d add(HSSVec3d a, HSSVec3d b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
HSSVec3d mul(HSSVec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(HSSVec3d a, HSSVec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
HSSVec3d cross(HSSVec3d a, HSSVec3d b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool bounded(HSSVec3d p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= kExtent;
}
Key epoch_key(const ingress::Event &e) {
  return uuid(e.capture_id) + ":" + std::to_string(e.session_epoch);
}
Key anchor_key(const ingress::Event &e) {
  return epoch_key(e) + ":" + uuid(e.anchor_id);
}
std::string provenance(const std::vector<std::string> &supports,
                       const char *origin = "fitted") {
  std::string s = "{\"geometry_origin\":" + quote(origin) +
                  ",\"semantic_origin\":\"unknown\",\"support_ids\":[";
  for (size_t i = 0; i < supports.size(); ++i) {
    if (i)
      s += ',';
    s += quote(supports[i]);
  }
  return s + "],\"uncertainty_m\":null,\"quality_state\":\"provisional\"}";
}
struct Anchor {
  std::shared_ptr<Evidence> source;
  uint64_t generation{1};
};
struct State {
  std::map<Key, std::shared_ptr<Evidence>> epochs;
  std::map<Key, Anchor> anchors;
  std::map<Key, uint64_t> sequences;
  std::vector<std::shared_ptr<Evidence>> frames;
};
struct Patch {
  std::string identity, support;
  std::vector<plane_fit::WeightedSample> samples;
  std::optional<plane_fit::Plane> plane;
  uint64_t source_sequence{};
  size_t expected_triangles{}, checked_triangles{};
  bool support_failed{false};
};

using TriangleKey = std::array<uint32_t, 3>;

TriangleKey canonical_triangle(TriangleKey triangle) {
  std::sort(triangle.begin(), triangle.end());
  return triangle;
}

uint64_t splitmix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::string triangle_suffix(const TriangleKey &triangle) {
  return std::to_string(triangle[0]) + ":" + std::to_string(triangle[1]) +
         ":" + std::to_string(triangle[2]);
}

struct FaceWork {
  TriangleKey triangle{};
  TriangleKey canonical{};
  HSSVec3d normal{};
  double offset{};
  bool valid{false};
};

struct EdgeIncidence {
  size_t count{};
  std::array<size_t, 2> faces{};
};

struct ComponentWork {
  TriangleKey canonical{
      std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::max()};
  size_t triangle_count{};
  size_t representative{std::numeric_limits<size_t>::max()};
  bool ambiguous{false};
  bool metric_skipped{false};
  size_t patch_index{std::numeric_limits<size_t>::max()};
  std::map<uint32_t, uint64_t> sample_ranks;
  std::string surface;
};

struct Node {
  HSSVec3d point;
  boundary::Point2d uv;
  std::string identity;
  std::vector<std::string> supports;
  std::vector<size_t> edges;
};
struct Edge {
  size_t a{}, b{};
  std::string identity, support, surface;
};
struct Region {
  std::vector<size_t> nodes;
  double area{};
  std::string identity;
  std::vector<std::string> supports;
};
} // namespace
std::shared_ptr<Lease> reserve(const std::shared_ptr<Budget> &b,
                               uint64_t bytes) {
  if (bytes > b->limit || b->used > b->limit - bytes)
    return {};
  auto lease = std::make_shared<Lease>();
  lease->budget = b;
  lease->bytes = bytes;
  b->used += bytes;
  return lease;
}
uint64_t event_reservation(const ingress::Event &e) {
  return e.payload_bytes + 2048;
}
struct Reconstruction::Impl {
  HSSConfig config;
  std::shared_ptr<Budget> budget;
  State state;
  std::shared_ptr<const Snapshot> accepted;
  bool synthetic = false, sealed = false, floor_confirmed = false;
  coordinate::FloorFrame floor = coordinate::default_floor_frame();
  double cut = 1.20;
  HSSStatus last = HSS_OK;
  struct Build {
    State state;
    std::shared_ptr<Snapshot> out;
    std::vector<std::pair<Key, Anchor>> anchors;
    size_t anchor = 0, vertex = 0, triangle = 0, base = 0, patch_index = 0,
           item = 0, loop_seed = 0;
    int phase = 0;
    int anchor_stage = 0;
    bool metric_limit = false;
    std::vector<Patch> patches;

    using EdgeMap =
        std::map<std::pair<uint32_t, uint32_t>, EdgeIncidence>;
    using ComponentMap = std::map<size_t, ComponentWork>;
    using CanonicalRoots = std::map<TriangleKey, std::vector<size_t>>;
    using FaceOrder = std::map<TriangleKey, std::vector<size_t>>;

    std::vector<FaceWork> faces;
    EdgeMap edge_incidence;
    EdgeMap::iterator edge_it{};
    std::vector<size_t> dsu_parent;
    std::vector<size_t> dsu_size;
    std::vector<TriangleKey> dsu_key;
    size_t face_index = 0;
    size_t anchor_fit_index = 0;
    ComponentMap components;
    ComponentMap::iterator component_it{};
    CanonicalRoots canonical_roots;
    CanonicalRoots::iterator canonical_it{};
    FaceOrder face_order;
    FaceOrder::iterator slice_it{};
    size_t slice_subindex = 0;
    size_t retained_constraint_index = 0, retained_constraint_count = 0;
    size_t canonical_subindex = 0;
    size_t degenerate_triangles = 0;
    size_t non_manifold_edges = 0;
    size_t ambiguous_components = 0;

    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Region> regions;
    std::map<std::tuple<long long, long long, long long>, size_t> node_lookup;
    std::set<std::pair<size_t, size_t>> edge_lookup;
    std::vector<bool> visited;
    std::vector<std::string> surfaces, issues;
    std::string epoch_json;
    bool plan_change = false;
    coordinate::FloorFrame old_floor{};
    double old_cut = 1.20;
    bool old_confirmed = false;
  };
  std::unique_ptr<Build> build;
  HSSStatus start_build(State candidate);
  explicit Impl(const HSSConfig &c)
      : config(c),
        budget(std::make_shared<Budget>(Budget{c.max_working_bytes, 0})) {}
  std::string basis() const {
    return "{\"handedness\":\"right\",\"up\":\"+Y\",\"floor_origin_m\":" +
           vec(floor.origin_m) + ",\"floor_u\":" + vec(floor.u) +
           ",\"floor_v\":" + vec(floor.v) + ",\"floor_n\":" + vec(floor.n) +
           "}";
  }
  std::string prefix(uint64_t rev) const {
    return "{\"schema_version\":\"1.1\",\"project_id\":" +
           quote(uuid(config.project_id)) +
           ",\"scene_revision\":" + quote(std::to_string(rev)) +
           ",\"source_is_synthetic\":" + (synthetic ? "true" : "false") +
           ",\"units\":\"m\",\"basis\":" + basis();
  }
  void empty() {
    auto s = std::make_shared<Snapshot>();
    s->json = prefix(0) +
              ",\"epochs\":[],\"nodes\":[],\"edges\":[],\"surfaces\":[],"
              "\"features\":[],\"regions\":[],\"objects\":[],\"constraints\":[]"
              ",\"issues\":[],\"quality_state\":\"provisional\"}";
    accepted = std::move(s);
  }
  void issue(const std::string &code, const std::string &reason,
             uint64_t seq = 0, const std::string &target = "") {
    auto &b = *build;
    b.issues.push_back(
        "{\"id\":" + quote(id(code + reason)) + ",\"code\":" + quote(code) +
        ",\"target_ids\":[" + (target.empty() ? "" : quote(target)) +
        "],\"scene_revision\":" + quote(std::to_string(b.out->revision)) +
        ",\"severity\":\"warning\",\"reason\":" + quote(reason) +
        ",\"action\":\"Review retained source evidence and capture missing "
        "support\",\"state\":\"active\",\"source_sequence\":" +
        quote(std::to_string(seq)) + "}");
  }
  std::string support(const Key &key, const Anchor &a) {
    return id(key + ":" + std::to_string(a.generation) + ":" +
              std::to_string(a.source->event.anchor_revision) + ":" +
              std::to_string(a.source->event.event_sequence));
  }

  size_t dsu_find(size_t index) {
    auto &b = *build;
    while (b.dsu_parent[index] != index) {
      b.dsu_parent[index] = b.dsu_parent[b.dsu_parent[index]];
      index = b.dsu_parent[index];
    }
    return index;
  }

  void dsu_unite(size_t left, size_t right) {
    auto &b = *build;
    left = dsu_find(left);
    right = dsu_find(right);
    if (left == right)
      return;
    if (b.dsu_size[left] < b.dsu_size[right] ||
        (b.dsu_size[left] == b.dsu_size[right] &&
         b.dsu_key[right] < b.dsu_key[left]))
      std::swap(left, right);
    b.dsu_parent[right] = left;
    b.dsu_size[left] += b.dsu_size[right];
    b.dsu_key[left] = std::min(b.dsu_key[left], b.dsu_key[right]);
  }

  void consider_sample(ComponentWork &component, uint32_t vertex) {
    if (component.sample_ranks.contains(vertex))
      return;
    const uint64_t rank =
        splitmix64(config.deterministic_seed ^ uint64_t(vertex));
    if (component.sample_ranks.size() < 96U) {
      component.sample_ranks.emplace(vertex, rank);
      return;
    }
    auto worst = std::max_element(
        component.sample_ranks.begin(), component.sample_ranks.end(),
        [](const auto &left, const auto &right) {
          return std::pair{left.second, left.first} <
                 std::pair{right.second, right.first};
        });
    if (std::pair{rank, vertex} < std::pair{worst->second, worst->first}) {
      component.sample_ranks.erase(worst);
      component.sample_ranks.emplace(vertex, rank);
    }
  }

  static bool opposite_vertex(const FaceWork &face, uint32_t edge_a,
                              uint32_t edge_b, uint32_t *out) {
    for (uint32_t vertex : face.triangle)
      if (vertex != edge_a && vertex != edge_b) {
        *out = vertex;
        return true;
      }
    return false;
  }

  bool compatible_faces(size_t left_index, size_t right_index,
                        uint32_t edge_a, uint32_t edge_b) {
    auto &b = *build;
    const auto &left = b.faces[left_index];
    const auto &right = b.faces[right_index];
    if (!left.valid || !right.valid ||
        std::abs(dot(left.normal, right.normal)) < 0.99999)
      return false;
    uint32_t left_vertex = 0U, right_vertex = 0U;
    if (!opposite_vertex(left, edge_a, edge_b, &left_vertex) ||
        !opposite_vertex(right, edge_a, edge_b, &right_vertex))
      return false;
    const auto &left_point = b.out->vertices[b.base + left_vertex];
    const auto &right_point = b.out->vertices[b.base + right_vertex];
    const double left_to_right =
        std::abs(dot(right.normal, left_point) + right.offset);
    const double right_to_left =
        std::abs(dot(left.normal, right_point) + left.offset);
    return std::max(left_to_right, right_to_left) <= 0.002;
  }

  void reset_anchor_work() {
    auto &b = *build;
    b.vertex = 0U;
    b.triangle = 0U;
    b.anchor_stage = 0;
    b.faces.clear();
    b.edge_incidence.clear();
    b.dsu_parent.clear();
    b.dsu_size.clear();
    b.dsu_key.clear();
    b.face_index = 0U;
    b.anchor_fit_index = b.patches.size();
    b.components.clear();
    b.canonical_roots.clear();
    b.face_order.clear();
    b.slice_subindex = 0U;
    b.canonical_subindex = 0U;
    b.degenerate_triangles = 0U;
    b.non_manifold_edges = 0U;
    b.ambiguous_components = 0U;
  }

  // Retire one map entry per quantum. Component sample maps are capped at 96;
  // the remaining vectors contain trivial elements and retain bounded capacity.
  // Do not hide an anchor-sized tree destruction behind its final triangle.
  bool retire_anchor_unit() {
    auto &b = *build;
    if (!b.edge_incidence.empty()) {
      b.edge_incidence.erase(b.edge_incidence.begin());
      return false;
    }
    if (!b.components.empty()) {
      b.components.erase(b.components.begin());
      return false;
    }
    if (!b.canonical_roots.empty()) {
      b.canonical_roots.erase(b.canonical_roots.begin());
      return false;
    }
    if (!b.face_order.empty()) {
      b.face_order.erase(b.face_order.begin());
      return false;
    }
    reset_anchor_work();
    return true;
  }

  size_t node(HSSVec3d p, const std::string &support_id) {
    auto &b = *build;
    auto key = std::tuple{std::llround(p.x / kJoin), std::llround(p.y / kJoin),
                          std::llround(p.z / kJoin)};
    auto it = b.node_lookup.find(key);
    if (it != b.node_lookup.end()) {
      auto &n = b.nodes[it->second];
      if (std::find(n.supports.begin(), n.supports.end(), support_id) ==
          n.supports.end())
        n.supports.push_back(support_id);
      return it->second;
    }
    size_t i = b.nodes.size();
    auto uv = coordinate::project_to_floor(floor, p);
    b.nodes.push_back(
        {p, {uv.u_m, uv.v_m}, id("node:" + vec(p)), {support_id}, {}});
    b.node_lookup.emplace(key, i);
    return i;
  }
  void slice(const std::array<HSSVec3d, 3> &p, const std::string &sup,
             const std::string &surface) {
    auto &b = *build;
    if (b.edges.size() >= kMaxMetric) {
      b.metric_limit = true;
      return;
    }
    std::vector<HSSVec3d> hits;
    for (size_t i = 0; i < 3; ++i) {
      auto a = p[i], c = p[(i + 1) % 3];
      double da = dot(sub(a, floor.origin_m), floor.n) - cut,
             dc = dot(sub(c, floor.origin_m), floor.n) - cut;
      // Half-open crossing avoids counting coplanar triangles as wall cuts.
      if ((da <= 0 && dc > 0) || (dc <= 0 && da > 0))
        hits.push_back(add(a, mul(sub(c, a), da / (da - dc))));
    }
    if (hits.size() != 2 ||
        dot(sub(hits[0], hits[1]), sub(hits[0], hits[1])) < 1e-12)
      return;
    // A cyclic rotation names the same oriented triangle and yields the same
    // two intersections, but may visit them in the opposite order. Canonical
    // endpoint order belongs to the derived cut only; raw indices stay intact.
    if (std::tie(hits[1].x, hits[1].y, hits[1].z) <
        std::tie(hits[0].x, hits[0].y, hits[0].z))
      std::swap(hits[0], hits[1]);
    size_t a = node(hits[0], sup), c = node(hits[1], sup);
    if (a == c)
      return;
    auto pair = std::minmax(a, c);
    if (!b.edge_lookup.emplace(pair.first, pair.second).second)
      return;
    size_t e = b.edges.size();
    b.edges.push_back({a, c, id(sup + ":" + std::to_string(e)), sup, surface});
    b.nodes[a].edges.push_back(e);
    b.nodes[c].edges.push_back(e);
  }
  void triangle_unit() {
    auto &b = *build;
    const auto &[key, a] = b.anchors[b.anchor];
    (void)key;
    const auto &e = a.source->event;

    TriangleKey local{};
    std::memcpy(local.data(),
                e.payload.data() + e.mesh.vertex_count * 12 + b.triangle * 12,
                12);
    const size_t face_index = b.faces.size();
    FaceWork face;
    face.triangle = local;
    face.canonical = canonical_triangle(local);

    std::array<HSSVec3d, 3> points{};
    for (size_t i = 0; i < 3; ++i) {
      const uint32_t global = local[i] + uint32_t(b.base);
      points[i] = b.out->vertices[global];
      b.out->triangles.push_back(global);
    }

    const bool repeated =
        local[0] == local[1] || local[1] == local[2] || local[0] == local[2];
    HSSVec3d normal =
        repeated ? HSSVec3d{} :
                   cross(sub(points[1], points[0]), sub(points[2], points[0]));
    const double length = std::sqrt(dot(normal, normal));
    if (!repeated && length > 1e-10) {
      normal = mul(normal, 1.0 / length);
      face.normal = normal;
      face.offset = -dot(normal, points[0]);
      face.valid = true;
      b.face_order[face.canonical].push_back(face_index);
      for (size_t i = 0; i < 3; ++i) {
        const auto edge = std::minmax(local[i], local[(i + 1U) % 3U]);
        auto &incidence = b.edge_incidence[{edge.first, edge.second}];
        if (incidence.count < incidence.faces.size())
          incidence.faces[incidence.count] = face_index;
        ++incidence.count;
      }
    } else {
      ++b.degenerate_triangles;
    }

    b.faces.push_back(face);
    b.dsu_parent.push_back(face_index);
    b.dsu_size.push_back(1U);
    b.dsu_key.push_back(face.canonical);
    ++b.triangle;
  }
  void loop_unit() {
    auto &b = *build;
    size_t seed = b.loop_seed++;
    if (b.visited[seed])
      return;
    std::vector<size_t> nodes, edges;
    size_t current = b.edges[seed].a, edge = seed, start = current;
    bool closed = false;
    for (size_t step = 0; step < 128; ++step) {
      if (b.nodes[current].edges.size() != 2 || b.visited[edge])
        break;
      b.visited[edge] = true;
      nodes.push_back(current);
      edges.push_back(edge);
      const auto &e = b.edges[edge];
      current = e.a == current ? e.b : e.a;
      if (current == start) {
        closed = true;
        break;
      }
      const auto &incident = b.nodes[current].edges;
      if (incident.size() != 2)
        break;
      edge = incident[0] == edge ? incident[1] : incident[0];
    }
    if (!closed || nodes.size() < 3 || !floor_confirmed)
      return;
    double area = 0;
    auto origin = b.nodes[nodes[0]].uv;
    for (size_t i = 0; i < nodes.size(); ++i) {
      auto p = b.nodes[nodes[i]].uv,
           q = b.nodes[nodes[(i + 1) % nodes.size()]].uv;
      area += (p.x - origin.x) * (q.y - origin.y) -
              (q.x - origin.x) * (p.y - origin.y);
    }
    if (area < 0) {
      std::reverse(nodes.begin(), nodes.end());
      std::reverse(edges.begin(), edges.end());
      area = -area;
    }
    boundary::Graph graph;
    topology_protection::Feature feature;
    feature.id = id("protected:" + b.nodes[nodes[0]].identity);
    feature.revision = b.out->revision;
    graph.outer.id = id("loop:" + feature.id);
    std::vector<std::string> supports;
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto &n = b.nodes[nodes[i]];
      const auto &next = b.nodes[nodes[(i + 1) % nodes.size()]];
      std::string eid = id(n.identity + ":" + next.identity);
      graph.nodes.push_back({n.identity, n.uv, {}, {}, n.supports, false});
      graph.edges.push_back(
          {eid, n.identity, next.identity, {}, {}, {}, n.supports, true});
      graph.outer.edge_ids.push_back(eid);
      feature.vertices.push_back(
          {n.identity, {n.uv.x, n.uv.y}, {}, n.supports, false});
      feature.ordered_segments.push_back(
          {eid, n.identity, next.identity, {}, n.supports, true});
      for (const auto &s : n.supports)
        if (std::find(supports.begin(), supports.end(), s) == supports.end())
          supports.push_back(s);
    }
    boundary::Options opt;
    opt.max_nodes = 128;
    opt.max_edges = 128;
    opt.max_pair_tests = 20000;
    opt.min_edge_length_m = 1e-6;
    opt.geometric_epsilon_m = 1e-8;
    opt.max_abs_coordinate_m = kExtent;
    // The protected-chain module is deliberately used on an open chain: its
    // contract is n vertices / n-1 segments, while boundary validates closure.
    feature.ordered_segments.pop_back();
    topology_protection::Options guard;
    guard.max_vertices = 128;
    guard.max_segments = 128;
    guard.max_pair_tests = 20000;
    guard.min_segment_length_m = 1e-6;
    guard.geometric_epsilon_m = 1e-8;
    guard.max_abs_coordinate_m = kExtent;
    if (boundary::validate(graph, opt).status != boundary::Status::kOk ||
        topology_protection::validate(feature, guard).status !=
            topology_protection::Status::kOk)
      return;
    hss::area::Input input;
    input.outer.id = graph.outer.id;
    input.outer.topology_state = hss::area::TopologyState::kAccepted;
    input.outer.provenance.geometry_origin = hss::area::GeometryOrigin::kFitted;
    input.outer.provenance.quality_state =
        hss::area::QualityState::kProvisional;
    input.outer.provenance.source_revision = std::to_string(b.out->revision);
    input.outer.provenance.support_ids = supports;
    for (auto node_index : nodes) {
      const auto &uv = b.nodes[node_index].uv;
      input.outer.vertices_m.push_back({uv.x, uv.y});
    }
    auto result = hss::area::calculate(input);
    if (result.status != hss::area::Status::kOk || !result.area_m2)
      return;
    b.regions.push_back({nodes, *result.area_m2, graph.outer.id, supports});
  }

#if defined(HSS_ENABLE_PLAN_ANNOTATION_RUNTIME)
  bool build_semantic_snapshot() {
    namespace annotations = hss::annotation_geometry;
    namespace plan = hss::plan_semantics;
    auto &b = *build;
    const std::string revision = std::to_string(b.out->revision);

    std::vector<std::string> supports;
    const auto add_support = [&supports](const std::string &value) {
      if (std::find(supports.begin(), supports.end(), value) == supports.end())
        supports.push_back(value);
    };
    for (const Edge &edge : b.edges)
      add_support(edge.support);
    if (supports.empty()) {
      for (const auto &[key, anchor] : b.anchors)
        add_support(support(key, anchor));
    }
    if (supports.empty())
      return false;

    plan::Input input;
    input.scene_revision = revision;
    input.source_is_synthetic = synthetic;
    input.view = plan::View::kArchitecturalCut;
    input.height.cut_height_m = cut;
    input.height.sampling_band_m = 0.10;
    input.height.reference_state =
        floor_confirmed ? plan::ReferenceState::kConfirmedFloor
                        : plan::ReferenceState::kFloorUnknown;
    if (floor_confirmed)
      input.height.floor_reference_id = id("floor-reference:" + basis());

    for (const Edge &edge : b.edges) {
      plan::Fragment fragment;
      fragment.id = edge.identity;
      fragment.state = plan::EvidenceState::kSupported;
      fragment.kind = plan::EvidenceKind::kCapturedSection;
      fragment.geometry_origin = area::GeometryOrigin::kFitted;
      const Node &start = b.nodes[edge.a];
      const Node &end = b.nodes[edge.b];
      fragment.segment = plan::Segment2d{{start.uv.x, start.uv.y},
                                         {end.uv.x, end.uv.y}};
      fragment.source_height_range_m = plan::HeightRange{cut, cut};
      fragment.source_revision = revision;
      fragment.support_ids = {edge.support};
      input.fragments.push_back(std::move(fragment));
    }

    const bool all_nodes_closed =
        !b.nodes.empty() &&
        std::all_of(b.nodes.begin(), b.nodes.end(),
                    [](const Node &node) { return node.edges.size() == 2U; });
    const bool one_classified_loop =
        floor_confirmed && !b.metric_limit && b.regions.size() == 1U &&
        b.regions.front().nodes.size() == b.edges.size() && all_nodes_closed;
    const bool open = !b.metric_limit && !b.edges.empty() && !all_nodes_closed;

    if (one_classified_loop) {
      input.boundary_state = plan::BoundaryState::kClosedAccepted;
    } else if (open) {
      input.boundary_state = plan::BoundaryState::kOpen;
    } else {
      input.boundary_state = b.metric_limit
                                 ? plan::BoundaryState::kUnsupported
                                 : plan::BoundaryState::kUnknown;
      plan::Fragment unresolved;
      unresolved.id = id((b.metric_limit ? "unsupported-runtime-cut:"
                                         : "unknown-runtime-boundary:") +
                         revision);
      unresolved.state = b.metric_limit ? plan::EvidenceState::kUnsupported
                                         : plan::EvidenceState::kUnknown;
      unresolved.kind = b.metric_limit ? plan::EvidenceKind::kUnsupported
                                        : plan::EvidenceKind::kUnknownBoundary;
      unresolved.geometry_origin = area::GeometryOrigin::kUnknown;
      unresolved.source_revision = revision;
      unresolved.support_ids = supports;
      input.fragments.push_back(std::move(unresolved));
    }

    area::Input area_input;
    area_input.policy = area::AreaPolicy::kInternalFinishedFace;
    area_input.outer.id = one_classified_loop
                              ? b.regions.front().identity
                              : id("runtime-plan-boundary:" + revision);
    area_input.outer.topology_state =
        one_classified_loop
            ? area::TopologyState::kAccepted
            : (open ? area::TopologyState::kIncomplete
                    : area::TopologyState::kUnaccepted);
    area_input.outer.provenance.geometry_origin = area::GeometryOrigin::kFitted;
    area_input.outer.provenance.quality_state = area::QualityState::kProvisional;
    area_input.outer.provenance.source_revision = revision;
    area_input.outer.provenance.support_ids = supports;
    if (one_classified_loop) {
      for (const size_t node_index : b.regions.front().nodes) {
        const auto &uv = b.nodes[node_index].uv;
        area_input.outer.vertices_m.push_back({uv.x, uv.y});
      }
    }
    input.area_input = std::move(area_input);

    const plan::Result assembled = plan::assemble(input);
    if (assembled.status != plan::Status::kOk || !assembled.layer)
      return false;
    b.out->plan = std::move(*assembled.layer);

    annotations::Input annotation_input;
    annotation_input.plan = *b.out->plan;
    for (const plan::Fragment &fragment : b.out->plan->fragments) {
      if (fragment.state != plan::EvidenceState::kSupported ||
          !fragment.segment)
        continue;
      annotations::Annotation annotation;
      annotation.id = id("runtime-length:" + fragment.id + ":" + revision);
      annotation.kind = annotations::Kind::kLength;
      annotation.evidence_source = annotations::EvidenceSource::kFitted;
      annotation.source_revision = revision;
      annotation.evidence_ids = fragment.support_ids;
      annotation.anchors = {
          {fragment.id, fragment.segment->start_m},
          {fragment.id, fragment.segment->end_m},
      };
      annotation_input.annotations.push_back(std::move(annotation));
    }
    const annotations::Result resolved = annotations::resolve(annotation_input);
    if (resolved.status != annotations::Status::kOk || !resolved.layout) {
      b.out->plan.reset();
      return false;
    }
    b.out->annotations = std::move(*resolved.layout);
    return true;
  }
#endif
};
Reconstruction::Reconstruction(const HSSConfig &c)
    : impl_(std::make_unique<Impl>(c)) {
  impl_->empty();
}
Reconstruction::~Reconstruction() = default;
bool Reconstruction::busy() const { return bool(impl_->build); }
HSSStatus Reconstruction::status() const { return impl_->last; }
const std::shared_ptr<const Snapshot> &Reconstruction::snapshot() const {
  return impl_->accepted;
}
std::shared_ptr<Budget> Reconstruction::budget() const { return impl_->budget; }
void Reconstruction::seal_configuration() { impl_->sealed = true; }
HSSStatus Reconstruction::set_synthetic(uint32_t value) {
  if (value > 1)
    return HSS_INVALID_ARGUMENT;
  if (impl_->sealed || impl_->build || impl_->accepted->revision != 0)
    return HSS_NOT_READY;
  bool previous = impl_->synthetic;
  impl_->synthetic = value != 0;
  try {
    impl_->empty();
  } catch (...) {
    impl_->synthetic = previous;
    throw;
  }
  return HSS_OK;
}
HSSStatus Reconstruction::set_plan_reference(const HSSMat4d &t, double cut) {
  auto &x = *impl_;
  if (!coordinate::is_rigid_transform(t))
    return HSS_INVALID_COORDINATE_FRAME;
  if (!std::isfinite(cut) || cut <= 0 || cut > 100)
    return HSS_INVALID_ARGUMENT;
  if (x.build)
    return HSS_NOT_READY;
  const auto *m = t.column_major;
  coordinate::FloorFrame next{{m[12], m[13], m[14]},
                              {m[0], m[1], m[2]},
                              {-m[8], -m[9], -m[10]},
                              {m[4], m[5], m[6]}};
  if (!bounded(next.origin_m))
    return HSS_INVALID_COORDINATE_FRAME;
  auto status = x.start_build(x.state);
  if (status != HSS_OK)
    return status;
  x.build->plan_change = true;
  x.build->old_floor = x.floor;
  x.build->old_cut = x.cut;
  x.build->old_confirmed = x.floor_confirmed;
  x.floor = next;
  x.cut = cut;
  x.floor_confirmed = true;
  return HSS_OK;
}
HSSStatus Reconstruction::publish_edited_scene(std::string candidate_scene_json,
                                               uint64_t expected_revision,
                                               uint64_t node_count,
                                               uint64_t edge_count,
                                               uint64_t issue_count,
                                               std::shared_ptr<const scene_editing::RetainedUnknowns>
                                                   retained_unknowns) {
  auto &x = *impl_;
  if (x.build || expected_revision == 0U ||
      expected_revision != x.accepted->revision + 1U ||
      candidate_scene_json.empty() || candidate_scene_json.size() > 16U * 1024U * 1024U)
    return HSS_STALE_REVISION;
  // The editing processor has already parsed, schema-validated, and assigned
  // this revision. Reserve a distinct immutable scene payload so readers of
  // the prior snapshot remain valid until they release it.
  const uint64_t charge = 4096U + candidate_scene_json.size();
  auto lease = reserve(x.budget, charge);
  if (!lease)
    return HSS_RESOURCE_LIMIT;
  try {
    std::shared_ptr<UnknownOverlay> overlay;
    if (retained_unknowns && !retained_unknowns->empty()) {
      if (retained_unknowns->size() > scene_editing::kMaxRetainedUnknowns)
        return HSS_RESOURCE_LIMIT;
      uint64_t payload = 0U, serialized = 0U;
      for (const auto &item : *retained_unknowns) {
        for (const auto *text : {&item.feature_json, &item.issue_before_revision,
                                 &item.issue_after_revision}) {
          if (text->capacity() > 16U * 1024U * 1024U ||
              payload > 16U * 1024U * 1024U - text->capacity())
            return HSS_RESOURCE_LIMIT;
          payload += text->capacity();
          serialized += text->size();
        }
        if (item.protection_constraints_json.size() > scene_editing::kMaxRetainedUnknowns)
          return HSS_RESOURCE_LIMIT;
        for (const auto &constraint : item.protection_constraints_json) {
          if (constraint.capacity() > 16U * 1024U * 1024U ||
              payload > 16U * 1024U * 1024U - constraint.capacity())
            return HSS_RESOURCE_LIMIT;
          payload += constraint.capacity();
          serialized += constraint.size() + 1U;
        }
        payload += item.protection_constraints_json.capacity() * sizeof(std::string);
        serialized += 24U; // Canonical quoted uint64 revision and separators.
      }
      auto overlay_lease = reserve(x.budget, 1024U + payload +
          retained_unknowns->capacity() * sizeof(scene_editing::RetainedUnknown));
      if (!overlay_lease)
        return HSS_RESOURCE_LIMIT;
      overlay = std::make_shared<UnknownOverlay>();
      overlay->lease = std::move(overlay_lease);
      overlay->records = std::move(retained_unknowns);
      overlay->serialized_bytes = serialized;
    }
    auto next = std::make_shared<Snapshot>();
    next->retained_unknowns = std::move(overlay);
    next->lease = std::move(lease);
    next->revision = expected_revision;
    next->node_count = node_count;
    next->edge_count = edge_count;
    next->issue_count = issue_count;
    next->vertices = x.accepted->vertices;
    next->triangles = x.accepted->triangles;
    next->json = std::move(candidate_scene_json);
    x.accepted = std::move(next);
    x.last = HSS_OK;
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  }
}
void Reconstruction::cancel(HSSStatus s) {
  auto &x = *impl_;
  if (x.build && x.build->plan_change) {
    x.floor = x.build->old_floor;
    x.cut = x.build->old_cut;
    x.floor_confirmed = x.build->old_confirmed;
  }
  x.build.reset();
  x.last = s;
}
HSSStatus Reconstruction::begin(std::shared_ptr<Evidence> input) {
  auto &x = *impl_;
  x.last = HSS_OK;
  if (x.build)
    return HSS_NOT_READY;
  const auto &e = input->event;
  State candidate = x.state;
  const auto capture = uuid(e.capture_id), ek = epoch_key(e),
             ak = anchor_key(e);
  auto prior = candidate.sequences.find(capture);
  if (prior != candidate.sequences.end() && e.event_sequence <= prior->second)
    return x.last = HSS_STALE_REVISION;
  if (candidate.sequences.size() >= 32 &&
      !candidate.sequences.contains(capture))
    return x.last = HSS_RESOURCE_LIMIT;
  if (e.kind == ingress::EventKind::epoch) {
    auto old = candidate.epochs.find(ek);
    if (e.epoch.transform_revision == 0 ||
        (old != candidate.epochs.end() &&
         e.epoch.transform_revision <=
             old->second->event.epoch.transform_revision))
      return x.last = HSS_STALE_REVISION;
    if (old == candidate.epochs.end() && candidate.epochs.size() >= kMaxEpochs)
      return x.last = HSS_RESOURCE_LIMIT;
    candidate.epochs[ek] = input;
  } else {
    if (!candidate.epochs.contains(ek))
      return x.last = HSS_INVALID_COORDINATE_FRAME;
    if (e.kind == ingress::EventKind::frame) {
      if (candidate.frames.size() >= kMaxFrames)
        return x.last = HSS_RESOURCE_LIMIT;
      candidate.frames.push_back(input);
    } else if (e.kind == ingress::EventKind::mesh_add ||
               e.kind == ingress::EventKind::mesh_replace ||
               e.kind == ingress::EventKind::mesh_remove) {
      auto old = candidate.anchors.find(ak);
      bool live =
          old != candidate.anchors.end() &&
          old->second.source->event.kind != ingress::EventKind::mesh_remove;
      uint64_t generation =
          old == candidate.anchors.end() ? 1 : old->second.generation;
      if (e.kind == ingress::EventKind::mesh_add) {
        if (live || e.anchor_revision != 1)
          return x.last = HSS_STALE_REVISION;
        if (old != candidate.anchors.end())
          ++generation;
      } else if (!live ||
                 (e.kind == ingress::EventKind::mesh_replace &&
                  old->second.source->event.anchor_revision == UINT64_MAX) ||
                 e.anchor_revision !=
                     old->second.source->event.anchor_revision +
                         (e.kind == ingress::EventKind::mesh_replace ? 1 : 0))
        return x.last = HSS_STALE_REVISION;
      if (old == candidate.anchors.end() &&
          candidate.anchors.size() >= kMaxAnchors)
        return x.last = HSS_RESOURCE_LIMIT;
      candidate.anchors[ak] = {input, generation};
    } else
      return x.last = HSS_UNSUPPORTED_CAPABILITY;
  }
  candidate.sequences[capture] = e.event_sequence;
  return x.start_build(std::move(candidate));
}
HSSStatus Reconstruction::Impl::start_build(State candidate) {
  auto &x = *this;
  if (x.accepted->revision == UINT64_MAX)
    return x.last = HSS_RESOURCE_LIMIT;
  size_t vertices = 0, triangles = 0;
  for (const auto &[key, a] : candidate.anchors) {
    (void)key;
    const auto &m = a.source->event;
    if (m.kind == ingress::EventKind::mesh_remove ||
        candidate.epochs.at(epoch_key(m))->event.epoch.relationship_state != 1)
      continue;
    vertices += m.mesh.vertex_count;
    triangles += m.mesh.triangle_count;
  }
  if (vertices > kMaxVertices || triangles > kMaxTriangles)
    return x.last = HSS_RESOURCE_LIMIT;
  // Covers vector growth, map nodes, plane/graph scratch, strings and the
  // simultaneously assembled JSON. Retained snapshots keep their reservation.
  uint64_t charge = 32768 + vertices * 64 + triangles * 512 +
                    std::min(triangles, kMaxMetric) * 8192 +
                    candidate.epochs.size() * 2048 +
                    candidate.anchors.size() * 4096;
  // Existing retained bytes already own their lease. Charge the additional
  // serialized copy/growth needed by this new immutable source snapshot.
  if (x.accepted->retained_unknowns)
    charge += 2U * x.accepted->retained_unknowns->serialized_bytes;
  auto lease = reserve(x.budget, charge);
  if (!lease)
    return x.last = HSS_RESOURCE_LIMIT;
  auto b = std::make_unique<Impl::Build>();
  b->state = std::move(candidate);
  b->out = std::make_shared<Snapshot>();
  b->out->lease = std::move(lease);
  b->out->revision = x.accepted->revision + 1;
  b->out->retained_unknowns = x.accepted->retained_unknowns;
  b->out->vertices.reserve(vertices);
  b->out->triangles.reserve(triangles * 3);
  for (const auto &[key, a] : b->state.anchors) {
    const auto &m = a.source->event;
    if (m.kind != ingress::EventKind::mesh_remove &&
        b->state.epochs.at(epoch_key(m))->event.epoch.relationship_state == 1)
      b->anchors.emplace_back(key, a);
  }
  x.build = std::move(b);
  x.last = HSS_OK;
  return HSS_OK;
}
bool Reconstruction::advance() {
  auto &x = *impl_;
  if (!x.build)
    return true;
  auto &b = *x.build;
  switch (b.phase) {
  case 0: {
    if (b.item < b.state.epochs.size()) {
      auto it = b.state.epochs.begin();
      std::advance(it, b.item++);
      const auto &e = it->second->event.epoch;
      if (!b.epoch_json.empty())
        b.epoch_json += ',';
      b.epoch_json += "{\"capture_id\":" + quote(uuid(e.capture_id)) +
                      ",\"session_epoch\":" + std::to_string(e.session_epoch) +
                      ",\"transform_revision\":" +
                      quote(std::to_string(e.transform_revision)) +
                      ",\"relationship_state\":" +
                      quote(e.relationship_state ? "accepted" : "unresolved") +
                      ",\"T_project_world\":";
      if (e.relationship_state) {
        b.epoch_json += '[';
        for (size_t i = 0; i < 16; ++i) {
          if (i)
            b.epoch_json += ',';
          b.epoch_json += num(e.T_project_world.column_major[i]);
        }
        b.epoch_json += ']';
      } else {
        b.epoch_json += "null";
        x.issue(
            "unresolved_epoch",
            "Unresolved epoch retained locally; excluded from project geometry",
            e.event_sequence);
      }
      b.epoch_json += '}';
      return false;
    }
    if (!x.floor_confirmed)
      x.issue("floor_reference_unknown",
              "Floor unconfirmed; provisional project slice only; no floor or "
              "room area");
    x.issue(
        "provisional_capture",
        "Captured mesh estimates with unknown semantics and uncertainty; "
        "architectural cut " +
            num(x.cut) +
            " m; closed slices are candidates, not classified room envelopes");
    b.phase = 1;
    b.item = 0;
    return false;
  }
  case 1: {
    if (b.anchor >= b.anchors.size()) {
      b.phase = 2;
      return false;
    }
    const auto &[key, a] = b.anchors[b.anchor];
    const auto &e = a.source->event;
    const auto sup = x.support(key, a);

    if (b.anchor_stage == 7) {
      if (x.retire_anchor_unit()) {
        b.base = b.out->vertices.size();
        ++b.anchor;
      }
      return false;
    }

    if (b.anchor_stage == 0) {
      if (b.vertex < e.mesh.vertex_count) {
        float v[3];
        std::memcpy(v, e.payload.data() + b.vertex * 12, 12);
        auto p = coordinate::transform_point(e.mesh.T_world_anchor,
                                             {v[0], v[1], v[2]});
        p = coordinate::transform_point(
            b.state.epochs.at(epoch_key(e))->event.epoch.T_project_world, p);
        if (!bounded(p)) {
          cancel(HSS_INVALID_COORDINATE_FRAME);
          return true;
        }
        b.out->vertices.push_back(p);
        ++b.vertex;
        return false;
      }
      if (b.triangle < e.mesh.triangle_count) {
        x.triangle_unit();
        return false;
      }
      b.edge_it = b.edge_incidence.begin();
      b.anchor_stage = 1;
      return false;
    }

    if (b.anchor_stage == 1) {
      if (b.edge_it != b.edge_incidence.end()) {
        const auto edge = b.edge_it->first;
        const auto incidence = b.edge_it->second;
        if (incidence.count > 2U) {
          ++b.non_manifold_edges;
        } else if (incidence.count == 2U &&
                   x.compatible_faces(incidence.faces[0], incidence.faces[1],
                                      edge.first, edge.second)) {
          x.dsu_unite(incidence.faces[0], incidence.faces[1]);
        }
        ++b.edge_it;
        return false;
      }
      b.face_index = 0U;
      b.anchor_stage = 2;
      return false;
    }

    if (b.anchor_stage == 2) {
      if (b.face_index < b.faces.size()) {
        const size_t index = b.face_index++;
        const auto &face = b.faces[index];
        if (!face.valid)
          return false;
        const size_t root = x.dsu_find(index);
        auto &component = b.components[root];
        ++component.triangle_count;
        if (face.canonical < component.canonical) {
          component.canonical = face.canonical;
          component.representative = index;
        }
        for (uint32_t vertex : face.triangle)
          x.consider_sample(component, vertex);
        return false;
      }
      b.component_it = b.components.begin();
      b.anchor_stage = 3;
      return false;
    }

    if (b.anchor_stage == 3) {
      if (b.component_it != b.components.end()) {
        b.canonical_roots[b.component_it->second.canonical].push_back(
            b.component_it->first);
        ++b.component_it;
        return false;
      }
      b.canonical_it = b.canonical_roots.begin();
      b.anchor_stage = 4;
      return false;
    }

    if (b.anchor_stage == 4) {
      if (b.canonical_it != b.canonical_roots.end()) {
        auto &roots = b.canonical_it->second;
        if (roots.size() != 1U) {
          if (b.canonical_subindex == 0U)
            b.ambiguous_components += roots.size();
          b.components.at(roots[b.canonical_subindex++]).ambiguous = true;
          if (b.canonical_subindex < roots.size())
            return false;
          b.canonical_subindex = 0U;
        } else {
          auto &component = b.components.at(roots.front());
          if (b.patches.size() >= kMaxMetric) {
            component.metric_skipped = true;
            b.metric_limit = true;
          } else {
            Patch patch;
            patch.identity =
                id(sup + ":patch:" + triangle_suffix(component.canonical));
            patch.support = sup;
            patch.source_sequence = e.event_sequence;
            patch.expected_triangles = component.triangle_count;
            patch.samples.reserve(component.sample_ranks.size());
            for (const auto &[vertex, rank] : component.sample_ranks) {
              (void)rank;
              const auto &point = b.out->vertices[b.base + vertex];
              patch.samples.push_back(
                  {{point.x, point.y, point.z}, 1.0, e.event_sequence});
            }
            component.surface = patch.identity;
            component.patch_index = b.patches.size();
            b.patches.push_back(std::move(patch));
          }
        }
        ++b.canonical_it;
        return false;
      }
      b.anchor_stage = 8;
      return false;
    }

    // Fit one bounded sample set per quantum before the existing face walk.
    // Neighbour compatibility is transitive, while planarity is not: a robust
    // inlier subset alone cannot label the entire connected component planar.
    if (b.anchor_stage == 8) {
      if (b.anchor_fit_index < b.patches.size()) {
        auto &p = b.patches[b.anchor_fit_index++];
        plane_fit::Options o;
        o.inlier_threshold_m = kPlaneSupportM;
        o.seed = x.config.deterministic_seed;
        o.max_input_samples = 96;
        o.candidate_iterations = 24;
        o.max_refinement_iterations = 4;
        o.max_residual_evaluations = 4096;
        o.min_support_samples = 3;
        o.min_distinct_support_points = 3;
        p.plane = plane_fit::fit(p.samples, o).plane;
        return false;
      }
      b.slice_it = b.face_order.begin();
      b.slice_subindex = 0U;
      b.anchor_stage = 5;
      return false;
    }

    if (b.anchor_stage == 5) {
      if (b.slice_it == b.face_order.end()) {
        b.anchor_stage = 6;
        return false;
      }
      if (b.slice_subindex >= b.slice_it->second.size()) {
        ++b.slice_it;
        b.slice_subindex = 0U;
        return false;
      }

      const size_t face_index = b.slice_it->second[b.slice_subindex++];
      const size_t root = x.dsu_find(face_index);
      auto found = b.components.find(root);
      if (found == b.components.end() || found->second.ambiguous ||
          found->second.metric_skipped || found->second.surface.empty())
        return false;

      const auto &face = b.faces[face_index];
      std::array<HSSVec3d, 3> points{};
      for (size_t i = 0; i < 3; ++i)
        points[i] = b.out->vertices[b.base + face.triangle[i]];
      auto &patch = b.patches.at(found->second.patch_index);
      ++patch.checked_triangles;
      if (patch.plane) {
        const auto &plane = *patch.plane;
        // Check all original support, including vertices omitted by the
        // deterministic 96-sample cap. Only three points are checked in this
        // quantum; no second anchor-sized pass or unbounded replay is added.
        for (const auto &point : points) {
          const double distance = std::abs(std::fma(
              plane.normal.x, point.x,
              std::fma(plane.normal.y, point.y,
                       std::fma(plane.normal.z, point.z, plane.offset_m))));
          patch.support_failed |=
              !std::isfinite(distance) || distance > kPlaneSupportM;
        }
      }
      x.slice(points, sup, found->second.surface);
      return false;
    }

    const std::string mesh_id = id(sup + ":mesh");
    if (b.non_manifold_edges != 0U)
      x.issue("non_manifold_mesh",
              "Non-manifold indexed mesh edges=" +
                  std::to_string(b.non_manifold_edges) +
                  "; bridge excluded; support=" + sup,
              e.event_sequence, mesh_id);
    if (b.ambiguous_components != 0U)
      x.issue("ambiguous_mesh_component",
              "Ambiguous canonical mesh components=" +
                  std::to_string(b.ambiguous_components) +
                  "; fitting and slicing excluded; support=" + sup,
              e.event_sequence, mesh_id);
    if (b.degenerate_triangles != 0U)
      x.issue("degenerate_mesh_triangle",
              "Degenerate mesh triangles=" +
                  std::to_string(b.degenerate_triangles) +
                  "; raw evidence retained; fitting and slicing excluded; support=" +
                  sup,
              e.event_sequence, mesh_id);

    b.surfaces.push_back(
        "{\"id\":" + quote(mesh_id) +
        ",\"kind\":\"mesh_patch\",\"normal_unit\":null,\"offset_m\":null,"
        "\"boundary_node_ids\":[],\"mesh_asset\":null,\"fit_residual_m\":null,"
        "\"provenance\":" +
        provenance({sup}, "observed") + "}");
    x.issue("mesh_source",
            "support=" + sup + " capture=" + uuid(e.capture_id) +
                " epoch=" + std::to_string(e.session_epoch) +
                " anchor=" + uuid(e.anchor_id) +
                " generation=" + std::to_string(a.generation) +
                " anchor_revision=" + std::to_string(e.anchor_revision) +
                " vertices=" + std::to_string(b.base) + "+" +
                std::to_string(e.mesh.vertex_count),
            e.event_sequence, mesh_id);

    b.anchor_stage = 7;
    return false;
  }
  case 2: {
    if (b.patch_index >= b.patches.size()) {
      b.phase = 3;
      b.visited.resize(b.edges.size());
      return false;
    }
    const auto &p = b.patches[b.patch_index++];
    const bool supported = p.plane && !p.support_failed &&
                           p.checked_triangles == p.expected_triangles;
    if (p.plane && !supported)
      x.issue("partial_plane_support",
              "Plane hypothesis does not cover original mesh component; "
              "captured shape retained; surface=" + p.identity +
              " support=" + p.support,
              p.source_sequence, p.identity);
    std::string s = "{\"id\":" + quote(p.identity);
    if (supported) {
      const auto &plane = *p.plane;
      s += ",\"kind\":\"plane\",\"normal_unit\":" +
           vec({plane.normal.x, plane.normal.y, plane.normal.z}) +
           ",\"offset_m\":" + num(plane.offset_m) +
           ",\"fit_residual_m\":" + num(plane.residuals.weighted_rms_m);
    } else
      s += ",\"kind\":\"mesh_patch\",\"normal_unit\":null,\"offset_m\":null,"
           "\"fit_residual_m\":null";
    s += ",\"boundary_node_ids\":[],\"mesh_asset\":null,\"provenance\":" +
         provenance({p.support}, supported ? "fitted" : "observed") + "}";
    b.surfaces.push_back(std::move(s));
    return false;
  }
  case 3:
    if (b.loop_seed < b.edges.size()) {
      x.loop_unit();
      return false;
    }
    // Nested rings are not silently classified as fixed obstructions/holes.
    // Without semantic attachment evidence, suppress all area in that case.
    if (b.regions.size() > 1)
      x.issue("unclassified_slice_loops",
              "Multiple closed patches preserved; attachment and hole "
              "classification unresolved; area unavailable");
    if (b.metric_limit) {
      b.regions.clear();
      x.issue("metric_work_limit",
              "Metric patch or slice limit reached; captured triangles "
              "preserved; area unavailable");
    }
    for (const auto &n : b.nodes)
      if (n.edges.size() != 2) {
        x.issue("open_or_ambiguous_slice",
                "Open or ambiguous measured slice; no invented gap closure");
        break;
      }
#if defined(HSS_ENABLE_PLAN_ANNOTATION_RUNTIME)
    if (!x.build_semantic_snapshot())
      x.issue("plan_semantics_unavailable",
              "Runtime cut cannot be represented by the revision-bound plan "
              "and annotation contracts without invented lineage");
#endif
    b.out->node_count = b.nodes.size();
    b.out->edge_count = b.edges.size();
    b.out->json = x.prefix(b.out->revision) + ",\"epochs\":[" + b.epoch_json +
                  "],\"nodes\":[";
    b.phase = 4;
    b.item = 0;
    return false;
  case 4:
    if (b.item < b.nodes.size()) {
      const auto &n = b.nodes[b.item];
      if (b.item++)
        b.out->json += ',';
      b.out->json += "{\"id\":" + quote(n.identity) +
                     ",\"position_m\":" + vec(n.point) +
                     ",\"position_locked\":false,\"provenance\":" +
                     provenance(n.supports) + "}";
      return false;
    }
    b.out->json += "],\"edges\":[";
    b.phase = 5;
    b.item = 0;
    return false;
  case 5:
    if (b.item < b.edges.size()) {
      const auto &e = b.edges[b.item];
      if (b.item++)
        b.out->json += ',';
      b.out->json += "{\"id\":" + quote(e.identity) +
                     ",\"start_node_id\":" + quote(b.nodes[e.a].identity) +
                     ",\"end_node_id\":" + quote(b.nodes[e.b].identity) +
                     ",\"surface_ids\":[" + quote(e.surface) +
                     "],\"parent_edge_ids\":[],\"protected_topology\":true,"
                     "\"provenance\":" +
                     provenance({e.support}) + "}";
      return false;
    }
    b.out->json += "],\"surfaces\":[";
    b.phase = 6;
    b.item = 0;
    return false;
  case 6:
    if (b.item < b.surfaces.size()) {
      if (b.item)
        b.out->json += ',';
      b.out->json += b.surfaces[b.item++];
      return false;
    }
    b.out->json += "],\"features\":[";
    b.phase = 7;
    b.item = 0;
    return false;
  case 7:
    if (b.out->retained_unknowns &&
        b.item < b.out->retained_unknowns->records->size()) {
      if (b.item) b.out->json += ',';
      b.out->json += (*b.out->retained_unknowns->records)[b.item++].feature_json;
      return false;
    }
    b.out->json += "],\"regions\":[";
    b.phase = 8;
    b.item = 0;
    return false;
  case 8:
    if (b.item < b.regions.size()) {
      const auto &r = b.regions[b.item];
      if (b.item++)
        b.out->json += ',';
      b.out->json += "{\"id\":" + quote(r.identity) + ",\"outer_node_ids\":[";
      for (size_t i = 0; i < r.nodes.size(); ++i) {
        if (i)
          b.out->json += ',';
        b.out->json += quote(b.nodes[r.nodes[i]].identity);
      }
      // Classification is unknown: a closed geometric section is a candidate,
      // not evidence of a finished room or a fixed obstruction floor footprint.
      b.out->json +=
          "],\"hole_node_ids\":[],\"area_m2\":" +
          (b.regions.size() == 1 && r.nodes.size() == b.edges.size() &&
                   std::all_of(
                       b.nodes.begin(), b.nodes.end(),
                       [](const Node &n) { return n.edges.size() == 2; })
               ? num(r.area)
               : "null") +
          ",\"area_policy\":\"internal_finished_face\",\"view\":"
          "\"architectural_cut\",\"cut_height_m\":" +
          num(x.cut) + ",\"provenance\":" + provenance(r.supports) + "}";
      return false;
    }
    b.out->json += "],\"objects\":[],\"constraints\":[";
    b.phase = 10;
    b.item = 0;
    return false;
  case 10:
    if (b.out->retained_unknowns && b.item < b.out->retained_unknowns->records->size()) {
      const auto &constraints = (*b.out->retained_unknowns->records)[b.item].protection_constraints_json;
      if (b.retained_constraint_index < constraints.size()) {
        if (b.retained_constraint_count++) b.out->json += ',';
        b.out->json += constraints[b.retained_constraint_index++];
      } else {
        ++b.item;
        b.retained_constraint_index = 0;
      }
      return false;
    }
    b.out->json += "],\"issues\":[";
    b.phase = 9;
    b.item = 0;
    return false;
  case 9:
    if (b.item < b.issues.size()) {
      if (b.item)
        b.out->json += ',';
      b.out->json += b.issues[b.item++];
      return false;
    }
    if (b.out->retained_unknowns &&
        b.item - b.issues.size() < b.out->retained_unknowns->records->size()) {
      const auto &record = (*b.out->retained_unknowns->records)[b.item - b.issues.size()];
      if (b.item++) b.out->json += ',';
      b.out->json += record.issue_before_revision +
                     quote(std::to_string(b.out->revision)) + record.issue_after_revision;
      return false;
    }
    b.out->json += "],\"quality_state\":\"provisional\"}";
    b.out->issue_count = b.issues.size() +
        (b.out->retained_unknowns ? b.out->retained_unknowns->records->size() : 0U);
    if (b.out->json.size() > 16 * 1024 * 1024) {
      cancel(HSS_RESOURCE_LIMIT);
      return true;
    }
    x.state = std::move(b.state);
    x.accepted = std::move(b.out);
    x.build.reset();
    x.last = HSS_OK;
    return true;
  default:
    cancel(HSS_INTERNAL_ERROR);
    return true;
  }
}
} // namespace hss::live

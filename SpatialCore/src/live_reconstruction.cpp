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
  HSSVec3d normal{};
  double offset{};
  std::vector<plane_fit::WeightedSample> samples;
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
    bool metric_limit = false;
    std::vector<Patch> patches;
    std::map<std::pair<uint32_t, uint32_t>, size_t> adjacency;
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
    const auto &e = a.source->event;
    std::array<uint32_t, 3> idx{};
    std::memcpy(idx.data(),
                e.payload.data() + e.mesh.vertex_count * 12 + b.triangle * 12,
                12);
    std::array<HSSVec3d, 3> p;
    for (size_t i = 0; i < 3; ++i) {
      idx[i] += uint32_t(b.base);
      p[i] = b.out->vertices[idx[i]];
      b.out->triangles.push_back(idx[i]);
    }
    const auto sup = support(key, a);
    HSSVec3d n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
    double len = std::sqrt(dot(n, n));
    if (len > 1e-10) {
      n = mul(n, 1 / len);
      double d = -dot(n, p[0]);
      size_t patch = b.patches.size();
      for (size_t i = 0; i < 3; ++i) {
        auto edge = std::minmax(idx[i], idx[(i + 1) % 3]);
        auto found = b.adjacency.find(edge);
        if (found == b.adjacency.end())
          continue;
        auto &prior = b.patches[found->second];
        if (prior.samples.size() <= 93 &&
            std::abs(dot(prior.normal, n)) > 0.99999 &&
            std::abs(dot(prior.normal, p[0]) + prior.offset) < 0.002) {
          patch = found->second;
          break;
        }
      }
      if (patch == b.patches.size() && patch < kMaxMetric)
        b.patches.push_back(
            {id(sup + ":patch:" + std::to_string(b.triangle)), sup, n, d, {}});
      if (patch < b.patches.size()) {
        auto &group = b.patches[patch];
        for (auto q : p)
          group.samples.push_back({{q.x, q.y, q.z}, 1.0, e.event_sequence});
        for (size_t i = 0; i < 3; ++i) {
          auto edge = std::minmax(idx[i], idx[(i + 1) % 3]);
          b.adjacency[edge] = patch;
        }
        slice(p, sup, group.identity);
      } else {
        b.metric_limit = true;
        slice(p, sup, id(sup + ":mesh"));
      }
    }
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
  auto lease = reserve(x.budget, charge);
  if (!lease)
    return x.last = HSS_RESOURCE_LIMIT;
  auto b = std::make_unique<Impl::Build>();
  b->state = std::move(candidate);
  b->out = std::make_shared<Snapshot>();
  b->out->lease = std::move(lease);
  b->out->revision = x.accepted->revision + 1;
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
    auto sup = x.support(key, a);
    b.surfaces.push_back(
        "{\"id\":" + quote(id(sup + ":mesh")) +
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
            e.event_sequence, id(sup + ":mesh"));
    b.base = b.out->vertices.size();
    b.vertex = 0;
    b.triangle = 0;
    ++b.anchor;
    b.adjacency.clear();
    return false;
  }
  case 2: {
    if (b.patch_index >= b.patches.size()) {
      b.phase = 3;
      b.visited.resize(b.edges.size());
      return false;
    }
    const auto &p = b.patches[b.patch_index++];
    plane_fit::Options o;
    o.inlier_threshold_m = 0.002;
    o.seed = x.config.deterministic_seed;
    o.max_input_samples = 96;
    o.candidate_iterations = 24;
    o.max_refinement_iterations = 4;
    o.max_residual_evaluations = 4096;
    o.min_support_samples = 3;
    o.min_distinct_support_points = 3;
    auto fit = plane_fit::fit(p.samples, o);
    std::string s = "{\"id\":" + quote(p.identity);
    if (fit.plane) {
      const auto &plane = *fit.plane;
      s += ",\"kind\":\"plane\",\"normal_unit\":" +
           vec({plane.normal.x, plane.normal.y, plane.normal.z}) +
           ",\"offset_m\":" + num(plane.offset_m) +
           ",\"fit_residual_m\":" + num(plane.residuals.weighted_rms_m);
    } else
      s += ",\"kind\":\"mesh_patch\",\"normal_unit\":null,\"offset_m\":null,"
           "\"fit_residual_m\":null";
    s += ",\"boundary_node_ids\":[],\"mesh_asset\":null,\"provenance\":" +
         provenance({p.support}, fit.plane ? "fitted" : "observed") + "}";
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
    b.out->json += "],\"features\":[],\"regions\":[";
    b.phase = 7;
    b.item = 0;
    return false;
  case 7:
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
    b.out->json += "],\"objects\":[],\"constraints\":[],\"issues\":[";
    b.phase = 8;
    b.item = 0;
    return false;
  case 8:
    if (b.item < b.issues.size()) {
      if (b.item)
        b.out->json += ',';
      b.out->json += b.issues[b.item++];
      return false;
    }
    b.out->json += "],\"quality_state\":\"provisional\"}";
    b.out->issue_count = b.issues.size();
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

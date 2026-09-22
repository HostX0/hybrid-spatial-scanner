#include "hss/hss_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static void require(bool b, const char *what) {
  if (!b) {
    std::cerr << what << '\n';
    std::exit(1);
  }
}
static HSSMat4d identity() {
  HSSMat4d m{};
  m.column_major[0] = m.column_major[5] = m.column_major[10] =
      m.column_major[15] = 1;
  return m;
}
static HSSId ident(uint8_t n) {
  HSSId id{};
  id.bytes[0] = n;
  return id;
}
static HSSConfig config(uint64_t bytes = 64 * 1024 * 1024) {
  HSSConfig c{};
  c.abi_version = 1;
  c.struct_size = sizeof c;
  c.project_id = ident(1);
  c.structure_voxel_m = .02;
  c.detail_voxel_m = .01;
  c.truncation_m = .04;
  c.max_working_bytes = bytes;
  c.max_pending_frames = 2;
  return c;
}
static HSSEngineStats stats(HSSEngine *e) {
  HSSEngineStats s{};
  require(hss_engine_get_stats(e, &s) == HSS_OK, "stats");
  return s;
}
static double max_slice_us = 0;
static HSSStatus drain(HSSEngine *e) {
  for (size_t i = 0; i < 200000; ++i) {
    if (stats(e).lifecycle_state == 1)
      return HSS_OK;
    auto t = std::chrono::steady_clock::now();
    auto s = hss_engine_step(e, 50);
    max_slice_us =
        std::max(max_slice_us, std::chrono::duration<double, std::micro>(
                                   std::chrono::steady_clock::now() - t)
                                   .count());
    if (s != HSS_OK)
      return s;
  }
  require(false, "bounded work must eventually complete");
  return HSS_INTERNAL_ERROR;
}
static HSSSnapshot *snapshot(HSSEngine *e) {
  HSSSnapshot *s{};
  require(hss_engine_acquire_snapshot(e, &s) == HSS_OK, "snapshot");
  return s;
}
static HSSCounts counts(HSSSnapshot *s) {
  HSSCounts c{};
  require(hss_snapshot_get_counts(s, &c) == HSS_OK, "counts");
  return c;
}
static std::string json(HSSSnapshot *s) {
  uint64_t n{};
  require(hss_snapshot_copy_scene_json(s, nullptr, 0, &n) ==
              HSS_BUFFER_TOO_SMALL,
          "json query");
  std::string r(n, ' ');
  require(hss_snapshot_copy_scene_json(s, reinterpret_cast<uint8_t *>(r.data()),
                                       n, &n) == HSS_OK,
          "json copy");
  return r;
}
static std::vector<HSSVec3d> vertices(HSSSnapshot *s) {
  uint64_t n = counts(s).vertex_count;
  std::vector<HSSVec3d> v(n);
  require(hss_snapshot_copy_vertices(s, v.data(), n, &n) == HSS_OK,
          "vertex copy");
  return v;
}
static HSSEpoch epoch(uint64_t seq, uint32_t ep = 0, bool accepted = true) {
  HSSEpoch e{};
  e.abi_version = 1;
  e.struct_size = sizeof e;
  e.capture_id = ident(2);
  e.event_sequence = seq;
  e.session_epoch = ep;
  e.relationship_state = accepted;
  e.transform_revision = 1;
  e.T_project_world = identity();
  return e;
}
struct Mesh {
  std::vector<float> v;
  std::vector<uint32_t> t;
  HSSMeshEvent event(uint64_t seq, uint32_t kind = 0, uint64_t revision = 1,
                     uint32_t ep = 0, uint8_t anchor = 3) const {
    HSSMeshEvent e{};
    e.abi_version = 1;
    e.struct_size = sizeof e;
    e.capture_id = ident(2);
    e.anchor_id = ident(anchor);
    e.event_sequence = seq;
    e.anchor_revision = revision;
    e.session_epoch = ep;
    e.kind = kind;
    e.T_world_anchor = identity();
    if (kind != 2) {
      e.vertices_xyz = v.data();
      e.vertex_count = v.size() / 3;
      e.triangle_indices = t.data();
      e.triangle_count = t.size() / 3;
    }
    return e;
  }
};
static Mesh room(bool closed = true) {
  const std::vector<std::pair<float, float>> outline{
      {0, 0},        {6, 0},     {6, 5}, {4, 5}, {4, 4.5f},
      {3.85f, 4.5f}, {3.85f, 5}, {2, 5}, {0, 4}};
  Mesh m;
  for (auto [x, v] : outline) {
    m.v.insert(m.v.end(), {x, 0, -v, x, 3, -v});
  }
  size_t n = outline.size();
  for (size_t i = 0; i < (closed ? n : n - 1); ++i) {
    uint32_t a = uint32_t(i * 2), b = uint32_t(((i + 1) % n) * 2);
    m.t.insert(m.t.end(), {a, b, b + 1, a, b + 1, a + 1});
  }
  return m;
}
int main(int argc, char **argv) {
  auto c = config();
  HSSEngine *e{};
  require(hss_engine_create(&c, &e) == HSS_OK, "create");
  require(hss_engine_set_source_is_synthetic(e, 2) == HSS_INVALID_ARGUMENT,
          "invalid provenance");
  require(hss_engine_set_source_is_synthetic(e, 1) == HSS_OK,
          "synthetic origin explicit");
  auto ep = epoch(1);
  require(hss_engine_set_epoch(e, &ep) == HSS_OK, "epoch enqueue");
  require(drain(e) == HSS_OK, "epoch apply");
  require(hss_engine_set_source_is_synthetic(e, 0) == HSS_NOT_READY,
          "source immutable after evidence");
  Mesh m = room();
  auto event = m.event(2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK, "mesh copy");
  auto before = stats(e).scene_revision;
  require(hss_engine_step(e, 1) == HSS_OK, "tiny slice");
  require(stats(e).scene_revision == before && stats(e).lifecycle_state == 2,
          "budget must not synchronously rebuild full mesh");
  // Caller mutation after submit cannot alter retained source.
  m.v[0] = 999;
  require(drain(e) == HSS_OK, "room reconstruct");
  auto unconfirmed = snapshot(e);
  require(vertices(unconfirmed)[0].x == 0, "owned captured source");
  require(json(unconfirmed).find("floor_reference_unknown") !=
              std::string::npos,
          "unknown floor explicit");
  require(json(unconfirmed).find("\"regions\":[]") != std::string::npos,
          "unknown floor has no area");
  auto floor = identity();
  require(hss_engine_set_plan_reference(e, &floor, 1.2) == HSS_OK,
          "runtime floor confirmation");
  require(drain(e) == HSS_OK, "floor rebuild");
  auto full = snapshot(e);
  auto full_json = json(full);
  auto full_vertices = vertices(full);
  uint64_t required_indices = 0;
  require(hss_snapshot_copy_triangles(full, nullptr, 0, &required_indices) ==
              HSS_BUFFER_TOO_SMALL,
          "triangle count query");
  std::vector<uint32_t> copied_indices(required_indices, UINT32_MAX);
  require(hss_snapshot_copy_triangles(
              full, copied_indices.data(), required_indices - 1,
              &required_indices) == HSS_BUFFER_TOO_SMALL &&
              copied_indices[0] == UINT32_MAX,
          "triangle undersize writes nothing");
  require(hss_snapshot_copy_triangles(full, copied_indices.data(),
                                      required_indices,
                                      &required_indices) == HSS_OK &&
              copied_indices == room().t,
          "captured triangle topology exact");

  require(counts(full).triangle_count == 18 &&
              counts(full).boundary_edge_count == 18,
          "all captured triangles and slice fragments");
  require(full_json.find("\"kind\":\"plane\"") != std::string::npos,
          "live robust plane fitting");
  require(full_json.find("\"area_m2\":null") == std::string::npos &&
              full_json.find("\"regions\":[]") == std::string::npos,
          "supported closed cut area");
  require(full_json.find("anchor_revision=1") != std::string::npos,
          "actual anchor provenance");
  if (argc > 1) {
    std::ofstream out(argv[1]);
    out << full_json;
  }
  // Invalid ingest cannot poison accepted revision or mesh.
  Mesh bad = room();
  bad.t[0] = 999;
  event = bad.event(3, 1, 2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_INVALID_ARGUMENT,
          "index validation");
  bad = room();
  bad.v[0] = std::numeric_limits<float>::quiet_NaN();
  event = bad.event(3, 1, 2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_INVALID_ARGUMENT,
          "finite validation");
  require(stats(e).scene_revision == counts(full).scene_revision,
          "bad input keeps revision");
  Mesh open = room(false);
  event = open.event(3, 1, 2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "replacement applies");
  auto partial = snapshot(e);
  require(counts(partial).triangle_count == 16 &&
              json(partial).find("\"regions\":[]") != std::string::npos,
          "open boundary no hull closure");
  require(counts(full).triangle_count == 18 && json(full) == full_json,
          "old snapshot immutable");
  event = open.event(4, 1, 4);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_STALE_REVISION,
          "skipped revision rejected transactionally");
  require(stats(e).scene_revision == counts(partial).scene_revision,
          "failed lifecycle keeps accepted revision");
  // Isolated epoch preserves source; accepting its transform later rebuilds it.
  ep = epoch(4, 1, false);
  require(hss_engine_set_epoch(e, &ep) == HSS_OK && drain(e) == HSS_OK,
          "unresolved epoch");
  Mesh isolated;
  isolated.v = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  isolated.t = {0, 1, 2};
  event = isolated.event(5, 0, 1, 1, 4);
  event.T_world_anchor.column_major[12] = 2;
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "isolated mesh retained");
  auto isolated_snapshot = snapshot(e);
  require(counts(isolated_snapshot).triangle_count == 16,
          "unresolved excluded");
  ep = epoch(6, 1, true);
  ep.transform_revision = 2;
  ep.T_project_world.column_major[12] = 10;
  require(hss_engine_set_epoch(e, &ep) == HSS_OK && drain(e) == HSS_OK,
          "accepted transform rebuild");
  auto transformed = snapshot(e);
  auto vv = vertices(transformed);
  require(vv.back().x == 12 && counts(transformed).triangle_count == 17,
          "anchor then epoch project transform");
  // Removal exact current revision and epoch isolation.
  event = open.event(7, 2, 2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "remove exact live revision");
  auto removed = snapshot(e);
  require(counts(removed).triangle_count == 1,
          "removal leaves unrelated epoch");
  event = open.event(8, 1, 3);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_STALE_REVISION,
          "replace tombstone rejected");
  event = open.event(8, 0, 1);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "readd new generation");
  auto readded = snapshot(e);
  require(json(readded).find("generation=2") != std::string::npos,
          "readd provenance generation");
  // Floor interpretation moves cut only, not captured mesh; huge transform
  // fails atomically.
  floor.column_major[13] = 5;
  require(hss_engine_set_plan_reference(e, &floor, 1.2) == HSS_OK &&
              drain(e) == HSS_OK,
          "reference changes after capture");
  auto high = snapshot(e);
  require(counts(high).boundary_edge_count == 0 &&
              counts(high).triangle_count == 17,
          "cut above capture invents no edges");
  require(vertices(high)[0].x == vertices(readded)[0].x,
          "floor reference does not move captured mesh");
  ep = epoch(9, 1, true);
  ep.transform_revision = 3;
  ep.T_project_world.column_major[12] = 20000;
  require(hss_engine_set_epoch(e, &ep) == HSS_OK &&
              drain(e) == HSS_INVALID_COORDINATE_FRAME,
          "invalid project extent rejected during rebuild");
  require(stats(e).scene_revision == counts(high).scene_revision,
          "late failure keeps accepted snapshot");
  // Same source+floor control history replayed with different budgets is
  // byte-identical.
  HSSEngine *replay{};
  require(hss_engine_create(&c, &replay) == HSS_OK, "replay create");
  require(hss_engine_set_source_is_synthetic(replay, 1) == HSS_OK,
          "replay synthetic");
  auto drain_large = [&]() {
    while (stats(replay).lifecycle_state != 1)
      require(hss_engine_step(replay, 100000) == HSS_OK, "replay large budget");
  };
  auto replay_epoch = epoch(1);
  require(hss_engine_set_epoch(replay, &replay_epoch) == HSS_OK,
          "replay epoch");
  drain_large();
  auto replay_mesh = room();
  auto replay_event = replay_mesh.event(2);
  require(hss_engine_submit_mesh_event(replay, &replay_event) == HSS_OK,
          "replay mesh");
  drain_large();
  auto replay_floor = identity();
  require(hss_engine_set_plan_reference(replay, &replay_floor, 1.2) == HSS_OK,
          "replay floor control");
  drain_large();
  auto replay_snapshot = snapshot(replay);
  require(json(replay_snapshot) == full_json,
          "revision and IDs independent of step scheduling");
  hss_snapshot_release(replay_snapshot);
  hss_engine_destroy(replay);
  hss_engine_destroy(e);
  require(json(full) == full_json &&
              vertices(full).size() == full_vertices.size(),
          "snapshot after engine destruction");
  for (auto s : {unconfirmed, full, partial, isolated_snapshot, transformed,
                 removed, readded, high})
    hss_snapshot_release(s);
  // Multiple nested closed sections do not justify a fixed-obstruction hole.
  require(hss_engine_create(&c, &e) == HSS_OK, "multiple patch create");
  require(hss_engine_set_source_is_synthetic(e, 1) == HSS_OK,
          "patch synthetic origin");
  ep = epoch(1);
  require(hss_engine_set_epoch(e, &ep) == HSS_OK && drain(e) == HSS_OK,
          "patch epoch");
  floor = identity();
  require(hss_engine_set_plan_reference(e, &floor, 1.2) == HSS_OK &&
              drain(e) == HSS_OK,
          "patch floor");
  auto outer = room(), inner = room();
  for (size_t i = 0; i < inner.v.size(); i += 3) {
    inner.v[i] = inner.v[i] * 0.1f + 1;
    inner.v[i + 2] = inner.v[i + 2] * 0.1f - 1;
  }
  auto offset = uint32_t(outer.v.size() / 3);
  outer.v.insert(outer.v.end(), inner.v.begin(), inner.v.end());
  for (auto index : inner.t)
    outer.t.push_back(index + offset);
  // Unclassified shelf top and sloped coffer retain all original 3D heights.
  offset = uint32_t(outer.v.size() / 3);
  outer.v.insert(outer.v.end(), {1, 2, -1, 2, 2, -1, 1, 2, -2, 1, 3, -1, 2,
                                 3.2f, -1, 1, 3.4f, -2});
  outer.t.insert(outer.t.end(), {offset, offset + 1, offset + 2, offset + 3,
                                 offset + 4, offset + 5});
  event = outer.event(2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "multiple patch mesh");
  auto patches = snapshot(e);
  auto patch_json = json(patches);
  auto pv = vertices(patches);
  require(counts(patches).triangle_count == 38 && pv.back().y == double(3.4f),
          "shelf/coffer captured without flattening");
  require(patch_json.find("unclassified_slice_loops") != std::string::npos &&
              patch_json.find("\"hole_node_ids\":[]") != std::string::npos &&
              patch_json.find("\"area_m2\":null") != std::string::npos,
          "nested loops retain unknown hole classification");
  // Calibrated sceneDepth and smoothed preview retained separately; neither
  // creates additional mesh or independent metric support in this backend.
  float depth = 2;
  HSSFrame frame{};
  frame.abi_version = 1;
  frame.struct_size = sizeof frame;
  frame.capture_id = ident(2);
  frame.event_sequence = 3;
  frame.T_world_camera = identity();
  frame.depth_intrinsics = {200, 200, 0, 0};
  frame.depth_width = frame.depth_height = 1;
  frame.depth_row_stride_bytes = frame.depth_byte_length = 4;
  frame.depth_bytes = reinterpret_cast<uint8_t *>(&depth);
  auto memory_before = stats(e).working_bytes;
  require(hss_engine_submit_frame(e, &frame) == HSS_OK && drain(e) == HSS_OK,
          "calibrated depth retained");
  frame.event_sequence = 4;
  frame.depth_source = 1;
  require(hss_engine_submit_frame(e, &frame) == HSS_OK && drain(e) == HSS_OK,
          "smoothed preview retained");
  depth = 999;
  auto after_depth = snapshot(e);
  require(counts(after_depth).triangle_count == 38 &&
              stats(e).working_bytes > memory_before,
          "depth payload retention without fabricated mesh");
  hss_snapshot_release(after_depth);
  hss_snapshot_release(patches);
  hss_engine_destroy(e);
  // Hard reservation bound includes pinned immutable versions and no silent
  // truncation.
  c = config(200000);
  require(hss_engine_create(&c, &e) == HSS_OK, "small budget create");
  require(hss_engine_set_source_is_synthetic(e, 1) == HSS_OK,
          "budget fixture synthetic origin");
  ep = epoch(1);
  require(hss_engine_set_epoch(e, &ep) == HSS_OK && drain(e) == HSS_OK,
          "small epoch");
  event = isolated.event(2);
  require(hss_engine_submit_mesh_event(e, &event) == HSS_OK &&
              drain(e) == HSS_OK,
          "small mesh");
  std::vector<HSSSnapshot *> held;
  HSSStatus last = HSS_OK;
  for (uint64_t revision = 2; revision < 30 && last == HSS_OK; ++revision) {
    held.push_back(snapshot(e));
    event = isolated.event(revision + 1, 1, revision);
    last = hss_engine_submit_mesh_event(e, &event);
    if (last == HSS_OK)
      last = drain(e);
    require(stats(e).working_bytes <= c.max_working_bytes,
            "budget accounting hard bound");
  }
  require(last == HSS_RESOURCE_LIMIT,
          "pinned versions enforce resource backpressure");
  for (auto s : held)
    hss_snapshot_release(s);
  hss_engine_destroy(e);
  std::cout << "Live C ABI reconstruction, lifecycle, plan, transforms, "
               "immutable snapshots, bounded reservations passed; maximum 50 "
               "us requested slice observed "
            << max_slice_us
            << " us (cooperative quantum, host ASan; not device latency).\n";
}

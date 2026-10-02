#include "hss/hss_api.h"
#include "hss/obj_mtl_exporter.hpp"
#include "hss/hss_project_store.h"
#include <filesystem>
#include <unistd.h>
#include "hss/scene_editing.hpp"
#include "hss/live_reconstruction.hpp"
#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <sstream>
#include <iomanip>
#include <vector>

namespace {
void require(bool ok, const char *message) {
  if (!ok) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

HSSId ident(uint8_t value) {
  HSSId id{};
  id.bytes[0] = value;
  id.bytes[6] = 0x40U; // Valid synthetic UUID version/variant for Scene validation.
  id.bytes[8] = 0x80U;
  return id;
}

HSSMat4d identity() {
  HSSMat4d matrix{};
  matrix.column_major[0] = matrix.column_major[5] =
      matrix.column_major[10] = matrix.column_major[15] = 1.0;
  return matrix;
}
HSSConfig config() {
  HSSConfig value{};
  value.abi_version = HSS_ABI_VERSION;
  value.struct_size = sizeof(value);
  value.project_id = ident(1);
  value.structure_voxel_m = 0.02;
  value.detail_voxel_m = 0.01;
  value.truncation_m = 0.04;
  value.max_working_bytes = 64U * 1024U * 1024U;
  value.max_pending_frames = 2U;
  value.deterministic_seed = 7U;
  return value;
}

HSSEngineStats stats(HSSEngine *engine) {
  HSSEngineStats value{};
  require(hss_engine_get_stats(engine, &value) == HSS_OK, "stats");
  return value;
}

void drain(HSSEngine *engine, uint64_t budget = 50U,
           std::vector<double> *timings = nullptr) {
  for (size_t step = 0; step < 200000U; ++step) {
    if (stats(engine).lifecycle_state == 1U)
      return;
    const auto begin = std::chrono::steady_clock::now();
    require(hss_engine_step(engine, budget) == HSS_OK, "step");
    if (timings)
      timings->push_back(std::chrono::duration<double, std::micro>(
          std::chrono::steady_clock::now() - begin).count());
  }
  require(false, "bounded work completes");
}
std::string scene_json(HSSEngine *engine) {
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "acquire snapshot");
  uint64_t required = 0U;
  require(hss_snapshot_copy_scene_json(snapshot, nullptr, 0U, &required) ==
              HSS_BUFFER_TOO_SMALL,
          "scene query");
  std::string json(required, '\0');
  require(hss_snapshot_copy_scene_json(
              snapshot, reinterpret_cast<uint8_t *>(json.data()), required,
              &required) == HSS_OK,
          "scene copy");
  hss_snapshot_release(snapshot);
  return json;
}

std::vector<uint32_t> raw_triangles(HSSEngine *engine) {
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "acquire triangles");
  uint64_t required = 0U;
  require(hss_snapshot_copy_triangles(snapshot, nullptr, 0U, &required) ==
              HSS_BUFFER_TOO_SMALL,
          "triangle query");
  std::vector<uint32_t> result(required);
  require(hss_snapshot_copy_triangles(snapshot, result.data(), required,
                                      &required) == HSS_OK,
          "triangle copy");
  hss_snapshot_release(snapshot);
  return result;
}
size_t plane_count(const std::string &json) {
  constexpr const char *needle = "\"kind\":\"plane\"";
  size_t count = 0U;
  size_t offset = 0U;
  while ((offset = json.find(needle, offset)) != std::string::npos) {
    ++count;
    offset += 8U;
  }
  return count;
}

struct Mesh {
  std::vector<float> vertices;
  std::vector<uint32_t> triangles;
};

Mesh shallow_crease(float width, bool bridge_first) {
  const float angle = float(0.2 * std::acos(-1.0) / 180.0);
  const float z = width * std::tan(angle);
  Mesh mesh;
  mesh.vertices = {
      -width, 0.0F, 0.0F, -width, 3.0F, 0.0F,
       0.0F,  0.0F, 0.0F,  0.0F,  3.0F, 0.0F,
       width, 0.0F, z,     width, 3.0F, z,
  };
  const std::vector<uint32_t> natural{
      0U, 2U, 3U, 0U, 3U, 1U, 2U, 4U, 5U, 2U, 5U, 3U};
  const std::vector<uint32_t> bridge{
      2U, 5U, 3U, 0U, 2U, 3U, 0U, 3U, 1U, 2U, 4U, 5U};
  mesh.triangles = bridge_first ? bridge : natural;
  return mesh;
}
struct Result {
  std::string json;
  std::vector<uint32_t> triangles;
  size_t planes{};
  std::vector<double> step_us;
};

Result reconstruct_many(const std::vector<Mesh> &meshes,
                        uint64_t step_budget = 50U,
                        bool floor_confirmed = false) {
  auto configuration = config();
  HSSEngine *engine{};
  require(hss_engine_create(&configuration, &engine) == HSS_OK, "create");
  require(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
          "synthetic");

  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = ident(2);
  epoch.event_sequence = 1U;
  epoch.relationship_state = 1U;
  epoch.transform_revision = 1U;
  epoch.T_project_world = identity();
  require(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "epoch");
  drain(engine);

  if (floor_confirmed) {
    const auto floor = identity();
    require(hss_engine_set_plan_reference(engine, &floor, 1.2) == HSS_OK,
            "confirmed floor");
    drain(engine);
  }
  std::vector<uint32_t> expected_triangles;
  std::vector<HSSVec3d> expected_vertices;
  std::vector<double> timings;
  for (size_t anchor = 0; anchor < meshes.size(); ++anchor) {
    const auto &mesh = meshes[anchor];
    const uint32_t base = uint32_t(expected_vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i += 3U)
      expected_vertices.push_back(
          {mesh.vertices[i], mesh.vertices[i + 1U], mesh.vertices[i + 2U]});
    for (uint32_t vertex : mesh.triangles)
      expected_triangles.push_back(base + vertex);
    HSSMeshEvent event{};
    event.abi_version = HSS_ABI_VERSION;
    event.struct_size = sizeof(event);
    event.capture_id = ident(2);
    event.anchor_id = ident(uint8_t(3U + anchor));
    event.event_sequence = 2U + anchor;
    event.anchor_revision = 1U;
    event.T_world_anchor = identity();
    event.vertices_xyz = mesh.vertices.data();
    event.vertex_count = mesh.vertices.size() / 3U;
    event.triangle_indices = mesh.triangles.data();
    event.triangle_count = mesh.triangles.size() / 3U;
    require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK, "mesh");
    const auto previous_revision = stats(engine).scene_revision;
    require(hss_engine_step(engine, 1U) == HSS_OK, "initial tiny step");
    require(stats(engine).scene_revision == previous_revision,
            "tiny step cannot synchronously publish an anchor");
    drain(engine, step_budget, &timings);
  }
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "vertex snapshot");
  std::vector<HSSVec3d> actual_vertices(expected_vertices.size());
  uint64_t count = 0U;
  require(hss_snapshot_copy_vertices(snapshot, actual_vertices.data(),
              actual_vertices.size(), &count) == HSS_OK &&
              count == expected_vertices.size(), "raw vertex count");
  for (size_t i = 0; i < expected_vertices.size(); ++i)
    require(actual_vertices[i].x == expected_vertices[i].x &&
                actual_vertices[i].y == expected_vertices[i].y &&
                actual_vertices[i].z == expected_vertices[i].z,
            "raw vertex values preserved exactly");
  hss_snapshot_release(snapshot);
  Result result;
  result.json = scene_json(engine);
  result.triangles = raw_triangles(engine);
  result.planes = plane_count(result.json);
  result.step_us = std::move(timings);
  require(result.triangles == expected_triangles,
          "raw triangles preserved with correct per-anchor global base");
  if (!hss::scene_editing::scene_counts(result.json).has_value())
    std::cerr << "Invalid emitted synthetic scene: " << result.json << "\n";
  require(hss::scene_editing::scene_counts(result.json).has_value(),
          "complete scene validates, including unique issue/entity IDs");
  hss_engine_destroy(engine);
  return result;
}
Result reconstruct(const Mesh &mesh) { return reconstruct_many({mesh}); }

Mesh wall_chain() {
  Mesh result;
  const std::array<std::pair<float, float>, 6> contour{{
      {0.0F, 0.0F}, {2.0F, 0.0F}, {2.0F, 0.50F},
      {2.15F, 0.50F}, {2.15F, 0.0F}, {4.0F, 0.0F}}};
  for (const auto &[x, z] : contour)
    result.vertices.insert(result.vertices.end(), {x, 0.0F, z, x, 3.0F, z});
  for (uint32_t i = 0U; i < 5U; ++i) {
    const uint32_t a = 2U * i, b = a + 2U;
    result.triangles.insert(result.triangles.end(), {a, b, b+1U, a, b+1U, a+1U});
  }
  return result;
}

Mesh grid(uint32_t cells, float noise_m) {
  Mesh result;
  for (uint32_t y = 0U; y <= cells; ++y)
    for (uint32_t x = 0U; x <= cells; ++x) {
      const int jitter = int((x * 17U + y * 31U) % 7U) - 3;
      result.vertices.insert(result.vertices.end(),
          {float(x)*0.25F, float(y)*0.25F, float(jitter)*noise_m/3.0F});
    }
  for (uint32_t y = 0U; y < cells; ++y)
    for (uint32_t x = 0U; x < cells; ++x) {
      const uint32_t a = y*(cells+1U)+x, b=a+1U, c=a+cells+1U, d=c+1U;
      result.triangles.insert(result.triangles.end(), {a,b,d,a,d,c});
    }
  return result;
}

Mesh disconnected(float offset) {
  Mesh result;
  result.vertices = {0,0,0, 1,0,0, 1,3,0, 0,3,0,
                     2,0,offset, 3,0,offset, 3,3,offset, 2,3,offset};
  result.triangles = {0,1,2, 0,2,3, 4,5,6, 4,6,7};
  return result;
}

void permutations(const Mesh &mesh, size_t expected, const char *name) {
  const auto baseline = reconstruct_many({mesh}, 50U, true);
  require(baseline.planes == expected, name);
  std::vector<std::array<uint32_t,3>> faces;
  for (size_t i=0; i<mesh.triangles.size(); i+=3U)
    faces.push_back({mesh.triangles[i],mesh.triangles[i+1U],mesh.triangles[i+2U]});
  for (uint32_t seed=0U; seed<16U; ++seed) {
    std::mt19937 random(seed);
    std::shuffle(faces.begin(), faces.end(), random);
    Mesh shuffled = mesh;
    shuffled.triangles.clear();
    for (const auto &face : faces)
      shuffled.triangles.insert(shuffled.triangles.end(), face.begin(), face.end());
    const auto value = reconstruct_many({shuffled}, 50U, true);
    require(value.planes == expected, name);
    require(value.json == baseline.json, "entire derived Scene permutation equality");
  }
}

// Count actual internal advance() quanta, not wall-clock-dependent facade calls.
// Every duplicate is scanned, accumulated, indexed, marked, skipped for slicing
// and retired independently. Previously all marks and retirements were batched.
void duplicate_heavy_quanta(size_t count) {
  hss::live::Reconstruction core(config());
  require(core.set_synthetic(1U) == HSS_OK, "direct synthetic origin");
  auto epoch = std::make_shared<hss::live::Evidence>();
  epoch->event.kind = hss::ingress::EventKind::epoch;
  epoch->event.capture_id = ident(2);
  epoch->event.event_sequence = 1U;
  epoch->event.epoch.capture_id = ident(2);
  epoch->event.epoch.event_sequence = 1U;
  epoch->event.epoch.relationship_state = 1U;
  epoch->event.epoch.transform_revision = 1U;
  epoch->event.epoch.T_project_world = identity();
  epoch->lease = hss::live::reserve(core.budget(),
      hss::live::event_reservation(epoch->event));
  require(bool(epoch->lease) && core.begin(epoch) == HSS_OK, "direct epoch");
  while (core.busy()) core.advance();
  const auto accepted = core.snapshot();
  auto evidence = std::make_shared<hss::live::Evidence>();
  auto &event = evidence->event;
  event.kind = hss::ingress::EventKind::mesh_add;
  event.capture_id = ident(2);
  event.anchor_id = ident(3);
  event.event_sequence = 2U;
  event.anchor_revision = 1U;
  event.mesh.T_world_anchor = identity();
  event.mesh.vertex_count = 3U;
  event.mesh.triangle_count = count;
  const std::array<float,9> vertices{0,0,0, 1,0,0, 0,1,0};
  const std::array<uint32_t,3> face{0U,1U,2U};
  event.payload.resize(sizeof(vertices) + count*sizeof(face));
  std::memcpy(event.payload.data(), vertices.data(), sizeof(vertices));
  for (size_t i=0; i<count; ++i)
    std::memcpy(event.payload.data()+sizeof(vertices)+i*sizeof(face),
                face.data(), sizeof(face));
  event.payload_bytes = event.payload.size();
  evidence->lease = hss::live::reserve(core.budget(),
      hss::live::event_reservation(event));
  require(bool(evidence->lease) && core.begin(evidence) == HSS_OK,
          "direct duplicate-heavy admission");
  size_t quanta=0U;
  while (core.busy() && quanta < 12U*count+256U) {
    const bool completed = core.advance();
    ++quanta;
    if (!completed)
      require(core.snapshot() == accepted, "no partial snapshot during cleanup");
  }
  require(!core.busy() && core.status() == HSS_OK,
          "duplicate-heavy build completes within bounded stage count");
  std::cout << "duplicate_triangles=" << count << " advance_quanta=" << quanta << '\n';
  require(quanta >= 6U*count,
          "ambiguous marking and scratch retirement must each yield per root");
  const auto result=core.snapshot();
  require(result->triangles.size()==3U*count && plane_count(result->json)==0U,
          "duplicate-heavy raw evidence kept without fitted bridging");
  for(size_t i=0; i<result->triangles.size(); ++i)
    require(result->triangles[i]==face[i%3U], "duplicate-heavy indices exact");
  require(hss::scene_editing::scene_counts(result->json).has_value(),
          "duplicate-heavy output scene validates");
}


// A connected smooth strip is not necessarily one plane. Each neighbouring
// facet passes the existing pairwise gate, but the full arc does not.
Mesh curved_strip(uint32_t facets, double turn_degrees) {
  Mesh mesh;
  double x = 0.0, z = 0.0;
  const double turn = turn_degrees * std::acos(-1.0) / 180.0;
  for (uint32_t i = 0U; i <= facets; ++i) {
    mesh.vertices.insert(mesh.vertices.end(),
                         {float(x), 0.0F, float(z), float(x), 3.0F, float(z)});
    if (i < facets) {
      const double angle = (double(i) - double(facets - 1U) / 2.0) * turn;
      x += 0.1 * std::cos(angle);
      z += 0.1 * std::sin(angle);
      const uint32_t a = 2U * i, b = a + 2U;
      mesh.triangles.insert(mesh.triangles.end(),
                            {a, b, b + 1U, a, b + 1U, a + 1U});
    }
  }
  return mesh;
}


Mesh unsampled_bend() {
  Mesh mesh = curved_strip(600U, 0.0);
  double z = 0.0;
  for (uint32_t i = 595U; i <= 600U; ++i) {
    z += 0.1 * std::tan(double(i - 594U) * 0.2 * std::acos(-1.0) / 180.0);
    mesh.vertices[6U * i + 2U] = mesh.vertices[6U * i + 5U] = float(z);
  }
  // Independently verify this adversary: every one of the currently selected
  // 96 vertices is on z=0, while the omitted tip exceeds the 2mm fit threshold.
  std::vector<std::pair<uint64_t, uint32_t>> ranks;
  for (uint32_t i = 0U; i < mesh.vertices.size()/3U; ++i) {
    uint64_t value = (config().deterministic_seed ^ uint64_t(i)) +
                     0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    ranks.emplace_back(value ^ (value >> 31U), i);
  }
  std::sort(ranks.begin(), ranks.end());
  for (size_t i = 0; i < 96U; ++i)
    require(mesh.vertices[3U * ranks[i].second + 2U] == 0.0F,
            "adversarial bend must lie outside all 96 selected samples");
  require(mesh.vertices.back() > 0.007F, "omitted raw tip exceeds 7mm");
  return mesh;
}

void durable_component_support() {
  char temporary[] = "/tmp/hss-component-support-XXXXXX";
  require(::mkdtemp(temporary) != nullptr, "private synthetic test directory");
  const std::string directory = temporary;
  HSSProjectStoreConfig sc{};
  sc.abi_version = HSS_PROJECT_STORE_ABI_VERSION; sc.struct_size = sizeof(sc);
  sc.project_id = ident(1);
  sc.trusted_root_directory_utf8 = {
      reinterpret_cast<const uint8_t *>(directory.data()), directory.size()};
  sc.open_mode = HSS_PROJECT_STORE_CREATE_NEW; sc.source_is_synthetic = 1U;
  sc.max_epoch_records = sc.max_depth_records = sc.max_mesh_records =
      sc.max_command_records = 8U;
  sc.max_retained_bytes = 16U * 1024U * 1024U;
  sc.max_record_bytes = 2U * 1024U * 1024U;
  sc.max_pending_bytes = 8U * 1024U * 1024U;
  HSSProjectStore *store{};
  require(hss_project_store_create(&sc, &store) == HSS_OK, "create actual store");
  auto cfg = config(); HSSEngine *engine{};
  require(hss_engine_create(&cfg, &engine) == HSS_OK, "create durable engine");
  require(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK, "synthetic store");
  auto context = [](uint64_t sequence) {
    HSSProjectEventContext c{};
    c.abi_version = HSS_PROJECT_STORE_ABI_VERSION; c.struct_size = sizeof(c);
    c.capture_id = ident(2); c.event_sequence = sequence;
    c.accepted_scene_revision = sequence; c.monotonic_time_s = double(sequence);
    return c;
  };
  HSSProjectStoreReceipt receipt{};
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION; epoch.struct_size = sizeof(epoch);
  epoch.capture_id = ident(2); epoch.event_sequence = 1U;
  epoch.relationship_state = 1U; epoch.transform_revision = 1U;
  epoch.T_project_world = identity();
  require(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "durable epoch");
  drain(engine, 1U); auto c = context(1U);
  require(hss_project_store_append_epoch(store, &c, &epoch, &receipt) == HSS_OK,
          "append epoch");
  const auto flat = curved_strip(100U, 0.0), curved = curved_strip(100U, 0.2);
  HSSMeshEvent event{};
  event.abi_version = HSS_ABI_VERSION; event.struct_size = sizeof(event);
  event.capture_id = ident(2); event.anchor_id = ident(3);
  event.T_world_anchor = identity();
  auto submit = [&](const Mesh &mesh, uint64_t sequence, uint64_t revision) {
    event.event_sequence = sequence; event.anchor_revision = revision;
    event.kind = revision == 1U ? 0U : 1U;
    event.vertices_xyz = mesh.vertices.data(); event.vertex_count = mesh.vertices.size()/3U;
    event.triangle_indices = mesh.triangles.data(); event.triangle_count = mesh.triangles.size()/3U;
    require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK, "source refinement");
    drain(engine, sequence % 2U == 0U ? 1U : 50U);
    require(stats(engine).scene_revision == sequence, "exact single source revision");
    c = context(sequence);
    require(hss_project_store_append_mesh_event(store, &c, &event, &receipt) == HSS_OK,
            "append exact replacement source");
    const auto json = scene_json(engine);
    require(hss::scene_editing::scene_counts(json).has_value(), "valid replacement scene");
    const bool is_flat = &mesh == &flat;
    require(plane_count(json) == (is_flat ? 1U : 0U), "classification follows fresh support");
    require((json.find("partial_plane_support") == std::string::npos) == is_flat,
            "warning appears and clears with actual source support");
  };
  submit(flat, 2U, 1U);
  HSSProjectPlanReference plan{};
  plan.abi_version = HSS_PROJECT_STORE_ABI_VERSION; plan.struct_size = sizeof(plan);
  plan.T_project_floor = identity(); plan.cut_height_m = 1.2;
  plan.before_scene_revision = 2U; plan.after_scene_revision = 3U;
  require(hss_engine_set_plan_reference(engine, &plan.T_project_floor, 1.2) == HSS_OK,
          "durable floor reference");
  drain(engine, 1U); c = context(3U);
  require(hss_project_store_append_plan_reference(store, &c, &plan, &receipt) == HSS_OK,
          "append floor reference");
  submit(curved, 4U, 2U); submit(flat, 5U, 3U); submit(curved, 6U, 4U);
  const auto accepted = scene_json(engine);
  auto verify_raw = [&]() {
    require(raw_triangles(engine) == curved.triangles, "all raw triangles retained through replay");
    HSSSnapshot *snapshot{};
    require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK, "raw replay snapshot");
    std::vector<HSSVec3d> points(curved.vertices.size()/3U); uint64_t count{};
    require(hss_snapshot_copy_vertices(snapshot, points.data(), points.size(), &count) == HSS_OK &&
                count == points.size(), "all raw points retained through replay");
    for (size_t i = 0; i < points.size(); ++i)
      require(points[i].x == curved.vertices[3U*i] && points[i].y == curved.vertices[3U*i+1U] &&
                  points[i].z == curved.vertices[3U*i+2U], "no flattening of raw observed shape");
    hss_snapshot_release(snapshot);
  };
  verify_raw(); HSSProjectCheckpointResult cp{};
  require(hss_project_store_checkpoint(store, &cp) == HSS_OK &&
              cp.durable_event_count == 6U && cp.durability_state == 1U, "durable checkpoint");
  hss_engine_destroy(engine); hss_project_store_destroy(store);
  sc.open_mode = HSS_PROJECT_STORE_OPEN_EXISTING;
  require(hss_project_store_create(&sc, &store) == HSS_OK, "reopen project");
  require(hss_engine_create(&cfg, &engine) == HSS_OK, "create replay engine");
  HSSProjectReplayResult replay{};
  require(hss_project_store_replay(store, engine, &replay) == HSS_OK &&
              replay.applied_event_count == 6U && replay.engine_status == HSS_OK,
          "replay original source and floor events");
  require(scene_json(engine) == accepted, "byte-equivalent warnings and geometry on durable replay");
  verify_raw(); hss_engine_destroy(engine); hss_project_store_destroy(store);
  std::filesystem::remove_all(directory);
}

void full_component_support_regression() {
  const auto mesh = curved_strip(100U, 0.2);
  const auto result = reconstruct_many({mesh}, 50U, true);
  std::cout << "Curved connected strip: vertices=" << mesh.vertices.size()/3U
            << " triangles=" << mesh.triangles.size()/3U
            << " emitted_planes=" << result.planes << std::endl;
  require(result.planes == 0U,
          "a subset plane cannot stand for a whole curved component");
  require(result.json.find("partial_plane_support") != std::string::npos,
          "rejected whole-component fit has an explicit issue");
  const auto omitted = unsampled_bend();
  const auto tail = reconstruct_many({omitted}, 1U, true);
  require(tail.planes == 0U && tail.json.find("partial_plane_support") != std::string::npos,
          "a missed sample cannot hide nonplanar raw support");
  auto ceiling = mesh;
  for (size_t i = 0; i < ceiling.vertices.size(); i += 3U) {
    const float previous_y = ceiling.vertices[i+1U];
    ceiling.vertices[i+1U] = 3.0F + ceiling.vertices[i+2U];
    ceiling.vertices[i+2U] = -previous_y;
  }
  require(reconstruct_many({ceiling}, 1U, true).planes == 0U,
          "curved overhead surface remains captured rather than flattened");
  permutations(mesh, 0U, "curved component triangle permutations");
  const auto multi = reconstruct_many({mesh, ceiling}, 50U, true);
  require(multi.planes == 0U, "independent nonplanar anchors retain unique valid issues");
  durable_component_support();
  std::cout << "Full original support, unsampled bend, ceiling and durable refinement passed\n";
}


// Authored metre-domain challenge. Zero sensor noise, Float32 input encoding,
// omitted skylight observations, and no semantic labels supplied to the core.
// These readers inspect our already scene_counts-validated canonical output;
// they are test utilities, not a replacement project JSON parser.
std::vector<std::string> scene_records(const std::string &json, const std::string &key) {
  const std::string prefix = "\"" + key + "\":[";
  const auto found = json.find(prefix);
  require(found != std::string::npos, "canonical scene collection exists");
  std::vector<std::string> result;
  bool quoted = false, escaped = false;
  size_t depth = 0U, start = 0U;
  for (size_t i = found + prefix.size(); i < json.size(); ++i) {
    const char c = json[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') quoted = true;
    else if (c == '{') { if (depth++ == 0U) start = i; }
    else if (c == '}') {
      require(depth != 0U, "balanced canonical record");
      if (--depth == 0U) result.push_back(json.substr(start, i - start + 1U));
    } else if (c == ']' && depth == 0U) return result;
  }
  require(false, "closed canonical collection"); return {};
}
std::string record_text(const std::string &record, const std::string &key) {
  const std::string prefix = "\"" + key + "\":\"";
  const auto pos = record.find(prefix);
  require(pos != std::string::npos, "canonical text exists");
  const auto start = pos + prefix.size(), end = record.find('"', start);
  require(end != std::string::npos, "canonical text terminates");
  return record.substr(start, end - start);
}
double record_number(const std::string &record, const std::string &key) {
  const std::string prefix = "\"" + key + "\":";
  const auto pos = record.find(prefix);
  require(pos != std::string::npos, "canonical number exists");
  return std::stod(record.substr(pos + prefix.size()));
}
HSSVec3d record_point(const std::string &record, const std::string &key) {
  const std::string prefix = "\"" + key + "\":[";
  const auto pos = record.find(prefix);
  require(pos != std::string::npos, "canonical point exists");
  const auto start = pos + prefix.size(), end = record.find(']', start);
  require(end != std::string::npos, "canonical point terminates");
  auto values = record.substr(start, end - start);
  std::replace(values.begin(), values.end(), ',', ' ');
  std::istringstream input(values); HSSVec3d point{};
  require(bool(input >> point.x >> point.y >> point.z), "three finite coordinates");
  require(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z), "finite point");
  return point;
}
HSSVec3d delta(HSSVec3d a, HSSVec3d b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
double length3(HSSVec3d p) { return std::hypot(p.x,p.y,p.z); }
double product3(HSSVec3d a,HSSVec3d b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
HSSVec3d cross3(HSSVec3d a,HSSVec3d b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
struct MixedRoom {
  std::vector<Mesh> meshes{3U};
  std::vector<std::array<HSSVec3d,4>> faces;
  std::vector<std::pair<HSSVec3d,HSSVec3d>> cuts;
};
void add_face(MixedRoom &room, size_t anchor, std::array<HSSVec3d,4> face) {
  auto &mesh=room.meshes.at(anchor); const uint32_t base=uint32_t(mesh.vertices.size()/3U);
  for (auto p:face) mesh.vertices.insert(mesh.vertices.end(),{float(p.x),float(p.y),float(p.z)});
  mesh.triangles.insert(mesh.triangles.end(),{base,base+1U,base+2U,base,base+2U,base+3U});
  room.faces.push_back(face);
}
MixedRoom mixed_room(double upper=3.5) {
  MixedRoom room;
  const std::vector<std::pair<double,double>> outer{{0,0},{6,0},{6,-5},{4,-5},
      {4,-4.5},{3.85,-4.5},{3.85,-5},{2,-5},{0,-4}};
  const std::vector<std::pair<double,double>> column{{1,-1},{1.3,-1},{1.3,-1.5},{1,-1.5}};
  auto walls=[&](const auto &ring,size_t anchor,double height) {
    for(size_t i=0;i<ring.size();++i) {
      const auto [x,z]=ring[i]; const auto [u,v]=ring[(i+1U)%ring.size()];
      add_face(room,anchor,{{{x,0,z},{u,0,v},{u,height,v},{x,height,z}}});
      room.cuts.push_back({{x,1.2,z},{u,1.2,v}});
    }
  };
  walls(outer,0U,3.6); walls(column,2U,2.8);
  add_face(room,1U,{{{0,2.8,0},{2,2.8,0},{2,2.8,-2},{0,2.8,-2}}});
  add_face(room,1U,{{{2,3.2,0},{4,3.2,0},{4,3.2,-2},{2,3.2,-2}}});
  add_face(room,1U,{{{2,2.8,0},{2,3.2,0},{2,3.2,-2},{2,2.8,-2}}});
  add_face(room,1U,{{{4,3.2,0},{6,upper,0},{6,upper,-2},{4,3.2,-2}}});
  // Four separately observed panels around an unobserved 1m by 1m skylight.
  for (const auto &r:std::vector<std::array<double,4>>{{0,6,-2,-2.5},{0,6,-3.5,-4},
                                                   {0,2.5,-2.5,-3.5},{3.5,6,-2.5,-3.5}})
    add_face(room,1U,{{{r[0],3.3,r[2]},{r[1],3.3,r[2]},{r[1],3.3,r[3]},{r[0],3.3,r[3]}}});
  return room;
}
void verify_mixed_geometry(const std::string &json,const MixedRoom &reference, bool open = false) {
  require(hss::scene_editing::scene_counts(json).has_value(),"complete mixed scene validates");
  std::vector<std::pair<HSSVec3d,double>> planes;
  for(const auto &surface:scene_records(json,"surfaces")) {
    if(record_text(surface,"kind")=="plane") {
      require(surface.find("\"semantic_origin\":\"unknown\"")!=std::string::npos,
              "unclassified planes do not gain room semantics");
      planes.push_back({record_point(surface,"normal_unit"),record_number(surface,"offset_m")});
    }
  }
  std::cout<<"mixed reference_faces="<<reference.faces.size()<<" actual_planes="<<planes.size()<<std::endl;
  require(planes.size()==reference.faces.size(),"every supported ceiling/wall/return has a separate fitted plane");
  std::vector<bool> used(planes.size()); double max_distance=0.0,max_sine=0.0;
  for(const auto &face:reference.faces) {
    auto normal=cross3(delta(face[1],face[0]),delta(face[3],face[0]));
    const auto norm=length3(normal); require(norm>0.0,"nondegenerate authored reference");
    normal={normal.x/norm,normal.y/norm,normal.z/norm};
    size_t selected=planes.size(); double best=1e100,chosen_sine=0.0;
    for(size_t i=0;i<planes.size();++i) if(!used[i]) {
      const auto &[n,d]=planes[i]; double distance=0.0;
      for(auto p:face) distance=std::max(distance,std::abs(product3(n,p)+d));
      const double sine=length3(cross3(n,normal));
      if(distance+sine<best) {best=distance+sine;selected=i;chosen_sine=sine;}
    }
    require(selected<planes.size(),"one-to-one reference plane matching"); used[selected]=true;
    double distance=0.0;
    for(auto p:face) distance=std::max(distance,std::abs(product3(planes[selected].first,p)+planes[selected].second));
    max_distance=std::max(max_distance,distance); max_sine=std::max(max_sine,chosen_sine);
    require(distance<1e-6 && chosen_sine<1e-6,"plane height/orientation follows authored reference, not flattened levels");
  }
  std::map<std::string,HSSVec3d> nodes;
  std::map<std::string,size_t> degree;
  for(const auto &node:scene_records(json,"nodes")) nodes.emplace(record_text(node,"id"),record_point(node,"position_m"));
  std::vector<std::vector<std::pair<double,double>>> intervals(reference.cuts.size());
  double max_endpoint_error=0.0;
  for(const auto &edge:scene_records(json,"edges")) {
    const auto start=record_text(edge,"start_node_id"),end=record_text(edge,"end_node_id");
    ++degree[start]; ++degree[end]; const auto a=nodes.at(start),b=nodes.at(end);
    size_t matches=0U;
    for(size_t i=0;i<reference.cuts.size();++i) {
      const auto &[p,q]=reference.cuts[i]; const auto direction=delta(q,p); const double l2=product3(direction,direction);
      const double ta=product3(delta(a,p),direction)/l2,tb=product3(delta(b,p),direction)/l2;
      auto error=[&](HSSVec3d x,double t) {return length3(delta(x,{p.x+t*direction.x,p.y+t*direction.y,p.z+t*direction.z}));};
      const double ea=error(a,ta),eb=error(b,tb);
      if(ea<1e-6 && eb<1e-6 && ta>=-1e-6 && ta<=1.0+1e-6 && tb>=-1e-6 && tb<=1.0+1e-6) {
        ++matches; intervals[i].push_back(std::minmax(ta,tb));
        max_endpoint_error=std::max({max_endpoint_error,ea,eb});
      }
    }
    require(matches==1U,"each measured cut fragment belongs to exactly one finite reference face");
  }
  size_t endpoints = 0U;
  for (const auto &[id, point] : nodes) {
    (void)point;
    if (degree[id] == 1U) ++endpoints;
    else require(degree[id] == 2U, "no invented branches in reference topology");
  }
  require(endpoints == (open ? 2U : 0U), "missing return stays open; complete reference closes");
  double max_length_error=0.0;
  for(size_t i=0;i<intervals.size();++i) {
    auto spans=intervals[i]; std::sort(spans.begin(),spans.end()); double cursor=0.0,total=0.0;
    require(!spans.empty(),"narrow front and both returns have actual support");
    for(const auto &[a,b]:spans) { require(std::abs(a-cursor)<1e-6,"no missing or overlapping cut intervals"); total+=b-a;cursor=b; }
    require(std::abs(cursor-1.0)<1e-6,"complete finite reference edge covered");
    max_length_error=std::max(max_length_error,std::abs(total-1.0)*length3(delta(reference.cuts[i].first,reference.cuts[i].second)));
  }
  const auto regions=scene_records(json,"regions");
  require(regions.size() == (open ? 1U : 2U), "missing room edge cannot invent a closed room loop");
  if (!open) {
    for (const auto &region : regions)
      require(region.find("\"area_m2\":null") != std::string::npos, "unknown attachment does not invent net floor area");
    require(json.find("unclassified_slice_loops") != std::string::npos, "unresolved column/room semantics explicit");
  } else {
    require(json.find("open_or_ambiguous_slice") != std::string::npos, "missing return generates explicit open-boundary issue");
  }
  std::cout<<std::setprecision(12)<<"synthetic metres: max_plane_distance="<<max_distance
           <<" max_normal_sine="<<max_sine<<" max_cut_perpendicular="<<max_endpoint_error
           <<" max_chain_length_error="<<max_length_error<<"; no physical accuracy claim"<<std::endl;
}

hss::obj_mtl::Artifact export_mixed(HSSEngine *engine,const MixedRoom &reference) {
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine,&snapshot)==HSS_OK,"frozen mixed export snapshot");
  HSSCounts counts{}; require(hss_snapshot_get_counts(snapshot,&counts)==HSS_OK,"mixed export counts");
  std::vector<HSSVec3d> vertices(counts.vertex_count); std::vector<uint32_t> triangles(counts.triangle_count*3U);
  uint64_t needed{};
  require(hss_snapshot_copy_vertices(snapshot,vertices.data(),vertices.size(),&needed)==HSS_OK && needed==vertices.size(),"mixed raw vertices copy");
  require(hss_snapshot_copy_triangles(snapshot,triangles.data(),triangles.size(),&needed)==HSS_OK && needed==triangles.size(),"mixed raw triangles copy");
  hss_snapshot_release(snapshot);
  size_t base=0U,face_offset=0U;
  for(const auto &mesh:reference.meshes) {
    for(size_t i=0;i<mesh.vertices.size()/3U;++i)
      require(vertices.at(base+i).x==mesh.vertices[3U*i] && vertices.at(base+i).y==mesh.vertices[3U*i+1U] && vertices.at(base+i).z==mesh.vertices[3U*i+2U],"new raw geometry exact, never stale-scene replacement");
    for(size_t i=0;i<mesh.triangles.size();++i)
      require(triangles.at(face_offset+i)==base+mesh.triangles[i],"raw index order and anchor offsets exact");
    base+=mesh.vertices.size()/3U; face_offset+=mesh.triangles.size();
  }
  require(base==vertices.size() && face_offset==triangles.size(),"no duplicate/fabricated raw entities");
  const auto json=scene_json(engine);
  std::vector<std::string> vertex_support(vertices.size());
  hss::obj_mtl::Snapshot output;
  output.export_id="mixed-room-export"; output.project_id=record_text(json,"project_id");
  output.scene_revision=counts.scene_revision; output.captured_mesh_id="mixed-raw-snapshot";
  output.source_is_synthetic=true; output.quality_state="provisional";
  for(const auto &issue:scene_records(json,"issues")) {
    const auto reason=record_text(issue,"reason");
    output.warnings.push_back(record_text(issue,"code"));
    if(record_text(issue,"code")!="mesh_source") continue;
    const auto support_at=reason.find("support="),range_at=reason.find("vertices=");
    require(support_at!=std::string::npos && range_at!=std::string::npos,"actual raw source mapping exists");
    const auto plus=reason.find('+',range_at);
    require(plus!=std::string::npos,"source vertex range exists");
    const auto first=std::stoull(reason.substr(range_at+9U));
    const auto count=std::stoull(reason.substr(plus+1U));
    require(first+count<=vertices.size(),"source vertex range bounded");
    const auto support=reason.substr(support_at+8U,36U);
    for(size_t i=first;i<first+count;++i) { require(vertex_support[i].empty(),"source ranges do not overlap"); vertex_support[i]=support; }
  }
  for(auto p:vertices) output.vertices_m.push_back({p.x,p.y,p.z});
  for(size_t i=0;i<triangles.size();i+=3U) {
    const auto a=triangles[i],b=triangles[i+1U],c=triangles[i+2U];
    require(!vertex_support.at(a).empty() && vertex_support[a]==vertex_support.at(b) && vertex_support[a]==vertex_support.at(c),"face export uses actual common source support");
    output.faces.push_back({"raw-face-"+std::to_string(i/3U),{a,b,c},{vertex_support[a]},"",std::nullopt});
  }
  hss::obj_mtl::Options options; options.include_textures=false;
  const auto result=hss::obj_mtl::export_snapshot(output,options);
  require(result.status==hss::obj_mtl::Status::kOk && result.artifact.has_value(),"real OBJ/MTL exporter accepts mixed raw snapshot");
  const auto &artifact=*result.artifact;
  require(artifact.texture_references.empty() && artifact.mtl_utf8.find("map_Kd")==std::string::npos,"no invented texture or private capture upload");
  require(artifact.manifest_json.find("\"scene_revision\":\""+std::to_string(counts.scene_revision)+"\"")!=std::string::npos,"OBJ sidecar binds exact scene revision");
  std::istringstream lines(artifact.obj_utf8); std::string line; size_t vi=0U,ti=0U;
  while(std::getline(lines,line)) {
    if(line.starts_with("v ")) {
      std::istringstream values(line.substr(2U)); HSSVec3d p{};
      require(bool(values>>p.x>>p.y>>p.z) && vi<vertices.size(),"OBJ vertex parse");
      const auto q=vertices[vi++]; require(p.x==q.x && p.y==q.y && p.z==q.z,"OBJ round-trip preserves every source coordinate exactly");
    } else if(line.starts_with("f ")) {
      std::istringstream values(line.substr(2U));
      for(size_t j=0;j<3U;++j) { uint64_t index{}; require(bool(values>>index) && ti<triangles.size() && index==uint64_t(triangles[ti++])+1U,"OBJ uses exact one-based raw indices"); }
    }
  }
  require(vi==vertices.size() && ti==triangles.size(),"OBJ omitted/added no geometry including skylight gap");
  return artifact;
}
void durable_mixed_room() {
  char temporary[]="/tmp/hss-mixed-room-XXXXXX";
  require(::mkdtemp(temporary)!=nullptr,"private synthetic mixed project directory");
  const std::string directory=temporary;
  HSSProjectStoreConfig sc{}; sc.abi_version=HSS_PROJECT_STORE_ABI_VERSION; sc.struct_size=sizeof(sc);
  sc.project_id=ident(1); sc.trusted_root_directory_utf8={reinterpret_cast<const uint8_t*>(directory.data()),directory.size()};
  sc.open_mode=HSS_PROJECT_STORE_CREATE_NEW; sc.source_is_synthetic=1U;
  sc.max_epoch_records=sc.max_depth_records=sc.max_mesh_records=sc.max_command_records=16U;
  sc.max_retained_bytes=32U*1024U*1024U; sc.max_record_bytes=2U*1024U*1024U; sc.max_pending_bytes=16U*1024U*1024U;
  HSSProjectStore *store{}; require(hss_project_store_create(&sc,&store)==HSS_OK,"create mixed project store");
  auto cfg=config(); HSSEngine *engine{}; require(hss_engine_create(&cfg,&engine)==HSS_OK,"create mixed engine");
  require(hss_engine_set_source_is_synthetic(engine,1U)==HSS_OK,"mixed synthetic provenance");
  uint64_t sequence=0U;
  auto context=[&]() {
    HSSProjectEventContext c{}; c.abi_version=HSS_PROJECT_STORE_ABI_VERSION;c.struct_size=sizeof(c);
    c.capture_id=ident(2);c.event_sequence=sequence;c.accepted_scene_revision=sequence;c.monotonic_time_s=double(sequence);return c;
  };
  auto finish=[&]() {drain(engine,sequence%2U?1U:50U);require(stats(engine).scene_revision==sequence,"one accepted revision per journaled event");};
  HSSProjectStoreReceipt receipt{}; HSSEpoch epoch{};
  epoch.abi_version=HSS_ABI_VERSION;epoch.struct_size=sizeof(epoch);epoch.capture_id=ident(2);
  epoch.event_sequence=++sequence;epoch.relationship_state=1U;epoch.transform_revision=1U;epoch.T_project_world=identity();
  require(hss_engine_set_epoch(engine,&epoch)==HSS_OK,"mixed epoch");finish();auto c=context();
  require(hss_project_store_append_epoch(store,&c,&epoch,&receipt)==HSS_OK,"persist mixed epoch");
  auto cut=[&](double height) {
    HSSProjectPlanReference plan{};plan.abi_version=HSS_PROJECT_STORE_ABI_VERSION;plan.struct_size=sizeof(plan);
    plan.T_project_floor=identity();plan.cut_height_m=height;plan.before_scene_revision=sequence;plan.after_scene_revision=++sequence;
    require(hss_engine_set_plan_reference(engine,&plan.T_project_floor,height)==HSS_OK,"mixed floor/cut control");finish();c=context();
    require(hss_project_store_append_plan_reference(store,&c,&plan,&receipt)==HSS_OK,"persist mixed floor/cut control");
  };
  cut(1.2);
  auto reference=mixed_room();
  auto submit=[&](size_t anchor,uint64_t revision) {
    const auto &mesh=reference.meshes.at(anchor);HSSMeshEvent event{};
    event.abi_version=HSS_ABI_VERSION;event.struct_size=sizeof(event);event.capture_id=ident(2);event.anchor_id=ident(uint8_t(3U+anchor));
    event.event_sequence=++sequence;event.anchor_revision=revision;event.kind=revision==1U?0U:1U;event.T_world_anchor=identity();
    event.vertices_xyz=mesh.vertices.data();event.vertex_count=mesh.vertices.size()/3U;event.triangle_indices=mesh.triangles.data();event.triangle_count=mesh.triangles.size()/3U;
    require(hss_engine_submit_mesh_event(engine,&event)==HSS_OK,"mixed source observation");finish();c=context();
    require(hss_project_store_append_mesh_event(store,&c,&event,&receipt)==HSS_OK,"persist mixed raw source");
  };
  submit(0U,1U);submit(1U,1U);submit(2U,1U);
  verify_mixed_geometry(scene_json(engine),reference);
  const auto original=export_mixed(engine,reference);
  const auto before=sequence++;
  const std::string command="{\"schema_version\":\"1.1\",\"command_id\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\",\"project_id\":\""+record_text(scene_json(engine),"project_id")+
      "\",\"expected_scene_revision\":\""+std::to_string(before)+"\",\"event_sequence\":\""+std::to_string(sequence)+
      "\",\"targets\":[],\"user_intent\":\"retain missing ceiling observation\",\"type\":\"mark_unknown\",\"payload\":{\"reason\":\"skylight depth is unavailable\"}}";
  require(hss_engine_apply_command_json(engine,{reinterpret_cast<const uint8_t*>(command.data()),command.size()})==HSS_OK,"mark actual unsupported ceiling unknown");finish();
  uint64_t bytes{};require(hss_engine_copy_command_result_json(engine,nullptr,0U,&bytes)==HSS_BUFFER_TOO_SMALL,"mixed exact result query");
  std::string result(bytes,'\0');require(hss_engine_copy_command_result_json(engine,reinterpret_cast<uint8_t*>(result.data()),result.size(),&bytes)==HSS_OK,"mixed exact result copy");
  HSSProjectCommandRecord record{};record.abi_version=HSS_PROJECT_STORE_ABI_VERSION;record.struct_size=sizeof(record);record.context=context();
  record.before_scene_revision=before;record.after_scene_revision=sequence;record.disposition=HSS_PROJECT_COMMAND_APPLIED;
  record.command_json={reinterpret_cast<const uint8_t*>(command.data()),command.size()};record.result_json={reinterpret_cast<const uint8_t*>(result.data()),result.size()};
  require(hss_project_store_append_command_record(store,&record,&receipt)==HSS_OK,"persist mixed exact command/result");
  reference=mixed_room(3.65);submit(1U,2U);
  verify_mixed_geometry(scene_json(engine),reference);
  auto refined=export_mixed(engine,reference);require(refined.obj_utf8!=original.obj_utf8,"fresh ceiling observations replace old geometry");
  // Remove only the observed triangles of the first 0.50m return. Keeping
  // original vertex storage must not fabricate the missing face/closed loop.
  const auto complete = reference;
  reference.meshes[0].triangles.erase(reference.meshes[0].triangles.begin()+18,
                                      reference.meshes[0].triangles.begin()+24);
  reference.faces.erase(reference.faces.begin()+3);
  reference.cuts.erase(reference.cuts.begin()+3);
  submit(0U,2U);
  verify_mixed_geometry(scene_json(engine),reference,true);
  export_mixed(engine,reference);
  reference = complete;
  submit(0U,3U);
  verify_mixed_geometry(scene_json(engine),reference);

  cut(3.1);export_mixed(engine,reference);cut(1.2);
  const auto accepted=scene_json(engine);verify_mixed_geometry(accepted,reference);
  require(scene_records(accepted,"features").size()==1U && accepted.find("skylight depth is unavailable")!=std::string::npos,"one explicit unknown intent survives mixed refinement");
  const auto final_export=export_mixed(engine,reference);
  require(final_export.manifest_json.find("HSS_UNKNOWN_REGION") != std::string::npos,
          "actual unknown issue code survives exported sidecar; reason stays in Scene/journal");
  HSSProjectCheckpointResult cp{};require(hss_project_store_checkpoint(store,&cp)==HSS_OK && cp.durable_event_count==sequence && cp.durability_state==1U,"mixed checkpoint is durable");
  hss_engine_destroy(engine);hss_project_store_destroy(store);sc.open_mode=HSS_PROJECT_STORE_OPEN_EXISTING;
  require(hss_project_store_create(&sc,&store)==HSS_OK && hss_engine_create(&cfg,&engine)==HSS_OK,"reopen mixed project with new engine");
  HSSProjectReplayResult replay{};require(hss_project_store_replay(store,engine,&replay)==HSS_OK && replay.engine_status==HSS_OK && replay.applied_event_count==sequence,"replay every mixed source and command in order");
  require(scene_json(engine)==accepted,"byte-equivalent mixed scene after durable reopen");verify_mixed_geometry(scene_json(engine),reference);
  require(export_mixed(engine,reference)==final_export,"byte-equivalent OBJ/MTL/manifest after replay");
  std::string replay_result(result.size(),'\0');require(hss_engine_copy_command_result_json(engine,reinterpret_cast<uint8_t*>(replay_result.data()),replay_result.size(),&bytes)==HSS_OK && replay_result==result,"exact accepted command result retained through source and replay");
  hss_engine_destroy(engine);hss_project_store_destroy(store);std::filesystem::remove_all(directory);
  std::cout<<"Mixed-room source/correction/refinement/"<<sequence<<"-event checkpoint/reopen/OBJ round-trip passed\n";
}

void mixed_room_characterization() {
  const auto reference=mixed_room();
  const auto result=reconstruct_many(reference.meshes,1U,true);
  verify_mixed_geometry(result.json,reference);
  // Reorder only faces, not identities/source vertices. Reference geometry
  // and the complete derived scene must remain unchanged across traversal.
  for (uint32_t seed : {11U, 27U, 83U}) {
    auto shuffled = reference.meshes;
    std::mt19937 generator(seed);
    for (auto &mesh : shuffled) {
      std::vector<std::array<uint32_t,3>> faces;
      for (size_t i=0; i<mesh.triangles.size(); i+=3U)
        faces.push_back({mesh.triangles[i],mesh.triangles[i+1U],mesh.triangles[i+2U]});
      std::shuffle(faces.begin(),faces.end(),generator);
      mesh.triangles.clear();
      for (const auto &face : faces)
        mesh.triangles.insert(mesh.triangles.end(),face.begin(),face.end());
    }
    const auto permutation=reconstruct_many(shuffled,50U,true);
    require(permutation.json==result.json,"mixed derived geometry is invariant to triangle traversal");
    verify_mixed_geometry(permutation.json,reference);
  }
  durable_mixed_room();
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--mixed-room") { mixed_room_characterization(); return 0; }
  if (argc == 2 && std::string(argv[1]) == "--full-component-support") {
    full_component_support_regression();
    return 0;
  }
  duplicate_heavy_quanta(2048U);
  for (const auto &[width, expected] :
       {std::pair{0.50F, size_t{1U}}, std::pair{1.00F, size_t{2U}}}) {
    const auto natural_mesh = shallow_crease(width, false);
    const auto bridge_mesh = shallow_crease(width, true);
    const auto natural = reconstruct(natural_mesh);
    const auto bridge = reconstruct(bridge_mesh);

    require(natural.triangles == natural_mesh.triangles,
            "natural raw triangle order preserved");
    require(bridge.triangles == bridge_mesh.triangles,
            "bridge-first raw triangle order preserved");
    require(natural.planes == expected, "natural deterministic plane count");
    require(bridge.planes == expected, "bridge deterministic plane count");
    require(natural.json == bridge.json, "derived scene is permutation invariant");
  }

  Mesh duplicate;
  duplicate.vertices = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                        0.0F, 1.0F, 0.0F};
  duplicate.triangles = {0U, 1U, 2U, 0U, 1U, 2U, 0U, 1U, 2U};
  const auto duplicate_result = reconstruct(duplicate);
  require(duplicate_result.triangles == duplicate.triangles,
          "non-manifold raw evidence preserved");
  require(duplicate_result.planes == 0U, "ambiguous duplicate is not fitted");
  require(duplicate_result.json.find("non_manifold_mesh") != std::string::npos,
          "non-manifold issue explicit");
  require(duplicate_result.json.find("ambiguous_mesh_component") !=
              std::string::npos,
          "ambiguous component issue explicit");

  Mesh degenerate;
  degenerate.vertices = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                         2.0F, 0.0F, 0.0F};
  degenerate.triangles = {0U, 1U, 2U};
  const auto degenerate_result = reconstruct(degenerate);
  require(degenerate_result.triangles == degenerate.triangles,
          "degenerate raw evidence preserved");
  require(degenerate_result.planes == 0U, "degenerate triangle is not fitted");
  require(degenerate_result.json.find("degenerate_mesh_triangle") !=
              std::string::npos,
          "degenerate issue explicit");
  permutations(wall_chain(), 5U, "protected 0.15m front / 0.50m returns");
  permutations(grid(12U, 0.00005F), 1U, "noisy connected coplanar wall");
  permutations(disconnected(0.0F), 2U, "disconnected coplanar faces");
  permutations(disconnected(0.02F), 2U, "disconnected offset parallel faces");
  const auto two_bad = reconstruct_many({duplicate, duplicate});
  require(two_bad.planes == 0U, "two ambiguous anchors never fit invented planes");
  const auto two_degenerate = reconstruct_many({degenerate, degenerate});
  require(two_degenerate.planes == 0U, "multiple degenerate anchors retained");
  const Mesh large = grid(64U, 0.00005F);
  const auto tiny = reconstruct_many({large}, 1U);
  const auto normal = reconstruct_many({large}, 50U);
  require(tiny.json == normal.json, "large-anchor output independent of step budget");
  require(tiny.step_us.size() > 100U && normal.step_us.size() > 1U,
          "large-anchor work spans many cooperative steps");
  auto report = [](std::vector<double> times, uint64_t requested) {
    std::sort(times.begin(), times.end());
    std::cout << "Synthetic host 8192-triangle grid: requested_us=" << requested
              << " steps=" << times.size() << " p95_us=" << times[times.size()*95U/100U]
              << " max_us=" << times.back() << " (not iPhone or hard-real-time evidence)\n";
  };
  report(tiny.step_us, 1U);
  report(normal.step_us, 50U);
  std::cout << "Extended live C-ABI reconstruction regressions passed\n";
  return 0;
}

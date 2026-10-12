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
#include <set>
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
           std::vector<double> *timings = nullptr,
           uint64_t *peak_working_bytes = nullptr) {
  for (size_t step = 0; step < 200000U; ++step) {
    const auto before = stats(engine);
    if (peak_working_bytes)
      *peak_working_bytes = std::max(*peak_working_bytes, before.working_bytes);
    if (before.lifecycle_state == 1U)
      return;
    const auto begin = std::chrono::steady_clock::now();
    require(hss_engine_step(engine, budget) == HSS_OK, "step");
    if (peak_working_bytes)
      *peak_working_bytes =
          std::max(*peak_working_bytes, stats(engine).working_bytes);
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


// Independent frozen development sibling: different outline, tessellation and
// yaw; bounded smooth coordinate perturbation is not a calibrated sensor model.
HSSVec3d perturb_point(HSSVec3d p, double amplitude, double yaw) {
  const double dx=amplitude*std::sin(1.7*p.x+0.9*p.y+0.3*p.z);
  const double dy=amplitude*std::sin(0.4*p.x+1.3*p.y-0.7*p.z);
  const double dz=amplitude*std::sin(0.8*p.x-0.6*p.y+1.1*p.z);
  p={p.x+dx,p.y+dy,p.z+dz};
  const double a=yaw*std::acos(-1.0)/180.0;
  return {1.234+std::cos(a)*p.x+std::sin(a)*p.z,p.y,
          -0.753-std::sin(a)*p.x+std::cos(a)*p.z};
}
MixedRoom perturbed_room(double amplitude, double yaw=37.0,
                         uint32_t columns=4U, uint32_t rows=5U,
                         bool missing_return=false) {
  MixedRoom ref;
  const std::vector<std::pair<double,double>> outer{{0,0},{7.2,0},{7.2,-4.7},
      {4.4,-4.7},{4.4,-4.2},{4.25,-4.2},{4.25,-4.7},{1.6,-4.7},{0,-3.6}};
  const std::vector<std::pair<double,double>> column{{1.3,-1.2},{1.6,-1.2},{1.6,-1.7},{1.3,-1.7}};
  auto grid_face=[&](size_t anchor,std::array<HSSVec3d,4> q,bool omit=false) {
    auto &mesh=ref.meshes.at(anchor); const auto base=uint32_t(mesh.vertices.size()/3U);
    for(uint32_t j=0;j<=rows;++j) for(uint32_t i=0;i<=columns;++i) {
      const double u=double(i)/columns,v=double(j)/rows;
      HSSVec3d p{std::lerp(std::lerp(q[0].x,q[1].x,u),std::lerp(q[3].x,q[2].x,u),v),
                 std::lerp(std::lerp(q[0].y,q[1].y,u),std::lerp(q[3].y,q[2].y,u),v),
                 std::lerp(std::lerp(q[0].z,q[1].z,u),std::lerp(q[3].z,q[2].z,u),v)};
      p=perturb_point(p,amplitude,yaw);
      mesh.vertices.insert(mesh.vertices.end(),{float(p.x),float(p.y),float(p.z)});
    }
    if(omit) return;
    for(uint32_t j=0;j<rows;++j) for(uint32_t i=0;i<columns;++i) {
      const uint32_t a=base+j*(columns+1U)+i,b=a+1U,c=a+columns+1U,d=c+1U;
      if((i+j)%2U==0U) mesh.triangles.insert(mesh.triangles.end(),{a,b,d,a,d,c});
      else mesh.triangles.insert(mesh.triangles.end(),{a,b,c,b,d,c});
    }
    for(auto &p:q) p=perturb_point(p,0.0,yaw);
    ref.faces.push_back(q);
  };
  auto walls=[&](const auto &ring,size_t anchor,double height) {
    for(size_t i=0;i<ring.size();++i) {
      const auto [x,z]=ring[i];const auto [u,v]=ring[(i+1U)%ring.size()];
      const bool omit=anchor==0U && i==3U && missing_return;
      grid_face(anchor,{{{x,0,z},{u,0,v},{u,height,v},{x,height,z}}},omit);
      if(!omit) ref.cuts.push_back({perturb_point({x,1.2,z},0.0,yaw),perturb_point({u,1.2,v},0.0,yaw)});
    }
  };
  walls(outer,0U,3.72);walls(column,2U,2.95);
  grid_face(1U,{{{0,2.7,0},{2.2,2.7,0},{2.2,2.7,-1.8},{0,2.7,-1.8}}});
  grid_face(1U,{{{2.2,3.11,0},{4.6,3.11,0},{4.6,3.11,-1.8},{2.2,3.11,-1.8}}});
  grid_face(1U,{{{2.2,2.7,0},{2.2,3.11,0},{2.2,3.11,-1.8},{2.2,2.7,-1.8}}});
  grid_face(1U,{{{4.6,3.11,0},{7.2,3.56,0},{7.2,3.56,-1.8},{4.6,3.11,-1.8}}});
  for(const auto &r:std::vector<std::array<double,4>>{{0,7.2,-1.8,-2.3},{0,7.2,-3.1,-3.6},
                                                  {0,2.8,-2.3,-3.1},{3.6,7.2,-2.3,-3.1}})
    grid_face(1U,{{{r[0],3.24,r[2]},{r[1],3.24,r[2]},{r[1],3.24,r[3]},{r[0],3.24,r[3]}}});
  return ref;
}

// Guard the unchanged finite-contact policy through the actual Scene validator.
bool accepts_ring(const std::vector<std::pair<double,double>> &ring) {
  const std::string provenance=R"({"geometry_origin":"fitted","semantic_origin":"unknown","support_ids":[],"uncertainty_m":null,"quality_state":"provisional"})";
  std::ostringstream nodes,ids;
  nodes<<std::setprecision(17);
  for(size_t i=0;i<ring.size();++i) {
    const auto suffix=std::to_string(i+1U);
    const auto id="aaaaaaaa-aaaa-4aaa-8aaa-"+std::string(12U-suffix.size(),'0')+suffix;
    if(i) { nodes<<','; ids<<','; }
    nodes<<"{\"id\":\""<<id<<"\",\"position_m\":["<<ring[i].first<<",0,"<<-ring[i].second
         <<"],\"position_locked\":false,\"provenance\":"<<provenance<<'}';
    ids<<'"'<<id<<'"';
  }
  const std::string scene=R"({"schema_version":"1.1","project_id":"01000000-0000-4000-8000-000000000000","scene_revision":"1","source_is_synthetic":true,"units":"m","basis":{"handedness":"right","up":"+Y","floor_origin_m":[0,0,0],"floor_u":[1,0,0],"floor_v":[0,0,-1],"floor_n":[0,1,0]},"epochs":[],"nodes":[)"+nodes.str()+
      R"(],"edges":[],"surfaces":[],"features":[],"regions":[{"id":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb","outer_node_ids":[)"+ids.str()+
      R"(],"hole_node_ids":[],"area_m2":null,"area_policy":"internal_finished_face","view":"floor_footprint","cut_height_m":0,"provenance":)"+provenance+
      R"(}],"objects":[],"constraints":[],"issues":[],"quality_state":"provisional"})";
  return hss::scene_editing::scene_counts(scene).has_value();
}
void finite_contact_guards() {
  require(accepts_ring({{0,0},{1,0},{2,0},{2,2},{0,2}}),"forward collinear ring is valid");
  require(accepts_ring({{0,0},{3,0},{3,3},{2,3},{2,1},{1,1},{1,3},{0,3}}),"concave ring stays valid");
  require(!accepts_ring({{0,0},{3,3},{0,3},{2,0}}),"proper crossing remains rejected");
  require(!accepts_ring({{0,0},{2,0},{2,2},{1,0},{0,2}}),"nonadjacent endpoint contact remains rejected");
  require(!accepts_ring({{0,0},{3,0},{3,2},{1,2},{1,0},{2,0},{2,3},{0,3}}),"collinear overlap remains rejected");
  require(!accepts_ring({{0,0},{2,0},{2,0},{2,2},{0,2}}),"zero-length ring edge remains rejected");
}
void verify_perturbed_geometry(const std::string &json,const MixedRoom &reference,
                               double amplitude,bool open=false) {
  require(hss::scene_editing::scene_counts(json).has_value(),"perturbed scene validates for supported corrections");
  // The oracle bound is tied to the authored per-coordinate perturbation, not
  // sensor accuracy or a changed production fit/merge threshold.
  const double bound=4.0*std::sqrt(3.0)*amplitude+2e-6;
  const auto surfaces=scene_records(json,"surfaces");
  std::vector<std::pair<HSSVec3d,double>> planes;
  for(const auto &surface:surfaces) if(record_text(surface,"kind")=="plane")
    planes.push_back({record_point(surface,"normal_unit"),record_number(surface,"offset_m")});
  require(planes.size()==reference.faces.size(),"perturbation does not lose separate supported planes");
  std::vector<bool> used(planes.size()); double max_distance=0.0,max_sine=0.0;
  for(const auto &face:reference.faces) {
    auto normal=cross3(delta(face[1],face[0]),delta(face[3],face[0]));
    const double n=length3(normal); normal={normal.x/n,normal.y/n,normal.z/n};
    size_t selected=planes.size();double best=1e100;
    for(size_t i=0;i<planes.size();++i) if(!used[i]) {
      double d=0.0;for(auto p:face)d=std::max(d,std::abs(product3(planes[i].first,p)+planes[i].second));
      if(d<best){best=d;selected=i;}
    }
    require(selected<planes.size() && best<=bound,"fitted plane covers the bounded nominal reference");
    used[selected]=true;max_distance=std::max(max_distance,best);
    max_sine=std::max(max_sine,length3(cross3(normal,planes[selected].first)));
  }
  std::map<std::string,HSSVec3d> nodes;std::map<std::string,size_t> degrees;
  for(const auto &node:scene_records(json,"nodes"))nodes[record_text(node,"id")]=record_point(node,"position_m");
  std::vector<std::vector<std::pair<double,double>>> intervals(reference.cuts.size());
  double max_perpendicular=0.0,max_length_error=0.0;
  for(const auto &edge:scene_records(json,"edges")) {
    const auto sa=record_text(edge,"start_node_id"),sb=record_text(edge,"end_node_id");
    ++degrees[sa];++degrees[sb];const auto a=nodes.at(sa),b=nodes.at(sb);size_t matched=0U;
    for(size_t i=0;i<reference.cuts.size();++i) {
      const auto [p,q]=reference.cuts[i];const auto d=delta(q,p);const auto l=length3(d);
      const double ta=product3(delta(a,p),d)/(l*l),tb=product3(delta(b,p),d)/(l*l);
      const double ea=length3(cross3(delta(a,p),d))/l,eb=length3(cross3(delta(b,p),d))/l;
      if(ea<=bound && eb<=bound && std::min(ta,tb)>=-bound/l && std::max(ta,tb)<=1.0+bound/l) {
        intervals[i].push_back(std::minmax(ta,tb));++matched;max_perpendicular=std::max({max_perpendicular,ea,eb});
      }
    }
    require(matched==1U,"each perturbed fragment has exactly one finite nominal support");
  }
  size_t endpoints=0U;
  for(const auto &[id,p]:nodes){(void)p;if(degrees[id]==1U)++endpoints;else require(degrees[id]==2U,"no perturbed branch or isolated node");}
  require(endpoints==(open?2U:0U),"only the genuinely omitted return creates open endpoints");
  for(size_t i=0;i<intervals.size();++i) {
    auto spans=intervals[i];std::sort(spans.begin(),spans.end());double cursor=0.0,total=0.0;
    const double l=length3(delta(reference.cuts[i].second,reference.cuts[i].first));
    require(!spans.empty(),"narrow face has perturbed cut coverage");
    for(const auto &[a,b]:spans){require(std::abs(a-cursor)*l<=bound,"no hidden gap or overlap in perturbed cut");total+=b-a;cursor=b;}
    require(std::abs(cursor-1.0)*l<=bound,"whole nominal edge remains covered within declared perturbation");
    max_length_error=std::max(max_length_error,std::abs(total-1.0)*l);
  }
  require(scene_records(json,"regions").size()==(open?1U:2U),"perturbed loop count respects absent observations");
  if(open)require(json.find("open_or_ambiguous_slice")!=std::string::npos,"omitted return warning is explicit");
  std::cout<<std::setprecision(12)<<"perturbed planes="<<planes.size()<<" endpoints="<<endpoints
           <<" max_reference_distance_m="<<max_distance<<" max_normal_sine="<<max_sine
           <<" max_cut_deviation_m="<<max_perpendicular<<" max_chain_error_m="<<max_length_error
           <<" bound_m="<<bound<<" (synthetic only)\n";
}
void durable_perturbed_room(double amplitude,double yaw,uint32_t columns,uint32_t rows) {
  char temporary[]="/tmp/hss-perturbed-room-XXXXXX";
  require(::mkdtemp(temporary)!=nullptr,"private perturbed project directory");
  const std::string directory=temporary;
  HSSProjectStoreConfig sc{};sc.abi_version=HSS_PROJECT_STORE_ABI_VERSION;sc.struct_size=sizeof(sc);
  sc.project_id=ident(1);sc.trusted_root_directory_utf8={reinterpret_cast<const uint8_t*>(directory.data()),directory.size()};
  sc.open_mode=HSS_PROJECT_STORE_CREATE_NEW;sc.source_is_synthetic=1U;
  sc.max_epoch_records=sc.max_depth_records=sc.max_mesh_records=sc.max_command_records=16U;
  sc.max_retained_bytes=32U*1024U*1024U;sc.max_record_bytes=2U*1024U*1024U;sc.max_pending_bytes=16U*1024U*1024U;
  HSSProjectStore *store{};require(hss_project_store_create(&sc,&store)==HSS_OK,"create perturbed store");
  auto cfg=config();HSSEngine *engine{};require(hss_engine_create(&cfg,&engine)==HSS_OK,"create perturbed engine");
  require(hss_engine_set_source_is_synthetic(engine,1U)==HSS_OK,"perturbed synthetic origin");
  uint64_t sequence=0U;
  auto context=[&](){HSSProjectEventContext c{};c.abi_version=HSS_PROJECT_STORE_ABI_VERSION;c.struct_size=sizeof(c);c.capture_id=ident(2);c.event_sequence=sequence;c.accepted_scene_revision=sequence;c.monotonic_time_s=double(sequence);return c;};
  auto finish=[&](){drain(engine,sequence%2U?1U:50U);require(stats(engine).scene_revision==sequence,"one revision per perturbed event");};
  HSSProjectStoreReceipt receipt{};HSSEpoch epoch{};
  epoch.abi_version=HSS_ABI_VERSION;epoch.struct_size=sizeof(epoch);epoch.capture_id=ident(2);epoch.event_sequence=++sequence;epoch.relationship_state=1U;epoch.transform_revision=1U;epoch.T_project_world=identity();
  require(hss_engine_set_epoch(engine,&epoch)==HSS_OK,"perturbed epoch");finish();auto c=context();
  require(hss_project_store_append_epoch(store,&c,&epoch,&receipt)==HSS_OK,"persist perturbed epoch");
  HSSProjectPlanReference plan{};plan.abi_version=HSS_PROJECT_STORE_ABI_VERSION;plan.struct_size=sizeof(plan);plan.T_project_floor=identity();plan.cut_height_m=1.2;plan.before_scene_revision=sequence;plan.after_scene_revision=++sequence;
  require(hss_engine_set_plan_reference(engine,&plan.T_project_floor,plan.cut_height_m)==HSS_OK,"perturbed floor");finish();c=context();
  require(hss_project_store_append_plan_reference(store,&c,&plan,&receipt)==HSS_OK,"persist perturbed floor");
  auto reference=perturbed_room(amplitude,yaw,columns,rows);
  auto submit=[&](size_t anchor,uint64_t revision){
    const auto &mesh=reference.meshes.at(anchor);HSSMeshEvent e{};e.abi_version=HSS_ABI_VERSION;e.struct_size=sizeof(e);e.capture_id=ident(2);e.anchor_id=ident(uint8_t(3U+anchor));e.event_sequence=++sequence;e.anchor_revision=revision;e.kind=revision==1U?0U:1U;e.T_world_anchor=identity();e.vertices_xyz=mesh.vertices.data();e.vertex_count=mesh.vertices.size()/3U;e.triangle_indices=mesh.triangles.data();e.triangle_count=mesh.triangles.size()/3U;
    require(hss_engine_submit_mesh_event(engine,&e)==HSS_OK,"perturbed raw source");finish();c=context();require(hss_project_store_append_mesh_event(store,&c,&e,&receipt)==HSS_OK,"persist perturbed raw source");
  };
  submit(0U,1U);submit(1U,1U);submit(2U,1U);
  verify_perturbed_geometry(scene_json(engine),reference,amplitude);export_mixed(engine,reference);
  const auto before=sequence++;
  const std::string command="{\"schema_version\":\"1.1\",\"command_id\":\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\",\"project_id\":\""+record_text(scene_json(engine),"project_id")+"\",\"expected_scene_revision\":\""+std::to_string(before)+"\",\"event_sequence\":\""+std::to_string(sequence)+"\",\"targets\":[],\"user_intent\":\"record missing coverage\",\"type\":\"mark_unknown\",\"payload\":{\"reason\":\"perturbed skylight remains unobserved\"}}";
  require(hss_engine_apply_command_json(engine,{reinterpret_cast<const uint8_t*>(command.data()),command.size()})==HSS_OK,"valid perturbed scan accepts real supported correction");finish();
  uint64_t bytes{};require(hss_engine_copy_command_result_json(engine,nullptr,0U,&bytes)==HSS_BUFFER_TOO_SMALL,"query perturbed command result");
  std::string result(bytes,'\0');require(hss_engine_copy_command_result_json(engine,reinterpret_cast<uint8_t*>(result.data()),bytes,&bytes)==HSS_OK,"copy perturbed result");
  HSSProjectCommandRecord record{};record.abi_version=HSS_PROJECT_STORE_ABI_VERSION;record.struct_size=sizeof(record);record.context=context();record.before_scene_revision=before;record.after_scene_revision=sequence;record.disposition=HSS_PROJECT_COMMAND_APPLIED;record.command_json={reinterpret_cast<const uint8_t*>(command.data()),command.size()};record.result_json={reinterpret_cast<const uint8_t*>(result.data()),result.size()};
  require(hss_project_store_append_command_record(store,&record,&receipt)==HSS_OK,"persist perturbed correction");
  reference=perturbed_room(amplitude,yaw,columns,rows,true);submit(0U,2U);
  verify_perturbed_geometry(scene_json(engine),reference,amplitude,true);export_mixed(engine,reference);
  reference=perturbed_room(amplitude,yaw,columns,rows);submit(0U,3U);
  reference.meshes[1]=perturbed_room(amplitude*0.5,yaw,columns,rows).meshes[1];submit(1U,2U);
  verify_perturbed_geometry(scene_json(engine),reference,amplitude);
  const auto accepted=scene_json(engine);const auto artifact=export_mixed(engine,reference);
  require(artifact.manifest_json.find("HSS_UNKNOWN_REGION")!=std::string::npos,"unknown intent exported after perturbed source updates");
  HSSProjectCheckpointResult cp{};require(hss_project_store_checkpoint(store,&cp)==HSS_OK && cp.durability_state==1U && cp.durable_event_count==sequence,"perturbed checkpoint durable");
  hss_engine_destroy(engine);hss_project_store_destroy(store);sc.open_mode=HSS_PROJECT_STORE_OPEN_EXISTING;
  require(hss_project_store_create(&sc,&store)==HSS_OK && hss_engine_create(&cfg,&engine)==HSS_OK,"reopen perturbed project");
  HSSProjectReplayResult replay{};require(hss_project_store_replay(store,engine,&replay)==HSS_OK && replay.applied_event_count==sequence && replay.engine_status==HSS_OK,"replay perturbed sources and correction");
  require(scene_json(engine)==accepted && export_mixed(engine,reference)==artifact,"perturbed Scene and OBJ/MTL sidecar byte-equivalent after reopen");
  std::string actual(result.size(),'\0');require(hss_engine_copy_command_result_json(engine,reinterpret_cast<uint8_t*>(actual.data()),actual.size(),&bytes)==HSS_OK && actual==result,"exact perturbed command result survives replay");
  hss_engine_destroy(engine);hss_project_store_destroy(store);std::filesystem::remove_all(directory);
  std::cout<<"perturbed durable events="<<sequence<<" yaw="<<yaw<<" grid="<<columns<<'x'<<rows<<" passed\n";
}

void perturbed_characterization() {
  finite_contact_guards();
  // This profile is frozen before the first baseline run. Cyclic rotation
  // preserves the oriented triangle, not merely its approximate appearance.
  const auto ref=perturbed_room(0.00015);
  const auto baseline=reconstruct_many(ref.meshes,1U,true);
  auto cycled=ref.meshes;
  for(auto &mesh:cycled) for(size_t i=0;i<mesh.triangles.size();i+=3U)
    std::rotate(mesh.triangles.begin()+std::ptrdiff_t(i),
                mesh.triangles.begin()+std::ptrdiff_t(i+1U),
                mesh.triangles.begin()+std::ptrdiff_t(i+3U));
  const auto reordered=reconstruct_many(cycled,50U,true);
  std::cout<<"perturbed cyclic source: planes="<<baseline.planes
           <<" versus="<<reordered.planes<<" same_scene="<<(baseline.json==reordered.json)<<std::endl;
  if (baseline.json != reordered.json) {
    const auto mismatch=std::mismatch(baseline.json.begin(),baseline.json.end(),
                                      reordered.json.begin(),reordered.json.end());
    const size_t offset=size_t(mismatch.first-baseline.json.begin());
    std::cerr << "first derived mismatch at " << offset << "\noriginal="
              << baseline.json.substr(offset,200U) << "\ncycled="
              << reordered.json.substr(offset,200U) << '\n';
  }
  require(baseline.json==reordered.json,
          "cyclic triangle indexing cannot change derived perturbed geometry");
  verify_perturbed_geometry(baseline.json,ref,0.00015);
  durable_perturbed_room(0.00015,37.0,4U,5U);
  durable_perturbed_room(0.0001,-23.0,3U,7U);
}

// Frozen independent-seam characterization.  The profile below is an
// authored metre-domain observation model, not a calibrated LiDAR model.  A
// whole face receives a face/anchor-specific planar offset whose magnitude is
// a declared function of a synthetic camera-to-face range.  This isolates
// endpoint correspondence from high-frequency nonplanarity; the latter is
// characterized separately below.  The scorer never feeds its references or
// candidate information back into the engine.
struct IndependentSeamProfile {
  const char *name;
  uint32_t seed;
  uint32_t columns;
  uint32_t rows;
  double seam_bias_m;
  double range_bias_m_per_m;
  HSSVec3d camera_center_m;
  bool nonidentity_anchor_transforms;
  double fixture_association_radius_m;
};

struct IndependentAnchor {
  Mesh mesh;
  HSSMat4d T_world_anchor = identity();
  std::vector<HSSVec3d> project_vertices;
};

struct IndependentEndpoint {
  size_t segment{};
  HSSVec3d point{};
};

struct IndependentScenario {
  std::vector<IndependentAnchor> anchors;
  std::vector<std::array<HSSVec3d, 4>> observed_faces;
  std::vector<std::pair<HSSVec3d, HSSVec3d>> reference_cuts;
  std::vector<IndependentEndpoint> fixture_endpoints;
  double scoring_tolerance_m{};
  double maximum_reference_range_offset_m{};
  double fixture_association_radius_m{};
  size_t intentionally_missing_ceiling_panels{};
};

HSSVec3d add3(HSSVec3d a, HSSVec3d b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

HSSVec3d scale3(HSSVec3d point, double scale) {
  return {point.x * scale, point.y * scale, point.z * scale};
}

HSSVec3d unit3(HSSVec3d point) {
  const double length = length3(point);
  require(length > 0.0, "independent fixture has a nonzero direction");
  return scale3(point, 1.0 / length);
}

HSSMat4d independent_anchor_transform(bool use_transform, size_t index) {
  HSSMat4d transform = identity();
  if (!use_transform)
    return transform;
  // Exact binary fractions keep the clean transformed control focused on
  // correspondence, rather than a decimal serialization artefact.
  transform.column_major[12] = 0.125 * double(1U + index % 4U);
  transform.column_major[14] = -0.0625 * double(1U + (3U * index) % 5U);
  return transform;
}

HSSVec3d independent_project_point(const HSSMat4d &transform,
                                   HSSVec3d local_point) {
  return {local_point.x + transform.column_major[12],
          local_point.y + transform.column_major[13],
          local_point.z + transform.column_major[14]};
}

HSSVec3d independent_local_point(const HSSMat4d &transform,
                                 HSSVec3d project_point) {
  return {project_point.x - transform.column_major[12],
          project_point.y - transform.column_major[13],
          project_point.z - transform.column_major[14]};
}

uint64_t independent_mix(uint64_t value) {
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

HSSVec3d independent_face_bias(const IndependentSeamProfile &profile,
                               size_t face_index,
                               const std::array<HSSVec3d, 4> &face) {
  const HSSVec3d tangent = unit3(delta(face[1], face[0]));
  const HSSVec3d normal =
      unit3(cross3(delta(face[1], face[0]), delta(face[3], face[0])));
  HSSVec3d center{};
  for (const auto point : face)
    center = add3(center, scale3(point, 0.25));
  const double range_m = length3(delta(center, profile.camera_center_m));
  const uint64_t mixed = independent_mix(uint64_t(profile.seed) +
                                         0x9e3779b97f4a7c15ULL * (face_index + 1U));
  const double face_factor =
      (double(mixed & 0xffffU) / 32767.5) - 1.0; // declared [-1, 1] factor.
  const double normal_factor =
      (double((mixed >> 16U) & 0xffffU) / 32767.5) - 1.0;
  return add3(scale3(tangent, profile.seam_bias_m * face_factor),
              scale3(normal, profile.range_bias_m_per_m *
                                 (range_m - 2.0) * normal_factor));
}

double independent_face_range_offset_m(
    const IndependentSeamProfile &profile,
    const std::array<HSSVec3d, 4> &face) {
  HSSVec3d center{};
  for (const auto point : face)
    center = add3(center, scale3(point, 0.25));
  return std::abs(length3(delta(center, profile.camera_center_m)) - 2.0);
}

HSSVec3d independent_stored_project_point(const HSSMat4d &transform,
                                          HSSVec3d project_point) {
  const HSSVec3d local = independent_local_point(transform, project_point);
  const HSSVec3d stored{double(float(local.x)), double(float(local.y)),
                         double(float(local.z))};
  return independent_project_point(transform, stored);
}

void add_independent_face(IndependentScenario &scenario,
                          const IndependentSeamProfile &profile,
                          std::array<HSSVec3d, 4> face,
                          bool contributes_cut) {
  IndependentAnchor anchor;
  const size_t face_index = scenario.anchors.size();
  anchor.T_world_anchor = independent_anchor_transform(
      profile.nonidentity_anchor_transforms, face_index);
  const HSSVec3d bias = independent_face_bias(profile, face_index, face);
  const double range_offset_m = independent_face_range_offset_m(profile, face);
  for (uint32_t row = 0U; row <= profile.rows; ++row) {
    for (uint32_t column = 0U; column <= profile.columns; ++column) {
      const double u = double(column) / double(profile.columns);
      const double v = double(row) / double(profile.rows);
      const HSSVec3d nominal{
          std::lerp(std::lerp(face[0].x, face[1].x, u),
                    std::lerp(face[3].x, face[2].x, u), v),
          std::lerp(std::lerp(face[0].y, face[1].y, u),
                    std::lerp(face[3].y, face[2].y, u), v),
          std::lerp(std::lerp(face[0].z, face[1].z, u),
                    std::lerp(face[3].z, face[2].z, u), v)};
      const HSSVec3d observed = add3(nominal, bias);
      const HSSVec3d local = independent_local_point(anchor.T_world_anchor,
                                                       observed);
      const HSSVec3d stored{double(float(local.x)), double(float(local.y)),
                             double(float(local.z))};
      anchor.mesh.vertices.insert(anchor.mesh.vertices.end(),
                                  {float(local.x), float(local.y), float(local.z)});
      anchor.project_vertices.push_back(
          independent_project_point(anchor.T_world_anchor, stored));
    }
  }
  for (uint32_t row = 0U; row < profile.rows; ++row) {
    for (uint32_t column = 0U; column < profile.columns; ++column) {
      const uint32_t a = row * (profile.columns + 1U) + column;
      const uint32_t b = a + 1U;
      const uint32_t c = a + profile.columns + 1U;
      const uint32_t d = c + 1U;
      if ((row + column) % 2U == 0U)
        anchor.mesh.triangles.insert(anchor.mesh.triangles.end(), {a, b, d, a, d, c});
      else
        anchor.mesh.triangles.insert(anchor.mesh.triangles.end(), {a, b, c, b, d, c});
    }
  }
  if (contributes_cut) {
    scenario.maximum_reference_range_offset_m = std::max(
        scenario.maximum_reference_range_offset_m, range_offset_m);
    const HSSVec3d reference_start{face[0].x, 1.2, face[0].z};
    const HSSVec3d reference_end{face[1].x, 1.2, face[1].z};
    const size_t segment = scenario.reference_cuts.size();
    scenario.reference_cuts.push_back({reference_start, reference_end});
    scenario.fixture_endpoints.push_back(
        {segment, independent_stored_project_point(anchor.T_world_anchor,
                                                    add3(reference_start, bias))});
    scenario.fixture_endpoints.push_back(
        {segment, independent_stored_project_point(anchor.T_world_anchor,
                                                    add3(reference_end, bias))});
  }
  scenario.observed_faces.push_back(face);
  scenario.anchors.push_back(std::move(anchor));
}

IndependentScenario independent_ring(const IndependentSeamProfile &profile) {
  IndependentScenario scenario;
  scenario.fixture_association_radius_m = profile.fixture_association_radius_m;
  const std::vector<std::pair<double, double>> contour{
      {0.0, 0.0}, {5.6, 0.0}, {5.6, -3.8}, {3.4, -3.8},
      {3.4, -3.3}, {3.25, -3.3}, {3.25, -3.8}, {0.0, -3.8}};
  for (size_t index = 0U; index < contour.size(); ++index) {
    const auto [x0, z0] = contour[index];
    const auto [x1, z1] = contour[(index + 1U) % contour.size()];
    add_independent_face(scenario, profile,
                         {{{x0, 0.0, z0}, {x1, 0.0, z1},
                           {x1, 3.2, z1}, {x0, 3.2, z0}}},
                         true);
  }
  // One observed panel and one intentionally absent panel make missing
  // ceiling evidence explicit without making it a cut/topology oracle.
  add_independent_face(scenario, profile,
                       {{{0.0, 2.7, 0.0}, {2.8, 2.7, 0.0},
                         {2.8, 2.7, -3.8}, {0.0, 2.7, -3.8}}},
                       false);
  scenario.intentionally_missing_ceiling_panels = 1U;
  // A measured cut is an intersection of two independently displaced faces.
  // Bound the scorer from the profile's actual reference-face ranges, rather
  // than treating the range coefficient as an unexplained one-metre scalar.
  const double maximum_face_displacement_m = std::hypot(
      std::abs(profile.seam_bias_m),
      std::abs(profile.range_bias_m_per_m) *
          scenario.maximum_reference_range_offset_m);
  scenario.scoring_tolerance_m = 2.0 * maximum_face_displacement_m + 2e-5;
  return scenario;
}

IndependentScenario permuted_independent_scenario(IndependentScenario scenario,
                                                   uint32_t seed) {
  for (auto &anchor : scenario.anchors) {
    std::vector<std::array<uint32_t, 3>> faces;
    for (size_t index = 0U; index < anchor.mesh.triangles.size(); index += 3U)
      faces.push_back({anchor.mesh.triangles[index], anchor.mesh.triangles[index + 1U],
                       anchor.mesh.triangles[index + 2U]});
    std::mt19937 random(seed++);
    std::shuffle(faces.begin(), faces.end(), random);
    anchor.mesh.triangles.clear();
    for (const auto &face : faces)
      anchor.mesh.triangles.insert(anchor.mesh.triangles.end(), face.begin(), face.end());
  }
  return scenario;
}

struct IndependentResult {
  Result core;
  std::vector<HSSVec3d> project_vertices;
  uint64_t peak_working_bytes{};
  uint64_t final_working_bytes{};
};

IndependentResult reconstruct_independent(const IndependentScenario &scenario,
                                         uint64_t step_budget) {
  auto configuration = config();
  HSSEngine *engine{};
  require(hss_engine_create(&configuration, &engine) == HSS_OK,
          "create independent-seam engine");
  require(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
          "declare independent-seam synthetic source");
  uint64_t peak_working_bytes = 0U;
  std::vector<double> timings;
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = ident(2);
  epoch.event_sequence = 1U;
  epoch.relationship_state = 1U;
  epoch.transform_revision = 1U;
  epoch.T_project_world = identity();
  require(hss_engine_set_epoch(engine, &epoch) == HSS_OK,
          "independent-seam epoch");
  drain(engine, step_budget, &timings, &peak_working_bytes);
  const HSSMat4d floor = identity();
  require(hss_engine_set_plan_reference(engine, &floor, 1.2) == HSS_OK,
          "independent-seam floor");
  drain(engine, step_budget, &timings, &peak_working_bytes);

  std::vector<HSSVec3d> expected_vertices;
  std::vector<uint32_t> expected_triangles;
  for (size_t anchor_index = 0U; anchor_index < scenario.anchors.size(); ++anchor_index) {
    const auto &anchor = scenario.anchors[anchor_index];
    const uint32_t base = uint32_t(expected_vertices.size());
    expected_vertices.insert(expected_vertices.end(), anchor.project_vertices.begin(),
                             anchor.project_vertices.end());
    for (const uint32_t index : anchor.mesh.triangles)
      expected_triangles.push_back(base + index);
    HSSMeshEvent event{};
    event.abi_version = HSS_ABI_VERSION;
    event.struct_size = sizeof(event);
    event.capture_id = ident(2);
    event.anchor_id = ident(uint8_t(30U + anchor_index));
    event.event_sequence = 3U + anchor_index;
    event.anchor_revision = 1U;
    event.T_world_anchor = anchor.T_world_anchor;
    event.vertices_xyz = anchor.mesh.vertices.data();
    event.vertex_count = anchor.mesh.vertices.size() / 3U;
    event.triangle_indices = anchor.mesh.triangles.data();
    event.triangle_count = anchor.mesh.triangles.size() / 3U;
    require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK,
            "independent-seam mesh event");
    drain(engine, step_budget, &timings, &peak_working_bytes);
  }
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "independent-seam snapshot");
  std::vector<HSSVec3d> actual_vertices(expected_vertices.size());
  uint64_t copied{};
  require(hss_snapshot_copy_vertices(snapshot, actual_vertices.data(),
                                     actual_vertices.size(), &copied) == HSS_OK &&
              copied == actual_vertices.size(),
          "independent-seam raw vertex copy");
  hss_snapshot_release(snapshot);
  for (size_t index = 0U; index < expected_vertices.size(); ++index) {
    const auto &actual = actual_vertices[index];
    const auto &expected = expected_vertices[index];
    require(actual.x == expected.x && actual.y == expected.y &&
                actual.z == expected.z,
            "independent-seam raw project vertices remain exact");
  }
  const std::vector<uint32_t> actual_triangles = raw_triangles(engine);
  require(actual_triangles == expected_triangles,
          "independent-seam raw triangle indices remain exact");
  IndependentResult result;
  result.core.json = scene_json(engine);
  result.core.triangles = actual_triangles;
  result.core.planes = plane_count(result.core.json);
  result.core.step_us = std::move(timings);
  result.project_vertices = std::move(expected_vertices);
  result.peak_working_bytes = peak_working_bytes;
  result.final_working_bytes = stats(engine).working_bytes;
  require(hss::scene_editing::scene_counts(result.core.json).has_value(),
          "independent-seam scene remains valid");
  hss_engine_destroy(engine);
  return result;
}

std::vector<HSSVec3d> independent_expected_vertices(
    const IndependentScenario &scenario) {
  std::vector<HSSVec3d> expected;
  for (const auto &anchor : scenario.anchors)
    expected.insert(expected.end(), anchor.project_vertices.begin(),
                    anchor.project_vertices.end());
  return expected;
}

std::vector<uint32_t> independent_expected_triangles(
    const IndependentScenario &scenario) {
  std::vector<uint32_t> expected;
  uint32_t base = 0U;
  for (const auto &anchor : scenario.anchors) {
    for (const uint32_t index : anchor.mesh.triangles)
      expected.push_back(base + index);
    base += uint32_t(anchor.project_vertices.size());
  }
  return expected;
}

struct IndependentRawSnapshot {
  HSSCounts counts{};
  std::vector<HSSVec3d> vertices;
  std::vector<uint32_t> triangles;
};

IndependentRawSnapshot independent_raw_snapshot(HSSEngine *engine,
                                                 const IndependentScenario &scenario) {
  HSSSnapshot *snapshot{};
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "independent durable snapshot");
  IndependentRawSnapshot result;
  require(hss_snapshot_get_counts(snapshot, &result.counts) == HSS_OK,
          "independent durable counts");
  const auto expected_vertices = independent_expected_vertices(scenario);
  const auto expected_triangles = independent_expected_triangles(scenario);
  require(result.counts.vertex_count == expected_vertices.size() &&
              result.counts.triangle_count * 3U == expected_triangles.size(),
          "independent durable raw counts remain exact");
  result.vertices.resize(expected_vertices.size());
  result.triangles.resize(expected_triangles.size());
  uint64_t copied{};
  require(hss_snapshot_copy_vertices(snapshot, result.vertices.data(),
                                     result.vertices.size(), &copied) == HSS_OK &&
              copied == result.vertices.size(),
          "independent durable raw vertex copy");
  require(hss_snapshot_copy_triangles(snapshot, result.triangles.data(),
                                      result.triangles.size(), &copied) == HSS_OK &&
              copied == result.triangles.size(),
          "independent durable raw triangle copy");
  hss_snapshot_release(snapshot);
  for (size_t index = 0U; index < expected_vertices.size(); ++index) {
    const auto &actual = result.vertices[index];
    const auto &expected = expected_vertices[index];
    require(actual.x == expected.x && actual.y == expected.y &&
                actual.z == expected.z,
            "independent durable raw project vertex remains exact");
  }
  require(result.triangles == expected_triangles,
          "independent durable raw triangle index remains exact");
  return result;
}

hss::obj_mtl::Artifact export_independent(HSSEngine *engine,
                                           const IndependentScenario &scenario) {
  const auto raw = independent_raw_snapshot(engine, scenario);
  const std::string json = scene_json(engine);
  std::vector<std::string> vertex_support(raw.vertices.size());
  std::set<std::string> source_supports;
  hss::obj_mtl::Snapshot output;
  output.export_id = "independent-seam-export";
  output.project_id = record_text(json, "project_id");
  output.scene_revision = raw.counts.scene_revision;
  output.captured_mesh_id = "independent-seam-raw-snapshot";
  output.source_is_synthetic = true;
  output.quality_state = "provisional";
  for (const auto &issue : scene_records(json, "issues")) {
    const std::string code = record_text(issue, "code");
    output.warnings.push_back(code);
    if (code != "mesh_source")
      continue;
    const std::string reason = record_text(issue, "reason");
    const size_t support_at = reason.find("support=");
    const size_t range_at = reason.find("vertices=");
    require(support_at != std::string::npos && range_at != std::string::npos,
            "independent durable source mapping exists");
    const size_t plus = reason.find('+', range_at);
    require(plus != std::string::npos,
            "independent durable source vertex range exists");
    const size_t first = std::stoull(reason.substr(range_at + 9U));
    const size_t count = std::stoull(reason.substr(plus + 1U));
    require(first + count <= raw.vertices.size(),
            "independent durable source range stays bounded");
    const std::string support = reason.substr(support_at + 8U, 36U);
    source_supports.insert(support);
    for (size_t index = first; index < first + count; ++index) {
      require(vertex_support[index].empty(),
              "independent durable source ranges do not overlap");
      vertex_support[index] = support;
    }
  }
  require(scenario.intentionally_missing_ceiling_panels == 1U &&
              source_supports.size() == scenario.anchors.size(),
          "independent durable export retains only the declared observed sources");
  for (const auto point : raw.vertices)
    output.vertices_m.push_back({point.x, point.y, point.z});
  for (size_t index = 0U; index < raw.triangles.size(); index += 3U) {
    const uint32_t a = raw.triangles[index];
    const uint32_t b = raw.triangles[index + 1U];
    const uint32_t c = raw.triangles[index + 2U];
    require(!vertex_support.at(a).empty() &&
                vertex_support[a] == vertex_support.at(b) &&
                vertex_support[a] == vertex_support.at(c),
            "independent durable face retains one captured source support");
    output.faces.push_back({"independent-raw-face-" + std::to_string(index / 3U),
                            {a, b, c}, {vertex_support[a]}, "", std::nullopt});
  }
  hss::obj_mtl::Options options;
  options.include_textures = false;
  const auto exported = hss::obj_mtl::export_snapshot(output, options);
  require(exported.status == hss::obj_mtl::Status::kOk && exported.artifact.has_value(),
          "independent durable OBJ/MTL export accepts exact raw snapshot");
  const auto &artifact = *exported.artifact;
  require(artifact.texture_references.empty() &&
              artifact.mtl_utf8.find("map_Kd") == std::string::npos,
          "independent durable export never invents textures");
  require(artifact.manifest_json.find("\"scene_revision\":\"" +
                                      std::to_string(raw.counts.scene_revision) + "\"") !=
              std::string::npos,
          "independent durable export binds the exact scene revision");
  std::istringstream lines(artifact.obj_utf8);
  std::string line;
  size_t vertex_index = 0U, triangle_index = 0U;
  while (std::getline(lines, line)) {
    if (line.starts_with("v ")) {
      HSSVec3d point{};
      std::istringstream values(line.substr(2U));
      require(bool(values >> point.x >> point.y >> point.z) &&
                  vertex_index < raw.vertices.size(),
              "independent durable OBJ vertex parses");
      const auto source = raw.vertices[vertex_index++];
      require(point.x == source.x && point.y == source.y && point.z == source.z,
              "independent durable OBJ preserves every raw coordinate");
    } else if (line.starts_with("f ")) {
      std::istringstream values(line.substr(2U));
      for (size_t offset = 0U; offset < 3U; ++offset) {
        uint64_t exported_index{};
        require(bool(values >> exported_index) &&
                    triangle_index < raw.triangles.size() &&
                    exported_index == uint64_t(raw.triangles[triangle_index++]) + 1U,
                "independent durable OBJ preserves raw one-based indices");
      }
    }
  }
  require(vertex_index == raw.vertices.size() &&
              triangle_index == raw.triangles.size(),
          "independent durable OBJ omits and adds no geometry");
  return artifact;
}

void durable_independent_profile(const IndependentSeamProfile &profile) {
  const auto scenario = independent_ring(profile);
  char temporary[] = "/tmp/hss-independent-seam-XXXXXX";
  require(::mkdtemp(temporary) != nullptr,
          "private independent-seam project directory");
  const std::string directory = temporary;
  HSSProjectStoreConfig storage{};
  storage.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  storage.struct_size = sizeof(storage);
  storage.project_id = ident(1);
  storage.trusted_root_directory_utf8 = {
      reinterpret_cast<const uint8_t *>(directory.data()), directory.size()};
  storage.open_mode = HSS_PROJECT_STORE_CREATE_NEW;
  storage.source_is_synthetic = 1U;
  storage.max_epoch_records = storage.max_depth_records = 16U;
  storage.max_mesh_records = storage.max_command_records = 16U;
  storage.max_retained_bytes = 32U * 1024U * 1024U;
  storage.max_record_bytes = 2U * 1024U * 1024U;
  storage.max_pending_bytes = 16U * 1024U * 1024U;
  HSSProjectStore *store{};
  require(hss_project_store_create(&storage, &store) == HSS_OK,
          "create independent-seam project store");
  const auto configuration = config();
  HSSEngine *engine{};
  require(hss_engine_create(&configuration, &engine) == HSS_OK,
          "create independent-seam durable engine");
  require(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
          "declare independent-seam durable provenance");
  uint64_t sequence = 0U;
  auto context = [&]() {
    HSSProjectEventContext value{};
    value.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
    value.struct_size = sizeof(value);
    value.capture_id = ident(2);
    value.event_sequence = sequence;
    value.accepted_scene_revision = sequence;
    value.monotonic_time_s = double(sequence);
    return value;
  };
  auto finish = [&]() {
    drain(engine, sequence % 2U == 0U ? 50U : 1U);
    require(stats(engine).scene_revision == sequence,
            "independent-seam durable event publishes one revision");
  };
  HSSProjectStoreReceipt receipt{};
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = ident(2);
  epoch.event_sequence = ++sequence;
  epoch.relationship_state = 1U;
  epoch.transform_revision = 1U;
  epoch.T_project_world = identity();
  require(hss_engine_set_epoch(engine, &epoch) == HSS_OK,
          "independent-seam durable epoch");
  finish();
  auto event_context = context();
  require(hss_project_store_append_epoch(store, &event_context, &epoch, &receipt) == HSS_OK,
          "persist independent-seam epoch");

  HSSProjectPlanReference plan{};
  plan.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  plan.struct_size = sizeof(plan);
  plan.T_project_floor = identity();
  plan.cut_height_m = 1.2;
  plan.before_scene_revision = sequence;
  plan.after_scene_revision = ++sequence;
  require(hss_engine_set_plan_reference(engine, &plan.T_project_floor,
                                        plan.cut_height_m) == HSS_OK,
          "independent-seam durable floor");
  finish();
  event_context = context();
  require(hss_project_store_append_plan_reference(store, &event_context, &plan,
                                                  &receipt) == HSS_OK,
          "persist independent-seam floor");

  for (size_t index = 0U; index < scenario.anchors.size(); ++index) {
    const auto &anchor = scenario.anchors[index];
    HSSMeshEvent event{};
    event.abi_version = HSS_ABI_VERSION;
    event.struct_size = sizeof(event);
    event.capture_id = ident(2);
    event.anchor_id = ident(uint8_t(30U + index));
    event.event_sequence = ++sequence;
    event.anchor_revision = 1U;
    event.T_world_anchor = anchor.T_world_anchor;
    event.vertices_xyz = anchor.mesh.vertices.data();
    event.vertex_count = anchor.mesh.vertices.size() / 3U;
    event.triangle_indices = anchor.mesh.triangles.data();
    event.triangle_count = anchor.mesh.triangles.size() / 3U;
    require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK,
            "independent-seam durable mesh event");
    finish();
    event_context = context();
    require(hss_project_store_append_mesh_event(store, &event_context, &event,
                                                &receipt) == HSS_OK,
            "persist independent-seam mesh event");
  }
  const auto baseline_export = export_independent(engine, scenario);
  const uint64_t before_command = sequence;
  const uint64_t command_sequence = ++sequence;
  const std::string command =
      "{\"schema_version\":\"1.1\",\"command_id\":\"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb\","
      "\"project_id\":\"" + record_text(scene_json(engine), "project_id") +
      "\",\"expected_scene_revision\":\"" + std::to_string(before_command) +
      "\",\"event_sequence\":\"" + std::to_string(command_sequence) +
      "\",\"targets\":[],\"user_intent\":\"record independent seam uncertainty\","
      "\"type\":\"mark_unknown\",\"payload\":{\"reason\":\"independent seam observations remain open\"}}";
  require(hss_engine_apply_command_json(
              engine, {reinterpret_cast<const uint8_t *>(command.data()), command.size()}) ==
              HSS_OK,
          "independent-seam durable zero-target correction");
  finish();
  uint64_t result_bytes{};
  require(hss_engine_copy_command_result_json(engine, nullptr, 0U, &result_bytes) ==
              HSS_BUFFER_TOO_SMALL,
          "independent-seam durable command result size");
  std::string command_result(result_bytes, '\0');
  require(hss_engine_copy_command_result_json(
              engine, reinterpret_cast<uint8_t *>(command_result.data()),
              command_result.size(), &result_bytes) == HSS_OK,
          "independent-seam durable command result copy");
  HSSProjectCommandRecord command_record{};
  command_record.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  command_record.struct_size = sizeof(command_record);
  command_record.context = context();
  command_record.before_scene_revision = before_command;
  command_record.after_scene_revision = sequence;
  command_record.disposition = HSS_PROJECT_COMMAND_APPLIED;
  command_record.command_json = {
      reinterpret_cast<const uint8_t *>(command.data()), command.size()};
  command_record.result_json = {reinterpret_cast<const uint8_t *>(command_result.data()),
                                command_result.size()};
  require(hss_project_store_append_command_record(store, &command_record, &receipt) == HSS_OK,
          "persist independent-seam exact command and result");
  const std::string accepted_scene = scene_json(engine);
  const auto accepted_export = export_independent(engine, scenario);
  require(accepted_export != baseline_export,
          "independent-seam correction is visible in its export sidecar");
  HSSProjectCheckpointResult checkpoint{};
  require(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK &&
              checkpoint.durability_state == 1U &&
              checkpoint.durable_event_count == sequence,
          "independent-seam checkpoint is durable");
  HSSProjectResumeCursor cursor{};
  cursor.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  cursor.struct_size = sizeof(cursor);
  require(hss_project_store_get_resume_cursor(store, &cursor) == HSS_OK &&
              cursor.last_event_sequence == sequence &&
              cursor.accepted_scene_revision == sequence,
          "independent-seam checkpoint exposes exact resume cursor");

  hss_engine_destroy(engine);
  hss_project_store_destroy(store);
  storage.open_mode = HSS_PROJECT_STORE_OPEN_EXISTING;
  require(hss_project_store_create(&storage, &store) == HSS_OK &&
              hss_engine_create(&configuration, &engine) == HSS_OK,
          "reopen independent-seam project");
  HSSProjectReplayResult replay{};
  require(hss_project_store_replay(store, engine, &replay) == HSS_OK &&
              replay.engine_status == HSS_OK && replay.applied_event_count == sequence,
          "replay independent-seam project stream");
  require(scene_json(engine) == accepted_scene,
          "independent-seam Scene remains byte-equivalent after replay");
  require(export_independent(engine, scenario) == accepted_export,
          "independent-seam OBJ/MTL artifact remains byte-equivalent after replay");
  std::string replay_result(command_result.size(), '\0');
  require(hss_engine_copy_command_result_json(
              engine, reinterpret_cast<uint8_t *>(replay_result.data()),
              replay_result.size(), &result_bytes) == HSS_OK &&
              replay_result == command_result,
          "independent-seam exact command result remains after replay");
  hss_engine_destroy(engine);
  hss_project_store_destroy(store);
  std::filesystem::remove_all(directory);
  std::cout << "independent_seam durable profile=" << profile.name
            << " events=" << sequence << " checkpoint_reopen_obj=passed\n";
}

struct IndependentMetrics {
  size_t plane_count{};
  size_t mesh_patch_count{};
  size_t partial_plane_support_count{};
  size_t open_issue_count{};
  size_t nodes{};
  size_t edges{};
  size_t open_endpoints{};
  size_t non_manifold_degrees{};
  size_t connected_components{};
  size_t regions{};
  size_t false_bridge_edges{};
  size_t ambiguous_fixture_endpoints{};
  size_t max_fixture_candidates{};
  double cut_coverage_m{};
  double minimum_cut_coverage_ratio{1.0};
  double max_reference_deviation_m{};
  double max_fit_residual_m{};
};

size_t issue_count_with_code(const std::string &json, const std::string &code) {
  size_t count = 0U;
  for (const auto &issue : scene_records(json, "issues"))
    if (record_text(issue, "code") == code)
      ++count;
  return count;
}

IndependentMetrics measure_independent(const IndependentScenario &scenario,
                                       const IndependentResult &result) {
  IndependentMetrics metrics;
  const auto surfaces = scene_records(result.core.json, "surfaces");
  for (const auto &surface : surfaces) {
    if (record_text(surface, "kind") == "plane") {
      ++metrics.plane_count;
      metrics.max_fit_residual_m =
          std::max(metrics.max_fit_residual_m, record_number(surface, "fit_residual_m"));
    } else {
      ++metrics.mesh_patch_count;
    }
  }
  metrics.partial_plane_support_count =
      issue_count_with_code(result.core.json, "partial_plane_support");
  metrics.open_issue_count =
      issue_count_with_code(result.core.json, "open_or_ambiguous_slice");
  metrics.regions = scene_records(result.core.json, "regions").size();
  std::map<std::string, HSSVec3d> nodes;
  std::map<std::string, size_t> degrees;
  std::map<std::string, std::vector<std::string>> adjacency;
  for (const auto &node : scene_records(result.core.json, "nodes"))
    nodes.emplace(record_text(node, "id"), record_point(node, "position_m"));
  std::vector<std::vector<std::pair<double, double>>> spans(
      scenario.reference_cuts.size());
  for (const auto &edge : scene_records(result.core.json, "edges")) {
    const auto start = record_text(edge, "start_node_id");
    const auto end = record_text(edge, "end_node_id");
    ++degrees[start];
    ++degrees[end];
    adjacency[start].push_back(end);
    adjacency[end].push_back(start);
    const HSSVec3d a = nodes.at(start), b = nodes.at(end);
    size_t matched = 0U;
    for (size_t index = 0U; index < scenario.reference_cuts.size(); ++index) {
      const auto &[p, q] = scenario.reference_cuts[index];
      const HSSVec3d direction = delta(q, p);
      const double length_squared = product3(direction, direction);
      const double endpoint_tolerance =
          scenario.scoring_tolerance_m / std::sqrt(length_squared);
      const double ta = product3(delta(a, p), direction) / length_squared;
      const double tb = product3(delta(b, p), direction) / length_squared;
      const double ea = length3(cross3(delta(a, p), direction)) /
                        std::sqrt(length_squared);
      const double eb = length3(cross3(delta(b, p), direction)) /
                        std::sqrt(length_squared);
      if (ea <= scenario.scoring_tolerance_m && eb <= scenario.scoring_tolerance_m &&
          std::min(ta, tb) >= -endpoint_tolerance &&
          std::max(ta, tb) <= 1.0 + endpoint_tolerance) {
        ++matched;
        spans[index].push_back(std::minmax(ta, tb));
        metrics.max_reference_deviation_m =
            std::max({metrics.max_reference_deviation_m, ea, eb});
      }
    }
    if (matched != 1U) {
      ++metrics.false_bridge_edges;
      std::cout << std::setprecision(12)
                << "independent_seam unmatched_edge matches=" << matched
                << " start_m=" << a.x << ',' << a.y << ',' << a.z
                << " end_m=" << b.x << ',' << b.y << ',' << b.z << '\n';
    }
  }
  metrics.nodes = nodes.size();
  metrics.edges = scene_records(result.core.json, "edges").size();
  for (const auto &[id, degree] : degrees) {
    (void)id;
    if (degree == 1U)
      ++metrics.open_endpoints;
    else if (degree != 2U)
      ++metrics.non_manifold_degrees;
  }
  std::map<std::string, bool> visited;
  for (const auto &[id, point] : nodes) {
    (void)point;
    if (visited[id])
      continue;
    ++metrics.connected_components;
    std::vector<std::string> pending{id};
    visited[id] = true;
    while (!pending.empty()) {
      const std::string current = std::move(pending.back());
      pending.pop_back();
      for (const auto &next : adjacency[current]) {
        if (!visited[next]) {
          visited[next] = true;
          pending.push_back(next);
        }
      }
    }
  }
  for (size_t index = 0U; index < spans.size(); ++index) {
    auto &intervals = spans[index];
    std::sort(intervals.begin(), intervals.end());
    double cursor = 0.0;
    double covered = 0.0;
    for (const auto &[first, second] : intervals) {
      const double begin = std::clamp(first, 0.0, 1.0);
      const double end = std::clamp(second, 0.0, 1.0);
      if (end > cursor) {
        covered += end - std::max(cursor, begin);
        cursor = end;
      }
    }
    const double length = length3(delta(scenario.reference_cuts[index].second,
                                        scenario.reference_cuts[index].first));
    metrics.cut_coverage_m += covered * length;
    metrics.minimum_cut_coverage_ratio =
        std::min(metrics.minimum_cut_coverage_ratio, covered);
  }
  for (size_t index = 0U; index < scenario.fixture_endpoints.size(); ++index) {
    size_t candidates = 0U;
    for (size_t other = 0U; other < scenario.fixture_endpoints.size(); ++other) {
      if (index == other || scenario.fixture_endpoints[index].segment ==
                                scenario.fixture_endpoints[other].segment)
        continue;
      if (length3(delta(scenario.fixture_endpoints[index].point,
                        scenario.fixture_endpoints[other].point)) <=
          scenario.fixture_association_radius_m)
        ++candidates;
    }
    metrics.max_fixture_candidates = std::max(metrics.max_fixture_candidates, candidates);
    if (candidates > 1U)
      ++metrics.ambiguous_fixture_endpoints;
  }
  return metrics;
}

bool same_independent_metrics(const IndependentMetrics &left,
                              const IndependentMetrics &right) {
  return left.plane_count == right.plane_count &&
         left.mesh_patch_count == right.mesh_patch_count &&
         left.partial_plane_support_count == right.partial_plane_support_count &&
         left.open_issue_count == right.open_issue_count && left.nodes == right.nodes &&
         left.edges == right.edges && left.open_endpoints == right.open_endpoints &&
         left.non_manifold_degrees == right.non_manifold_degrees &&
         left.connected_components == right.connected_components &&
         left.regions == right.regions &&
         left.false_bridge_edges == right.false_bridge_edges &&
         left.ambiguous_fixture_endpoints == right.ambiguous_fixture_endpoints &&
         left.max_fixture_candidates == right.max_fixture_candidates &&
         std::abs(left.cut_coverage_m - right.cut_coverage_m) < 1e-12 &&
         std::abs(left.minimum_cut_coverage_ratio -
                  right.minimum_cut_coverage_ratio) < 1e-12 &&
         std::abs(left.max_reference_deviation_m - right.max_reference_deviation_m) <
             1e-12 &&
         std::abs(left.max_fit_residual_m - right.max_fit_residual_m) < 1e-12;
}

void print_independent_metrics(const IndependentSeamProfile &profile,
                               const IndependentScenario &scenario,
                               const IndependentResult &result,
                               const IndependentMetrics &metrics) {
  std::vector<double> timings = result.core.step_us;
  std::sort(timings.begin(), timings.end());
  const double p95 = timings.empty() ? 0.0 : timings[timings.size() * 95U / 100U];
  const double maximum = timings.empty() ? 0.0 : timings.back();
  std::cout << std::setprecision(12) << "independent_seam profile=" << profile.name
            << " planes=" << metrics.plane_count << " mesh_patches="
            << metrics.mesh_patch_count << " endpoints=" << metrics.open_endpoints
            << " components=" << metrics.connected_components
            << " regions=" << metrics.regions << " false_bridges="
            << metrics.false_bridge_edges << " fixture_ambiguous_endpoints="
            << metrics.ambiguous_fixture_endpoints << " max_fixture_candidates="
            << metrics.max_fixture_candidates << " cut_coverage_m="
            << metrics.cut_coverage_m << " max_reference_deviation_m="
            << metrics.max_reference_deviation_m << " min_cut_coverage_ratio="
            << metrics.minimum_cut_coverage_ratio << " max_fit_residual_m="
            << metrics.max_fit_residual_m << " peak_working_bytes="
            << result.peak_working_bytes << " steps=" << timings.size()
            << " p95_step_us=" << p95 << " max_step_us=" << maximum
            << " scorer_tolerance_m=" << scenario.scoring_tolerance_m
            << " max_reference_range_offset_m="
            << scenario.maximum_reference_range_offset_m
            << " engine_candidate_ambiguity=unavailable\n";
}

void characterize_independent_profile(const IndependentSeamProfile &profile,
                                      bool expect_closed_control) {
  const auto scenario = independent_ring(profile);
  require(scenario.intentionally_missing_ceiling_panels == 1U,
          "independent seam fixture retains a missing ceiling panel");
  const auto tiny = reconstruct_independent(scenario, 1U);
  const auto normal = reconstruct_independent(scenario, 50U);
  const auto tiny_metrics = measure_independent(scenario, tiny);
  const auto normal_metrics = measure_independent(scenario, normal);
  require(tiny.core.json == normal.core.json,
          "independent-seam output is invariant to 1us/50us cooperative requests");
  require(same_independent_metrics(tiny_metrics, normal_metrics),
          "independent-seam metrics are invariant to work slicing");
  const auto permuted_scenario =
      permuted_independent_scenario(scenario, profile.seed ^ 0x5a17U);
  const auto permuted_tiny = reconstruct_independent(permuted_scenario, 1U);
  const auto permuted = reconstruct_independent(permuted_scenario, 50U);
  const auto permuted_metrics = measure_independent(scenario, permuted);
  // Emit the frozen profile's measurements before applying its assertions, so
  // a failed characterization still leaves actionable local evidence.
  print_independent_metrics(profile, scenario, normal, normal_metrics);
  require(normal.core.json == permuted.core.json,
          "independent-seam derived Scene is triangle-permutation invariant");
  require(same_independent_metrics(normal_metrics, permuted_metrics),
          "independent-seam metrics are triangle-permutation invariant");
  require(permuted_tiny.core.json == permuted.core.json,
          "permuted independent-seam output is invariant to 1us/50us cooperative requests");
  require(same_independent_metrics(
              measure_independent(scenario, permuted_tiny), permuted_metrics),
          "permuted independent-seam metrics are invariant to work slicing");
  require(normal_metrics.false_bridge_edges == 0U,
          "independent seam never manufactures a cross-reference bridge");
  require(normal_metrics.non_manifold_degrees == 0U,
          "independent seam never manufactures a branch node");
  require(normal_metrics.minimum_cut_coverage_ratio >= 0.98,
          "independent seam retains each declared reference cut within its bound");
  if (expect_closed_control) {
    require(normal_metrics.open_endpoints == 0U && normal_metrics.regions == 1U &&
                normal_metrics.connected_components == 1U,
            "clean transformed control remains a closed measured loop");
  } else {
    require(normal_metrics.open_endpoints != 0U && normal_metrics.regions == 0U,
            "independent face observations expose an open conservative cut");
  }
}

IndependentScenario independent_negative_controls() {
  const IndependentSeamProfile profile{"negative-controls", 0x7a31U, 3U, 4U,
                                       0.0, 0.0, {0.0, 1.0, 2.0}, false, 0.001};
  IndependentScenario scenario;
  scenario.fixture_association_radius_m = profile.fixture_association_radius_m;
  scenario.scoring_tolerance_m = 2e-5;
  // The first two faces have a real 0.45mm source gap.  The next two are
  // disconnected near-parallel faces; their endpoint cluster deliberately
  // supplies more than one fixture-side association candidate inside 1mm.
  add_independent_face(scenario, profile,
                       {{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                         {1.0, 3.0, 0.0}, {0.0, 3.0, 0.0}}}, true);
  add_independent_face(scenario, profile,
                       {{{1.00045, 0.0, 0.0}, {2.0, 0.0, 0.0},
                         {2.0, 3.0, 0.0}, {1.00045, 3.0, 0.0}}}, true);
  add_independent_face(scenario, profile,
                       {{{0.0, 0.0, -0.00055}, {1.0, 0.0, -0.00055},
                         {1.0, 3.0, -0.00055}, {0.0, 3.0, -0.00055}}}, true);
  add_independent_face(scenario, profile,
                       {{{0.0, 0.0, 0.00042}, {1.0, 0.0, 0.00042},
                         {1.0, 3.0, 0.00042}, {0.0, 3.0, 0.00042}}}, true);
  return scenario;
}

Mesh high_frequency_nonplanar_wall() {
  Mesh mesh;
  constexpr uint32_t columns = 9U, rows = 7U;
  for (uint32_t row = 0U; row <= rows; ++row) {
    for (uint32_t column = 0U; column <= columns; ++column) {
      const uint64_t value = independent_mix(0x84a1U + 31U * row + column);
      const double signed_step = double(value % 5U) - 2.0;
      mesh.vertices.insert(mesh.vertices.end(),
                           {0.35F * float(column), 0.42F * float(row),
                            float(0.0015 * signed_step)});
    }
  }
  for (uint32_t row = 0U; row < rows; ++row) {
    for (uint32_t column = 0U; column < columns; ++column) {
      const uint32_t a = row * (columns + 1U) + column;
      const uint32_t b = a + 1U;
      const uint32_t c = a + columns + 1U;
      const uint32_t d = c + 1U;
      mesh.triangles.insert(mesh.triangles.end(), {a, b, d, a, d, c});
    }
  }
  return mesh;
}

void characterize_high_frequency_nonplanarity() {
  const Mesh source = high_frequency_nonplanar_wall();
  const auto tiny = reconstruct_many({source}, 1U, true);
  const auto normal = reconstruct_many({source}, 50U, true);
  require(tiny.json == normal.json,
          "high-frequency fallback is invariant to cooperative work slicing");
  std::vector<std::array<uint32_t, 3>> faces;
  for (size_t index = 0U; index < source.triangles.size(); index += 3U)
    faces.push_back({source.triangles[index], source.triangles[index + 1U],
                     source.triangles[index + 2U]});
  std::mt19937 random(0x91a5U);
  std::shuffle(faces.begin(), faces.end(), random);
  Mesh permuted = source;
  permuted.triangles.clear();
  for (const auto &face : faces)
    permuted.triangles.insert(permuted.triangles.end(), face.begin(), face.end());
  const auto shuffled = reconstruct_many({permuted}, 50U, true);
  require(normal.json == shuffled.json,
          "high-frequency fallback is triangle-permutation invariant");
  const size_t partial = issue_count_with_code(normal.json, "partial_plane_support");
  const size_t patches = scene_records(normal.json, "surfaces").size() - normal.planes;
  std::cout << "independent_seam high_frequency_nonplanarity planes=" << normal.planes
            << " mesh_patches=" << patches << " partial_plane_support=" << partial
            << " raw_triangles=" << normal.triangles.size() / 3U
            << " (conservative fallback is characterization, not failure)\n";
}

void independent_seam_characterization() {
  // Both profiles are declared before either is executed. The second variant
  // changes seed, camera/range relationship, tessellation, and face offsets;
  // it is a predeclared synthetic variant, not an independent physical set.
  // No production tolerance is tuned from either result.
  // This is a transform-only control: all observations are exact, but every
  // anchor has a nonidentity translation.  Rotations are deliberately out of
  // scope for this frozen translation-only seam characterization.
  const IndependentSeamProfile clean{"clean-translation-control", 0x1101U, 4U,
                                     5U, 0.0, 0.0, {1.5, 1.2, 2.0}, true, 0.001};
  const IndependentSeamProfile development{"independent-development", 0x22d7U,
                                            4U, 5U, 0.00042, 0.00009,
                                            {1.5, 1.2, 2.0}, true, 0.001};
  const IndependentSeamProfile variant{"independent-predeclared-variant", 0x5b39U,
                                        3U, 7U, 0.00031, 0.00012,
                                        {4.7, 1.5, 1.7}, true, 0.001};
  characterize_independent_profile(clean, true);
  characterize_independent_profile(development, false);
  characterize_independent_profile(variant, false);
  durable_independent_profile(clean);
  durable_independent_profile(variant);

  const auto negatives = independent_negative_controls();
  const auto tiny = reconstruct_independent(negatives, 1U);
  const auto normal = reconstruct_independent(negatives, 50U);
  const auto tiny_metrics = measure_independent(negatives, tiny);
  const auto normal_metrics = measure_independent(negatives, normal);
  require(tiny.core.json == normal.core.json &&
              same_independent_metrics(tiny_metrics, normal_metrics),
          "negative controls are invariant to cooperative work slicing");
  const auto permuted_scenario =
      permuted_independent_scenario(negatives, 0x6331U);
  const auto permuted_tiny = reconstruct_independent(permuted_scenario, 1U);
  const auto permuted = reconstruct_independent(permuted_scenario, 50U);
  const auto permuted_metrics = measure_independent(negatives, permuted);
  require(normal.core.json == permuted.core.json &&
              same_independent_metrics(normal_metrics, permuted_metrics),
          "negative controls are triangle-permutation invariant");
  require(permuted_tiny.core.json == permuted.core.json &&
              same_independent_metrics(measure_independent(negatives, permuted_tiny),
                                       permuted_metrics),
          "permuted negative controls are invariant to cooperative work slicing");
  require(normal_metrics.max_fixture_candidates >= 2U &&
              normal_metrics.ambiguous_fixture_endpoints != 0U,
          "negative controls contain predeclared ambiguous proximity candidates");
  require(normal_metrics.false_bridge_edges == 0U && normal_metrics.regions == 0U &&
              normal_metrics.open_endpoints != 0U &&
              normal_metrics.non_manifold_degrees == 0U &&
              normal_metrics.connected_components == negatives.reference_cuts.size(),
          "sub-millimetre real gaps and parallel faces remain open without false closure");
  std::cout << std::setprecision(12)
            << "independent_seam negative_controls radius_m="
            << negatives.fixture_association_radius_m << " real_gap_m=0.00045"
            << " endpoints=" << normal_metrics.open_endpoints
            << " components=" << normal_metrics.connected_components
            << " false_bridges=" << normal_metrics.false_bridge_edges
            << " max_fixture_candidates=" << normal_metrics.max_fixture_candidates
            << " engine_candidate_ambiguity=unavailable\n";
  characterize_high_frequency_nonplanarity();
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--independent-seams") {
    independent_seam_characterization();
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--perturbed-room") { perturbed_characterization(); return 0; }
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

#include "hss/hss_api.h"
#include "hss/scene_editing.hpp"
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
} // namespace

int main() {
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

#include "hss/hss_api.h"
#include "hss/hss_project_store.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HSSId id(uint8_t first) {
  HSSId result{};
  result.bytes[0] = first;
  result.bytes[6] = 0x50U;
  result.bytes[8] = 0x80U;
  return result;
}

HSSMat4d identity() {
  HSSMat4d result{};
  result.column_major[0] = result.column_major[5] =
      result.column_major[10] = result.column_major[15] = 1.0;
  return result;
}

HSSConfig engine_config() {
  HSSConfig result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.project_id = id(1U);
  result.structure_voxel_m = 0.02;
  result.detail_voxel_m = 0.01;
  result.truncation_m = 0.04;
  result.max_working_bytes = 16U * 1024U * 1024U;
  result.max_pending_frames = 2U;
  return result;
}

HSSProjectEventContext context(uint64_t sequence, uint64_t revision) {
  HSSProjectEventContext result{};
  result.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.event_sequence = sequence;
  result.accepted_scene_revision = revision;
  result.session_epoch = 0U;
  result.monotonic_time_s = static_cast<double>(sequence);
  return result;
}

HSSProjectStoreConfig store_config(const std::string &root, uint32_t mode) {
  HSSProjectStoreConfig result{};
  result.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.project_id = id(1U);
  result.trusted_root_directory_utf8 = {
      reinterpret_cast<const uint8_t *>(root.data()), root.size()};
  result.open_mode = mode;
  result.source_is_synthetic = 1U;
  result.max_epoch_records = 8U;
  result.max_depth_records = 8U;
  result.max_mesh_records = 8U;
  result.max_command_records = 8U;
  result.max_retained_bytes = 16U * 1024U * 1024U;
  result.max_record_bytes = 2U * 1024U * 1024U;
  result.max_pending_bytes = 8U * 1024U * 1024U;
  return result;
}

void drain(HSSEngine *engine, uint64_t expected_revision) {
  for (size_t iteration = 0U; iteration != 200000U; ++iteration) {
    HSSEngineStats stats{};
    check(hss_engine_get_stats(engine, &stats) == HSS_OK, "read engine stats");
    if (stats.lifecycle_state == 1U) {
      check(stats.scene_revision == expected_revision, "expected published revision");
      return;
    }
    check(hss_engine_step(engine, 100U) == HSS_OK, "advance source rebuild");
  }
  check(false, "source rebuild completed within bounded work");
}

std::string scene_json(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK, "acquire snapshot");
  uint64_t required = 0U;
  check(hss_snapshot_copy_scene_json(snapshot, nullptr, 0U, &required) ==
            HSS_BUFFER_TOO_SMALL,
        "query scene JSON");
  std::string result(required, '\0');
  check(hss_snapshot_copy_scene_json(
            snapshot, reinterpret_cast<uint8_t *>(result.data()), result.size(),
            &required) == HSS_OK,
        "copy scene JSON");
  hss_snapshot_release(snapshot);
  return result;
}

std::vector<HSSVec3d> mesh_vertices(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK, "acquire mesh snapshot");
  HSSCounts counts{};
  check(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK, "read mesh counts");
  std::vector<HSSVec3d> result(counts.vertex_count);
  uint64_t required = 0U;
  check(hss_snapshot_copy_vertices(snapshot, result.data(), result.size(), &required) ==
            HSS_OK &&
            required == result.size(),
        "copy captured vertices");
  hss_snapshot_release(snapshot);
  return result;
}

bool same_vertices(const std::vector<HSSVec3d> &left,
                   const std::vector<HSSVec3d> &right) {
  if (left.size() != right.size())
    return false;
  for (size_t index = 0U; index < left.size(); ++index) {
    if (left[index].x != right[index].x || left[index].y != right[index].y ||
        left[index].z != right[index].z)
      return false;
  }
  return true;
}

std::string result_json(HSSEngine *engine) {
  uint64_t required = 0U;
  const HSSStatus initial = hss_engine_copy_command_result_json(
      engine, nullptr, 0U, &required);
  check(initial == HSS_BUFFER_TOO_SMALL && required != 0U,
        "query exact command result");
  std::string result(required, '\0');
  check(hss_engine_copy_command_result_json(
            engine, reinterpret_cast<uint8_t *>(result.data()), result.size(),
            &required) == HSS_OK,
        "copy exact command result");
  return result;
}

std::string project_id_from_scene(const std::string &scene) {
  constexpr std::string_view prefix = "\"project_id\":\"";
  const size_t begin = scene.find(prefix);
  check(begin != std::string::npos, "scene project id present");
  const size_t first = begin + prefix.size();
  const size_t end = scene.find('"', first);
  check(end != std::string::npos, "scene project id closed");
  return scene.substr(first, end - first);
}

std::string mark_unknown_command(const std::string &project_id) {
  return "{\"schema_version\":\"1.1\",\"command_id\":"
         "\"aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa1\",\"project_id\":\"" +
         project_id +
         "\",\"expected_scene_revision\":\"3\",\"event_sequence\":\"5\","
         "\"targets\":[],\"user_intent\":\"host regression\","
         "\"type\":\"mark_unknown\",\"payload\":{\"reason\":"
         "\"occluded rear face remains unknown\"}}";
}

std::string pending_exclusion_command(const std::string &project_id) {
  return "{\"schema_version\":\"1.1\",\"command_id\":"
         "\"aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa2\",\"project_id\":\"" +
         project_id +
         "\",\"expected_scene_revision\":\"3\",\"event_sequence\":\"4\","
         "\"targets\":[],\"user_intent\":\"host regression\","
         "\"type\":\"exclude_dynamic_region\",\"payload\":{"
         "\"min_m\":[0,0,-1],\"max_m\":[1,1,0],"
         "\"start_sequence\":\"1\",\"end_sequence\":\"2\","
         "\"reason\":\"moving person\"}}";
}

void submit_baseline(HSSEngine *engine, HSSProjectStore *store,
                     const std::string &command, std::string *edited_scene,
                     std::string *exact_result) {
  HSSProjectStoreReceipt receipt{};
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = id(2U);
  epoch.event_sequence = 1U;
  epoch.relationship_state = 1U;
  epoch.T_project_world = identity();
  epoch.transform_revision = 1U;
  const auto c1 = context(1U, 1U);
  check(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "queue epoch");
  drain(engine, 1U);
  check(hss_project_store_append_epoch(store, &c1, &epoch, &receipt) == HSS_OK,
        "persist epoch");

  const std::array<float, 9U> vertices{0.F, 0.F, 0.F, 2.F, 0.F, 0.F,
                                        0.F, 3.F, 0.F};
  const std::array<uint32_t, 3U> triangles{0U, 1U, 2U};
  HSSMeshEvent mesh{};
  mesh.abi_version = HSS_ABI_VERSION;
  mesh.struct_size = sizeof(mesh);
  mesh.capture_id = id(2U);
  mesh.anchor_id = id(3U);
  mesh.event_sequence = 2U;
  mesh.anchor_revision = 1U;
  mesh.T_world_anchor = identity();
  mesh.vertices_xyz = vertices.data();
  mesh.vertex_count = 3U;
  mesh.triangle_indices = triangles.data();
  mesh.triangle_count = 1U;
  const auto c2 = context(2U, 2U);
  check(hss_engine_submit_mesh_event(engine, &mesh) == HSS_OK, "queue mesh");
  drain(engine, 2U);
  check(hss_project_store_append_mesh_event(store, &c2, &mesh, &receipt) ==
            HSS_OK,
        "persist mesh");

  const HSSMat4d floor = identity();
  HSSProjectPlanReference plan{};
  plan.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  plan.struct_size = sizeof(plan);
  plan.T_project_floor = floor;
  plan.cut_height_m = 1.2;
  plan.before_scene_revision = 2U;
  plan.after_scene_revision = 3U;
  const auto c3 = context(3U, 3U);
  check(hss_engine_set_plan_reference(engine, &floor, plan.cut_height_m) == HSS_OK,
        "apply floor control");
  drain(engine, 3U);
  check(hss_project_store_append_plan_reference(store, &c3, &plan, &receipt) ==
            HSS_OK,
        "persist floor control");

  const auto raw_vertices = mesh_vertices(engine);
  const std::string pending = pending_exclusion_command(project_id_from_scene(scene_json(engine)));
  const std::string before_pending = scene_json(engine);
  check(hss_engine_apply_command_json(
            engine, {reinterpret_cast<const uint8_t *>(pending.data()), pending.size()}) ==
            HSS_UNSUPPORTED_CAPABILITY,
        "typed dynamic exclusion remains pending without a B035 consumer");
  const std::string pending_result = result_json(engine);
  check(!pending_result.empty() && scene_json(engine) == before_pending,
        "pending exclusion retains exact result without replacing snapshot");
  const auto c_pending = context(4U, 3U);
  HSSProjectCommandRecord pending_record{};
  pending_record.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  pending_record.struct_size = sizeof(pending_record);
  pending_record.context = c_pending;
  pending_record.before_scene_revision = 3U;
  pending_record.after_scene_revision = 3U;
  pending_record.disposition = HSS_PROJECT_COMMAND_PENDING_EXCLUSION;
  pending_record.command_json = {reinterpret_cast<const uint8_t *>(pending.data()), pending.size()};
  pending_record.result_json = {reinterpret_cast<const uint8_t *>(pending_result.data()),
                                pending_result.size()};
  HSSProjectStoreReceipt pending_receipt{};
  check(hss_project_store_append_command_record(store, &pending_record,
                                                &pending_receipt) == HSS_OK,
        "persist pending typed exclusion exact result");
  const HSSStatus edit_status = hss_engine_apply_command_json(
      engine, {reinterpret_cast<const uint8_t *>(command.data()), command.size()});
  if (edit_status != HSS_OK)
    std::cerr << "scene edit status " << edit_status << " result "
              << result_json(engine) << "\n";
  check(edit_status == HSS_OK, "apply accepted scene edit");
  *exact_result = result_json(engine);
  *edited_scene = scene_json(engine);
  check(edited_scene->find("\"geometry_origin\":\"unknown\"") !=
            std::string::npos,
        "edited metric scene is published");
  check(same_vertices(mesh_vertices(engine), raw_vertices),
        "editing preserves captured mesh buffers exactly");

  const auto c4 = context(5U, 4U);
  HSSProjectCommandRecord record{};
  record.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  record.struct_size = sizeof(record);
  record.context = c4;
  record.before_scene_revision = 3U;
  record.after_scene_revision = 4U;
  record.command_json = {reinterpret_cast<const uint8_t *>(command.data()), command.size()};
  record.result_json = {reinterpret_cast<const uint8_t *>(exact_result->data()),
                        exact_result->size()};
  check(hss_project_store_append_command_record(store, &record, &receipt) == HSS_OK,
        "persist exact command/result control");
}

} // namespace

int main() {
  char temporary[] = "/tmp/hss-engine-edit-XXXXXX";
  check(mkdtemp(temporary) != nullptr, "temporary project root");
  const std::string root(temporary);

  HSSProjectStore *store = nullptr;
  auto store_create = store_config(root, HSS_PROJECT_STORE_CREATE_NEW);
  check(hss_project_store_create(&store_create, &store) == HSS_OK, "create store");
  HSSEngine *engine = nullptr;
  const auto config = engine_config();
  check(hss_engine_create(&config, &engine) == HSS_OK, "create engine");
  check(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
        "set fixture provenance before source");

  // The command is constructed only after the captured baseline has supplied
  // its actual project identity; it never imports a fixture's unrelated IDs.
  const std::string provisional_project = project_id_from_scene(scene_json(engine));
  const std::string command = mark_unknown_command(provisional_project);
  std::string edited_scene;
  std::string exact_result;
  submit_baseline(engine, store, command, &edited_scene, &exact_result);

  // Byte-identical retry returns the recorded result and cannot publish a
  // second revision. Pending source work prevents applying an edit mid-build.
  const auto before_retry = scene_json(engine);
  check(hss_engine_apply_command_json(
            engine, {reinterpret_cast<const uint8_t *>(command.data()), command.size()}) == HSS_OK,
        "byte-identical command retry");
  check(result_json(engine) == exact_result && scene_json(engine) == before_retry,
        "retry preserves exact result and snapshot revision");

  HSSProjectCheckpointResult checkpoint{};
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK,
        "checkpoint command after captured baseline");
  hss_engine_destroy(engine);
  hss_project_store_destroy(store);

  auto store_open = store_config(root, HSS_PROJECT_STORE_OPEN_EXISTING);
  check(hss_project_store_create(&store_open, &store) == HSS_OK, "reopen store");
  check(hss_engine_create(&config, &engine) == HSS_OK, "create replay engine");
  HSSProjectReplayResult replay{};
  check(hss_project_store_replay(store, engine, &replay) == HSS_OK,
        "replay source baseline then command once");
  check(replay.applied_event_count == 5U && replay.engine_status == HSS_OK,
        "all durable source/control events replayed");
  check(scene_json(engine) == edited_scene && result_json(engine) == exact_result,
        "reopened scene and exact command result match accepted publication");

  hss_engine_destroy(engine);
  hss_project_store_destroy(store);
  std::filesystem::remove_all(root);
  std::cout << "live scene-edit adapter publishes immutable edits and replays committed commands\n";
}

#include "hss/hss_project_store.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

struct HSSEngine {
  uint64_t revision{0};
  bool pending{false};
  bool source_set{false};
  std::string pending_name;
  std::vector<std::string> applied;
  std::vector<uint32_t> epoch_relationships;
  std::vector<uint8_t> command_result;
};
struct HSSSnapshot { std::string scene; };

extern "C" HSSStatus hss_engine_set_source_is_synthetic(HSSEngine *engine,
                                                        uint32_t value) {
  if (engine == nullptr || value > 1U || engine->pending ||
      !engine->applied.empty())
    return HSS_INVALID_ARGUMENT;
  engine->source_set = true;
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_set_epoch(HSSEngine *engine,
                                          const HSSEpoch *value) {
  if (!engine || !value || engine->pending)
    return HSS_NOT_READY;
  engine->pending = true;
  engine->pending_name = "epoch";
  engine->epoch_relationships.push_back(value->relationship_state);
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_submit_frame(HSSEngine *engine,
                                             const HSSFrame *) {
  if (!engine || engine->pending)
    return HSS_NOT_READY;
  engine->pending = true;
  engine->pending_name = "frame";
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_submit_mesh_event(HSSEngine *engine,
                                                  const HSSMeshEvent *value) {
  if (!engine || !value || engine->pending)
    return HSS_NOT_READY;
  engine->pending = true;
  engine->pending_name = value->kind == 2U ? "mesh-remove" : "mesh-add";
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_apply_command_json(HSSEngine *engine,
                                                     HSSBytes command) {
  if (!engine || engine->pending)
    return HSS_NOT_READY;
  const std::string input(reinterpret_cast<const char *>(command.data), command.length);
  if (input.find("conflict") != std::string::npos) {
    const std::string result = "{\"state\":\"conflict\"}";
    engine->command_result.assign(result.begin(), result.end());
    return HSS_OK;
  }
  engine->pending = true;
  engine->pending_name = "command";
  const std::string result = "{\"accepted\":true}";
  engine->command_result.assign(result.begin(), result.end());
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_acquire_snapshot(const HSSEngine *engine, HSSSnapshot **out) {
  if (!engine || !out) return HSS_INVALID_ARGUMENT;
  *out = new HSSSnapshot{"{\"revision\":" + std::to_string(engine->revision) + "}"};
  return HSS_OK;
}
extern "C" void hss_snapshot_release(HSSSnapshot *snapshot) { delete snapshot; }
extern "C" HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot *snapshot, uint8_t *out,
                                                     uint64_t capacity, uint64_t *required) {
  if (!snapshot || !required || (!out && capacity != 0U)) return HSS_INVALID_ARGUMENT;
  *required = snapshot->scene.size();
  if (capacity < *required) return HSS_BUFFER_TOO_SMALL;
  if (*required != 0U) std::memcpy(out, snapshot->scene.data(), snapshot->scene.size());
  return HSS_OK;
}
extern "C" HSSStatus
hss_engine_set_plan_reference(HSSEngine *engine, const HSSMat4d *, double cut) {
  if (!engine || engine->pending || cut <= 0.0)
    return HSS_NOT_READY;
  engine->pending = true;
  engine->pending_name = "plan";
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_step(HSSEngine *engine, uint32_t budget) {
  if (!engine || budget == 0U)
    return HSS_INVALID_ARGUMENT;
  if (engine->pending) {
    engine->pending = false;
    ++engine->revision;
    engine->applied.push_back(engine->pending_name);
  }
  return HSS_OK;
}
extern "C" HSSStatus hss_engine_get_stats(const HSSEngine *engine,
                                          HSSEngineStats *stats) {
  if (!engine || !stats)
    return HSS_INVALID_ARGUMENT;
  std::memset(stats, 0, sizeof(*stats));
  stats->abi_version = HSS_ABI_VERSION;
  stats->struct_size = sizeof(*stats);
  stats->scene_revision = engine->revision;
  stats->lifecycle_state = engine->pending ? 2U : 1U;
  return HSS_OK;
}
extern "C" HSSStatus
hss_engine_copy_command_result_json(const HSSEngine *engine, uint8_t *out,
                                    uint64_t capacity, uint64_t *required) {
  if (!engine || !required || (!out && capacity != 0U))
    return HSS_INVALID_ARGUMENT;
  *required = engine->command_result.size();
  if (capacity < *required)
    return HSS_BUFFER_TOO_SMALL;
  if (*required != 0U)
    std::memcpy(out, engine->command_result.data(),
                engine->command_result.size());
  return HSS_OK;
}

namespace {
int failures = 0;
void check(bool value, const char *message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
HSSMat4d identity() {
  HSSMat4d m{};
  m.column_major[0] = m.column_major[5] = m.column_major[10] =
      m.column_major[15] = 1;
  return m;
}
HSSId id(uint8_t value) {
  HSSId result{};
  result.bytes[15] = value;
  return result;
}
HSSProjectEventContext context(uint64_t sequence, uint64_t revision,
                               uint32_t epoch, double time) {
  HSSProjectEventContext c{};
  c.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  c.struct_size = sizeof(c);
  c.capture_id = id(7);
  c.event_sequence = sequence;
  c.accepted_scene_revision = revision;
  c.session_epoch = epoch;
  c.monotonic_time_s = time;
  return c;
}
HSSProjectStoreConfig config(const std::string &root, uint32_t mode,
                             uint64_t pending = 1024U * 1024U) {
  HSSProjectStoreConfig c{};
  c.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  c.struct_size = sizeof(c);
  c.project_id = id(9);
  c.trusted_root_directory_utf8 = {
      reinterpret_cast<const uint8_t *>(root.data()), root.size()};
  c.open_mode = mode;
  c.max_epoch_records = 8;
  c.max_depth_records = 8;
  c.max_mesh_records = 8;
  c.max_command_records = 8;
  c.max_retained_bytes = 8U * 1024U * 1024U;
  c.max_record_bytes = 1024U * 1024U;
  c.max_pending_bytes = pending;
  return c;
}
void append_epoch(HSSProjectStore *store, uint64_t seq, uint64_t rev,
                  uint32_t epoch, uint32_t relationship) {
  auto c = context(seq, rev, epoch, static_cast<double>(seq));
  HSSEpoch e{};
  e.abi_version = HSS_ABI_VERSION;
  e.struct_size = sizeof(e);
  e.capture_id = c.capture_id;
  e.event_sequence = seq;
  e.session_epoch = epoch;
  e.relationship_state = relationship;
  e.T_project_world = identity();
  e.transform_revision = 1;
  HSSProjectStoreReceipt receipt{};
  check(hss_project_store_append_epoch(store, &c, &e, &receipt) == HSS_OK,
        "append epoch");
  check(receipt.durability_state == 0U, "append is receipt only");
}
void run() {
  char pattern[] = "/tmp/hss-project-store-XXXXXX";
  char *root = mkdtemp(pattern);
  check(root != nullptr, "temporary root");
  if (!root)
    return;
  const std::string root_path(root);
  HSSProjectStore *store = nullptr;
  auto create = config(root_path, HSS_PROJECT_STORE_CREATE_NEW);
  check(hss_project_store_create(&create, &store) == HSS_OK && store,
        "create project");
  append_epoch(store, 1, 1, 1, 1);
  std::array<uint8_t, 16> depth{};
  for (size_t i = 0; i < 4; ++i) {
    uint32_t bits = std::bit_cast<uint32_t>(1.0F + static_cast<float>(i));
    for (size_t b = 0; b < 4; ++b)
      depth[i * 4 + b] = static_cast<uint8_t>(bits >> (b * 8U));
  }
  auto c2 = context(2, 2, 1, 2.0);
  HSSFrame frame{};
  frame.abi_version = HSS_ABI_VERSION;
  frame.struct_size = sizeof(frame);
  frame.capture_id = c2.capture_id;
  frame.event_sequence = 2;
  frame.session_epoch = 1;
  frame.timestamp_s = 2;
  frame.T_world_camera = identity();
  frame.depth_intrinsics = {1, 1, 0.5, 0.5};
  frame.depth_width = 2;
  frame.depth_height = 2;
  frame.depth_row_stride_bytes = 8;
  frame.depth_byte_length = depth.size();
  frame.depth_bytes = depth.data();
  HSSProjectStoreReceipt receipt{};
  check(hss_project_store_append_frame(store, &c2, &frame, &receipt) == HSS_OK,
        "append frame");
  HSSProjectCheckpointResult checkpoint{};
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK &&
            checkpoint.durable_revision == 1,
        "first checkpoint");
  append_epoch(store, 3, 3, 2, 0);
  const std::array<float, 9> vertices{0, 0, 0, 1, 0, 0, 0, 1, 0};
  const std::array<uint32_t, 3> indices{0, 1, 2};
  auto c4 = context(4, 4, 1, 4.0);
  HSSMeshEvent mesh{};
  mesh.abi_version = HSS_ABI_VERSION;
  mesh.struct_size = sizeof(mesh);
  mesh.capture_id = c4.capture_id;
  mesh.anchor_id = id(8);
  mesh.event_sequence = 4;
  mesh.anchor_revision = 1;
  mesh.session_epoch = 1;
  mesh.kind = 0;
  mesh.T_world_anchor = identity();
  mesh.vertices_xyz = vertices.data();
  mesh.vertex_count = 3;
  mesh.triangle_indices = indices.data();
  mesh.triangle_count = 1;
  check(hss_project_store_append_mesh_event(store, &c4, &mesh, &receipt) ==
            HSS_OK,
        "append mesh add");
  auto c5 = context(5, 5, 1, 5.0);
  mesh.event_sequence = 5;
  mesh.kind = 2;
  mesh.vertices_xyz = nullptr;
  mesh.vertex_count = 0;
  mesh.triangle_indices = nullptr;
  mesh.triangle_count = 0;
  check(hss_project_store_append_mesh_event(store, &c5, &mesh, &receipt) ==
            HSS_OK,
        "append mesh removal");
  const std::string command = "{\"command_id\":\"x\"}";
  const std::string result = "{\"accepted\":true}";
  auto c6 = context(6, 6, 1, 6.0);
  HSSProjectCommandRecord cmd{};
  cmd.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  cmd.struct_size = sizeof(cmd);
  cmd.context = c6;
  cmd.before_scene_revision = 5;
  cmd.after_scene_revision = 6;
  cmd.command_json = {reinterpret_cast<const uint8_t *>(command.data()),
                      command.size()};
  cmd.result_json = {reinterpret_cast<const uint8_t *>(result.data()),
                     result.size()};
  check(hss_project_store_append_command_record(store, &cmd, &receipt) ==
            HSS_OK,
        "append accepted command record");
  auto c7 = context(7, 7, 1, 7.0);
  HSSProjectPlanReference plan{};
  plan.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  plan.struct_size = sizeof(plan);
  plan.T_project_floor = identity();
  plan.cut_height_m = 1.2;
  plan.before_scene_revision = 6;
  plan.after_scene_revision = 7;
  check(hss_project_store_append_plan_reference(store, &c7, &plan, &receipt) ==
            HSS_OK,
        "append plan reference");
  const std::string conflict_command = "{\"command_id\":\"conflict\"}";
  const std::string conflict_result = "{\"state\":\"conflict\"}";
  auto c8 = context(8, 7, 1, 8.0);
  cmd.context = c8;
  cmd.before_scene_revision = 7;
  cmd.after_scene_revision = 7;
  cmd.disposition = HSS_PROJECT_COMMAND_CONFLICT;
  cmd.command_json = {reinterpret_cast<const uint8_t *>(conflict_command.data()), conflict_command.size()};
  cmd.result_json = {reinterpret_cast<const uint8_t *>(conflict_result.data()), conflict_result.size()};
  check(hss_project_store_append_command_record(store, &cmd, &receipt) == HSS_OK,
        "append nonmutating conflict audit");
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK &&
            checkpoint.durable_revision == 2 &&
            checkpoint.durable_event_count == 8,
        "second checkpoint");
  hss_project_store_destroy(store);
  store = nullptr;
  auto reopen = config(root_path, HSS_PROJECT_STORE_OPEN_EXISTING);
  check(hss_project_store_create(&reopen, &store) == HSS_OK, "reopen project");
  HSSEngine engine;
  HSSProjectReplayResult replay{};
  check(hss_project_store_replay(store, &engine, &replay) == HSS_OK,
        "replay committed project");
  check(replay.applied_event_count == 8 && engine.revision == 7,
        "all events replay with exact revisions");
  check(engine.applied ==
            std::vector<std::string>({"epoch", "frame", "epoch", "mesh-add",
                                      "mesh-remove", "command", "plan"}),
        "original event/control order");
  check(engine.epoch_relationships == std::vector<uint32_t>({1, 0}),
        "unresolved epoch remains unresolved");
  hss_project_store_destroy(store);
  std::filesystem::path project =
      std::filesystem::path(root_path) / "00000000000000000000000000000009";
  std::ifstream manifest(project / "active.manifest");
  std::string line, last;
  while (std::getline(manifest, line))
    if (line.rfind("record plan-reference ", 0) == 0)
      last = line;
  const size_t path_pos = last.rfind(' ');
  check(path_pos != std::string::npos, "plan chunk path in manifest");
  std::ofstream corrupt(project / last.substr(path_pos + 1),
                        std::ios::binary | std::ios::trunc);
  corrupt.put('\0');
  corrupt.close();
  check(hss_project_store_create(&reopen, &store) == HSS_OK,
        "corrupt active falls back on reopen");
  HSSEngine fallback;
  replay = {};
  check(hss_project_store_replay(store, &fallback, &replay) == HSS_OK,
        "fallback replay");
  check(replay.used_previous_manifest == 1U && replay.durable_revision == 1U &&
            replay.applied_event_count == 2,
        "prior checkpoint preserved");
  hss_project_store_destroy(store);
  store = nullptr;
  auto bounded = config(root_path, HSS_PROJECT_STORE_CREATE_NEW, 1U);
  bounded.project_id = id(10);
  check(hss_project_store_create(&bounded, &store) == HSS_OK,
        "create bounded project");
  auto limited_context = context(1, 1, 1, 1.0);
  limited_context.capture_id = id(7);
  HSSEpoch limited{};
  limited.abi_version = HSS_ABI_VERSION;
  limited.struct_size = sizeof(limited);
  limited.capture_id = limited_context.capture_id;
  limited.event_sequence = 1;
  limited.session_epoch = 1;
  limited.relationship_state = 1;
  limited.T_project_world = identity();
  limited.transform_revision = 1;
  check(hss_project_store_append_epoch(store, &limited_context, &limited,
                                       &receipt) == HSS_RESOURCE_LIMIT,
        "pending budget pauses instead of evicting source");
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_NOT_READY,
        "resource rejection creates no saved revision");
  hss_project_store_destroy(store);
  std::error_code error;
  std::filesystem::remove_all(root_path, error);
}
} // namespace
int main() {
  run();
  if (failures)
    return 1;
  std::cout << "project store create/checkpoint/reopen/replay checks passed\n";
  return 0;
}

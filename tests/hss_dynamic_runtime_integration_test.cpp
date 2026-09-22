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
    std::cerr << "FAIL: " << message << '\n';
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

std::string uuid(const HSSId &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(36U);
  for (size_t index = 0U; index < 16U; ++index) {
    if (index == 4U || index == 6U || index == 8U || index == 10U)
      result.push_back('-');
    result.push_back(digits[value.bytes[index] >> 4U]);
    result.push_back(digits[value.bytes[index] & 0x0fU]);
  }
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
  result.max_working_bytes = 32U * 1024U * 1024U;
  result.max_pending_frames = 2U;
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
  result.max_retained_bytes = 32U * 1024U * 1024U;
  result.max_record_bytes = 2U * 1024U * 1024U;
  result.max_pending_bytes = 16U * 1024U * 1024U;
  return result;
}

HSSProjectEventContext context(uint64_t sequence, uint64_t revision) {
  HSSProjectEventContext result{};
  result.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.event_sequence = sequence;
  result.accepted_scene_revision = revision;
  result.session_epoch = 1U;
  result.monotonic_time_s = static_cast<double>(sequence);
  return result;
}

void drain(HSSEngine *engine, uint64_t expected_revision) {
  for (size_t iteration = 0U; iteration < 200'000U; ++iteration) {
    HSSEngineStats stats{};
    check(hss_engine_get_stats(engine, &stats) == HSS_OK, "read stats");
    if (stats.lifecycle_state == 1U) {
      check(stats.scene_revision == expected_revision, "expected revision");
      return;
    }
    check(hss_engine_step(engine, 100U) == HSS_OK, "drain engine");
  }
  check(false, "bounded engine drain");
}

std::string command_result(HSSEngine *engine) {
  uint64_t required = 0U;
  check(hss_engine_copy_command_result_json(engine, nullptr, 0U, &required) ==
            HSS_BUFFER_TOO_SMALL,
        "query command result");
  std::string result(required, '\0');
  check(hss_engine_copy_command_result_json(
            engine, reinterpret_cast<uint8_t *>(result.data()), result.size(),
            &required) == HSS_OK,
        "copy command result");
  return result;
}

struct SnapshotView {
  std::vector<HSSVec3d> vertices;
  std::vector<uint32_t> indices;
  std::vector<uint8_t> inclusion;
  HSSDynamicMaskSummary summary{};
};

SnapshotView snapshot_view(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
        "acquire snapshot");
  HSSCounts counts{};
  check(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK,
        "snapshot counts");
  SnapshotView result;
  result.vertices.resize(static_cast<size_t>(counts.vertex_count));
  result.indices.resize(static_cast<size_t>(counts.triangle_count * 3U));
  result.inclusion.resize(static_cast<size_t>(counts.triangle_count));
  uint64_t required = 0U;
  check(hss_snapshot_copy_vertices(snapshot, result.vertices.data(),
                                   result.vertices.size(), &required) == HSS_OK,
        "copy raw vertices");
  check(hss_snapshot_copy_triangles(snapshot, result.indices.data(),
                                    result.indices.size(), &required) == HSS_OK,
        "copy raw triangles");
  check(hss_snapshot_copy_triangle_inclusion(
            snapshot, result.inclusion.data(), result.inclusion.size(),
            &required) == HSS_OK,
        "copy derived inclusion mask");
  check(hss_snapshot_get_dynamic_mask_summary(snapshot, &result.summary) ==
            HSS_OK,
        "copy mask summary");
  hss_snapshot_release(snapshot);
  return result;
}

bool same_raw(const SnapshotView &left, const SnapshotView &right) {
  if (left.indices != right.indices || left.vertices.size() != right.vertices.size())
    return false;
  for (size_t index = 0U; index < left.vertices.size(); ++index) {
    if (left.vertices[index].x != right.vertices[index].x ||
        left.vertices[index].y != right.vertices[index].y ||
        left.vertices[index].z != right.vertices[index].z)
      return false;
  }
  return true;
}

std::string exclusion_command() {
  return "{\"schema_version\":\"1.1\",\"command_id\":\"" +
         uuid(id(4U)) + "\",\"project_id\":\"" + uuid(id(1U)) +
         "\",\"expected_scene_revision\":\"2\",\"event_sequence\":\"3\","
         "\"targets\":[],\"user_intent\":\"verified moving object\","
         "\"type\":\"exclude_dynamic_region\",\"payload\":{"
         "\"min_m\":[0,0,-0.1],\"max_m\":[1,1,0.1],"
         "\"start_sequence\":\"2\",\"end_sequence\":\"2\","
         "\"reason\":\"tracked motion\"}}";
}

HSSDynamicEvidence evidence() {
  HSSDynamicEvidence result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.evidence_id = id(5U);
  result.exclusion_command_id = id(4U);
  result.project_id = id(1U);
  result.source_id = id(3U);
  result.capture_id = id(2U);
  result.session_epoch = 1U;
  result.event_sequence = 2U;
  result.bounds_min_m = {0.0, 0.0, -0.05};
  result.bounds_max_m = {1.0, 1.0, 0.05};
  result.source_kind = HSS_DYNAMIC_SOURCE_MESH_ANCHOR;
  result.evidence_kind = HSS_DYNAMIC_EVIDENCE_TRACKED_MULTI_FRAME_MOTION;
  return result;
}

void append_baseline(HSSEngine *engine, HSSProjectStore *store,
                     HSSMeshEvent *mesh, std::string *command,
                     std::string *result) {
  HSSProjectStoreReceipt receipt{};
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = id(2U);
  epoch.event_sequence = 1U;
  epoch.session_epoch = 1U;
  epoch.relationship_state = 1U;
  epoch.T_project_world = identity();
  epoch.transform_revision = 1U;
  check(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "queue epoch");
  drain(engine, 1U);
  auto c1 = context(1U, 1U);
  check(hss_project_store_append_epoch(store, &c1, &epoch, &receipt) == HSS_OK,
        "persist epoch");

  static const std::array<float, 18U> vertices{
      0.1F, 0.1F, 0.0F, 0.8F, 0.1F, 0.0F, 0.1F, 0.8F, 0.0F,
      2.0F, 0.1F, 0.0F, 2.8F, 0.1F, 0.0F, 2.0F, 0.8F, 0.0F};
  static const std::array<uint32_t, 6U> indices{0U, 1U, 2U, 3U, 4U, 5U};
  mesh->abi_version = HSS_ABI_VERSION;
  mesh->struct_size = sizeof(*mesh);
  mesh->capture_id = id(2U);
  mesh->anchor_id = id(3U);
  mesh->event_sequence = 2U;
  mesh->anchor_revision = 1U;
  mesh->session_epoch = 1U;
  mesh->kind = 0U;
  mesh->T_world_anchor = identity();
  mesh->vertices_xyz = vertices.data();
  mesh->vertex_count = 6U;
  mesh->triangle_indices = indices.data();
  mesh->triangle_count = 2U;
  check(hss_engine_submit_mesh_event(engine, mesh) == HSS_OK, "queue mesh");
  drain(engine, 2U);
  auto c2 = context(2U, 2U);
  check(hss_project_store_append_mesh_event(store, &c2, mesh, &receipt) ==
            HSS_OK,
        "persist mesh");

  *command = exclusion_command();
  check(hss_engine_apply_command_json(
            engine,
            {reinterpret_cast<const uint8_t *>(command->data()),
             command->size()}) == HSS_UNSUPPORTED_CAPABILITY,
        "journal pending exclusion");
  *result = command_result(engine);
  HSSProjectCommandRecord pending{};
  pending.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  pending.struct_size = sizeof(pending);
  pending.context = context(3U, 2U);
  pending.before_scene_revision = 2U;
  pending.after_scene_revision = 2U;
  pending.disposition = HSS_PROJECT_COMMAND_PENDING_EXCLUSION;
  pending.command_json = {
      reinterpret_cast<const uint8_t *>(command->data()), command->size()};
  pending.result_json = {
      reinterpret_cast<const uint8_t *>(result->data()), result->size()};
  check(hss_project_store_append_command_record(store, &pending, &receipt) ==
            HSS_OK,
        "persist pending exclusion");
}

HSSDynamicExclusionAudit apply_verified(HSSEngine *engine,
                                        const HSSDynamicEvidence &verified) {
  HSSDynamicEvidenceBatch batch{};
  batch.abi_version = HSS_ABI_VERSION;
  batch.struct_size = sizeof(batch);
  batch.exclusion_command_id = id(4U);
  batch.expected_scene_revision = 2U;
  batch.evidence = &verified;
  batch.evidence_count = 1U;
  HSSDynamicExclusionAudit audit{};
  const HSSStatus status =
      hss_engine_apply_dynamic_evidence(engine, &batch, &audit);
  if (status != HSS_OK)
    std::cerr << "dynamic evidence status: " << status << '\n';
  check(status == HSS_OK, "apply verified evidence");
  return audit;
}

} // namespace

int main() {
  char temporary[] = "/tmp/hss-dynamic-runtime-XXXXXX";
  check(mkdtemp(temporary) != nullptr, "temporary project root");
  const std::string root(temporary);

  const auto config = engine_config();
  HSSProjectStore *store = nullptr;
  auto create = store_config(root, HSS_PROJECT_STORE_CREATE_NEW);
  check(hss_project_store_create(&create, &store) == HSS_OK, "create store");
  HSSEngine *engine = nullptr;
  check(hss_engine_create(&config, &engine) == HSS_OK, "create engine");
  check(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
        "declare synthetic fixture");

  HSSMeshEvent mesh{};
  std::string command;
  std::string exact_result;
  append_baseline(engine, store, &mesh, &command, &exact_result);
  const SnapshotView raw = snapshot_view(engine);
  check(raw.inclusion == std::vector<uint8_t>({1U, 1U}),
        "pending command alone excludes nothing");

  const HSSDynamicEvidence verified = evidence();
  HSSDynamicEvidence wrong_capture = verified;
  wrong_capture.capture_id = id(9U);
  HSSDynamicEvidenceBatch invalid{};
  invalid.abi_version = HSS_ABI_VERSION;
  invalid.struct_size = sizeof(invalid);
  invalid.exclusion_command_id = id(4U);
  invalid.expected_scene_revision = 2U;
  invalid.evidence = &wrong_capture;
  invalid.evidence_count = 1U;
  HSSDynamicExclusionAudit rejected{};
  check(hss_engine_apply_dynamic_evidence(engine, &invalid, &rejected) ==
            HSS_INSUFFICIENT_EVIDENCE &&
            snapshot_view(engine).inclusion == raw.inclusion,
        "capture identity mismatch fails with no mask state");

  const HSSDynamicExclusionAudit audit = apply_verified(engine, verified);
  check(audit.scene_revision == 2U && audit.linked_evidence_count == 1U &&
            audit.masked_triangle_count == 1U &&
            audit.triangle_mask_fingerprint != 0U &&
            audit.disposition == HSS_DYNAMIC_VERIFIED_MASK,
        "typed audit records one verified exclusion");
  const SnapshotView masked = snapshot_view(engine);
  check(same_raw(raw, masked), "mask leaves raw mesh byte values unchanged");
  check(masked.inclusion == std::vector<uint8_t>({0U, 1U}) &&
            masked.summary.excluded_triangle_count == 1U &&
            masked.summary.stale_exclusion_count == 0U,
        "snapshot distinguishes excluded and included raw triangles");

  HSSDynamicEvidenceBatch stale{};
  stale.abi_version = HSS_ABI_VERSION;
  stale.struct_size = sizeof(stale);
  stale.exclusion_command_id = id(4U);
  stale.expected_scene_revision = 1U;
  stale.evidence = &verified;
  stale.evidence_count = 1U;
  HSSDynamicExclusionAudit ignored{};
  check(hss_engine_apply_dynamic_evidence(engine, &stale, &ignored) ==
            HSS_STALE_REVISION &&
            snapshot_view(engine).inclusion == masked.inclusion,
        "stale evidence has no partial effect");

  HSSProjectDynamicExclusionRecord durable{};
  durable.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  durable.struct_size = sizeof(durable);
  durable.context = context(4U, 2U);
  durable.scene_revision = 2U;
  durable.exclusion_command_id = id(4U);
  durable.evidence = &verified;
  durable.evidence_count = 1U;
  durable.linked_evidence_count = audit.linked_evidence_count;
  durable.masked_triangle_count = audit.masked_triangle_count;
  durable.triangle_mask_fingerprint = audit.triangle_mask_fingerprint;
  durable.disposition = audit.disposition;
  HSSProjectStoreReceipt receipt{};
  check(hss_project_store_append_dynamic_exclusion_record(
            store, &durable, &receipt) == HSS_OK,
        "append typed dynamic audit");
  HSSProjectCheckpointResult checkpoint{};
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK &&
            checkpoint.durable_event_count == 4U,
        "checkpoint typed dynamic audit");
  hss_engine_destroy(engine);
  hss_project_store_destroy(store);

  auto reopen = store_config(root, HSS_PROJECT_STORE_OPEN_EXISTING);
  check(hss_project_store_create(&reopen, &store) == HSS_OK, "reopen store");
  check(hss_engine_create(&config, &engine) == HSS_OK, "create replay engine");
  HSSProjectReplayResult replay{};
  check(hss_project_store_replay(store, engine, &replay) == HSS_OK &&
            replay.applied_event_count == 4U &&
            replay.durable_event_count == 4U,
        "replay typed exclusion audit");
  const SnapshotView reopened = snapshot_view(engine);
  check(reopened.inclusion == masked.inclusion && same_raw(reopened, masked) &&
            reopened.summary.applied_exclusion_count == 1U,
        "save and reopen deterministically restores derived mask");

  mesh.kind = 1U;
  mesh.anchor_revision = 2U;
  mesh.event_sequence = 5U;
  check(hss_engine_submit_mesh_event(engine, &mesh) == HSS_OK,
        "queue replacement source revision");
  drain(engine, 3U);
  const SnapshotView replaced = snapshot_view(engine);
  check(replaced.inclusion == std::vector<uint8_t>({1U, 1U}) &&
            replaced.summary.stale_exclusion_count == 1U &&
            replaced.summary.excluded_triangle_count == 0U,
        "stale source identity fails safe to fully included raw geometry");

  hss_engine_destroy(engine);
  hss_project_store_destroy(store);
  std::filesystem::remove_all(root);
  std::cout << "dynamic provenance integrates with immutable snapshots and durable replay\n";
}

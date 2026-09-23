#include "hss/hss_api.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

HSSId id(uint8_t value) {
  HSSId result{};
  result.bytes[0] = value;
  return result;
}

HSSMat4d identity() {
  HSSMat4d result{};
  result.column_major[0] = result.column_major[5] =
      result.column_major[10] = result.column_major[15] = 1.0;
  return result;
}

HSSEngineStats stats(HSSEngine *engine) {
  HSSEngineStats result{};
  require(hss_engine_get_stats(engine, &result) == HSS_OK, "stats");
  return result;
}

void drain(HSSEngine *engine) {
  for (size_t attempt = 0U; attempt < 100000U; ++attempt) {
    if (stats(engine).lifecycle_state == 1U)
      return;
    require(hss_engine_step(engine, 50U) == HSS_OK, "drain step");
  }
  require(false, "drain bounded");
}

HSSEpoch epoch(uint64_t sequence, uint32_t session_epoch) {
  HSSEpoch result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.event_sequence = sequence;
  result.session_epoch = session_epoch;
  result.relationship_state = 1U;
  result.transform_revision = 1U;
  result.T_project_world = identity();
  return result;
}

HSSMeshEvent mesh(uint64_t sequence, uint32_t kind, uint64_t revision,
                  uint32_t session_epoch) {
  static const float vertices[]{0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                                0.0F, 0.0F, 1.0F, 0.0F};
  static const uint32_t triangles[]{0U, 1U, 2U};
  HSSMeshEvent result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.anchor_id = id(3U);
  result.event_sequence = sequence;
  result.anchor_revision = revision;
  result.session_epoch = session_epoch;
  result.kind = kind;
  result.T_world_anchor = identity();
  if (kind != 2U) {
    result.vertices_xyz = vertices;
    result.vertex_count = 3U;
    result.triangle_indices = triangles;
    result.triangle_count = 1U;
  }
  return result;
}

HSSFrame frame(uint64_t sequence, uint32_t session_epoch,
               uint32_t depth_source) {
  static float depth = 2.0F;
  HSSFrame result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.event_sequence = sequence;
  result.session_epoch = session_epoch;
  result.tracking_state = 0U;
  result.timestamp_s = static_cast<double>(sequence);
  result.T_world_camera = identity();
  result.depth_intrinsics = {200.0, 200.0, 0.0, 0.0};
  result.depth_width = 1U;
  result.depth_height = 1U;
  result.depth_row_stride_bytes = sizeof(depth);
  result.depth_byte_length = sizeof(depth);
  result.depth_bytes = reinterpret_cast<const uint8_t *>(&depth);
  result.depth_source = depth_source;
  return result;
}

HSSResourceSample sample(uint64_t sequence, uint32_t thermal,
                         uint64_t free_storage_bytes) {
  HSSResourceSample result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.sequence = sequence;
  result.monotonic_time_s = static_cast<double>(sequence);
  result.thermal_status = HSS_RESOURCE_MEASUREMENT_MEASURED;
  result.thermal_state = thermal;
  result.memory_status = HSS_RESOURCE_MEASUREMENT_MEASURED;
  result.memory_pressure = HSS_RESOURCE_MEMORY_NOMINAL;
  result.storage_status = HSS_RESOURCE_MEASUREMENT_MEASURED;
  result.free_storage_bytes = free_storage_bytes;
  return result;
}

uint64_t triangle_count(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  require(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
          "snapshot");
  HSSCounts counts{};
  require(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK,
          "snapshot counts");
  hss_snapshot_release(snapshot);
  return counts.triangle_count;
}

} // namespace

int main() {
  HSSConfig config{};
  config.abi_version = HSS_ABI_VERSION;
  config.struct_size = sizeof(config);
  config.project_id = id(1U);
  config.structure_voxel_m = 0.02;
  config.detail_voxel_m = 0.01;
  config.truncation_m = 0.04;
  config.max_working_bytes = 16U * 1024U * 1024U;
  config.max_pending_frames = 4U;
  HSSEngine *engine = nullptr;
  require(hss_engine_create(&config, &engine) == HSS_OK, "create");

  HSSResourceDecision decision{};
  require(hss_engine_get_resource_decision(engine, &decision) == HSS_OK &&
              decision.mode == HSS_RESOURCE_MODE_NORMAL &&
              decision.reason == HSS_RESOURCE_REASON_MEASUREMENTS_UNAVAILABLE &&
              decision.accept_metric_observations == 1U &&
              decision.validity_checks_enabled == 1U,
          "safe initial decision without invented measurements");

  auto accepted_epoch = epoch(1U, 0U);
  require(hss_engine_set_epoch(engine, &accepted_epoch) == HSS_OK,
          "initial epoch");
  drain(engine);

  auto constrained = sample(1U, HSS_RESOURCE_THERMAL_SERIOUS,
                            2ULL * 1024ULL * 1024ULL * 1024ULL);
  require(hss_engine_observe_resource_sample(engine, &constrained, &decision) ==
              HSS_OK &&
              decision.mode == HSS_RESOURCE_MODE_CONSERVING &&
              decision.reduce_preview_quality == 1U &&
              decision.accept_metric_observations == 1U &&
              decision.validity_checks_enabled == 1U,
          "conserving keeps metric validity and intake");

  auto retained_mesh = mesh(2U, 0U, 1U, 0U);
  require(hss_engine_submit_mesh_event(engine, &retained_mesh) == HSS_OK,
          "conserving metric mesh accepted");
  auto preview = frame(3U, 0U, 1U);
  require(hss_engine_submit_frame(engine, &preview) == HSS_OK &&
              stats(engine).dropped_frames == 1U,
          "conserving preview dropped");
  auto metric_frame = frame(3U, 0U, 0U);
  require(hss_engine_submit_frame(engine, &metric_frame) == HSS_OK,
          "conserving calibrated depth accepted");

  auto unsafe = sample(2U, HSS_RESOURCE_THERMAL_CRITICAL,
                       256ULL * 1024ULL * 1024ULL);
  require(hss_engine_observe_resource_sample(engine, &unsafe, &decision) ==
              HSS_OK &&
              decision.mode == HSS_RESOURCE_MODE_CHECKPOINT_PENDING &&
              decision.request_checkpoint == 1U &&
              decision.pause_capture == 1U &&
              decision.retain_existing_sources == 1U,
          "unsafe pressure requests checkpoint and retains sources");
  auto rejected_replace = mesh(4U, 1U, 2U, 0U);
  require(hss_engine_submit_mesh_event(engine, &rejected_replace) ==
              HSS_NOT_READY,
          "unsafe pressure rejects new metric mesh");
  drain(engine);
  require(triangle_count(engine) == 1U,
          "work accepted before unsafe pressure drains and remains retained");
  auto floor = identity();
  require(hss_engine_set_plan_reference(engine, &floor, 1.2) == HSS_OK,
          "plan reference control protected while checkpoint pending");
  drain(engine);
  const uint8_t invalid_command[]{'{', '}'};
  require(hss_engine_apply_command_json(
              engine, {invalid_command, sizeof(invalid_command)}) ==
              HSS_INVALID_ARGUMENT,
          "command reaches ordinary validation while checkpoint pending");

  auto removal = mesh(4U, 2U, 1U, 0U);
  require(hss_engine_submit_mesh_event(engine, &removal) == HSS_OK,
          "mesh removal protected while checkpoint pending");
  drain(engine);
  require(triangle_count(engine) == 0U, "explicit removal applies");

  require(hss_engine_acknowledge_resource_checkpoint(
              engine, HSS_RESOURCE_CHECKPOINT_SUCCEEDED, &decision) == HSS_OK &&
              decision.mode == HSS_RESOURCE_MODE_PAUSED &&
              decision.pause_capture == 1U,
          "durable checkpoint acknowledgement pauses intake");
  auto next_epoch = epoch(5U, 1U);
  require(hss_engine_set_epoch(engine, &next_epoch) == HSS_OK,
          "epoch transition protected while paused");
  drain(engine);

  for (uint64_t sequence = 3U; sequence <= 5U; ++sequence) {
    auto clear = sample(sequence, HSS_RESOURCE_THERMAL_NOMINAL,
                        2ULL * 1024ULL * 1024ULL * 1024ULL);
    require(hss_engine_observe_resource_sample(engine, &clear, &decision) ==
                HSS_OK,
            "ordered recovery sample");
  }
  require(decision.mode == HSS_RESOURCE_MODE_NORMAL &&
              decision.accept_metric_observations == 1U,
          "complete hysteresis recovers metric intake");

  auto stale = sample(5U, HSS_RESOURCE_THERMAL_NOMINAL,
                      2ULL * 1024ULL * 1024ULL * 1024ULL);
  require(hss_engine_observe_resource_sample(engine, &stale, &decision) ==
              HSS_STALE_REVISION &&
              decision.input_fault == 1U &&
              decision.accept_metric_observations == 0U &&
              decision.pause_capture == 1U,
          "out-of-order sample fails closed");
  auto rejected_frame = frame(6U, 1U, 0U);
  require(hss_engine_submit_frame(engine, &rejected_frame) == HSS_NOT_READY,
          "input fault blocks metric depth");

  HSSResourceSample unmeasured{};
  unmeasured.abi_version = HSS_ABI_VERSION;
  unmeasured.struct_size = sizeof(unmeasured);
  unmeasured.sequence = 6U;
  unmeasured.monotonic_time_s = 6.0;
  require(hss_engine_observe_resource_sample(engine, &unmeasured, &decision) ==
              HSS_OK &&
              decision.input_fault == 1U &&
              decision.accept_metric_observations == 0U,
          "unmeasured sample cannot clear input fault");
  auto complete = sample(7U, HSS_RESOURCE_THERMAL_NOMINAL,
                         2ULL * 1024ULL * 1024ULL * 1024ULL);
  require(hss_engine_observe_resource_sample(engine, &complete, &decision) ==
              HSS_OK &&
              decision.input_fault == 0U &&
              decision.accept_metric_observations == 1U,
          "complete ordered sample clears input fault");
  require(hss_engine_submit_frame(engine, &rejected_frame) == HSS_OK,
          "metric depth resumes after complete sample");
  drain(engine);

  auto invalid = sample(8U, HSS_RESOURCE_THERMAL_NOMINAL,
                        2ULL * 1024ULL * 1024ULL * 1024ULL);
  invalid.thermal_state = 99U;
  require(hss_engine_observe_resource_sample(engine, &invalid, &decision) ==
              HSS_INVALID_ARGUMENT &&
              decision.input_fault == 1U &&
              decision.last_accepted_sample_sequence == 7U,
          "invalid measured value fails closed without advancing order");
  auto fault_frame = frame(7U, 1U, 0U);
  require(hss_engine_submit_frame(engine, &fault_frame) == HSS_NOT_READY,
          "invalid measured value blocks metric depth");
  auto repaired = sample(8U, HSS_RESOURCE_THERMAL_NOMINAL,
                         2ULL * 1024ULL * 1024ULL * 1024ULL);
  require(hss_engine_observe_resource_sample(engine, &repaired, &decision) ==
              HSS_OK &&
              decision.input_fault == 0U,
          "corrected ordered sample is accepted");

  hss_engine_destroy(engine);
  std::cout << "resource runtime integration checks passed\n";
  return 0;
}

#include "hss/hss_api.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HSSConfig valid_config() {
  HSSConfig config{};
  config.abi_version = HSS_ABI_VERSION;
  config.struct_size = sizeof(config);
  config.structure_voxel_m = 0.02;
  config.detail_voxel_m = 0.01;
  config.truncation_m = 0.04;
  config.max_working_bytes = 1024U * 1024U;
  config.max_pending_frames = 2;
  return config;
}

HSSMat4d identity_transform() {
  HSSMat4d transform{};
  transform.column_major[0] = 1.0;
  transform.column_major[5] = 1.0;
  transform.column_major[10] = 1.0;
  transform.column_major[15] = 1.0;
  return transform;
}

HSSFrame valid_frame(const uint8_t* depth) {
  HSSFrame frame{};
  frame.abi_version = HSS_ABI_VERSION;
  frame.struct_size = sizeof(frame);
  frame.tracking_state = 0;
  frame.timestamp_s = 0.0;
  frame.T_world_camera = identity_transform();
  frame.depth_intrinsics = {1.0, 1.0, 0.0, 0.0};
  frame.depth_width = 1;
  frame.depth_height = 1;
  frame.depth_row_stride_bytes = 4;
  frame.depth_byte_length = 4;
  frame.depth_bytes = depth;
  frame.depth_source = 0;
  return frame;
}

template <typename T, size_t ExtensionSize>
struct ExtendedMessage {
  T value;
  uint8_t extension[ExtensionSize];
};

}  // namespace

int main() {
  HSSEngine* engine = nullptr;
  HSSConfig invalid = valid_config();
  invalid.abi_version = HSS_ABI_VERSION + 1U;
  check(hss_engine_create(&invalid, &engine) == HSS_UNSUPPORTED_VERSION, "invalid ABI version maps safely");
  check(engine == nullptr, "failed create clears handle");

  HSSConfig config = valid_config();
  check(hss_engine_create(&config, &engine) == HSS_OK && engine != nullptr, "create");

  ExtendedMessage<HSSConfig, 8> extended_config{};
  extended_config.value = valid_config();
  extended_config.value.struct_size = sizeof(extended_config);
  HSSEngine* extension_engine = nullptr;
  check(hss_engine_create(&extended_config.value, &extension_engine) == HSS_OK && extension_engine != nullptr,
        "zero ABI extension accepted when allocated");
  hss_engine_destroy(extension_engine);
  extended_config.extension[3] = 1U;
  check(hss_engine_create(&extended_config.value, &extension_engine) == HSS_INVALID_ARGUMENT && extension_engine == nullptr,
        "nonzero ABI extension rejected and output cleared");

  HSSConfig undersized_config = valid_config();
  undersized_config.struct_size = sizeof(undersized_config) - 1U;
  check(hss_engine_create(&undersized_config, &extension_engine) == HSS_INVALID_ARGUMENT && extension_engine == nullptr,
        "undersized ABI message rejected");
  HSSConfig unknown_flag = valid_config();
  unknown_flag.flags = 1U;
  check(hss_engine_create(&unknown_flag, &extension_engine) == HSS_INVALID_ARGUMENT && extension_engine == nullptr,
        "unsupported config flags rejected");

  try {
    check(hss_engine_submit_frame(engine, nullptr) == HSS_INVALID_ARGUMENT,
          "invalid C input returns a status rather than escaping an exception");
  } catch (...) {
    check(false, "C facade allowed an exception to escape");
  }

  const uint8_t depth[] = {0U, 0U, 0U, 0U};
  HSSFrame frame = valid_frame(depth);
  check(hss_engine_submit_frame(engine, &frame) == HSS_OK,
        "well-formed frame is copied into bounded ingress");
  HSSEngineStats ingress_stats{};
  check(hss_engine_get_stats(engine, &ingress_stats) == HSS_OK &&
            ingress_stats.accepted_frames == 1U && ingress_stats.pending_frames == 1U &&
            ingress_stats.working_bytes == 4U,
        "ingress exposes accepted frame, pending frame, and owned byte accounting");
  frame.abi_version = HSS_ABI_VERSION + 1U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_UNSUPPORTED_VERSION,
        "frame ABI version mismatch rejected");
  frame = valid_frame(depth);
  frame.struct_size -= 1U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "undersized frame rejected before access");
  frame = valid_frame(depth);
  frame.reserved = 1U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "unsupported frame flags rejected");
  frame = valid_frame(depth);
  frame.depth_source = 2U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "unsupported frame enum rejected");
  frame = valid_frame(depth);
  frame.depth_bytes = nullptr;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "null depth with dimensions rejected");
  frame = valid_frame(depth);
  frame.confidence_byte_length = 1U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "null confidence length mismatch rejected");
  frame = valid_frame(depth);
  frame.depth_row_stride_bytes = 3U;
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "depth stride shorter than row rejected");
  frame = valid_frame(depth);
  frame.depth_height = std::numeric_limits<uint32_t>::max();
  frame.depth_row_stride_bytes = std::numeric_limits<uint64_t>::max();
  frame.depth_byte_length = std::numeric_limits<uint64_t>::max();
  check(hss_engine_submit_frame(engine, &frame) == HSS_INVALID_ARGUMENT,
        "overflowing depth address range rejected before copy");

  ExtendedMessage<HSSFrame, 8> extended_frame{};
  extended_frame.value = valid_frame(depth);
  extended_frame.value.struct_size = sizeof(extended_frame);
  check(hss_engine_submit_frame(engine, &extended_frame.value) == HSS_OK,
        "zero frame extension accepted when allocated");
  extended_frame.extension[0] = 1U;
  check(hss_engine_submit_frame(engine, &extended_frame.value) == HSS_INVALID_ARGUMENT,
        "nonzero frame extension rejected");

  HSSMeshEvent mesh{};
  mesh.abi_version = HSS_ABI_VERSION;
  mesh.struct_size = sizeof(mesh);
  mesh.kind = 3U;
  mesh.T_world_anchor = identity_transform();
  check(hss_engine_submit_mesh_event(engine, &mesh) == HSS_INVALID_ARGUMENT,
        "unsupported mesh enum rejected");
  float vertex = 0.0F;
  mesh.kind = 0U;
  mesh.vertices_xyz = &vertex;
  mesh.vertex_count = std::numeric_limits<uint64_t>::max();
  check(hss_engine_submit_mesh_event(engine, &mesh) == HSS_RESOURCE_LIMIT,
        "overflowing mesh byte count rejected before copy");

  uint8_t byte = 0U;
  check(hss_engine_submit_room_proposal_json(engine, {nullptr, 1U}) == HSS_INVALID_ARGUMENT,
        "null JSON pointer with length rejected");
  check(hss_engine_apply_command_json(engine, {&byte, 16U * 1024U * 1024U + 1U}) == HSS_RESOURCE_LIMIT,
        "oversized JSON rejected before dereference");
  check(hss_engine_apply_command_json(engine, {&byte, 1U}) == HSS_OK,
        "command is synchronously copied into bounded ingress");
  check(hss_engine_get_stats(engine, &ingress_stats) == HSS_OK &&
            ingress_stats.accepted_commands == 1U && ingress_stats.pending_frames == 2U,
        "command acceptance and frame queue accounting are visible in stats");
  check(hss_engine_step(engine, 1U) == HSS_OK, "bounded step drains at most one FIFO event");
  check(hss_engine_get_stats(engine, &ingress_stats) == HSS_OK && ingress_stats.pending_frames == 1U,
        "step releases the first queued frame without retaining its input buffer");

  HSSConfig constrained_config = valid_config();
  constrained_config.max_working_bytes = 4U;
  constrained_config.max_pending_frames = 1U;
  HSSEngine* constrained_engine = nullptr;
  check(hss_engine_create(&constrained_config, &constrained_engine) == HSS_OK,
        "create bounded overload engine");
  HSSFrame constrained_frame = valid_frame(depth);
  check(hss_engine_submit_frame(constrained_engine, &constrained_frame) == HSS_OK,
        "first constrained frame accepted");
  check(hss_engine_submit_frame(constrained_engine, &constrained_frame) == HSS_OK,
        "second constrained frame is an observable drop, not an error");
  check(hss_engine_get_stats(constrained_engine, &ingress_stats) == HSS_OK &&
            ingress_stats.accepted_frames == 1U && ingress_stats.dropped_frames == 1U &&
            ingress_stats.pending_frames == 1U,
        "frame overload increments an explicit drop counter");
  HSSMeshEvent removal{};
  removal.abi_version = HSS_ABI_VERSION;
  removal.struct_size = sizeof(removal);
  removal.kind = 2U;
  removal.T_world_anchor = identity_transform();
  check(hss_engine_submit_mesh_event(constrained_engine, &removal) == HSS_OK,
        "zero-byte mesh removal is retained while frames are full");
  check(hss_engine_apply_command_json(constrained_engine, {&byte, 1U}) == HSS_RESOURCE_LIMIT,
        "stalled consumer gets explicit command backpressure");
  check(hss_engine_step(constrained_engine, 1U) == HSS_OK,
        "constrained consumer drains one event");
  check(hss_engine_apply_command_json(constrained_engine, {&byte, 1U}) == HSS_OK,
        "caller can retry command after capacity returns");
  hss_engine_destroy(constrained_engine);

  uint64_t command_required = 99U;
  check(hss_engine_copy_command_result_json(engine, nullptr, 0U, &command_required) == HSS_NOT_READY &&
            command_required == 0U,
        "not-ready command output clears required count");

  HSSSnapshot* snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK && snapshot != nullptr, "acquire snapshot");
  hss_engine_destroy(engine);

  HSSEngineStats failed_stats{};
  std::memset(&failed_stats, 0xa5, sizeof(failed_stats));
  check(hss_engine_get_stats(nullptr, &failed_stats) == HSS_INVALID_ARGUMENT,
        "null engine stats rejected");
  for (const uint8_t value : std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&failed_stats),
                                                   reinterpret_cast<uint8_t*>(&failed_stats) + sizeof(failed_stats))) {
    check(value == 0U, "failed stats output cleared");
  }

  HSSCounts counts{};
  check(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK, "snapshot survives engine destroy");
  check(counts.vertex_count == 0U && counts.triangle_count == 0U, "empty facade snapshot has no scanner geometry");

  uint64_t required = 0;
  check(hss_snapshot_copy_scene_json(snapshot, nullptr, 0, &required) == HSS_BUFFER_TOO_SMALL && required > 0U,
        "scene JSON size query");
  std::vector<uint8_t> undersized(required, 0xa5U);
  check(hss_snapshot_copy_scene_json(snapshot, undersized.data(), required - 1U, &required) == HSS_BUFFER_TOO_SMALL,
        "undersized scene JSON rejected");
  for (uint8_t value : undersized) {
    check(value == 0xa5U, "undersized copy does not partially write");
  }
  std::vector<uint8_t> scene(required);
  check(hss_snapshot_copy_scene_json(snapshot, scene.data(), scene.size(), &required) == HSS_OK,
        "scene JSON complete copy");
  check(std::memcmp(scene.data(), "{\"schema_version\"", 17) == 0, "schema-shaped empty snapshot");
  hss_snapshot_release(snapshot);
  hss_snapshot_release(nullptr);

  std::cout << "HSS engine facade ownership and copy-out checks passed (no scanner output)\n";
  return EXIT_SUCCESS;
}

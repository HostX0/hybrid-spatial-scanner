#include "hss/hss_api.h"
#include "hss/coordinate_basis.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

// This facade intentionally owns only lifecycle, validation, synchronous input
// copies, and immutable empty-scene snapshots. It is not a capture or
// reconstruction implementation.
struct HSSEngine {
  HSSConfig config{};
  std::vector<uint8_t> staged_input;
  std::string scene_json;
  uint64_t scene_revision{0};
  uint64_t accepted_frames{0};
  uint64_t dropped_frames{0};
  uint64_t accepted_commands{0};
  HSSStatus last_status{HSS_OK};
};

struct HSSSnapshot {
  uint64_t scene_revision{0};
  std::vector<HSSVec3d> vertices;
  std::vector<uint32_t> triangles;
  std::string scene_json;
};

namespace {

template <typename T>
HSSStatus validate_message(const T* value) {
  if (value == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  if (value->abi_version != HSS_ABI_VERSION) {
    return HSS_UNSUPPORTED_VERSION;
  }
  if (value->struct_size < sizeof(T)) {
    return HSS_INVALID_ARGUMENT;
  }
  const auto* trailing = reinterpret_cast<const uint8_t*>(value) + sizeof(T);
  const size_t trailing_size = static_cast<size_t>(value->struct_size - sizeof(T));
  if (!std::all_of(trailing, trailing + trailing_size,
                   [](uint8_t byte) { return byte == 0; })) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool multiply_add_fits(uint64_t stride, uint32_t rows, uint64_t row_bytes,
                       uint64_t* required) {
  if (required == nullptr) {
    return false;
  }
  if (rows == 0U) {
    *required = 0;
    return true;
  }
  const uint64_t preceding_rows = static_cast<uint64_t>(rows - 1U);
  if (preceding_rows != 0U &&
      stride > (std::numeric_limits<uint64_t>::max() - row_bytes) / preceding_rows) {
    return false;
  }
  *required = stride * preceding_rows + row_bytes;
  return true;
}

HSSStatus validate_config(const HSSConfig* config) {
  const HSSStatus header_status = validate_message(config);
  if (header_status != HSS_OK) {
    return header_status;
  }
  if (!finite_positive(config->structure_voxel_m) || !finite_positive(config->detail_voxel_m) ||
      !finite_positive(config->truncation_m) || config->max_working_bytes == 0U ||
      config->max_pending_frames == 0U || config->flags != 0U || config->reserved != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

HSSStatus copy_into_stage(HSSEngine* engine, const uint8_t* data, uint64_t length) {
  if ((data == nullptr && length != 0U) || engine == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  if (length > engine->config.max_working_bytes ||
      length > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    return HSS_RESOURCE_LIMIT;
  }
  if (length == 0U) {
    engine->staged_input.clear();
    return HSS_OK;
  }
  engine->staged_input.assign(data, data + static_cast<size_t>(length));
  return HSS_OK;
}

HSSStatus append_into_stage(HSSEngine* engine, const uint8_t* data, uint64_t length) {
  if ((data == nullptr && length != 0U) || engine == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  if (length > engine->config.max_working_bytes ||
      engine->staged_input.size() > engine->config.max_working_bytes - length ||
      length > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    return HSS_RESOURCE_LIMIT;
  }
  if (length == 0U) {
    return HSS_OK;
  }
  engine->staged_input.insert(engine->staged_input.end(), data,
                              data + static_cast<size_t>(length));
  return HSS_OK;
}

std::string uuid_string(const HSSId& id) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (size_t i = 0; i < 16; ++i) {
    result.push_back(kHex[id.bytes[i] >> 4U]);
    result.push_back(kHex[id.bytes[i] & 0x0fU]);
    if (i == 3 || i == 5 || i == 7 || i == 9) {
      result.push_back('-');
    }
  }
  return result;
}

std::string empty_scene_json(const HSSId& project_id) {
  return "{\"schema_version\":\"1.1\",\"project_id\":\"" + uuid_string(project_id) +
         "\",\"scene_revision\":\"0\",\"source_is_synthetic\":false,\"units\":\"m\","
         "\"basis\":{\"handedness\":\"right\",\"up\":\"+Y\",\"floor_origin_m\":[0,0,0],"
         "\"floor_u\":[1,0,0],\"floor_v\":[0,0,-1],\"floor_n\":[0,1,0]},\"epochs\":[],"
         "\"nodes\":[],\"edges\":[],\"surfaces\":[],\"features\":[],\"regions\":[],"
         "\"objects\":[],\"constraints\":[],\"issues\":[],\"quality_state\":\"provisional\"}";
}

template <typename T>
HSSStatus copy_out(const T* source, uint64_t count, T* destination, uint64_t capacity,
                   uint64_t* required) {
  if (required == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  *required = 0;
  if (destination == nullptr && capacity != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  if (capacity < count) {
    *required = count;
    return HSS_BUFFER_TOO_SMALL;
  }
  *required = count;
  if (count != 0U) {
    std::memcpy(destination, source, static_cast<size_t>(count) * sizeof(T));
  }
  return HSS_OK;
}

HSSStatus validate_bytes(HSSBytes bytes) {
  if (bytes.data == nullptr && bytes.length != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

HSSStatus remembered(HSSEngine* engine, HSSStatus status) {
  engine->last_status = status;
  return status;
}

}  // namespace

extern "C" HSSStatus hss_engine_create(const HSSConfig* config, HSSEngine** out_engine) {
  if (out_engine != nullptr) {
    *out_engine = nullptr;
  }
  try {
    if (out_engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus status = validate_config(config);
    if (status != HSS_OK) {
      return status;
    }
    auto* engine = new HSSEngine();
    engine->config = *config;
    engine->scene_json = empty_scene_json(config->project_id);
    *out_engine = engine;
    return HSS_OK;
  } catch (const std::bad_alloc&) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_engine_destroy(HSSEngine* engine) {
  try {
    delete engine;
  } catch (...) {
    // C linkage must not expose a C++ exception. There is no status channel.
  }
}

extern "C" HSSStatus hss_engine_set_epoch(HSSEngine* engine, const HSSEpoch* epoch) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus status = validate_message(epoch);
    if (status != HSS_OK) {
      return remembered(engine, status);
    }
    if (epoch->relationship_state > 1U ||
        !hss::coordinate::is_rigid_transform(epoch->T_project_world)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    return remembered(engine, HSS_OK);
  } catch (const std::bad_alloc&) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_frame(HSSEngine* engine, const HSSFrame* frame) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(frame);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (frame->tracking_state > 2U || frame->depth_source > 1U || !std::isfinite(frame->timestamp_s) ||
        !hss::coordinate::is_rigid_transform(frame->T_world_camera)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    if (!finite_positive(frame->depth_intrinsics.fx) || !finite_positive(frame->depth_intrinsics.fy) ||
        !std::isfinite(frame->depth_intrinsics.cx) || !std::isfinite(frame->depth_intrinsics.cy)) {
      return remembered(engine, HSS_INVALID_CALIBRATION);
    }
    if (frame->depth_width == 0U || frame->depth_height == 0U ||
        frame->depth_row_stride_bytes < static_cast<uint64_t>(frame->depth_width) * 4U ||
        frame->depth_bytes == nullptr) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    uint64_t required_depth = 0;
    if (!multiply_add_fits(frame->depth_row_stride_bytes, frame->depth_height,
                           static_cast<uint64_t>(frame->depth_width) * 4U, &required_depth) ||
        frame->depth_byte_length < required_depth) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (frame->confidence_bytes == nullptr) {
      if (frame->confidence_row_stride_bytes != 0U || frame->confidence_byte_length != 0U) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    } else {
      if (frame->confidence_row_stride_bytes < frame->depth_width) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
      uint64_t required_confidence = 0;
      if (!multiply_add_fits(frame->confidence_row_stride_bytes, frame->depth_height,
                             frame->depth_width, &required_confidence) ||
          frame->confidence_byte_length < required_confidence) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    }
    HSSStatus status = copy_into_stage(engine, frame->depth_bytes, frame->depth_byte_length);
    if (status == HSS_OK && frame->confidence_bytes != nullptr) {
      status = append_into_stage(engine, frame->confidence_bytes, frame->confidence_byte_length);
    }
    if (status != HSS_OK) {
      return remembered(engine, status);
    }
    return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
  } catch (const std::bad_alloc&) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_mesh_event(HSSEngine* engine, const HSSMeshEvent* event) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(event);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (event->kind > 2U || !hss::coordinate::is_rigid_transform(event->T_world_anchor)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    const bool remove = event->kind == 2U;
    if ((remove && (event->vertices_xyz != nullptr || event->triangle_indices != nullptr ||
                    event->vertex_count != 0U || event->triangle_count != 0U)) ||
        (!remove && ((event->vertex_count != 0U && event->vertices_xyz == nullptr) ||
                     (event->triangle_count != 0U && event->triangle_indices == nullptr)))) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (event->vertex_count > UINT64_MAX / (3U * sizeof(float)) ||
        event->triangle_count > UINT64_MAX / (3U * sizeof(uint32_t))) {
      return remembered(engine, HSS_RESOURCE_LIMIT);
    }
    const uint64_t vertex_bytes = event->vertex_count * 3U * sizeof(float);
    const uint64_t triangle_bytes = event->triangle_count * 3U * sizeof(uint32_t);
    HSSStatus status = copy_into_stage(engine,
                                       reinterpret_cast<const uint8_t*>(event->vertices_xyz), vertex_bytes);
    if (status == HSS_OK) {
      status = append_into_stage(engine, reinterpret_cast<const uint8_t*>(event->triangle_indices), triangle_bytes);
    }
    if (status != HSS_OK) {
      return remembered(engine, status);
    }
    return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
  } catch (const std::bad_alloc&) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_room_proposal_json(HSSEngine* engine, HSSBytes proposal) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_bytes(proposal);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    const HSSStatus status = copy_into_stage(engine, proposal.data, proposal.length);
    return remembered(engine, status == HSS_OK ? HSS_UNSUPPORTED_CAPABILITY : status);
  } catch (const std::bad_alloc&) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_apply_command_json(HSSEngine* engine, HSSBytes command) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_bytes(command);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    const HSSStatus status = copy_into_stage(engine, command.data, command.length);
    return remembered(engine, status == HSS_OK ? HSS_UNSUPPORTED_CAPABILITY : status);
  } catch (const std::bad_alloc&) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_copy_command_result_json(const HSSEngine* engine, uint8_t* destination,
                                                            uint64_t capacity, uint64_t* required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (engine == nullptr || required == nullptr || (destination == nullptr && capacity != 0U)) {
      return HSS_INVALID_ARGUMENT;
    }
    return HSS_NOT_READY;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_step(HSSEngine* engine, uint32_t work_budget_us) {
  try {
    if (engine == nullptr || work_budget_us == 0U) {
      return engine == nullptr ? HSS_INVALID_ARGUMENT : remembered(engine, HSS_INVALID_ARGUMENT);
    }
    return remembered(engine, HSS_OK);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_get_stats(const HSSEngine* engine, HSSEngineStats* stats) {
  if (stats != nullptr) {
    std::memset(stats, 0, sizeof(*stats));
  }
  try {
    if (engine == nullptr || stats == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    stats->abi_version = HSS_ABI_VERSION;
    stats->struct_size = sizeof(*stats);
    stats->accepted_frames = engine->accepted_frames;
    stats->dropped_frames = engine->dropped_frames;
    stats->accepted_commands = engine->accepted_commands;
    stats->working_bytes = engine->staged_input.size();
    stats->pending_frames = 0;
    stats->scene_revision = engine->scene_revision;
    stats->lifecycle_state = 1;
    stats->last_status = static_cast<uint32_t>(engine->last_status);
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_acquire_snapshot(const HSSEngine* engine, HSSSnapshot** out_snapshot) {
  if (out_snapshot != nullptr) {
    *out_snapshot = nullptr;
  }
  try {
    if (engine == nullptr || out_snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    auto* snapshot = new HSSSnapshot();
    snapshot->scene_revision = engine->scene_revision;
    snapshot->scene_json = engine->scene_json;
    *out_snapshot = snapshot;
    return HSS_OK;
  } catch (const std::bad_alloc&) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_snapshot_release(HSSSnapshot* snapshot) {
  try {
    delete snapshot;
  } catch (...) {
  }
}

extern "C" HSSStatus hss_snapshot_get_counts(const HSSSnapshot* snapshot, HSSCounts* counts) {
  if (counts != nullptr) {
    std::memset(counts, 0, sizeof(*counts));
  }
  try {
    if (snapshot == nullptr || counts == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    counts->abi_version = HSS_ABI_VERSION;
    counts->struct_size = sizeof(*counts);
    counts->scene_revision = snapshot->scene_revision;
    counts->vertex_count = snapshot->vertices.size();
    counts->triangle_count = snapshot->triangles.size() / 3U;
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_vertices(const HSSSnapshot* snapshot, HSSVec3d* destination,
                                                   uint64_t capacity, uint64_t* required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->vertices.data(), snapshot->vertices.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_triangles(const HSSSnapshot* snapshot, uint32_t* destination,
                                                    uint64_t capacity, uint64_t* required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->triangles.data(), snapshot->triangles.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot* snapshot, uint8_t* destination,
                                                     uint64_t capacity, uint64_t* required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(reinterpret_cast<const uint8_t*>(snapshot->scene_json.data()), snapshot->scene_json.size(),
                    destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

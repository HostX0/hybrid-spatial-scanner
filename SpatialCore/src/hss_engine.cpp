#include "hss/bounded_ingress.hpp"
#include "hss/coordinate_basis.hpp"
#include "hss/hss_api.h"
#include "hss/live_reconstruction.hpp"
#include "hss/scene_editing.hpp"
#include <chrono>
#include <deque>
#include <memory>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct HSSEngine {
  explicit HSSEngine(const HSSConfig &c)
      : config(c), ingress(c.max_working_bytes, c.max_pending_frames), live(c) {
  }
  HSSConfig config{};
  hss::ingress::BoundedIngress ingress;
  hss::live::Reconstruction live;
  hss::scene_editing::Processor scene_editor;
  std::deque<std::shared_ptr<hss::live::Lease>> queued_leases;
  std::string command_result;
  bool has_command_result{false};
  uint64_t accepted_frames{}, accepted_commands{};
  HSSStatus last_status{HSS_OK};
};
struct HSSSnapshot {
  std::shared_ptr<const hss::live::Snapshot> data;
};

namespace {

constexpr uint64_t kMaxJsonRecordBytes = 16U * 1024U * 1024U;

template <typename T> HSSStatus validate_message(const T *value) {
  if (value == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  if (value->abi_version != HSS_ABI_VERSION) {
    return HSS_UNSUPPORTED_VERSION;
  }
  if (value->struct_size < sizeof(T)) {
    return HSS_INVALID_ARGUMENT;
  }
  const auto *trailing = reinterpret_cast<const uint8_t *>(value) + sizeof(T);
  const size_t trailing_size =
      static_cast<size_t>(value->struct_size - sizeof(T));
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
                       uint64_t *required) {
  if (required == nullptr) {
    return false;
  }
  if (rows == 0U) {
    *required = 0;
    return true;
  }
  const uint64_t preceding_rows = static_cast<uint64_t>(rows - 1U);
  if (preceding_rows != 0U &&
      stride >
          (std::numeric_limits<uint64_t>::max() - row_bytes) / preceding_rows) {
    return false;
  }
  *required = stride * preceding_rows + row_bytes;
  return true;
}

HSSStatus validate_config(const HSSConfig *config) {
  const HSSStatus header_status = validate_message(config);
  if (header_status != HSS_OK) {
    return header_status;
  }
  if (!finite_positive(config->structure_voxel_m) ||
      !finite_positive(config->detail_voxel_m) ||
      !finite_positive(config->truncation_m) ||
      config->max_working_bytes == 0U || config->max_pending_frames == 0U ||
      config->flags != 0U || config->reserved != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

bool add_fits(uint64_t first, uint64_t second, uint64_t *result) {
  if (result == nullptr ||
      second > std::numeric_limits<uint64_t>::max() - first) {
    return false;
  }
  *result = first + second;
  return true;
}

HSSStatus admission_status(hss::ingress::Admission admission) {
  switch (admission) {
  case hss::ingress::Admission::accepted:
  case hss::ingress::Admission::dropped_frame:
    return HSS_OK;
  case hss::ingress::Admission::resource_limit:
    return HSS_RESOURCE_LIMIT;
  case hss::ingress::Admission::closed:
    return HSS_NOT_READY;
  }
  return HSS_INTERNAL_ERROR;
}

std::vector<uint8_t> copy_payloads(const uint8_t *first, uint64_t first_length,
                                   const uint8_t *second,
                                   uint64_t second_length) {
  uint64_t total = 0;
  if (!add_fits(first_length, second_length, &total) ||
      total > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw std::bad_alloc();
  }
  std::vector<uint8_t> result;
  result.reserve(static_cast<size_t>(total));
  if (first_length != 0U) {
    result.insert(result.end(), first,
                  first + static_cast<size_t>(first_length));
  }
  if (second_length != 0U) {
    result.insert(result.end(), second,
                  second + static_cast<size_t>(second_length));
  }
  return result;
}

hss::ingress::Admission check_admission(HSSEngine *engine,
                                        hss::ingress::EventKind kind,
                                        uint64_t bytes) {
  auto a = engine->ingress.admission(kind, bytes);
  if (a != hss::ingress::Admission::accepted)
    return a;
  auto b = engine->live.budget();
  if (bytes > UINT64_MAX - 2048 || bytes + 2048 > b->limit ||
      b->used > b->limit - (bytes + 2048))
    return hss::ingress::Admission::resource_limit;
  return a;
}
HSSStatus enqueue_owned_event(HSSEngine *engine, hss::ingress::Event &&event) {
  auto lease = hss::live::reserve(engine->live.budget(),
                                  hss::live::event_reservation(event));
  if (!lease)
    return HSS_RESOURCE_LIMIT;
  engine->queued_leases.push_back(std::move(lease));
  try {
    auto result = engine->ingress.enqueue(std::move(event));
    if (result != hss::ingress::Admission::accepted)
      engine->queued_leases.pop_back();
    else
      engine->live.seal_configuration();
    return admission_status(result);
  } catch (...) {
    engine->queued_leases.pop_back();
    throw;
  }
}

template <typename T>
HSSStatus copy_out(const T *source, uint64_t count, T *destination,
                   uint64_t capacity, uint64_t *required) {
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

HSSStatus validate_json_bytes(HSSBytes bytes) {
  const HSSStatus status = validate_bytes(bytes);
  if (status != HSS_OK) {
    return status;
  }
  return bytes.length > kMaxJsonRecordBytes ? HSS_RESOURCE_LIMIT : HSS_OK;
}

HSSStatus remembered(HSSEngine *engine, HSSStatus status) {
  engine->last_status = status;
  return status;
}

} // namespace

extern "C" HSSStatus hss_engine_create(const HSSConfig *config,
                                       HSSEngine **out_engine) {
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
    auto engine = std::make_unique<HSSEngine>(*config);
    *out_engine = engine.release();
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_engine_destroy(HSSEngine *engine) {
  try {
    delete engine;
  } catch (...) {
    // C linkage must not expose a C++ exception. There is no status channel.
  }
}

extern "C" HSSStatus hss_engine_set_epoch(HSSEngine *engine,
                                          const HSSEpoch *epoch) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus status = validate_message(epoch);
    if (status != HSS_OK) {
      return remembered(engine, status);
    }
    if (epoch->relationship_state > 1U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!hss::coordinate::is_rigid_transform(epoch->T_project_world)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    hss::ingress::Event event{};
    event.kind = hss::ingress::EventKind::epoch;
    event.epoch = *epoch;
    event.capture_id = epoch->capture_id;
    event.event_sequence = epoch->event_sequence;
    event.session_epoch = epoch->session_epoch;
    const hss::ingress::Admission admission =
        check_admission(engine, event.kind, 0U);
    if (admission != hss::ingress::Admission::accepted) {
      return remembered(engine, admission_status(admission));
    }
    return remembered(engine, enqueue_owned_event(engine, std::move(event)));
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_frame(HSSEngine *engine,
                                             const HSSFrame *frame) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(frame);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (frame->tracking_state > 2U || frame->depth_source > 1U ||
        frame->reserved != 0U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!std::isfinite(frame->timestamp_s) ||
        !hss::coordinate::is_rigid_transform(frame->T_world_camera)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    if (!finite_positive(frame->depth_intrinsics.fx) ||
        !finite_positive(frame->depth_intrinsics.fy) ||
        !std::isfinite(frame->depth_intrinsics.cx) ||
        !std::isfinite(frame->depth_intrinsics.cy)) {
      return remembered(engine, HSS_INVALID_CALIBRATION);
    }
    if (frame->depth_width == 0U || frame->depth_height == 0U ||
        frame->depth_row_stride_bytes <
            static_cast<uint64_t>(frame->depth_width) * 4U ||
        frame->depth_bytes == nullptr) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    uint64_t required_depth = 0;
    if (!multiply_add_fits(frame->depth_row_stride_bytes, frame->depth_height,
                           static_cast<uint64_t>(frame->depth_width) * 4U,
                           &required_depth) ||
        frame->depth_byte_length < required_depth) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (frame->confidence_bytes == nullptr) {
      if (frame->confidence_row_stride_bytes != 0U ||
          frame->confidence_byte_length != 0U) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    } else {
      if (frame->confidence_row_stride_bytes < frame->depth_width) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
      uint64_t required_confidence = 0;
      if (!multiply_add_fits(frame->confidence_row_stride_bytes,
                             frame->depth_height, frame->depth_width,
                             &required_confidence) ||
          frame->confidence_byte_length < required_confidence) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    }
    uint64_t payload_bytes = required_depth;
    if (frame->confidence_bytes != nullptr) {
      uint64_t required_confidence = 0;
      if (!multiply_add_fits(frame->confidence_row_stride_bytes,
                             frame->depth_height, frame->depth_width,
                             &required_confidence) ||
          !add_fits(payload_bytes, required_confidence, &payload_bytes)) {
        return remembered(engine, HSS_RESOURCE_LIMIT);
      }
    }
    const hss::ingress::Admission admission =
        check_admission(engine, hss::ingress::EventKind::frame, payload_bytes);
    if (admission == hss::ingress::Admission::dropped_frame) {
      engine->ingress.record_dropped_frame();
      return remembered(engine, HSS_OK);
    }
    if (admission != hss::ingress::Admission::accepted) {
      return remembered(engine, admission_status(admission));
    }
    hss::ingress::Event event{};
    event.kind = hss::ingress::EventKind::frame;
    event.frame = *frame;
    event.frame.depth_bytes = nullptr;
    event.frame.depth_byte_length = required_depth;
    event.frame.confidence_bytes = nullptr;
    event.frame.confidence_byte_length = payload_bytes - required_depth;
    event.capture_id = frame->capture_id;
    event.event_sequence = frame->event_sequence;
    event.session_epoch = frame->session_epoch;
    event.payload_bytes = payload_bytes;
    event.payload =
        copy_payloads(frame->depth_bytes, required_depth,
                      frame->confidence_bytes, payload_bytes - required_depth);
    const HSSStatus enqueue_status =
        enqueue_owned_event(engine, std::move(event));
    if (enqueue_status == HSS_OK) {
      ++engine->accepted_frames;
    }
    return remembered(engine, enqueue_status);
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_mesh_event(HSSEngine *engine,
                                                  const HSSMeshEvent *event) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(event);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (event->kind > 2U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!hss::coordinate::is_rigid_transform(event->T_world_anchor)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    const bool remove = event->kind == 2U;
    if ((remove &&
         (event->vertices_xyz != nullptr ||
          event->triangle_indices != nullptr || event->vertex_count != 0U ||
          event->triangle_count != 0U)) ||
        (!remove &&
         ((event->vertex_count != 0U && event->vertices_xyz == nullptr) ||
          (event->triangle_count != 0U &&
           event->triangle_indices == nullptr)))) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (event->vertex_count > UINT64_MAX / (3U * sizeof(float)) ||
        event->triangle_count > UINT64_MAX / (3U * sizeof(uint32_t))) {
      return remembered(engine, HSS_RESOURCE_LIMIT);
    }
    if (event->vertex_count > 65536 || event->triangle_count > 65536)
      return remembered(engine, HSS_RESOURCE_LIMIT);
    for (uint64_t i = 0; i < event->vertex_count * 3; ++i)
      if (!std::isfinite(event->vertices_xyz[i]) ||
          std::abs(event->vertices_xyz[i]) > 10000)
        return remembered(engine, HSS_INVALID_ARGUMENT);
    for (uint64_t i = 0; i < event->triangle_count * 3; ++i)
      if (event->triangle_indices[i] >= event->vertex_count)
        return remembered(engine, HSS_INVALID_ARGUMENT);
    const uint64_t vertex_bytes = event->vertex_count * 3U * sizeof(float);
    const uint64_t triangle_bytes =
        event->triangle_count * 3U * sizeof(uint32_t);
    uint64_t payload_bytes = 0;
    if (!add_fits(vertex_bytes, triangle_bytes, &payload_bytes)) {
      return remembered(engine, HSS_RESOURCE_LIMIT);
    }
    const hss::ingress::EventKind kind =
        event->kind == 0U
            ? hss::ingress::EventKind::mesh_add
            : (event->kind == 1U ? hss::ingress::EventKind::mesh_replace
                                 : hss::ingress::EventKind::mesh_remove);
    const hss::ingress::Admission admission =
        check_admission(engine, kind, payload_bytes);
    if (admission != hss::ingress::Admission::accepted) {
      // Mesh removals/replacements are structural control events.  They must
      // surface retry/backpressure instead of becoming a best-effort drop.
      return remembered(engine, admission_status(admission));
    }
    hss::ingress::Event queued{};
    queued.kind = kind;
    queued.mesh = *event;
    queued.mesh.vertices_xyz = nullptr;
    queued.mesh.triangle_indices = nullptr;
    queued.capture_id = event->capture_id;
    queued.anchor_id = event->anchor_id;
    queued.event_sequence = event->event_sequence;
    queued.anchor_revision = event->anchor_revision;
    queued.session_epoch = event->session_epoch;
    queued.payload_bytes = payload_bytes;
    queued.payload = copy_payloads(
        reinterpret_cast<const uint8_t *>(event->vertices_xyz), vertex_bytes,
        reinterpret_cast<const uint8_t *>(event->triangle_indices),
        triangle_bytes);
    return remembered(engine, enqueue_owned_event(engine, std::move(queued)));
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_room_proposal_json(HSSEngine *engine,
                                                          HSSBytes proposal) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_json_bytes(proposal);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_apply_command_json(HSSEngine *engine,
                                                   HSSBytes command) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_json_bytes(command);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    if (engine->live.busy() || engine->ingress.pending_events() != 0U)
      return remembered(engine, HSS_NOT_READY);
    const std::string exact(reinterpret_cast<const char *>(command.data),
                            static_cast<size_t>(command.length));
    const auto processed = engine->scene_editor.process(
        engine->live.snapshot()->json, exact);
    if (processed.exact_result_json.size() > kMaxJsonRecordBytes)
      return remembered(engine, HSS_RESOURCE_LIMIT);
    engine->command_result = processed.exact_result_json;
    engine->has_command_result = true;
    if (processed.scene_changed) {
      const auto counts =
          hss::scene_editing::scene_counts(processed.candidate_scene_json);
      if (!counts)
        return remembered(engine, HSS_INTERNAL_ERROR);
      const HSSStatus published = engine->live.publish_edited_scene(
          processed.candidate_scene_json,
          engine->live.snapshot()->revision + 1U, counts->node_count,
          counts->edge_count, counts->issue_count);
      if (published != HSS_OK)
        return remembered(engine, published);
      ++engine->accepted_commands;
    }
    switch (processed.status) {
    case hss::scene_editing::Status::kApplied:
    case hss::scene_editing::Status::kStoredRetry:
      return remembered(engine, HSS_OK);
    case hss::scene_editing::Status::kConflict:
      return remembered(engine, HSS_CONSTRAINT_CONFLICT);
    case hss::scene_editing::Status::kDuplicateMismatch:
      return remembered(engine, HSS_DUPLICATE_MISMATCH);
    case hss::scene_editing::Status::kStaleRevision:
    case hss::scene_editing::Status::kFutureRevision:
    case hss::scene_editing::Status::kTargetNotCurrent:
    case hss::scene_editing::Status::kTargetLineageChanged:
      return remembered(engine, HSS_STALE_REVISION);
    case hss::scene_editing::Status::kPendingUnsupported:
      return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
    case hss::scene_editing::Status::kResourceLimitExceeded:
      return remembered(engine, HSS_RESOURCE_LIMIT);
    default:
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus
hss_engine_copy_command_result_json(const HSSEngine *engine,
                                    uint8_t *destination, uint64_t capacity,
                                    uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (engine == nullptr || required == nullptr ||
        (destination == nullptr && capacity != 0U)) {
      return HSS_INVALID_ARGUMENT;
    }
    if (!engine->has_command_result)
      return HSS_NOT_READY;
    return copy_out(reinterpret_cast<const uint8_t *>(engine->command_result.data()),
                    engine->command_result.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_step(HSSEngine *engine,
                                     uint32_t work_budget_us) {
  try {
    if (engine == nullptr || work_budget_us == 0U) {
      return engine == nullptr ? HSS_INVALID_ARGUMENT
                               : remembered(engine, HSS_INVALID_ARGUMENT);
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(work_budget_us);
    // At least one bounded quantum makes forward progress even for a 1 us
    // request. A quantum is one vertex, triangle, <=96-point fit, <=128-edge
    // contour validation, bounded metadata setup or one JSON record.
    do {
      if (engine->live.busy()) {
        engine->live.advance();
        if (engine->live.status() != HSS_OK)
          return remembered(engine, engine->live.status());
      } else {
        // Allocate before popping: allocation failure must not desynchronize
        // the event queue and its parallel reservation leases.
        auto owned = std::make_shared<hss::live::Evidence>();
        hss::ingress::Event event{};
        if (!engine->ingress.pop(&event))
          break;
        owned->lease = std::move(engine->queued_leases.front());
        engine->queued_leases.pop_front();
        owned->event = std::move(event);
        auto status = engine->live.begin(std::move(owned));
        if (status != HSS_OK)
          return remembered(engine, status);
      }
    } while (std::chrono::steady_clock::now() < deadline);
    return remembered(engine, HSS_OK);
  } catch (const std::bad_alloc &) {
    engine->live.cancel(HSS_RESOURCE_LIMIT);
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    engine->live.cancel(HSS_INTERNAL_ERROR);
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_get_stats(const HSSEngine *engine,
                                          HSSEngineStats *stats) {
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
    stats->dropped_frames = engine->ingress.dropped_frames();
    stats->accepted_commands = engine->accepted_commands;
    stats->working_bytes = engine->live.budget()->used;
    stats->pending_frames = engine->ingress.pending_frames();
    stats->scene_revision = engine->live.snapshot()->revision;
    stats->lifecycle_state =
        engine->live.busy() || engine->ingress.pending_events() != 0 ? 2 : 1;
    stats->last_status = static_cast<uint32_t>(engine->last_status);
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_acquire_snapshot(const HSSEngine *engine,
                                                 HSSSnapshot **out_snapshot) {
  if (out_snapshot != nullptr) {
    *out_snapshot = nullptr;
  }
  try {
    if (engine == nullptr || out_snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    auto *snapshot = new HSSSnapshot();
    snapshot->data = engine->live.snapshot();
    *out_snapshot = snapshot;
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_snapshot_release(HSSSnapshot *snapshot) {
  try {
    delete snapshot;
  } catch (...) {
  }
}

extern "C" HSSStatus hss_snapshot_get_counts(const HSSSnapshot *snapshot,
                                             HSSCounts *counts) {
  if (counts != nullptr) {
    std::memset(counts, 0, sizeof(*counts));
  }
  try {
    if (snapshot == nullptr || counts == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    counts->abi_version = HSS_ABI_VERSION;
    counts->struct_size = sizeof(*counts);
    counts->scene_revision = snapshot->data->revision;
    counts->vertex_count = snapshot->data->vertices.size();
    counts->triangle_count = snapshot->data->triangles.size() / 3U;
    counts->boundary_node_count = snapshot->data->node_count;
    counts->boundary_edge_count = snapshot->data->edge_count;
    counts->issue_count = snapshot->data->issue_count;
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_vertices(const HSSSnapshot *snapshot,
                                                HSSVec3d *destination,
                                                uint64_t capacity,
                                                uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->data->vertices.data(),
                    snapshot->data->vertices.size(), destination, capacity,
                    required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_triangles(const HSSSnapshot *snapshot,
                                                 uint32_t *destination,
                                                 uint64_t capacity,
                                                 uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->data->triangles.data(),
                    snapshot->data->triangles.size(), destination, capacity,
                    required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot *snapshot,
                                                  uint8_t *destination,
                                                  uint64_t capacity,
                                                  uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(
        reinterpret_cast<const uint8_t *>(snapshot->data->json.data()),
        snapshot->data->json.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_set_source_is_synthetic(HSSEngine *e,
                                                        uint32_t value) {
  if (!e)
    return HSS_INVALID_ARGUMENT;
  try {
    return remembered(e, e->live.set_synthetic(value));
  } catch (...) {
    return remembered(e, HSS_RESOURCE_LIMIT);
  }
}
extern "C" HSSStatus
hss_engine_set_plan_reference(HSSEngine *e, const HSSMat4d *t, double cut) {
  if (!e || !t)
    return HSS_INVALID_ARGUMENT;
  if (e->ingress.pending_events() != 0 || e->live.busy())
    return remembered(e, HSS_NOT_READY);
  try {
    return remembered(e, e->live.set_plan_reference(*t, cut));
  } catch (...) {
    e->live.cancel(HSS_RESOURCE_LIMIT);
    return remembered(e, HSS_RESOURCE_LIMIT);
  }
}

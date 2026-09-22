#include "hss/hss_project_store.h"

#include "hss/durable_source_store.hpp"
#include "hss/normalized_replay.hpp"
#include "hss/project_event_encoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

extern "C" HSSStatus hss_engine_set_source_is_synthetic(HSSEngine *, uint32_t)
    __attribute__((weak));
extern "C" HSSStatus hss_engine_set_plan_reference(HSSEngine *,
                                                   const HSSMat4d *, double)
    __attribute__((weak));

namespace {

constexpr uint64_t kMaximumPathBytes = 4096U;
constexpr uint64_t kMaximumJsonBytes = 16U * 1024U * 1024U;
constexpr uint64_t kMaximumRecordBytes = 256U * 1024U * 1024U;
constexpr uint64_t kMaximumProjectBytes = 8ULL * 1024U * 1024U * 1024U;

template <typename T>
HSSStatus validate_message(const T *value, uint32_t abi_version) {
  if (value == nullptr || value->abi_version != abi_version ||
      value->struct_size < sizeof(T)) {
    return value != nullptr && value->abi_version != abi_version
               ? HSS_UNSUPPORTED_VERSION
               : HSS_INVALID_ARGUMENT;
  }
  const auto *bytes = reinterpret_cast<const uint8_t *>(value);
  for (uint32_t index = sizeof(T); index < value->struct_size; ++index) {
    if (bytes[index] != 0U)
      return HSS_UNSUPPORTED_VERSION;
  }
  return HSS_OK;
}

bool zero_id(const HSSId &id) {
  return std::all_of(std::begin(id.bytes), std::end(id.bytes),
                     [](uint8_t value) { return value == 0U; });
}

hss::replay::Id replay_id(const HSSId &id) {
  hss::replay::Id result{};
  std::copy(std::begin(id.bytes), std::end(id.bytes), result.begin());
  return result;
}

HSSId c_id(const hss::replay::Id &id) {
  HSSId result{};
  std::copy(id.begin(), id.end(), std::begin(result.bytes));
  return result;
}

std::string hex_id(const HSSId &id) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string value;
  value.reserve(32U);
  for (uint8_t byte : id.bytes) {
    value.push_back(kHex[byte >> 4U]);
    value.push_back(kHex[byte & 0x0fU]);
  }
  return value;
}

HSSStatus store_status(hss::storage::StoreStatus status) {
  using S = hss::storage::StoreStatus;
  switch (status) {
  case S::kOk:
    return HSS_OK;
  case S::kInvalidArgument:
    return HSS_INVALID_ARGUMENT;
  case S::kResourceLimit:
    return HSS_RESOURCE_LIMIT;
  case S::kIoError:
    return HSS_PROJECT_STORE_IO_ERROR;
  case S::kCorrupt:
    return HSS_PROJECT_STORE_CORRUPT;
  case S::kAlreadyExists:
    return HSS_DUPLICATE_MISMATCH;
  case S::kNotFound:
    return HSS_NOT_READY;
  case S::kProjectMismatch:
    return HSS_PROJECT_STORE_PROJECT_MISMATCH;
  }
  return HSS_INTERNAL_ERROR;
}

HSSStatus replay_status(hss::replay::Status status) {
  using S = hss::replay::Status;
  switch (status) {
  case S::kOk:
    return HSS_OK;
  case S::kInvalidArgument:
    return HSS_INVALID_ARGUMENT;
  case S::kUnsupportedVersion:
    return HSS_UNSUPPORTED_VERSION;
  case S::kResourceLimit:
    return HSS_RESOURCE_LIMIT;
  case S::kCorrupt:
    return HSS_PROJECT_STORE_CORRUPT;
  case S::kUnsupportedType:
  case S::kIncompatibleProvenance:
    return HSS_UNSUPPORTED_CAPABILITY;
  case S::kOutOfOrder:
    return HSS_STALE_REVISION;
  case S::kSinkRejected:
    return HSS_INTERNAL_ERROR;
  }
  return HSS_INTERNAL_ERROR;
}

bool add_fits(uint64_t left, uint64_t right, uint64_t *result) {
  if (right > std::numeric_limits<uint64_t>::max() - left)
    return false;
  *result = left + right;
  return true;
}

struct MeshState {
  uint64_t generation{0};
  uint64_t revision{0};
  bool live{false};
};

struct StreamState {
  bool have_event{false};
  hss::replay::Id capture_id{};
  uint64_t last_sequence{0};
  double last_time{0.0};
  uint64_t last_scene_revision{0};
  std::map<uint32_t, uint64_t> epoch_revisions;
  std::map<hss::replay::Id, MeshState> meshes;
};

const hss::replay::EventMetadata &metadata(const hss::replay::Event &event) {
  return std::visit(
      [](const auto &value) -> const hss::replay::EventMetadata & {
        return value.metadata;
      },
      event);
}

HSSStatus accept_common(StreamState *state, const hss::replay::Id &capture_id,
                        uint64_t sequence, double time,
                        uint64_t scene_revision) {
  if (state == nullptr || sequence == 0U || scene_revision == 0U ||
      !std::isfinite(time) || time < 0.0 ||
      (state->have_event &&
       (capture_id != state->capture_id || sequence <= state->last_sequence ||
        time < state->last_time ||
        scene_revision != state->last_scene_revision + 1U)) ||
      (!state->have_event && scene_revision != 1U)) {
    return HSS_STALE_REVISION;
  }
  if (!state->have_event)
    state->capture_id = capture_id;
  state->have_event = true;
  state->last_sequence = sequence;
  state->last_time = time;
  state->last_scene_revision = scene_revision;
  return HSS_OK;
}

HSSStatus accept_event(StreamState *state, const hss::replay::Event &event,
                       uint64_t scene_revision) {
  StreamState next = *state;
  const auto &common = metadata(event);
  HSSStatus status =
      accept_common(&next, common.capture_id, common.event_sequence,
                    common.monotonic_time_s, scene_revision);
  if (status != HSS_OK)
    return status;
  if (const auto *epoch = std::get_if<hss::replay::EpochEvent>(&event)) {
    const auto found = next.epoch_revisions.find(common.session_epoch);
    if (found != next.epoch_revisions.end() &&
        epoch->transform_revision <= found->second)
      return HSS_STALE_REVISION;
    next.epoch_revisions[common.session_epoch] = epoch->transform_revision;
  } else {
    if (!next.epoch_revisions.contains(common.session_epoch))
      return HSS_STALE_REVISION;
    if (const auto *mesh = std::get_if<hss::replay::MeshAnchorEvent>(&event)) {
      auto found = next.meshes.find(mesh->anchor_id);
      if (mesh->kind == hss::replay::MeshEventKind::kAdd) {
        const uint64_t generation =
            found == next.meshes.end() ? 1U : found->second.generation + 1U;
        if ((found != next.meshes.end() && found->second.live) ||
            mesh->generation != generation || mesh->anchor_revision != 1U)
          return HSS_STALE_REVISION;
        next.meshes[mesh->anchor_id] = {generation, 1U, true};
      } else if (mesh->kind == hss::replay::MeshEventKind::kReplace) {
        if (found == next.meshes.end() || !found->second.live ||
            mesh->generation != found->second.generation ||
            mesh->anchor_revision != found->second.revision + 1U)
          return HSS_STALE_REVISION;
        found->second.revision = mesh->anchor_revision;
      } else {
        if (found == next.meshes.end() || !found->second.live ||
            mesh->generation != found->second.generation ||
            mesh->anchor_revision != found->second.revision)
          return HSS_STALE_REVISION;
        found->second.live = false;
      }
    }
  }
  *state = std::move(next);
  return HSS_OK;
}

hss::project::Context control_context(const HSSProjectEventContext &value) {
  hss::project::Context result;
  result.capture_id = value.capture_id;
  result.event_sequence = value.event_sequence;
  result.accepted_scene_revision = value.accepted_scene_revision;
  result.session_epoch = value.session_epoch;
  result.monotonic_time_s = value.monotonic_time_s;
  return result;
}

HSSStatus accept_control(StreamState *state,
                         const hss::project::Control &control) {
  const auto &context = std::visit(
      [](const auto &value) -> const hss::project::Context & {
        return value.context;
      },
      control);
  StreamState next = *state;
  if (!next.have_event || replay_id(context.capture_id) != next.capture_id ||
      context.event_sequence <= next.last_sequence ||
      context.monotonic_time_s < next.last_time ||
      !next.epoch_revisions.contains(context.session_epoch))
    return HSS_STALE_REVISION;
  const uint64_t before = std::visit(
      [](const auto &value) { return value.before_scene_revision; }, control);
  const uint64_t after = std::visit(
      [](const auto &value) { return value.after_scene_revision; }, control);
  const auto* command = std::get_if<hss::project::CommandControl>(&control);
  const bool mutates = command == nullptr ||
      command->disposition == hss::project::CommandControl::Disposition::kApplied;
  if (before != next.last_scene_revision || after != context.accepted_scene_revision ||
      (mutates && after != before + 1U) || (!mutates && after != before)) {
    return HSS_STALE_REVISION;
  }
  next.last_sequence = context.event_sequence;
  next.last_time = context.monotonic_time_s;
  next.last_scene_revision = after;
  *state = std::move(next);
  return HSS_OK;
}

} // namespace

struct HSSProjectStore {
  HSSId project_id{};
  bool source_is_synthetic{false};
  hss::replay::Limits replay_limits{};
  uint64_t max_record_bytes{0};
  uint64_t max_pending_bytes{0};
  uint64_t pending_bytes{0};
  uint64_t durable_revision{0};
  StreamState state;
  std::vector<hss::storage::SourceRecord> pending;
  std::unique_ptr<hss::storage::NativeFileSystem> file_system;
  std::unique_ptr<hss::storage::DurableSourceStore> durable;
};

namespace {
HSSStatus scan_committed(HSSProjectStore *, hss::storage::RecoveryResult *,
                         StreamState *);
HSSStatus append_encoded(HSSProjectStore *, hss::storage::SourceRecord,
                         const hss::replay::Event *,
                         const hss::project::Control *,
                         HSSProjectStoreReceipt *);
} // namespace

extern "C" HSSStatus
hss_project_store_create(const HSSProjectStoreConfig *config,
                         HSSProjectStore **out_store) {
  if (out_store != nullptr)
    *out_store = nullptr;
  try {
    HSSStatus status = validate_message(config, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK || out_store == nullptr ||
        zero_id(config->project_id) || config->flags != 0U ||
        config->reserved != 0U || config->source_is_synthetic > 1U ||
        config->open_mode > HSS_PROJECT_STORE_OPEN_OR_CREATE ||
        config->trusted_root_directory_utf8.length == 0U ||
        config->trusted_root_directory_utf8.length > kMaximumPathBytes ||
        !hss::project::valid_utf8(config->trusted_root_directory_utf8.data,
                                  config->trusted_root_directory_utf8.length) ||
        config->max_epoch_records == 0U || config->max_depth_records == 0U ||
        config->max_mesh_records == 0U || config->max_command_records == 0U ||
        config->max_retained_bytes == 0U ||
        config->max_retained_bytes > kMaximumProjectBytes ||
        config->max_record_bytes == 0U ||
        config->max_record_bytes > kMaximumRecordBytes ||
        config->max_record_bytes > config->max_retained_bytes ||
        config->max_pending_bytes == 0U ||
        config->max_pending_bytes > config->max_retained_bytes) {
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    }
    const std::string root(
        reinterpret_cast<const char *>(
            config->trusted_root_directory_utf8.data),
        static_cast<size_t>(config->trusted_root_directory_utf8.length));
    const std::filesystem::path root_path(root);
    if (!root_path.is_absolute())
      return HSS_INVALID_ARGUMENT;
    auto store = std::make_unique<HSSProjectStore>();
    store->project_id = config->project_id;
    store->source_is_synthetic = config->source_is_synthetic == 1U;
    store->max_record_bytes = config->max_record_bytes;
    store->max_pending_bytes = config->max_pending_bytes;
    store->replay_limits.max_record_bytes = config->max_record_bytes;
    store->replay_limits.max_stream_bytes = config->max_pending_bytes;
    store->replay_limits.max_records =
        config->max_epoch_records + config->max_depth_records +
        config->max_mesh_records + config->max_command_records;
    const std::filesystem::path project_path =
        root_path / hex_id(config->project_id);
    store->file_system =
        std::make_unique<hss::storage::NativeFileSystem>(project_path.string());
    hss::storage::RetentionLimits limits;
    limits.max_epoch_records = config->max_epoch_records;
    limits.max_depth_records = config->max_depth_records;
    limits.max_mesh_records = config->max_mesh_records;
    limits.max_command_records = config->max_command_records;
    limits.max_retained_bytes = config->max_retained_bytes;
    limits.max_chunk_bytes = config->max_record_bytes;
    store->durable = std::make_unique<hss::storage::DurableSourceStore>(
        *store->file_system, limits);
    const auto mode =
        static_cast<hss::storage::ProjectOpenMode>(config->open_mode);
    status = store_status(store->durable->open_project(
        {.project_id = replay_id(config->project_id),
         .source_is_synthetic = store->source_is_synthetic},
        mode));
    if (status != HSS_OK)
      return status;
    hss::storage::RecoveryResult recovered;
    status = scan_committed(store.get(), &recovered, &store->state);
    if (status != HSS_OK)
      return status;
    store->durable_revision = recovered.revision;
    *out_store = store.release();
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_project_store_destroy(HSSProjectStore *store) {
  try {
    delete store;
  } catch (...) {
  }
}

extern "C" HSSStatus hss_project_store_append_epoch(
    HSSProjectStore *store, const HSSProjectEventContext *context,
    const HSSEpoch *epoch, HSSProjectStoreReceipt *receipt) {
  try {
    HSSStatus status = validate_message(context, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK)
      return status;
    status = validate_message(epoch, HSS_ABI_VERSION);
    if (status != HSS_OK || store == nullptr || receipt == nullptr ||
        context->reserved != 0U ||
        std::memcmp(context->capture_id.bytes, epoch->capture_id.bytes, 16U) !=
            0 ||
        context->event_sequence != epoch->event_sequence ||
        context->session_epoch != epoch->session_epoch) {
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    }
    hss::replay::EpochEvent event;
    event.metadata.capture_id = replay_id(epoch->capture_id);
    event.metadata.event_sequence = epoch->event_sequence;
    event.metadata.session_epoch = epoch->session_epoch;
    event.metadata.monotonic_time_s = context->monotonic_time_s;
    event.relationship_state =
        static_cast<hss::replay::RelationshipState>(epoch->relationship_state);
    event.transform_revision = epoch->transform_revision;
    std::copy(std::begin(epoch->T_project_world.column_major),
              std::end(epoch->T_project_world.column_major),
              event.project_from_world.begin());
    std::vector<uint8_t> bytes;
    const auto encoded =
        hss::replay::encode_record(event, store->replay_limits, &bytes);
    if (encoded != hss::replay::Status::kOk)
      return replay_status(encoded);
    hss::storage::SourceRecord record;
    record.kind = hss::storage::SourceKind::kEpoch;
    record.capture_id = event.metadata.capture_id;
    record.session_epoch = event.metadata.session_epoch;
    record.event_sequence = event.metadata.event_sequence;
    record.monotonic_time_s = event.metadata.monotonic_time_s;
    record.source_revision = context->accepted_scene_revision;
    record.payload = std::move(bytes);
    const hss::replay::Event normalized = event;
    return append_encoded(store, std::move(record), &normalized, nullptr,
                          receipt);
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_project_store_append_frame(
    HSSProjectStore *store, const HSSProjectEventContext *context,
    const HSSFrame *frame, HSSProjectStoreReceipt *receipt) {
  try {
    HSSStatus status = validate_message(context, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK)
      return status;
    status = validate_message(frame, HSS_ABI_VERSION);
    if (status != HSS_OK || store == nullptr || receipt == nullptr ||
        context->reserved != 0U ||
        std::memcmp(context->capture_id.bytes, frame->capture_id.bytes, 16U) !=
            0 ||
        context->event_sequence != frame->event_sequence ||
        context->session_epoch != frame->session_epoch ||
        context->monotonic_time_s != frame->timestamp_s ||
        frame->tracking_state != 0U || frame->depth_source != 0U ||
        frame->reserved != 0U || frame->depth_width == 0U ||
        frame->depth_height == 0U || frame->depth_bytes == nullptr ||
        frame->depth_row_stride_bytes <
            static_cast<uint64_t>(frame->depth_width) * 4U ||
        frame->depth_height > std::numeric_limits<uint64_t>::max() /
                                  frame->depth_row_stride_bytes) {
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    }
    const uint64_t required_depth =
        frame->depth_row_stride_bytes * (frame->depth_height - 1U) +
        static_cast<uint64_t>(frame->depth_width) * 4U;
    const bool supplied_confidence = frame->confidence_bytes != nullptr;
    uint64_t required_confidence = 0U;
    if (supplied_confidence) {
      if (frame->confidence_row_stride_bytes < frame->depth_width ||
          frame->depth_height > std::numeric_limits<uint64_t>::max() /
                                    frame->confidence_row_stride_bytes) {
        return HSS_INVALID_ARGUMENT;
      }
      required_confidence =
          frame->confidence_row_stride_bytes * (frame->depth_height - 1U) +
          frame->depth_width;
    } else if (frame->confidence_row_stride_bytes != 0U ||
               frame->confidence_byte_length != 0U) {
      return HSS_INVALID_ARGUMENT;
    }
    if (frame->depth_byte_length < required_depth ||
        frame->confidence_byte_length < required_confidence) {
      return HSS_INVALID_ARGUMENT;
    }
    const uint64_t samples64 =
        static_cast<uint64_t>(frame->depth_width) * frame->depth_height;
    if (samples64 > store->replay_limits.max_depth_samples ||
        samples64 > std::numeric_limits<size_t>::max()) {
      return HSS_RESOURCE_LIMIT;
    }
    hss::replay::DepthFrameEvent event;
    event.metadata.capture_id = replay_id(frame->capture_id);
    event.metadata.session_epoch = frame->session_epoch;
    event.metadata.event_sequence = frame->event_sequence;
    event.metadata.monotonic_time_s = frame->timestamp_s;
    event.rgb_layout = {
        frame->depth_width,
        frame->depth_height,
        0x4e4f4e45U,
        {{frame->depth_width, frame->depth_height, frame->depth_width}}};
    event.depth_layout = {frame->depth_width,
                          frame->depth_height,
                          0x46444550U,
                          {{frame->depth_width, frame->depth_height,
                            frame->depth_row_stride_bytes}}};
    event.confidence_was_supplied = supplied_confidence;
    if (supplied_confidence) {
      event.confidence_layout = {frame->depth_width,
                                 frame->depth_height,
                                 0x434f4e46U,
                                 {{frame->depth_width, frame->depth_height,
                                   frame->confidence_row_stride_bytes}}};
    }
    event.calibration = {frame->depth_width,
                         frame->depth_height,
                         frame->depth_intrinsics.fx,
                         frame->depth_intrinsics.fy,
                         frame->depth_intrinsics.cx,
                         frame->depth_intrinsics.cy,
                         0.0,
                         0.0,
                         static_cast<double>(frame->depth_width),
                         static_cast<double>(frame->depth_height)};
    std::copy(std::begin(frame->T_world_camera.column_major),
              std::end(frame->T_world_camera.column_major),
              event.world_from_camera.begin());
    const size_t samples = static_cast<size_t>(samples64);
    event.depth_meters.resize(samples);
    event.confidence.resize(samples);
    event.valid_depth_mask.resize(samples);
    event.metric_fusion_mask.resize(samples);
    event.provisional_preview_mask.resize(samples);
    for (uint32_t y = 0; y < frame->depth_height; ++y) {
      const uint8_t *depth_row =
          frame->depth_bytes + y * frame->depth_row_stride_bytes;
      const uint8_t *confidence_row =
          supplied_confidence
              ? frame->confidence_bytes + y * frame->confidence_row_stride_bytes
              : nullptr;
      for (uint32_t x = 0; x < frame->depth_width; ++x) {
        const size_t index = static_cast<size_t>(y) * frame->depth_width + x;
        const uint32_t bits =
            static_cast<uint32_t>(depth_row[x * 4U]) |
            (static_cast<uint32_t>(depth_row[x * 4U + 1U]) << 8U) |
            (static_cast<uint32_t>(depth_row[x * 4U + 2U]) << 16U) |
            (static_cast<uint32_t>(depth_row[x * 4U + 3U]) << 24U);
        const float depth = std::bit_cast<float>(bits);
        const bool valid = std::isfinite(depth) && depth > 0.0F;
        const uint8_t confidence = supplied_confidence
                                       ? confidence_row[x]
                                       : hss::replay::kConfidenceUnavailable;
        if (confidence != 0U && confidence != 1U && confidence != 2U &&
            confidence != hss::replay::kConfidenceUnavailable)
          return HSS_INVALID_ARGUMENT;
        event.depth_meters[index] = valid ? depth : 0.0F;
        event.confidence[index] = confidence;
        event.valid_depth_mask[index] = valid ? 1U : 0U;
        event.metric_fusion_mask[index] = valid && confidence != 0U ? 1U : 0U;
        event.provisional_preview_mask[index] =
            valid && confidence == 0U ? 1U : 0U;
      }
    }
    std::vector<uint8_t> bytes;
    const auto encoded =
        hss::replay::encode_record(event, store->replay_limits, &bytes);
    if (encoded != hss::replay::Status::kOk)
      return replay_status(encoded);
    hss::storage::SourceRecord record;
    record.kind = hss::storage::SourceKind::kDepth;
    record.capture_id = event.metadata.capture_id;
    record.session_epoch = event.metadata.session_epoch;
    record.event_sequence = event.metadata.event_sequence;
    record.monotonic_time_s = event.metadata.monotonic_time_s;
    record.source_revision = context->accepted_scene_revision;
    record.payload = std::move(bytes);
    const hss::replay::Event normalized = std::move(event);
    return append_encoded(store, std::move(record), &normalized, nullptr,
                          receipt);
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_project_store_append_mesh_event(
    HSSProjectStore *store, const HSSProjectEventContext *context,
    const HSSMeshEvent *mesh, HSSProjectStoreReceipt *receipt) {
  try {
    HSSStatus status = validate_message(context, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK)
      return status;
    status = validate_message(mesh, HSS_ABI_VERSION);
    if (status != HSS_OK || store == nullptr || receipt == nullptr ||
        context->reserved != 0U ||
        std::memcmp(context->capture_id.bytes, mesh->capture_id.bytes, 16U) !=
            0 ||
        context->event_sequence != mesh->event_sequence ||
        context->session_epoch != mesh->session_epoch || mesh->kind > 2U ||
        zero_id(mesh->anchor_id))
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    const bool remove = mesh->kind == 2U;
    if ((remove &&
         (mesh->vertices_xyz != nullptr || mesh->triangle_indices != nullptr ||
          mesh->vertex_count != 0U || mesh->triangle_count != 0U)) ||
        (!remove &&
         (mesh->vertices_xyz == nullptr || mesh->triangle_indices == nullptr ||
          mesh->vertex_count == 0U || mesh->triangle_count == 0U)) ||
        mesh->vertex_count > store->replay_limits.max_mesh_vertices ||
        mesh->triangle_count > store->replay_limits.max_mesh_triangles ||
        mesh->vertex_count > std::numeric_limits<size_t>::max() / 3U ||
        mesh->triangle_count > std::numeric_limits<size_t>::max() / 3U)
      return HSS_INVALID_ARGUMENT;
    hss::replay::MeshAnchorEvent event;
    event.metadata.capture_id = replay_id(mesh->capture_id);
    event.metadata.session_epoch = mesh->session_epoch;
    event.metadata.event_sequence = mesh->event_sequence;
    event.metadata.monotonic_time_s = context->monotonic_time_s;
    event.kind = static_cast<hss::replay::MeshEventKind>(mesh->kind);
    event.anchor_id = replay_id(mesh->anchor_id);
    event.anchor_revision = mesh->anchor_revision;
    const auto found = store->state.meshes.find(event.anchor_id);
    event.generation = mesh->kind == 0U ? (found == store->state.meshes.end()
                                               ? 1U
                                               : found->second.generation + 1U)
                                        : (found == store->state.meshes.end()
                                               ? 0U
                                               : found->second.generation);
    std::copy(std::begin(mesh->T_world_anchor.column_major),
              std::end(mesh->T_world_anchor.column_major),
              event.world_from_anchor.begin());
    if (!remove) {
      event.payload.vertices_xyz.assign(
          mesh->vertices_xyz, mesh->vertices_xyz + mesh->vertex_count * 3U);
      event.payload.triangle_indices.assign(mesh->triangle_indices,
                                            mesh->triangle_indices +
                                                mesh->triangle_count * 3U);
      event.payload.local_bounds_min = {std::numeric_limits<float>::infinity(),
                                        std::numeric_limits<float>::infinity(),
                                        std::numeric_limits<float>::infinity()};
      event.payload.local_bounds_max = {
          -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()};
      for (size_t index = 0; index < event.payload.vertices_xyz.size();
           index += 3U) {
        for (size_t axis = 0; axis < 3U; ++axis) {
          event.payload.local_bounds_min[axis] =
              std::min(event.payload.local_bounds_min[axis],
                       event.payload.vertices_xyz[index + axis]);
          event.payload.local_bounds_max[axis] =
              std::max(event.payload.local_bounds_max[axis],
                       event.payload.vertices_xyz[index + axis]);
        }
      }
    }
    std::vector<uint8_t> bytes;
    const auto encoded =
        hss::replay::encode_record(event, store->replay_limits, &bytes);
    if (encoded != hss::replay::Status::kOk)
      return replay_status(encoded);
    hss::storage::SourceRecord record;
    record.kind = hss::storage::SourceKind::kMesh;
    record.capture_id = event.metadata.capture_id;
    record.anchor_id = event.anchor_id;
    record.session_epoch = event.metadata.session_epoch;
    record.event_sequence = event.metadata.event_sequence;
    record.monotonic_time_s = event.metadata.monotonic_time_s;
    record.source_revision = context->accepted_scene_revision;
    record.payload = std::move(bytes);
    const hss::replay::Event normalized = std::move(event);
    return append_encoded(store, std::move(record), &normalized, nullptr,
                          receipt);
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus
hss_project_store_append_command_record(HSSProjectStore *store,
                                        const HSSProjectCommandRecord *input,
                                        HSSProjectStoreReceipt *receipt) {
  try {
    HSSStatus status = validate_message(input, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK)
      return status;
    status = validate_message(&input->context, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK || store == nullptr || receipt == nullptr ||
        input->context.reserved != 0U ||
        input->context.accepted_scene_revision != input->after_scene_revision ||
        input->disposition > HSS_PROJECT_COMMAND_PENDING_EXCLUSION ||
        (input->disposition == HSS_PROJECT_COMMAND_APPLIED &&
         input->after_scene_revision != input->before_scene_revision + 1U) ||
        (input->disposition != HSS_PROJECT_COMMAND_APPLIED &&
         input->after_scene_revision != input->before_scene_revision) ||
        input->command_json.length == 0U ||
        input->command_json.length > kMaximumJsonBytes ||
        input->result_json.length > kMaximumJsonBytes ||
        !hss::project::valid_utf8(input->command_json.data,
                                  input->command_json.length) ||
        !hss::project::valid_utf8(input->result_json.data,
                                  input->result_json.length)) {
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    }
    hss::project::CommandControl command;
    command.context = control_context(input->context);
    command.before_scene_revision = input->before_scene_revision;
    command.after_scene_revision = input->after_scene_revision;
    command.disposition = static_cast<hss::project::CommandControl::Disposition>(input->disposition);
    command.command_json.assign(input->command_json.data,
                                input->command_json.data +
                                    input->command_json.length);
    command.result_json.assign(input->result_json.data,
                               input->result_json.data +
                                   input->result_json.length);
    hss::project::Control control = std::move(command);
    std::vector<uint8_t> bytes;
    const auto encoded =
        hss::project::encode_control(control, store->max_record_bytes, &bytes);
    if (encoded == hss::project::ControlStatus::kResourceLimit)
      return HSS_RESOURCE_LIMIT;
    if (encoded != hss::project::ControlStatus::kOk)
      return HSS_INVALID_ARGUMENT;
    const auto &context =
        std::get<hss::project::CommandControl>(control).context;
    hss::storage::SourceRecord record;
    record.kind = hss::storage::SourceKind::kCommand;
    record.provenance = hss::storage::SourceProvenance::kAcceptedControl;
    record.capture_id = replay_id(context.capture_id);
    record.session_epoch = context.session_epoch;
    record.event_sequence = context.event_sequence;
    record.monotonic_time_s = context.monotonic_time_s;
    record.source_revision = context.accepted_scene_revision;
    record.payload = std::move(bytes);
    return append_encoded(store, std::move(record), nullptr, &control, receipt);
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_project_store_append_plan_reference(
    HSSProjectStore *store, const HSSProjectEventContext *context,
    const HSSProjectPlanReference *input, HSSProjectStoreReceipt *receipt) {
  try {
    HSSStatus status = validate_message(context, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK)
      return status;
    status = validate_message(input, HSS_PROJECT_STORE_ABI_VERSION);
    if (status != HSS_OK || store == nullptr || receipt == nullptr ||
        context->reserved != 0U ||
        context->accepted_scene_revision != input->after_scene_revision ||
        input->after_scene_revision != input->before_scene_revision + 1U) {
      return status == HSS_OK ? HSS_INVALID_ARGUMENT : status;
    }
    hss::project::PlanReferenceControl plan;
    plan.context = control_context(*context);
    plan.project_from_floor = input->T_project_floor;
    plan.cut_height_m = input->cut_height_m;
    plan.before_scene_revision = input->before_scene_revision;
    plan.after_scene_revision = input->after_scene_revision;
    hss::project::Control control = plan;
    std::vector<uint8_t> bytes;
    const auto encoded =
        hss::project::encode_control(control, store->max_record_bytes, &bytes);
    if (encoded == hss::project::ControlStatus::kResourceLimit)
      return HSS_RESOURCE_LIMIT;
    if (encoded != hss::project::ControlStatus::kOk)
      return HSS_INVALID_ARGUMENT;
    hss::storage::SourceRecord record;
    record.kind = hss::storage::SourceKind::kPlanReference;
    record.provenance = hss::storage::SourceProvenance::kAcceptedControl;
    record.capture_id = replay_id(plan.context.capture_id);
    record.session_epoch = plan.context.session_epoch;
    record.event_sequence = plan.context.event_sequence;
    record.monotonic_time_s = plan.context.monotonic_time_s;
    record.source_revision = plan.context.accepted_scene_revision;
    record.payload = std::move(bytes);
    return append_encoded(store, std::move(record), nullptr, &control, receipt);
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus
hss_project_store_checkpoint(HSSProjectStore *store,
                             HSSProjectCheckpointResult *result) {
  if (result != nullptr)
    std::memset(result, 0, sizeof(*result));
  try {
    if (store == nullptr || result == nullptr)
      return HSS_INVALID_ARGUMENT;
    if (store->pending.empty())
      return HSS_NOT_READY;
    HSSStatus status = store_status(store->durable->checkpoint(store->pending));
    if (status != HSS_OK)
      return status;
    const auto recovered = store->durable->recover();
    status = store_status(recovered.status);
    if (status != HSS_OK)
      return status;
    uint64_t durable_bytes = 0U;
    for (const auto &record : recovered.records) {
      if (!add_fits(durable_bytes, record.payload_length, &durable_bytes))
        return HSS_RESOURCE_LIMIT;
    }
    store->durable_revision = recovered.revision;
    store->pending.clear();
    store->pending_bytes = 0U;
    result->abi_version = HSS_PROJECT_STORE_ABI_VERSION;
    result->struct_size = sizeof(*result);
    result->durable_revision = recovered.revision;
    result->durable_event_count = recovered.records.size();
    result->durable_bytes = durable_bytes;
    result->durability_state = 1U;
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

namespace {

HSSStatus engine_stats(HSSEngine *engine, HSSEngineStats *stats) {
  std::memset(stats, 0, sizeof(*stats));
  stats->abi_version = HSS_ABI_VERSION;
  stats->struct_size = sizeof(*stats);
  return hss_engine_get_stats(engine, stats);
}

HSSStatus drain_engine(HSSEngine *engine, uint64_t expected_revision) {
  for (uint32_t iteration = 0; iteration < 1'000'000U; ++iteration) {
    HSSStatus status = hss_engine_step(engine, 1U);
    if (status != HSS_OK)
      return status;
    HSSEngineStats stats{};
    status = engine_stats(engine, &stats);
    if (status != HSS_OK)
      return status;
    if (stats.lifecycle_state == 1U) {
      return stats.scene_revision == expected_revision ? HSS_OK
                                                       : HSS_STALE_REVISION;
    }
  }
  return HSS_RESOURCE_LIMIT;
}

HSSStatus apply_source_event(HSSEngine *engine, const hss::replay::Event &event,
                             uint64_t expected_revision) {
  HSSStatus status = HSS_INTERNAL_ERROR;
  if (const auto *epoch = std::get_if<hss::replay::EpochEvent>(&event)) {
    HSSEpoch value{};
    value.abi_version = HSS_ABI_VERSION;
    value.struct_size = sizeof(value);
    value.capture_id = c_id(epoch->metadata.capture_id);
    value.event_sequence = epoch->metadata.event_sequence;
    value.session_epoch = epoch->metadata.session_epoch;
    value.relationship_state = static_cast<uint32_t>(epoch->relationship_state);
    std::copy(epoch->project_from_world.begin(),
              epoch->project_from_world.end(),
              std::begin(value.T_project_world.column_major));
    value.transform_revision = epoch->transform_revision;
    status = hss_engine_set_epoch(engine, &value);
  } else if (const auto *frame =
                 std::get_if<hss::replay::DepthFrameEvent>(&event)) {
    std::vector<uint8_t> depth(frame->depth_meters.size() * sizeof(float));
    for (size_t index = 0; index < frame->depth_meters.size(); ++index) {
      const uint32_t bits = std::bit_cast<uint32_t>(frame->depth_meters[index]);
      for (size_t byte = 0; byte < 4U; ++byte)
        depth[index * 4U + byte] = static_cast<uint8_t>(bits >> (byte * 8U));
    }
    HSSFrame value{};
    value.abi_version = HSS_ABI_VERSION;
    value.struct_size = sizeof(value);
    value.capture_id = c_id(frame->metadata.capture_id);
    value.event_sequence = frame->metadata.event_sequence;
    value.session_epoch = frame->metadata.session_epoch;
    value.tracking_state = static_cast<uint32_t>(frame->tracking_state);
    value.timestamp_s = frame->metadata.monotonic_time_s;
    std::copy(frame->world_from_camera.begin(), frame->world_from_camera.end(),
              std::begin(value.T_world_camera.column_major));
    value.depth_intrinsics = {frame->calibration.fx, frame->calibration.fy,
                              frame->calibration.cx, frame->calibration.cy};
    value.depth_width = frame->depth_layout.width;
    value.depth_height = frame->depth_layout.height;
    value.depth_row_stride_bytes =
        static_cast<uint64_t>(value.depth_width) * 4U;
    value.depth_byte_length = depth.size();
    value.depth_bytes = depth.data();
    std::vector<uint8_t> confidence;
    if (frame->confidence_was_supplied) {
      confidence = frame->confidence;
      value.confidence_row_stride_bytes = value.depth_width;
      value.confidence_byte_length = confidence.size();
      value.confidence_bytes = confidence.data();
    }
    value.depth_source = static_cast<uint32_t>(frame->depth_source);
    status = hss_engine_submit_frame(engine, &value);
  } else {
    const auto &mesh = std::get<hss::replay::MeshAnchorEvent>(event);
    HSSMeshEvent value{};
    value.abi_version = HSS_ABI_VERSION;
    value.struct_size = sizeof(value);
    value.capture_id = c_id(mesh.metadata.capture_id);
    value.anchor_id = c_id(mesh.anchor_id);
    value.event_sequence = mesh.metadata.event_sequence;
    value.anchor_revision = mesh.anchor_revision;
    value.session_epoch = mesh.metadata.session_epoch;
    value.kind = static_cast<uint32_t>(mesh.kind);
    std::copy(mesh.world_from_anchor.begin(), mesh.world_from_anchor.end(),
              std::begin(value.T_world_anchor.column_major));
    value.vertices_xyz = mesh.payload.vertices_xyz.empty()
                             ? nullptr
                             : mesh.payload.vertices_xyz.data();
    value.vertex_count = mesh.payload.vertices_xyz.size() / 3U;
    value.triangle_indices = mesh.payload.triangle_indices.empty()
                                 ? nullptr
                                 : mesh.payload.triangle_indices.data();
    value.triangle_count = mesh.payload.triangle_indices.size() / 3U;
    status = hss_engine_submit_mesh_event(engine, &value);
  }
  return status == HSS_OK ? drain_engine(engine, expected_revision) : status;
}

HSSStatus copy_command_result(HSSEngine *engine, std::vector<uint8_t> *bytes) {
  uint64_t required = 0U;
  HSSStatus status =
      hss_engine_copy_command_result_json(engine, nullptr, 0U, &required);
  if (status == HSS_OK && required == 0U) {
    bytes->clear();
    return HSS_OK;
  }
  if (status != HSS_BUFFER_TOO_SMALL || required > kMaximumJsonBytes ||
      required > std::numeric_limits<size_t>::max()) {
    return status == HSS_BUFFER_TOO_SMALL ? HSS_RESOURCE_LIMIT : status;
  }
  bytes->resize(static_cast<size_t>(required));
  uint64_t second_required = 0U;
  status = hss_engine_copy_command_result_json(engine, bytes->data(),
                                               bytes->size(), &second_required);
  return status == HSS_OK && second_required == required
             ? HSS_OK
             : (status == HSS_OK ? HSS_INTERNAL_ERROR : status);
}

HSSStatus copy_scene_json(HSSEngine *engine, std::vector<uint8_t> *bytes) {
  HSSSnapshot *snapshot = nullptr;
  HSSStatus status = hss_engine_acquire_snapshot(engine, &snapshot);
  if (status != HSS_OK || snapshot == nullptr)
    return status == HSS_OK ? HSS_INTERNAL_ERROR : status;
  uint64_t required = 0U;
  status = hss_snapshot_copy_scene_json(snapshot, nullptr, 0U, &required);
  if (status == HSS_OK && required == 0U) {
    bytes->clear();
    hss_snapshot_release(snapshot);
    return HSS_OK;
  }
  if (status != HSS_BUFFER_TOO_SMALL || required > kMaximumJsonBytes ||
      required > std::numeric_limits<size_t>::max()) {
    hss_snapshot_release(snapshot);
    return status == HSS_BUFFER_TOO_SMALL ? HSS_RESOURCE_LIMIT : status;
  }
  bytes->resize(static_cast<size_t>(required));
  uint64_t second_required = 0U;
  status = hss_snapshot_copy_scene_json(snapshot, bytes->data(), bytes->size(), &second_required);
  hss_snapshot_release(snapshot);
  return status == HSS_OK && second_required == required
             ? HSS_OK
             : (status == HSS_OK ? HSS_INTERNAL_ERROR : status);
}

HSSStatus apply_control(HSSEngine *engine,
                        const hss::project::Control &control) {
  const uint64_t before = std::visit(
      [](const auto &value) { return value.before_scene_revision; }, control);
  HSSEngineStats stats{};
  HSSStatus status = engine_stats(engine, &stats);
  if (status != HSS_OK || stats.lifecycle_state != 1U ||
      stats.scene_revision != before) {
    return status == HSS_OK ? HSS_STALE_REVISION : status;
  }
  if (const auto *command =
          std::get_if<hss::project::CommandControl>(&control)) {
    std::vector<uint8_t> scene_before;
    const bool nonmutating = command->disposition != hss::project::CommandControl::Disposition::kApplied;
    if (nonmutating && (status = copy_scene_json(engine, &scene_before)) != HSS_OK)
      return status;
    status = hss_engine_apply_command_json(
        engine, {command->command_json.data(), command->command_json.size()});
    if (status != HSS_OK) {
      // Conflict, rejection, and a pending typed exclusion are durable audit
      // controls. The engine returns their normal C status while retaining the
      // exact result bytes and the existing revision; replay must verify those
      // bytes instead of treating the recorded non-mutation as a crash.
      const bool replayable_nonmutation =
          nonmutating &&
          (status == HSS_CONSTRAINT_CONFLICT || status == HSS_INVALID_ARGUMENT ||
           status == HSS_STALE_REVISION || status == HSS_UNSUPPORTED_CAPABILITY ||
           status == HSS_DUPLICATE_MISMATCH);
      if (!replayable_nonmutation)
        return status;
    }
    status = drain_engine(engine, command->after_scene_revision);
    if (status != HSS_OK)
      return status;
    std::vector<uint8_t> actual;
    status = copy_command_result(engine, &actual);
    if (status != HSS_OK || actual != command->result_json)
      return status == HSS_OK ? HSS_DUPLICATE_MISMATCH : status;
    if (!nonmutating)
      return HSS_OK;
    std::vector<uint8_t> scene_after;
    status = copy_scene_json(engine, &scene_after);
    return status == HSS_OK && scene_after == scene_before
               ? HSS_OK
               : (status == HSS_OK ? HSS_STALE_REVISION : status);
  }
  const auto &plan = std::get<hss::project::PlanReferenceControl>(control);
  if (hss_engine_set_plan_reference == nullptr)
    return HSS_UNSUPPORTED_CAPABILITY;
  status = hss_engine_set_plan_reference(engine, &plan.project_from_floor,
                                         plan.cut_height_m);
  return status == HSS_OK ? drain_engine(engine, plan.after_scene_revision)
                          : status;
}

} // namespace

extern "C" HSSStatus hss_project_store_replay(HSSProjectStore *store,
                                              HSSEngine *engine,
                                              HSSProjectReplayResult *result) {
  if (result != nullptr)
    std::memset(result, 0, sizeof(*result));
  try {
    if (store == nullptr || engine == nullptr || result == nullptr)
      return HSS_INVALID_ARGUMENT;
    hss::storage::RecoveryResult recovered;
    StreamState validated;
    HSSStatus status = scan_committed(store, &recovered, &validated);
    if (status != HSS_OK)
      return status;
    result->abi_version = HSS_PROJECT_STORE_ABI_VERSION;
    result->struct_size = sizeof(*result);
    result->durable_revision = recovered.revision;
    result->durable_event_count = recovered.records.size();
    result->used_previous_manifest = recovered.used_previous_manifest ? 1U : 0U;
    if (recovered.records.empty())
      return HSS_OK;
    if (hss_engine_set_source_is_synthetic != nullptr) {
      status = hss_engine_set_source_is_synthetic(
          engine, store->source_is_synthetic ? 1U : 0U);
      if (status != HSS_OK) {
        result->engine_status = status;
        return status;
      }
    } else if (store->source_is_synthetic) {
      result->engine_status = HSS_UNSUPPORTED_CAPABILITY;
      return HSS_UNSUPPORTED_CAPABILITY;
    }
    for (const auto &record : recovered.records) {
      result->failed_event_sequence = record.source.event_sequence;
      std::vector<uint8_t> payload;
      status = store_status(store->durable->read_payload(record, &payload));
      if (status != HSS_OK)
        return status;
      if (record.source.kind == hss::storage::SourceKind::kCommand ||
          record.source.kind == hss::storage::SourceKind::kPlanReference) {
        auto decoded =
            hss::project::decode_control(payload, store->max_record_bytes);
        if (decoded.status != hss::project::ControlStatus::kOk)
          return HSS_PROJECT_STORE_CORRUPT;
        status = apply_control(engine, decoded.control);
      } else {
        auto decoded =
            hss::replay::decode_record(payload, store->replay_limits);
        if (decoded.status != hss::replay::Status::kOk)
          return replay_status(decoded.status);
        status = apply_source_event(engine, decoded.event,
                                    record.source.source_revision);
      }
      if (status != HSS_OK) {
        result->engine_status = status;
        return status;
      }
      ++result->applied_event_count;
    }
    result->failed_event_sequence = 0U;
    result->engine_status = HSS_OK;
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

namespace {

HSSStatus validate_stored_record(HSSProjectStore *store,
                                 const hss::storage::StoredRecord &stored,
                                 StreamState *state) {
  std::vector<uint8_t> payload;
  HSSStatus status =
      store_status(store->durable->read_payload(stored, &payload));
  if (status != HSS_OK)
    return status;
  if (stored.source.kind == hss::storage::SourceKind::kCommand ||
      stored.source.kind == hss::storage::SourceKind::kPlanReference) {
    auto decoded =
        hss::project::decode_control(payload, store->max_record_bytes);
    if (decoded.status != hss::project::ControlStatus::kOk)
      return HSS_PROJECT_STORE_CORRUPT;
    const bool command =
        std::holds_alternative<hss::project::CommandControl>(decoded.control);
    if (command != (stored.source.kind == hss::storage::SourceKind::kCommand))
      return HSS_PROJECT_STORE_CORRUPT;
    const auto &common = std::visit(
        [](const auto &value) -> const hss::project::Context & {
          return value.context;
        },
        decoded.control);
    if (replay_id(common.capture_id) != stored.source.capture_id ||
        common.session_epoch != stored.source.session_epoch ||
        common.event_sequence != stored.source.event_sequence ||
        common.monotonic_time_s != stored.source.monotonic_time_s ||
        common.accepted_scene_revision != stored.source.source_revision) {
      return HSS_PROJECT_STORE_CORRUPT;
    }
    status = accept_control(state, decoded.control);
  } else {
    auto decoded = hss::replay::decode_record(payload, store->replay_limits);
    if (decoded.status != hss::replay::Status::kOk)
      return replay_status(decoded.status);
    const bool correct_kind =
        (stored.source.kind == hss::storage::SourceKind::kEpoch &&
         std::holds_alternative<hss::replay::EpochEvent>(decoded.event)) ||
        (stored.source.kind == hss::storage::SourceKind::kDepth &&
         std::holds_alternative<hss::replay::DepthFrameEvent>(decoded.event)) ||
        (stored.source.kind == hss::storage::SourceKind::kMesh &&
         std::holds_alternative<hss::replay::MeshAnchorEvent>(decoded.event));
    if (!correct_kind)
      return HSS_PROJECT_STORE_CORRUPT;
    const auto &common = metadata(decoded.event);
    if (common.capture_id != stored.source.capture_id ||
        common.session_epoch != stored.source.session_epoch ||
        common.event_sequence != stored.source.event_sequence ||
        common.monotonic_time_s != stored.source.monotonic_time_s)
      return HSS_PROJECT_STORE_CORRUPT;
    if (const auto *mesh =
            std::get_if<hss::replay::MeshAnchorEvent>(&decoded.event)) {
      if (mesh->anchor_id != stored.source.anchor_id)
        return HSS_PROJECT_STORE_CORRUPT;
    }
    status = accept_event(state, decoded.event, stored.source.source_revision);
  }
  if (status != HSS_OK ||
      state->last_sequence != stored.source.event_sequence ||
      state->last_scene_revision != stored.source.source_revision)
    return status == HSS_OK ? HSS_PROJECT_STORE_CORRUPT : status;
  return HSS_OK;
}

HSSStatus scan_committed(HSSProjectStore *store,
                         hss::storage::RecoveryResult *recovered,
                         StreamState *state) {
  *recovered = store->durable->recover();
  HSSStatus status = store_status(recovered->status);
  if (status != HSS_OK)
    return status;
  StreamState next;
  for (const auto &record : recovered->records) {
    status = validate_stored_record(store, record, &next);
    if (status != HSS_OK)
      return status;
  }
  *state = std::move(next);
  return HSS_OK;
}

void fill_receipt(const HSSProjectStore &store, uint64_t sequence,
                  HSSProjectStoreReceipt *receipt) {
  std::memset(receipt, 0, sizeof(*receipt));
  receipt->abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  receipt->struct_size = sizeof(*receipt);
  receipt->event_sequence = sequence;
  receipt->pending_event_count = store.pending.size();
  receipt->pending_bytes = store.pending_bytes;
  receipt->durable_revision = store.durable_revision;
}

HSSStatus append_encoded(HSSProjectStore *store,
                         hss::storage::SourceRecord record,
                         const hss::replay::Event *event,
                         const hss::project::Control *control,
                         HSSProjectStoreReceipt *receipt) {
  if (store == nullptr || receipt == nullptr ||
      record.payload.size() > store->max_pending_bytes ||
      store->pending_bytes > store->max_pending_bytes - record.payload.size())
    return HSS_RESOURCE_LIMIT;
  StreamState next = store->state;
  HSSStatus status = event != nullptr
                         ? accept_event(&next, *event, record.source_revision)
                         : accept_control(&next, *control);
  if (status != HSS_OK)
    return status;
  store->pending_bytes += record.payload.size();
  store->pending.push_back(std::move(record));
  store->state = std::move(next);
  fill_receipt(*store, store->state.last_sequence, receipt);
  return HSS_OK;
}

} // namespace

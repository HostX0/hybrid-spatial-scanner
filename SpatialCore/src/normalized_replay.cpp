#include "hss/normalized_replay.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>

namespace hss::replay {
namespace {

constexpr std::array<uint8_t, 8> kMagic{'H', 'S', 'S', 'R', 'P', 'L', 'Y', 0};
constexpr uint64_t kHeaderBytes = 32;

bool is_zero(const Id& id) {
  return std::all_of(id.begin(), id.end(), [](uint8_t value) { return value == 0; });
}

bool add_fits(uint64_t left, uint64_t right, uint64_t* result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) return false;
  *result = left + right;
  return true;
}

bool multiply_fits(uint64_t left, uint64_t right, uint64_t* result) {
  if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left) return false;
  *result = left * right;
  return true;
}

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

bool rigid(const Matrix4d& matrix) {
  if (!std::all_of(matrix.begin(), matrix.end(), [](double value) { return finite(value); })) return false;
  constexpr double tolerance = 1e-6;
  if (std::abs(matrix[3]) > tolerance || std::abs(matrix[7]) > tolerance ||
      std::abs(matrix[11]) > tolerance || std::abs(matrix[15] - 1.0) > tolerance) {
    return false;
  }
  const auto dot = [&](size_t a, size_t b) {
    return matrix[a] * matrix[b] + matrix[a + 1] * matrix[b + 1] + matrix[a + 2] * matrix[b + 2];
  };
  if (std::abs(dot(0, 0) - 1.0) > tolerance || std::abs(dot(4, 4) - 1.0) > tolerance ||
      std::abs(dot(8, 8) - 1.0) > tolerance || std::abs(dot(0, 4)) > tolerance ||
      std::abs(dot(0, 8)) > tolerance || std::abs(dot(4, 8)) > tolerance) {
    return false;
  }
  const double cross_x = matrix[1] * matrix[6] - matrix[2] * matrix[5];
  const double cross_y = matrix[2] * matrix[4] - matrix[0] * matrix[6];
  const double cross_z = matrix[0] * matrix[5] - matrix[1] * matrix[4];
  return cross_x * matrix[8] + cross_y * matrix[9] + cross_z * matrix[10] > 1.0 - tolerance;
}

class Writer {
 public:
  explicit Writer(std::vector<uint8_t>* bytes) : bytes_(*bytes) {}

  void u8(uint8_t value) { bytes_.push_back(value); }
  void u16(uint16_t value) { integer(value); }
  void u32(uint32_t value) { integer(value); }
  void u64(uint64_t value) { integer(value); }
  void f32(float value) { u32(std::bit_cast<uint32_t>(value)); }
  void f64(double value) { u64(std::bit_cast<uint64_t>(value)); }
  void raw(const uint8_t* data, size_t count) {
    if (count != 0) bytes_.insert(bytes_.end(), data, data + count);
  }

 private:
  template <typename T>
  void integer(T value) {
    static_assert(std::is_unsigned_v<T>);
    for (size_t index = 0; index < sizeof(T); ++index) {
      bytes_.push_back(static_cast<uint8_t>(value & 0xffU));
      value = static_cast<T>(value >> 8U);
    }
  }
  std::vector<uint8_t>& bytes_;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool u8(uint8_t* value) {
    if (remaining() < 1) return false;
    *value = data_[offset_++];
    return true;
  }
  bool u16(uint16_t* value) { return integer(value); }
  bool u32(uint32_t* value) { return integer(value); }
  bool u64(uint64_t* value) { return integer(value); }
  bool f32(float* value) {
    uint32_t bits = 0;
    if (!u32(&bits)) return false;
    *value = std::bit_cast<float>(bits);
    return true;
  }
  bool f64(double* value) {
    uint64_t bits = 0;
    if (!u64(&bits)) return false;
    *value = std::bit_cast<double>(bits);
    return true;
  }
  bool raw(uint8_t* output, size_t count) {
    if (remaining() < count) return false;
    if (count == 0) return true;
    std::copy_n(data_ + offset_, count, output);
    offset_ += count;
    return true;
  }
  size_t remaining() const { return size_ - offset_; }

 private:
  template <typename T>
  bool integer(T* value) {
    static_assert(std::is_unsigned_v<T>);
    if (remaining() < sizeof(T)) return false;
    T result = 0;
    for (size_t index = 0; index < sizeof(T); ++index) {
      result = static_cast<T>(result | (static_cast<T>(data_[offset_ + index]) << (8U * index)));
    }
    offset_ += sizeof(T);
    *value = result;
    return true;
  }
  const uint8_t* data_;
  size_t size_;
  size_t offset_{0};
};

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(0U - (crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

void write_metadata(Writer& writer, const EventMetadata& metadata) {
  writer.u16(metadata.schema_major);
  writer.u16(metadata.schema_minor);
  writer.u8(static_cast<uint8_t>(metadata.provenance));
  writer.u8(0);
  writer.u16(0);
  writer.raw(metadata.capture_id.data(), metadata.capture_id.size());
  writer.u32(metadata.session_epoch);
  writer.u64(metadata.event_sequence);
  writer.f64(metadata.monotonic_time_s);
}

bool read_metadata(Reader& reader, EventMetadata* metadata, Status* status) {
  uint8_t provenance = 0;
  uint8_t reserved8 = 0;
  uint16_t reserved16 = 0;
  if (!reader.u16(&metadata->schema_major) || !reader.u16(&metadata->schema_minor) ||
      !reader.u8(&provenance) || !reader.u8(&reserved8) || !reader.u16(&reserved16) ||
      !reader.raw(metadata->capture_id.data(), metadata->capture_id.size()) ||
      !reader.u32(&metadata->session_epoch) || !reader.u64(&metadata->event_sequence) ||
      !reader.f64(&metadata->monotonic_time_s)) {
    *status = Status::kCorrupt;
    return false;
  }
  if (reserved8 != 0 || reserved16 != 0) {
    *status = Status::kCorrupt;
    return false;
  }
  if (metadata->schema_major != kNormalizedSchemaMajor || metadata->schema_minor != kNormalizedSchemaMinor) {
    *status = Status::kUnsupportedVersion;
    return false;
  }
  if (provenance != static_cast<uint8_t>(Provenance::kCaptured)) {
    *status = Status::kIncompatibleProvenance;
    return false;
  }
  metadata->provenance = Provenance::kCaptured;
  return true;
}

void write_matrix(Writer& writer, const Matrix4d& matrix) {
  for (double value : matrix) writer.f64(value);
}

bool read_matrix(Reader& reader, Matrix4d* matrix) {
  for (double& value : *matrix) {
    if (!reader.f64(&value)) return false;
  }
  return true;
}

void write_layout(Writer& writer, const PixelLayout& layout) {
  writer.u32(layout.width);
  writer.u32(layout.height);
  writer.u32(layout.pixel_format);
  writer.u32(static_cast<uint32_t>(layout.planes.size()));
  for (const PixelPlaneLayout& plane : layout.planes) {
    writer.u32(plane.width);
    writer.u32(plane.height);
    writer.u64(plane.row_stride_bytes);
  }
}

bool read_layout(Reader& reader, const Limits& limits, PixelLayout* layout, Status* status) {
  uint32_t plane_count = 0;
  if (!reader.u32(&layout->width) || !reader.u32(&layout->height) ||
      !reader.u32(&layout->pixel_format) || !reader.u32(&plane_count)) {
    *status = Status::kCorrupt;
    return false;
  }
  if (plane_count > limits.max_pixel_planes) {
    *status = Status::kResourceLimit;
    return false;
  }
  uint64_t plane_bytes = 0;
  if (!multiply_fits(plane_count, 2U * sizeof(uint32_t) + sizeof(uint64_t), &plane_bytes) ||
      plane_bytes > reader.remaining()) {
    *status = Status::kCorrupt;
    return false;
  }
  layout->planes.resize(plane_count);
  for (PixelPlaneLayout& plane : layout->planes) {
    if (!reader.u32(&plane.width) || !reader.u32(&plane.height) || !reader.u64(&plane.row_stride_bytes)) {
      *status = Status::kCorrupt;
      return false;
    }
  }
  return true;
}

bool valid_metadata(const EventMetadata& metadata) {
  return metadata.schema_major == kNormalizedSchemaMajor &&
         metadata.schema_minor == kNormalizedSchemaMinor &&
         metadata.provenance == Provenance::kCaptured && !is_zero(metadata.capture_id) &&
         finite(metadata.monotonic_time_s) && metadata.monotonic_time_s >= 0.0;
}

bool valid_layout(const PixelLayout& layout, uint64_t bytes_per_pixel) {
  if (layout.width == 0 || layout.height == 0 || layout.pixel_format == 0 || layout.planes.empty()) return false;
  for (const PixelPlaneLayout& plane : layout.planes) {
    uint64_t minimum_stride = 0;
    if (plane.width == 0 || plane.height == 0 || plane.width > layout.width || plane.height > layout.height ||
        !multiply_fits(plane.width, bytes_per_pixel, &minimum_stride) ||
        plane.row_stride_bytes < minimum_stride) {
      return false;
    }
  }
  return true;
}

bool empty_layout(const PixelLayout& layout) {
  return layout.width == 0 && layout.height == 0 && layout.pixel_format == 0 && layout.planes.empty();
}

Status validate_epoch(const EpochEvent& event) {
  if (!valid_metadata(event.metadata) || event.transform_revision == 0 || !rigid(event.project_from_world)) {
    return Status::kInvalidArgument;
  }
  if (event.relationship_state != RelationshipState::kUnresolved &&
      event.relationship_state != RelationshipState::kAccepted) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status validate_depth(const DepthFrameEvent& event, const Limits& limits) {
  if (!valid_metadata(event.metadata) || event.tracking_state != TrackingState::kNormal ||
      event.depth_source != DepthSource::kSceneDepth || !valid_layout(event.rgb_layout, 1) ||
      !valid_layout(event.depth_layout, sizeof(float)) || !rigid(event.world_from_camera) ||
      event.rgb_layout.planes.size() > limits.max_pixel_planes ||
      event.depth_layout.planes.size() > limits.max_pixel_planes ||
      event.confidence_layout.planes.size() > limits.max_pixel_planes) {
    return Status::kInvalidArgument;
  }
  if (event.depth_layout.planes.size() != 1 ||
      event.depth_layout.planes[0].width != event.depth_layout.width ||
      event.depth_layout.planes[0].height != event.depth_layout.height) {
    return Status::kInvalidArgument;
  }
  if (event.confidence_was_supplied) {
    if (!valid_layout(event.confidence_layout, 1) ||
        event.confidence_layout.width != event.depth_layout.width ||
        event.confidence_layout.height != event.depth_layout.height ||
        event.confidence_layout.planes.size() != 1 ||
        event.confidence_layout.planes[0].width != event.confidence_layout.width ||
        event.confidence_layout.planes[0].height != event.confidence_layout.height) {
      return Status::kInvalidArgument;
    }
  } else if (!empty_layout(event.confidence_layout)) {
    return Status::kInvalidArgument;
  }
  const auto& calibration = event.calibration;
  const std::array<double, 8> values{calibration.fx, calibration.fy, calibration.cx, calibration.cy,
                                     calibration.crop_x, calibration.crop_y,
                                     calibration.crop_width, calibration.crop_height};
  if (calibration.image_width == 0 || calibration.image_height == 0 ||
      !std::all_of(values.begin(), values.end(), [](double value) { return finite(value); }) ||
      calibration.fx <= 0.0 || calibration.fy <= 0.0 || calibration.cx < 0.0 || calibration.cy < 0.0 ||
      calibration.cx >= event.depth_layout.width || calibration.cy >= event.depth_layout.height ||
      calibration.crop_x < 0.0 || calibration.crop_y < 0.0 || calibration.crop_width <= 0.0 ||
      calibration.crop_height <= 0.0 ||
      calibration.crop_x + calibration.crop_width > calibration.image_width ||
      calibration.crop_y + calibration.crop_height > calibration.image_height) {
    return Status::kInvalidArgument;
  }
  uint64_t sample_count = 0;
  if (!multiply_fits(event.depth_layout.width, event.depth_layout.height, &sample_count) ||
      sample_count == 0 || sample_count > limits.max_depth_samples) {
    return Status::kResourceLimit;
  }
  if (event.depth_meters.size() != sample_count || event.confidence.size() != sample_count ||
      event.valid_depth_mask.size() != sample_count || event.metric_fusion_mask.size() != sample_count ||
      event.provisional_preview_mask.size() != sample_count) {
    return Status::kInvalidArgument;
  }
  bool any_valid = false;
  for (size_t index = 0; index < event.depth_meters.size(); ++index) {
    const float depth = event.depth_meters[index];
    const uint8_t confidence = event.confidence[index];
    const uint8_t valid = event.valid_depth_mask[index];
    const uint8_t metric = event.metric_fusion_mask[index];
    const uint8_t provisional = event.provisional_preview_mask[index];
    if ((valid > 1) || (metric > 1) || (provisional > 1) ||
        (confidence != 0 && confidence != 1 && confidence != 2 && confidence != kConfidenceUnavailable) ||
        !finite(depth) || (valid == 1 ? depth <= 0.0F : depth != 0.0F) ||
        metric != static_cast<uint8_t>(valid == 1 && confidence != 0) ||
        provisional != static_cast<uint8_t>(valid == 1 && confidence == 0) ||
        (!event.confidence_was_supplied && confidence != kConfidenceUnavailable)) {
      return Status::kInvalidArgument;
    }
    any_valid = any_valid || valid == 1;
  }
  return any_valid ? Status::kOk : Status::kInvalidArgument;
}

Status validate_mesh(const MeshAnchorEvent& event, const Limits& limits) {
  if (!valid_metadata(event.metadata) || is_zero(event.anchor_id) || event.generation == 0 ||
      event.anchor_revision == 0 || !rigid(event.world_from_anchor)) {
    return Status::kInvalidArgument;
  }
  if (event.kind != MeshEventKind::kAdd && event.kind != MeshEventKind::kReplace &&
      event.kind != MeshEventKind::kRemove) {
    return Status::kInvalidArgument;
  }
  const MeshPayload& payload = event.payload;
  if (event.kind == MeshEventKind::kRemove) {
    const bool empty = payload.vertices_xyz.empty() && payload.normals_xyz.empty() &&
                       payload.triangle_indices.empty() && payload.classifications.empty() &&
                       std::all_of(payload.local_bounds_min.begin(), payload.local_bounds_min.end(),
                                   [](float value) { return value == 0.0F; }) &&
                       std::all_of(payload.local_bounds_max.begin(), payload.local_bounds_max.end(),
                                   [](float value) { return value == 0.0F; });
    return empty ? Status::kOk : Status::kInvalidArgument;
  }
  if (payload.vertices_xyz.empty() || payload.vertices_xyz.size() % 3 != 0 ||
      payload.triangle_indices.empty() || payload.triangle_indices.size() % 3 != 0 ||
      (!payload.normals_xyz.empty() && payload.normals_xyz.size() != payload.vertices_xyz.size())) {
    return Status::kInvalidArgument;
  }
  const uint64_t vertex_count = payload.vertices_xyz.size() / 3U;
  const uint64_t triangle_count = payload.triangle_indices.size() / 3U;
  if (vertex_count > limits.max_mesh_vertices || triangle_count > limits.max_mesh_triangles) {
    return Status::kResourceLimit;
  }
  if (!payload.classifications.empty() && payload.classifications.size() != triangle_count) {
    return Status::kInvalidArgument;
  }
  if (!std::all_of(payload.vertices_xyz.begin(), payload.vertices_xyz.end(), [](float value) { return finite(value); }) ||
      !std::all_of(payload.normals_xyz.begin(), payload.normals_xyz.end(), [](float value) { return finite(value); }) ||
      !std::all_of(payload.local_bounds_min.begin(), payload.local_bounds_min.end(), [](float value) { return finite(value); }) ||
      !std::all_of(payload.local_bounds_max.begin(), payload.local_bounds_max.end(), [](float value) { return finite(value); })) {
    return Status::kInvalidArgument;
  }
  for (size_t axis = 0; axis < 3; ++axis) {
    if (payload.local_bounds_min[axis] > payload.local_bounds_max[axis]) return Status::kInvalidArgument;
  }
  if (std::any_of(payload.triangle_indices.begin(), payload.triangle_indices.end(),
                  [vertex_count](uint32_t index) { return index >= vertex_count; })) {
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status validate_event(const Event& event, const Limits& limits) {
  if (const auto* epoch = std::get_if<EpochEvent>(&event)) return validate_epoch(*epoch);
  if (const auto* frame = std::get_if<DepthFrameEvent>(&event)) return validate_depth(*frame, limits);
  return validate_mesh(std::get<MeshAnchorEvent>(event), limits);
}

const EventMetadata& metadata(const Event& event) {
  return std::visit([](const auto& value) -> const EventMetadata& { return value.metadata; }, event);
}

EventType event_type(const Event& event) {
  if (std::holds_alternative<EpochEvent>(event)) return EventType::kEpoch;
  if (std::holds_alternative<DepthFrameEvent>(event)) return EventType::kDepthFrame;
  return EventType::kMeshAnchor;
}

void write_depth_body(Writer& writer, const DepthFrameEvent& event) {
  writer.u8(static_cast<uint8_t>(event.tracking_state));
  writer.u8(static_cast<uint8_t>(event.depth_source));
  writer.u8(event.confidence_was_supplied ? 1U : 0U);
  writer.u8(0);
  write_layout(writer, event.rgb_layout);
  write_layout(writer, event.depth_layout);
  write_layout(writer, event.confidence_layout);
  writer.u32(event.calibration.image_width);
  writer.u32(event.calibration.image_height);
  writer.f64(event.calibration.fx);
  writer.f64(event.calibration.fy);
  writer.f64(event.calibration.cx);
  writer.f64(event.calibration.cy);
  writer.f64(event.calibration.crop_x);
  writer.f64(event.calibration.crop_y);
  writer.f64(event.calibration.crop_width);
  writer.f64(event.calibration.crop_height);
  write_matrix(writer, event.world_from_camera);
  writer.u64(event.depth_meters.size());
  for (float value : event.depth_meters) writer.f32(value);
  writer.raw(event.confidence.data(), event.confidence.size());
  writer.raw(event.valid_depth_mask.data(), event.valid_depth_mask.size());
  writer.raw(event.metric_fusion_mask.data(), event.metric_fusion_mask.size());
  writer.raw(event.provisional_preview_mask.data(), event.provisional_preview_mask.size());
}

void write_mesh_body(Writer& writer, const MeshAnchorEvent& event) {
  writer.u8(static_cast<uint8_t>(event.kind));
  for (int index = 0; index < 7; ++index) writer.u8(0);
  writer.raw(event.anchor_id.data(), event.anchor_id.size());
  writer.u64(event.generation);
  writer.u64(event.anchor_revision);
  write_matrix(writer, event.world_from_anchor);
  writer.u64(event.payload.vertices_xyz.size() / 3U);
  writer.u64(event.payload.normals_xyz.size() / 3U);
  writer.u64(event.payload.triangle_indices.size() / 3U);
  writer.u64(event.payload.classifications.size());
  for (float value : event.payload.local_bounds_min) writer.f32(value);
  for (float value : event.payload.local_bounds_max) writer.f32(value);
  for (float value : event.payload.vertices_xyz) writer.f32(value);
  for (float value : event.payload.normals_xyz) writer.f32(value);
  for (uint32_t value : event.payload.triangle_indices) writer.u32(value);
  writer.raw(event.payload.classifications.data(), event.payload.classifications.size());
}

Status decoded_validation_status(Status status) {
  if (status == Status::kOk || status == Status::kResourceLimit) return status;
  return Status::kCorrupt;
}

bool resize_checked(uint64_t count, uint64_t limit, auto* values, Status* status) {
  if (count > limit || count > values->max_size()) {
    *status = Status::kResourceLimit;
    return false;
  }
  values->resize(static_cast<size_t>(count));
  return true;
}

}  // namespace

Status encode_record(const Event& event, const Limits& limits, std::vector<uint8_t>* bytes) {
  if (bytes == nullptr || limits.max_record_bytes < kHeaderBytes) return Status::kInvalidArgument;
  const Status validation = validate_event(event, limits);
  if (validation != Status::kOk) return validation;

  std::vector<uint8_t> body;
  Writer body_writer(&body);
  write_metadata(body_writer, metadata(event));
  if (const auto* epoch = std::get_if<EpochEvent>(&event)) {
    body_writer.u8(static_cast<uint8_t>(epoch->relationship_state));
    for (int index = 0; index < 7; ++index) body_writer.u8(0);
    body_writer.u64(epoch->transform_revision);
    write_matrix(body_writer, epoch->project_from_world);
  } else if (const auto* frame = std::get_if<DepthFrameEvent>(&event)) {
    write_depth_body(body_writer, *frame);
  } else {
    write_mesh_body(body_writer, std::get<MeshAnchorEvent>(event));
  }
  uint64_t total_size = 0;
  if (!add_fits(kHeaderBytes, body.size(), &total_size) || total_size > limits.max_record_bytes) {
    return Status::kResourceLimit;
  }

  std::vector<uint8_t> encoded;
  encoded.reserve(static_cast<size_t>(total_size));
  Writer writer(&encoded);
  writer.raw(kMagic.data(), kMagic.size());
  writer.u16(kRecordFormatMajor);
  writer.u16(kRecordFormatMinor);
  writer.u16(static_cast<uint16_t>(event_type(event)));
  writer.u16(0);
  writer.u64(body.size());
  writer.u32(crc32(body.data(), body.size()));
  writer.u32(0);
  writer.raw(body.data(), body.size());
  *bytes = std::move(encoded);
  return Status::kOk;
}

DecodeResult decode_record(const std::vector<uint8_t>& bytes, const Limits& limits) {
  DecodeResult result;
  if (bytes.size() < kHeaderBytes) {
    result.status = Status::kCorrupt;
    return result;
  }
  if (bytes.size() > limits.max_record_bytes) {
    result.status = Status::kResourceLimit;
    return result;
  }
  Reader header(bytes.data(), kHeaderBytes);
  std::array<uint8_t, 8> magic{};
  uint16_t major = 0;
  uint16_t minor = 0;
  uint16_t raw_type = 0;
  uint16_t reserved16 = 0;
  uint64_t body_size = 0;
  uint32_t expected_crc = 0;
  uint32_t reserved32 = 0;
  if (!header.raw(magic.data(), magic.size()) || !header.u16(&major) || !header.u16(&minor) ||
      !header.u16(&raw_type) || !header.u16(&reserved16) || !header.u64(&body_size) ||
      !header.u32(&expected_crc) || !header.u32(&reserved32) || magic != kMagic ||
      reserved16 != 0 || reserved32 != 0 || body_size != bytes.size() - kHeaderBytes) {
    result.status = Status::kCorrupt;
    return result;
  }
  if (major != kRecordFormatMajor || minor != kRecordFormatMinor) {
    result.status = Status::kUnsupportedVersion;
    return result;
  }
  if (raw_type < static_cast<uint16_t>(EventType::kEpoch) ||
      raw_type > static_cast<uint16_t>(EventType::kMeshAnchor)) {
    result.status = Status::kUnsupportedType;
    return result;
  }
  const uint8_t* body_data = bytes.data() + kHeaderBytes;
  if (crc32(body_data, static_cast<size_t>(body_size)) != expected_crc) {
    result.status = Status::kCorrupt;
    return result;
  }
  Reader reader(body_data, static_cast<size_t>(body_size));
  Status status = Status::kOk;
  const EventType type = static_cast<EventType>(raw_type);
  if (type == EventType::kEpoch) {
    EpochEvent event;
    uint8_t relationship = 0;
    std::array<uint8_t, 7> reserved{};
    if (!read_metadata(reader, &event.metadata, &status)) {
      result.status = status;
      return result;
    }
    if (!reader.u8(&relationship) || !reader.raw(reserved.data(), reserved.size()) ||
        !std::all_of(reserved.begin(), reserved.end(), [](uint8_t value) { return value == 0; }) ||
        !reader.u64(&event.transform_revision) || !read_matrix(reader, &event.project_from_world)) {
      result.status = Status::kCorrupt;
      return result;
    }
    event.relationship_state = static_cast<RelationshipState>(relationship);
    result.event = std::move(event);
  } else if (type == EventType::kDepthFrame) {
    DepthFrameEvent event;
    uint8_t tracking = 0;
    uint8_t source = 0;
    uint8_t supplied = 0;
    uint8_t reserved = 0;
    uint64_t sample_count = 0;
    if (!read_metadata(reader, &event.metadata, &status)) {
      result.status = status;
      return result;
    }
    if (!reader.u8(&tracking) || !reader.u8(&source) || !reader.u8(&supplied) || !reader.u8(&reserved) ||
        supplied > 1 || reserved != 0 || !read_layout(reader, limits, &event.rgb_layout, &status) ||
        !read_layout(reader, limits, &event.depth_layout, &status) ||
        !read_layout(reader, limits, &event.confidence_layout, &status) ||
        !reader.u32(&event.calibration.image_width) || !reader.u32(&event.calibration.image_height) ||
        !reader.f64(&event.calibration.fx) || !reader.f64(&event.calibration.fy) ||
        !reader.f64(&event.calibration.cx) || !reader.f64(&event.calibration.cy) ||
        !reader.f64(&event.calibration.crop_x) || !reader.f64(&event.calibration.crop_y) ||
        !reader.f64(&event.calibration.crop_width) || !reader.f64(&event.calibration.crop_height) ||
        !read_matrix(reader, &event.world_from_camera) || !reader.u64(&sample_count)) {
      result.status = status == Status::kOk ? Status::kCorrupt : status;
      return result;
    }
    event.tracking_state = static_cast<TrackingState>(tracking);
    event.depth_source = static_cast<DepthSource>(source);
    event.confidence_was_supplied = supplied == 1;
    uint64_t required_sample_bytes = 0;
    if (!multiply_fits(sample_count, sizeof(float) + 4U * sizeof(uint8_t), &required_sample_bytes) ||
        required_sample_bytes > reader.remaining()) {
      result.status = Status::kCorrupt;
      return result;
    }
    if (!resize_checked(sample_count, limits.max_depth_samples, &event.depth_meters, &status) ||
        !resize_checked(sample_count, limits.max_depth_samples, &event.confidence, &status) ||
        !resize_checked(sample_count, limits.max_depth_samples, &event.valid_depth_mask, &status) ||
        !resize_checked(sample_count, limits.max_depth_samples, &event.metric_fusion_mask, &status) ||
        !resize_checked(sample_count, limits.max_depth_samples, &event.provisional_preview_mask, &status)) {
      result.status = status;
      return result;
    }
    for (float& value : event.depth_meters) {
      if (!reader.f32(&value)) {
        result.status = Status::kCorrupt;
        return result;
      }
    }
    if (!reader.raw(event.confidence.data(), event.confidence.size()) ||
        !reader.raw(event.valid_depth_mask.data(), event.valid_depth_mask.size()) ||
        !reader.raw(event.metric_fusion_mask.data(), event.metric_fusion_mask.size()) ||
        !reader.raw(event.provisional_preview_mask.data(), event.provisional_preview_mask.size())) {
      result.status = Status::kCorrupt;
      return result;
    }
    result.event = std::move(event);
  } else {
    MeshAnchorEvent event;
    uint8_t kind = 0;
    std::array<uint8_t, 7> reserved{};
    uint64_t vertex_count = 0;
    uint64_t normal_count = 0;
    uint64_t triangle_count = 0;
    uint64_t classification_count = 0;
    if (!read_metadata(reader, &event.metadata, &status)) {
      result.status = status;
      return result;
    }
    if (!reader.u8(&kind) || !reader.raw(reserved.data(), reserved.size()) ||
        !std::all_of(reserved.begin(), reserved.end(), [](uint8_t value) { return value == 0; }) ||
        !reader.raw(event.anchor_id.data(), event.anchor_id.size()) || !reader.u64(&event.generation) ||
        !reader.u64(&event.anchor_revision) || !read_matrix(reader, &event.world_from_anchor) ||
        !reader.u64(&vertex_count) || !reader.u64(&normal_count) || !reader.u64(&triangle_count) ||
        !reader.u64(&classification_count)) {
      result.status = Status::kCorrupt;
      return result;
    }
    event.kind = static_cast<MeshEventKind>(kind);
    uint64_t vertex_scalars = 0;
    uint64_t normal_scalars = 0;
    uint64_t index_count = 0;
    uint64_t max_vertex_scalars = 0;
    uint64_t max_index_count = 0;
    uint64_t required_payload_bytes = 6U * sizeof(float);
    uint64_t scalar_count = 0;
    uint64_t vector_bytes = 0;
    if (!multiply_fits(vertex_count, 3, &vertex_scalars) || !multiply_fits(normal_count, 3, &normal_scalars) ||
        !multiply_fits(triangle_count, 3, &index_count) ||
        !multiply_fits(limits.max_mesh_vertices, 3, &max_vertex_scalars) ||
        !multiply_fits(limits.max_mesh_triangles, 3, &max_index_count) ||
        !add_fits(vertex_scalars, normal_scalars, &scalar_count) ||
        !add_fits(scalar_count, index_count, &scalar_count) ||
        !multiply_fits(scalar_count, sizeof(uint32_t), &vector_bytes) ||
        !add_fits(required_payload_bytes, vector_bytes, &required_payload_bytes) ||
        !add_fits(required_payload_bytes, classification_count, &required_payload_bytes)) {
      result.status = Status::kResourceLimit;
      return result;
    }
    if (required_payload_bytes > reader.remaining()) {
      result.status = Status::kCorrupt;
      return result;
    }
    if (!resize_checked(vertex_scalars, max_vertex_scalars, &event.payload.vertices_xyz, &status) ||
        !resize_checked(normal_scalars, max_vertex_scalars, &event.payload.normals_xyz, &status) ||
        !resize_checked(index_count, max_index_count, &event.payload.triangle_indices, &status) ||
        !resize_checked(classification_count, limits.max_mesh_triangles, &event.payload.classifications, &status)) {
      result.status = status == Status::kOk ? Status::kResourceLimit : status;
      return result;
    }
    for (float& value : event.payload.local_bounds_min) {
      if (!reader.f32(&value)) { result.status = Status::kCorrupt; return result; }
    }
    for (float& value : event.payload.local_bounds_max) {
      if (!reader.f32(&value)) { result.status = Status::kCorrupt; return result; }
    }
    for (float& value : event.payload.vertices_xyz) {
      if (!reader.f32(&value)) { result.status = Status::kCorrupt; return result; }
    }
    for (float& value : event.payload.normals_xyz) {
      if (!reader.f32(&value)) { result.status = Status::kCorrupt; return result; }
    }
    for (uint32_t& value : event.payload.triangle_indices) {
      if (!reader.u32(&value)) { result.status = Status::kCorrupt; return result; }
    }
    if (!reader.raw(event.payload.classifications.data(), event.payload.classifications.size())) {
      result.status = Status::kCorrupt;
      return result;
    }
    result.event = std::move(event);
  }
  if (reader.remaining() != 0) {
    result.status = Status::kCorrupt;
    return result;
  }
  result.status = decoded_validation_status(validate_event(result.event, limits));
  return result;
}

Status replay_records(const std::vector<std::vector<uint8_t>>& records,
                      const Limits& limits,
                      EventSink& sink) {
  if (records.size() > limits.max_records) return Status::kResourceLimit;
  uint64_t stream_bytes = 0;
  for (const auto& record : records) {
    if (!add_fits(stream_bytes, record.size(), &stream_bytes) || stream_bytes > limits.max_stream_bytes) {
      return Status::kResourceLimit;
    }
  }
  std::vector<Event> events;
  events.reserve(records.size());
  for (const auto& record : records) {
    DecodeResult decoded = decode_record(record, limits);
    if (decoded.status != Status::kOk) return decoded.status;
    events.push_back(std::move(decoded.event));
  }
  if (events.empty()) return Status::kOk;

  const Id capture_id = metadata(events.front()).capture_id;
  uint64_t prior_sequence = 0;
  double prior_time = 0.0;
  bool have_prior = false;
  std::map<uint32_t, uint64_t> epoch_revisions;
  struct MeshState { uint64_t generation; uint64_t revision; bool live; };
  std::map<Id, MeshState> mesh_states;

  for (const Event& event : events) {
    const EventMetadata& common = metadata(event);
    if (common.capture_id != capture_id || (have_prior && common.event_sequence <= prior_sequence) ||
        (have_prior && common.monotonic_time_s < prior_time)) {
      return Status::kOutOfOrder;
    }
    prior_sequence = common.event_sequence;
    prior_time = common.monotonic_time_s;
    have_prior = true;

    if (const auto* epoch = std::get_if<EpochEvent>(&event)) {
      const auto found = epoch_revisions.find(common.session_epoch);
      if (found != epoch_revisions.end() && epoch->transform_revision <= found->second) return Status::kOutOfOrder;
      epoch_revisions[common.session_epoch] = epoch->transform_revision;
      continue;
    }
    if (!epoch_revisions.contains(common.session_epoch)) return Status::kOutOfOrder;
    const auto* mesh = std::get_if<MeshAnchorEvent>(&event);
    if (mesh == nullptr) continue;

    const auto found = mesh_states.find(mesh->anchor_id);
    if (mesh->kind == MeshEventKind::kAdd) {
      const uint64_t expected_generation = found == mesh_states.end() ? 1U : found->second.generation + 1U;
      if ((found != mesh_states.end() && found->second.live) || mesh->generation != expected_generation ||
          mesh->anchor_revision != 1U) {
        return Status::kOutOfOrder;
      }
      mesh_states[mesh->anchor_id] = {mesh->generation, mesh->anchor_revision, true};
    } else if (mesh->kind == MeshEventKind::kReplace) {
      if (found == mesh_states.end() || !found->second.live || mesh->generation != found->second.generation ||
          mesh->anchor_revision != found->second.revision + 1U) {
        return Status::kOutOfOrder;
      }
      found->second.revision = mesh->anchor_revision;
    } else {
      if (found == mesh_states.end() || !found->second.live || mesh->generation != found->second.generation ||
          mesh->anchor_revision != found->second.revision) {
        return Status::kOutOfOrder;
      }
      found->second.live = false;
    }
  }

  for (const Event& event : events) {
    const bool accepted = std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, EpochEvent>) {
            return sink.on_epoch(value);
          } else if constexpr (std::is_same_v<T, DepthFrameEvent>) {
            return sink.on_depth_frame(value);
          } else {
            return sink.on_mesh_anchor(value);
          }
        }, event);
    if (!accepted) return Status::kSinkRejected;
  }
  return Status::kOk;
}

}  // namespace hss::replay

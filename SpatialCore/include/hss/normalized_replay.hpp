#ifndef HSS_NORMALIZED_REPLAY_HPP
#define HSS_NORMALIZED_REPLAY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace hss::replay {

using Id = std::array<uint8_t, 16>;
using Matrix4d = std::array<double, 16>;

constexpr uint16_t kRecordFormatMajor = 1;
constexpr uint16_t kRecordFormatMinor = 0;
constexpr uint16_t kNormalizedSchemaMajor = 1;
constexpr uint16_t kNormalizedSchemaMinor = 1;
constexpr uint8_t kConfidenceUnavailable = 255;

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kUnsupportedVersion,
  kUnsupportedType,
  kResourceLimit,
  kCorrupt,
  kIncompatibleProvenance,
  kOutOfOrder,
  kSinkRejected,
};

enum class EventType : uint16_t { kEpoch = 1, kDepthFrame = 2, kMeshAnchor = 3 };
enum class Provenance : uint8_t { kCaptured = 1 };
enum class RelationshipState : uint8_t { kUnresolved = 0, kAccepted = 1 };
enum class TrackingState : uint8_t { kNormal = 0, kLimited = 1, kUnavailable = 2 };
enum class DepthSource : uint8_t { kSceneDepth = 0 };
enum class MeshEventKind : uint8_t { kAdd = 0, kReplace = 1, kRemove = 2 };

struct EventMetadata {
  uint16_t schema_major{kNormalizedSchemaMajor};
  uint16_t schema_minor{kNormalizedSchemaMinor};
  Provenance provenance{Provenance::kCaptured};
  Id capture_id{};
  uint32_t session_epoch{0};
  uint64_t event_sequence{0};
  double monotonic_time_s{0.0};
  bool operator==(const EventMetadata&) const = default;
};

struct PixelPlaneLayout {
  uint32_t width{0};
  uint32_t height{0};
  uint64_t row_stride_bytes{0};
  bool operator==(const PixelPlaneLayout&) const = default;
};

struct PixelLayout {
  uint32_t width{0};
  uint32_t height{0};
  uint32_t pixel_format{0};
  std::vector<PixelPlaneLayout> planes;
  bool operator==(const PixelLayout&) const = default;
};

struct DepthCalibration {
  uint32_t image_width{0};
  uint32_t image_height{0};
  double fx{0.0};
  double fy{0.0};
  double cx{0.0};
  double cy{0.0};
  double crop_x{0.0};
  double crop_y{0.0};
  double crop_width{0.0};
  double crop_height{0.0};
  bool operator==(const DepthCalibration&) const = default;
};

struct EpochEvent {
  EventMetadata metadata;
  RelationshipState relationship_state{RelationshipState::kUnresolved};
  uint64_t transform_revision{0};
  Matrix4d project_from_world{};
  bool operator==(const EpochEvent&) const = default;
};

struct DepthFrameEvent {
  EventMetadata metadata;
  TrackingState tracking_state{TrackingState::kNormal};
  DepthSource depth_source{DepthSource::kSceneDepth};
  PixelLayout rgb_layout;
  PixelLayout depth_layout;
  bool confidence_was_supplied{false};
  PixelLayout confidence_layout;
  DepthCalibration calibration;
  Matrix4d world_from_camera{};
  std::vector<float> depth_meters;
  std::vector<uint8_t> confidence;
  std::vector<uint8_t> valid_depth_mask;
  std::vector<uint8_t> metric_fusion_mask;
  std::vector<uint8_t> provisional_preview_mask;
  bool operator==(const DepthFrameEvent&) const = default;
};

struct MeshPayload {
  std::vector<float> vertices_xyz;
  std::vector<float> normals_xyz;
  std::vector<uint32_t> triangle_indices;
  std::vector<uint8_t> classifications;
  std::array<float, 3> local_bounds_min{};
  std::array<float, 3> local_bounds_max{};
  bool operator==(const MeshPayload&) const = default;
};

struct MeshAnchorEvent {
  EventMetadata metadata;
  MeshEventKind kind{MeshEventKind::kAdd};
  Id anchor_id{};
  uint64_t generation{0};
  uint64_t anchor_revision{0};
  Matrix4d world_from_anchor{};
  MeshPayload payload;
  bool operator==(const MeshAnchorEvent&) const = default;
};

using Event = std::variant<EpochEvent, DepthFrameEvent, MeshAnchorEvent>;

struct Limits {
  uint64_t max_record_bytes{256U * 1024U * 1024U};
  uint64_t max_stream_bytes{512U * 1024U * 1024U};
  uint64_t max_records{1000000U};
  uint32_t max_pixel_planes{4};
  uint64_t max_depth_samples{640U * 480U};
  uint64_t max_mesh_vertices{1000000U};
  uint64_t max_mesh_triangles{2000000U};
};

struct DecodeResult {
  Status status{Status::kOk};
  Event event{EpochEvent{}};
};

// Versioned records are little-endian and own all decoded arrays. They are not
// raw ABI-struct dumps and retain no platform buffer or caller pointer.
Status encode_record(const Event& event, const Limits& limits, std::vector<uint8_t>* bytes);
DecodeResult decode_record(const std::vector<uint8_t>& bytes, const Limits& limits);

class EventSink {
 public:
  virtual ~EventSink() = default;
  virtual bool on_epoch(const EpochEvent& event) = 0;
  virtual bool on_depth_frame(const DepthFrameEvent& event) = 0;
  virtual bool on_mesh_anchor(const MeshAnchorEvent& event) = 0;
};

// Decodes and validates the complete stream, including ordering, epochs, and
// mesh lineage, before invoking the sink. A malformed suffix causes no sink
// callbacks and repeated playback receives identical owned event values.
Status replay_records(const std::vector<std::vector<uint8_t>>& records,
                      const Limits& limits,
                      EventSink& sink);

}  // namespace hss::replay

#endif

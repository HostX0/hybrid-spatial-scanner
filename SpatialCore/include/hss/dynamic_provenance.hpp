#pragma once

#include "hss/scene_editing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::dynamic_provenance {

inline constexpr size_t kHardMaxVertices = 262'144U;
inline constexpr size_t kHardMaxTriangles = 524'288U;
inline constexpr size_t kHardMaxSourceRecords = 262'144U;
inline constexpr size_t kHardMaxProvenanceReferences = 1'048'576U;
inline constexpr size_t kHardMaxEvidenceRecords = 65'536U;

struct Point3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  bool operator==(const Point3d &) const = default;
};

struct Bounds3d {
  Point3d min_m;
  Point3d max_m;
  bool operator==(const Bounds3d &) const = default;
};

enum class SourceKind : uint8_t {
  kDepthFrame,
  kMeshAnchor,
};

// Identifies retained captured evidence. source_id identifies the depth frame
// or mesh anchor revision; capture_id, epoch, and event_sequence prevent a
// similarly located observation from another source interval being masked.
struct SourceReference {
  SourceKind kind{SourceKind::kDepthFrame};
  std::string source_id;
  std::string capture_id;
  uint64_t session_epoch{0U};
  uint64_t event_sequence{0U};
  bool operator==(const SourceReference &) const = default;
};

struct Triangle {
  std::array<uint32_t, 3U> vertex_indices{};
  // Strictly increasing indices into CapturedMesh::sources. Every source that
  // supports a triangle must be independently verified before it is masked.
  std::vector<size_t> source_indices;
  bool operator==(const Triangle &) const = default;
};

// This is caller-owned raw captured geometry. apply_exclusion takes it by
// const reference and returns only a separate display/processing mask layer.
struct CapturedMesh {
  std::string project_id;
  std::vector<Point3d> vertices_m;
  std::vector<Triangle> triangles;
  std::vector<SourceReference> sources;
  bool operator==(const CapturedMesh &) const = default;
};

enum class EvidenceKind : uint8_t {
  kTemporalDepthChange,
  kTrackedMultiFrameMotion,
};

// A verifier outside this module creates these records from retained capture
// evidence. A user command, semantic label, or scene-edit audit marker alone
// is not a VerifiedDynamicEvidence record.
struct VerifiedDynamicEvidence {
  std::string evidence_id;
  std::string exclusion_command_id;
  std::string project_id;
  SourceReference source;
  Bounds3d bounds_m;
  EvidenceKind kind{EvidenceKind::kTemporalDepthChange};
  bool operator==(const VerifiedDynamicEvidence &) const = default;
};

struct Options {
  size_t max_vertices{65'536U};
  size_t max_triangles{131'072U};
  size_t max_source_records{65'536U};
  size_t max_provenance_references{262'144U};
  size_t max_evidence_records{8'192U};
  double max_abs_coordinate_m{1'000.0};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInputLimitExceeded,
  kInvalidExclusion,
  kInvalidMesh,
  kInvalidProvenance,
  kNonFiniteGeometry,
  kCoordinateOutOfBounds,
  kInvalidEvidence,
  kDuplicateEvidenceId,
  kEvidenceOutsideExclusion,
  kEvidenceSourceMissing,
  kResourceLimit,
  kInternalFailure,
};

enum class Disposition : uint8_t {
  // The pending exclusion remains an audit marker. No geometry effect may be
  // claimed when no captured source has independently verified evidence.
  kNoVerifiedEffect,
  // A separate mask was produced from fully matched captured provenance.
  kVerifiedMask,
};

struct MaskedTriangle {
  size_t triangle_index{0U};
  std::vector<size_t> source_indices;
  // Aligned with source_indices. One linked verification record is permitted
  // per captured source, avoiding ambiguous or order-dependent selection.
  std::vector<std::string> evidence_ids;
  bool operator==(const MaskedTriangle &) const = default;
};

struct Audit {
  std::string exclusion_command_id;
  std::string project_id;
  Bounds3d requested_bounds_m;
  uint64_t start_sequence{0U};
  uint64_t end_sequence{0U};
  size_t linked_evidence_count{0U};
  size_t masked_triangle_count{0U};
  Disposition disposition{Disposition::kNoVerifiedEffect};
  bool operator==(const Audit &) const = default;
};

struct MaskLayer {
  // One byte per raw input triangle: 1 means hidden from the selected derived
  // view, 0 means retained. Raw vertices and triangles are never returned as
  // replacements and remain the export/rebuild source.
  std::vector<uint8_t> triangle_mask;
  std::vector<MaskedTriangle> masked_triangles;
  Audit audit;
  bool operator==(const MaskLayer &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Empty for every validation/resource failure. kOk always supplies a layer,
  // including kNoVerifiedEffect for an audit marker with no matching evidence.
  std::optional<MaskLayer> layer;
};

// Consumes the typed pending exclusion produced by I-EDIT. The marker limits
// which verified evidence may affect a derived view; it never proves motion by
// itself. A triangle is masked only when all of its captured support records
// match verified evidence by command, project, source, capture, epoch, sequence
// and spatial bounds. Partial support keeps the whole triangle visible.
[[nodiscard]] Result apply_exclusion(
    const CapturedMesh &raw_captured_mesh,
    const scene_editing::DynamicExclusion &pending_exclusion,
    const std::vector<VerifiedDynamicEvidence> &verified_evidence,
    const Options &options = {});

[[nodiscard]] const char *status_name(Status status);

} // namespace hss::dynamic_provenance

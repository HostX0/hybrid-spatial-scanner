#pragma once

#include "hss/annotation_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::dxf {

// The pinned ASCII DXF release used by this exporter.  All model-space
// coordinates are canonical plan metres: X=U and Y=V (where V=-Z in the
// project's documented floor frame).
inline constexpr const char *kAcadVersion = "AC1015";
inline constexpr int kInsUnitsMetres = 6;
inline constexpr size_t kHardMaxPrimitives = 65'536U;
inline constexpr size_t kHardMaxVertices = 1'048'576U;
inline constexpr size_t kHardMaxWarnings = 4'096U;

enum class Layer : uint8_t {
  kBoundary,
  kFixed,
  kOpenings,
  kFurniture,
  kOverhead,
  kDimensions,
  kLabels,
  kWarnings,
};

// This is provenance for geometry explicitly supplied by the frozen scene
// adapter.  It intentionally distinguishes uncertain geometry from metric
// evidence; the exporter does not promote one state into another.
enum class Provenance : uint8_t {
  kObserved,
  kFitted,
  kUserMeasured,
  kInferred,
  kUnknown,
};

// One caller-authored primitive.  The exporter copies every supplied vertex
// verbatim after finite/range validation; it never closes, joins, offsets or
// otherwise repairs geometry.  `source_ids` must identify retained plan
// fragment/support lineage in the same frozen revision.
struct Primitive {
  std::string id;
  std::string source_revision;
  Layer layer{Layer::kBoundary};
  Provenance provenance{Provenance::kUnknown};
  std::vector<std::string> source_ids;
  std::vector<area::Point2d> vertices_m;
  bool closed{false};
  bool operator==(const Primitive &) const = default;
};

struct Snapshot {
  // The accepted B-031 layer and B-032 resolved annotations are immutable
  // input authorities.  All revisions must agree before any bytes are made.
  plan_semantics::PlanLayer plan;
  annotation_geometry::Layout annotations;
  std::vector<Primitive> primitives;
  std::vector<std::string> warnings;
};

struct Options {
  size_t max_primitives{4'096U};
  size_t max_vertices{262'144U};
  size_t max_warnings{1'024U};
  double max_abs_coordinate_m{10'000.0};
  // AC1015's text encoding is not portable for arbitrary Unicode.  The DXF
  // therefore writes only printable ASCII TEXT; full UTF-8 labels and IDs
  // remain in the companion JSON when this is false.
  bool emit_ascii_text{true};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInvalidSnapshot,
  kRevisionMismatch,
  kInvalidPrimitive,
  kInvalidProvenance,
  kInvalidAnnotation,
  kResourceLimitExceeded,
  kInternalFailure,
};

struct Artifact {
  // `dxf_ascii` is an AC1015 model-space metre drawing.  `provenance_json`
  // is the companion UTF-8 record for source IDs, unknowns, warnings and
  // labels that cannot safely be represented as AC1015 text.
  std::string dxf_ascii;
  std::string provenance_json;
  bool operator==(const Artifact &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  std::optional<Artifact> artifact;
};

[[nodiscard]] Result export_snapshot(const Snapshot &snapshot,
                                     const Options &options = {});
[[nodiscard]] const char *status_name(Status status);
[[nodiscard]] const char *layer_name(Layer layer);
[[nodiscard]] const char *provenance_name(Provenance provenance);

} // namespace hss::dxf

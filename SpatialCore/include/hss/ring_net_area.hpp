#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::area {

inline constexpr size_t kHardMaxRings = 4'097U;
inline constexpr size_t kHardMaxVertices = 65'536U;

struct Point2d {
  double x_m{0.0};
  double y_m{0.0};
  bool operator==(const Point2d &) const = default;
};

enum class LinearUnit : uint8_t { kMetres };
enum class AreaUnit : uint8_t { kSquareMetres };

enum class TopologyState : uint8_t {
  kAccepted,
  kIncomplete,
  kUnaccepted,
};

enum class GeometryOrigin : uint8_t {
  kObserved,
  kFitted,
  kUserMeasured,
  kInferred,
  kUnknown,
};

enum class QualityState : uint8_t {
  kProvisional,
  kNeedsReview,
  kReady,
  kInvalid,
};

struct Provenance {
  GeometryOrigin geometry_origin{GeometryOrigin::kUnknown};
  QualityState quality_state{QualityState::kInvalid};
  std::string source_revision;
  std::vector<std::string> support_ids;
  bool operator==(const Provenance &) const = default;
};

// The caller supplies rings accepted by the boundary/topology authority.
// This calculator checks only the minimum safe consumption contract (finite
// coordinates, vertex count, and declared winding). It never closes, hulls,
// reorders, reverses, intersects, or otherwise repairs a ring.
struct ValidatedRing {
  std::string id;
  std::vector<Point2d> vertices_m;
  TopologyState topology_state{TopologyState::kUnaccepted};
  Provenance provenance;
  bool operator==(const ValidatedRing &) const = default;
};

enum class HoleKind : uint8_t {
  kFixedObstruction,
  kFurniture,
  kOther,
};

enum class Attachment : uint8_t {
  kDetached,
  kAttached,
  kUncertain,
};

struct Hole {
  ValidatedRing ring;
  HoleKind kind{HoleKind::kOther};
  Attachment attachment{Attachment::kUncertain};
  // This is an explicit planning classification. A semantic label alone is
  // not fixed=true and cannot cause subtraction.
  bool explicitly_fixed{false};
  bool operator==(const Hole &) const = default;
};

enum class AreaPolicy : uint8_t {
  kInternalFinishedFace,
  kNetFixedObstructions,
};

struct Input {
  LinearUnit coordinate_unit{LinearUnit::kMetres};
  AreaPolicy policy{AreaPolicy::kInternalFinishedFace};
  ValidatedRing outer;
  std::vector<Hole> holes;
};

struct Options {
  size_t max_rings{257U};
  size_t max_vertices{16'384U};
};

enum class Decision : uint8_t {
  kOuterBase,
  kSubtractedFixedDetachedObstruction,
  kIgnoredByInternalFinishedFacePolicy,
  kIgnoredNotExplicitlyFixed,
  kIgnoredAttached,
  kIgnoredUncertainAttachment,
  kIgnoredFurniture,
  kIgnoredOther,
};

struct RingAssessment {
  std::string ring_id;
  // Outer is positive and validated holes are negative. This retains the
  // authored winding rather than hiding it behind an absolute-area API.
  double signed_area_m2{0.0};
  double applied_delta_m2{0.0};
  Decision decision{Decision::kIgnoredOther};
  Provenance provenance;
  bool operator==(const RingAssessment &) const = default;
};

enum class Availability : uint8_t {
  kReady,
  kProvisional,
  kUnavailable,
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInvalidUnit,
  kInvalidPolicy,
  kResourceLimitExceeded,
  kTopologyIncomplete,
  kTopologyUnaccepted,
  kInvalidClassification,
  kInvalidRing,
  kNonFiniteCoordinate,
  kWrongWinding,
  kNumericalFailure,
};

struct Result {
  Status status{Status::kInvalidOptions};
  Availability availability{Availability::kUnavailable};
  AreaPolicy policy{AreaPolicy::kInternalFinishedFace};
  AreaUnit area_unit{AreaUnit::kSquareMetres};
  // Null for every non-ready result. Partial numeric areas are never exposed.
  std::optional<double> area_m2;
  // Populated only for kOk. Every assessment copies the input provenance.
  std::vector<RingAssessment> assessments;
};

const char *status_name(Status status);
const char *decision_name(Decision decision);

// Computes area from already-validated CCW outer and CW hole rings using a
// translated origin and compensated long-double summation. Only a hole that is
// all of fixed-obstruction, explicitly fixed, and detached is subtractable,
// and only under kNetFixedObstructions.
Result calculate(const Input &input, const Options &options = {});

} // namespace hss::area

#pragma once

#include "hss/ceiling_overhead.hpp"
#include "hss/ring_net_area.hpp"
#include "hss/surface_proposal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::plan_semantics {

inline constexpr size_t kHardMaxFragments = 65'536U;
inline constexpr size_t kHardMaxSupportIds = 262'144U;

enum class View : uint8_t {
  kFloorFootprint,
  kArchitecturalCut,
  kReflectedCeiling,
};

enum class ReferenceState : uint8_t {
  kConfirmedFloor,
  kFloorUnknown,
  kUnsupported,
  kNotApplicable,
};

enum class BoundaryState : uint8_t {
  kClosedAccepted,
  kOpen,
  kUnknown,
  kUnsupported,
};

enum class EvidenceState : uint8_t {
  kSupported,
  kUnknown,
  kUnsupported,
};

enum class EvidenceKind : uint8_t {
  kSurfaceIntersection,
  kFloorJunction,
  kCeilingPatch,
  kUnknownRegion,
  kUnknownBoundary,
  kUnsupported,
  // An exact section of retained captured geometry at the declared height.
  // Unlike kSurfaceIntersection, this carries no B-017 plane-intersection
  // claim. Its segment and support IDs must already exist; assemble never
  // extends or otherwise repairs it.
  kCapturedSection,
};

struct HeightRange {
  double minimum_m{0.0};
  double maximum_m{0.0};
  bool operator==(const HeightRange &) const = default;
};

struct HeightPolicy {
  // Floor and architectural views require both values. Reflected-ceiling
  // views require neither because their source patches retain local heights.
  std::optional<double> cut_height_m;
  std::optional<double> sampling_band_m;
  ReferenceState reference_state{ReferenceState::kFloorUnknown};
  // Required only for a confirmed floor. It names the accepted floor-frame
  // authority; an empty value is never treated as an implicit confirmation.
  std::string floor_reference_id;
  bool operator==(const HeightPolicy &) const = default;
};

struct Segment2d {
  area::Point2d start_m;
  area::Point2d end_m;
  bool operator==(const Segment2d &) const = default;
};

// A fragment is submitted geometry plus its retained source lineage. This
// module never connects, closes, extends, projects, or otherwise repairs it.
struct Fragment {
  std::string id;
  EvidenceState state{EvidenceState::kUnknown};
  EvidenceKind kind{EvidenceKind::kUnknownBoundary};
  area::GeometryOrigin geometry_origin{area::GeometryOrigin::kUnknown};
  std::optional<Segment2d> segment;
  std::optional<HeightRange> source_height_range_m;
  std::string source_revision;
  std::vector<std::string> support_ids;

  // Required for kSurfaceIntersection and forbidden for other evidence kinds.
  // Its B-017 source IDs must all remain present in support_ids.
  std::optional<surface_proposal::IntersectionProposal> intersection;

  // Required for kCeilingPatch/kUnknownRegion and must identify an entity in
  // the supplied B-021 model. Zero is never a valid patch identity.
  std::optional<uint64_t> overhead_patch_id;
  bool operator==(const Fragment &) const = default;
};

struct Input {
  std::string scene_revision;
  bool source_is_synthetic{false};
  View view{View::kArchitecturalCut};
  HeightPolicy height;
  BoundaryState boundary_state{BoundaryState::kUnknown};
  std::vector<Fragment> fragments;

  // Required for floor/cut views and forbidden for reflected-ceiling views.
  // Its rings remain the B-019 metric authority.
  std::optional<area::Input> area_input;

  // Required for reflected-ceiling views and forbidden for floor/cut views.
  // The accepted B-021 classification is copied without filling unknowns.
  std::optional<ceiling_overhead::Model> ceiling_model;
};

struct Options {
  size_t max_fragments{4'096U};
  size_t max_support_ids{65'536U};
  double max_abs_coordinate_m{10'000.0};
  double min_fragment_length_m{1.0e-9};
  double height_tolerance_m{1.0e-9};
  area::Options area_options;
};

enum class IssueCode : uint8_t {
  kFloorReferenceUnknown,
  kFloorReferenceUnsupported,
  kOpenBoundary,
  kUnknownBoundaryEvidence,
  kUnsupportedBoundaryEvidence,
  kInferredGeometry,
  kQualityNotReady,
  kCeilingUnknownRegion,
  kAreaNotApplicable,
};

struct Issue {
  IssueCode code{IssueCode::kUnknownBoundaryEvidence};
  std::string target_id;
  bool operator==(const Issue &) const = default;
};

enum class Availability : uint8_t {
  kReady,
  kProvisional,
  kUnavailable,
  kUnsupported,
  kNotApplicable,
};

struct AreaOutcome {
  area::AreaPolicy policy{area::AreaPolicy::kInternalFinishedFace};
  Availability availability{Availability::kUnavailable};
  std::optional<double> area_m2;
  std::optional<area::Status> calculator_status;
  std::vector<area::RingAssessment> assessments;
  bool operator==(const AreaOutcome &) const = default;
};

struct PlanLayer {
  std::string scene_revision;
  bool source_is_synthetic{false};
  View view{View::kArchitecturalCut};
  HeightPolicy height;
  BoundaryState boundary_state{BoundaryState::kUnknown};
  Availability validity{Availability::kUnavailable};
  std::vector<Fragment> fragments;
  AreaOutcome area;
  std::optional<ceiling_overhead::Model> ceiling_model;
  std::vector<Issue> issues;
  bool operator==(const PlanLayer &) const = default;
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInvalidEnumValue,
  kInvalidRevision,
  kResourceLimitExceeded,
  kInvalidHeightPolicy,
  kInvalidFragment,
  kInvalidProvenance,
  kHeightEvidenceMismatch,
  kViewEvidenceMismatch,
  kAreaInputMismatch,
  kBoundaryStateMismatch,
  kAreaRejected,
  kResourceLimit,
  kInternalFailure,
};

struct Result {
  Status status{Status::kInvalidOptions};
  // Present for kOk only. Semantic unavailability is a successful, explicit
  // layer outcome; malformed or contradictory inputs fail atomically.
  std::optional<PlanLayer> layer;
};

const char *status_name(Status status);
const char *issue_name(IssueCode issue);

// Validates and assembles one plan view without deriving new geometry. A clean
// numeric area is exposed only for a confirmed floor reference and a closed,
// accepted, source-consistent metric boundary. Unknown/unsupported evidence
// remains visible in fragments/issues and never receives generated geometry.
Result assemble(const Input &input, const Options &options = {});

} // namespace hss::plan_semantics

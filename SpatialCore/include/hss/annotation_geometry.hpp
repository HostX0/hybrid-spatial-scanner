#pragma once

#include "hss/plan_semantics.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::annotation_geometry {

inline constexpr size_t kHardMaxAnnotations = 65'536U;
inline constexpr size_t kHardMaxEvidenceIds = 262'144U;

// Annotation geometry always remains in the metre-based floor frame of the
// frozen plan. Display units affect labels only; stored coordinates do not.
enum class DisplayUnit : uint8_t { kMetres, kMillimetres, kFeet };

enum class Kind : uint8_t { kLength, kAngle, kHeight, kLabel };

enum class EvidenceSource : uint8_t {
  kObserved,
  kFitted,
  kUserMeasured,
  kInferred,
  kUnknown,
};

enum class Availability : uint8_t {
  kReady,
  kProvisional,
  kUnavailable,
};

// A point is both geometrically explicit and tied to an ID retained by the
// frozen plan. A consumer may draw leaders differently, but may not alter it.
struct Anchor {
  std::string geometry_id;
  area::Point2d position_m;
  bool operator==(const Anchor &) const = default;
};

struct Annotation {
  std::string id;
  Kind kind{Kind::kLength};
  EvidenceSource evidence_source{EvidenceSource::kUnknown};
  std::string source_revision;
  std::vector<std::string> evidence_ids;

  // Length/height use exactly two anchors. Angle uses vertex, first ray, then
  // second ray. Label uses exactly one anchor and deliberately has no value.
  std::vector<Anchor> anchors;
  DisplayUnit display_unit{DisplayUnit::kMetres};
  uint8_t display_precision{2U};
  std::optional<double> uncertainty_m;

  // Only an explicit user measurement can provide this independent value.
  // Observed/fitted dimensions are derived solely from the submitted anchors.
  std::optional<double> user_measured_value_m;
  std::string label;
  bool operator==(const Annotation &) const = default;
};

struct Input {
  // The caller supplies an already accepted B-031 layer. This module copies
  // it only as revision/lineage authority and never creates plan geometry.
  plan_semantics::PlanLayer plan;
  std::vector<Annotation> annotations;
};

struct Options {
  size_t max_annotations{4'096U};
  size_t max_evidence_ids{65'536U};
  double max_abs_coordinate_m{10'000.0};
  double min_length_m{1.0e-9};
  uint8_t max_display_precision{6U};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInvalidPlan,
  kInvalidAnnotation,
  kInvalidProvenance,
  kInvalidGeometry,
  kInvalidMeasurement,
  kResourceLimitExceeded,
  kInternalFailure,
};

struct ResolvedAnnotation {
  Annotation annotation;
  Availability availability{Availability::kUnavailable};
  // Empty for labels, unknown/inferred evidence, unavailable plans, and every
  // invalid request. A provisional plan may retain its explicit calculation,
  // but its availability remains provisional. Values are never fabricated from
  // text or a semantic category.
  std::optional<double> metric_value;
  bool operator==(const ResolvedAnnotation &) const = default;
};

struct Layout {
  std::string scene_revision;
  std::vector<ResolvedAnnotation> annotations;
  bool operator==(const Layout &) const = default;
};

struct Result {
  Status status{Status::kInvalidOptions};
  std::optional<Layout> layout;
};

[[nodiscard]] Result resolve(const Input &input, const Options &options = {});
[[nodiscard]] const char *status_name(Status status);

} // namespace hss::annotation_geometry

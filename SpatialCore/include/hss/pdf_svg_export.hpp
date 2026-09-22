#pragma once

#include "hss/annotation_geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::pdf_svg_export {

// This is a portable, in-memory export boundary. Persistence owns its own
// manifest-last transaction and writes these immutable bytes atomically.
enum class Paper : uint8_t { kA4Portrait, kA4Landscape, kA3Portrait, kA3Landscape };
enum class ScalePolicy : uint8_t { kFitToPage, kExplicit }; 

struct Input {
  std::string project_id;
  plan_semantics::PlanLayer plan;
  annotation_geometry::Layout annotations;
  std::string level_or_room;
  std::vector<std::string> caller_warnings;
};

struct Options {
  Paper paper{Paper::kA4Landscape};
  ScalePolicy scale_policy{ScalePolicy::kFitToPage};
  // Required for explicit scale and expressed as the denominator in 1:N.
  uint32_t explicit_scale_denominator{0U};
  double margin_mm{12.0};
  double scale_bar_m{1.0};
  size_t max_fragments{4'096U};
  size_t max_annotations{4'096U};
  size_t max_warning_bytes{32'768U};
};

enum class Status : uint8_t {
  kOk,
  kInvalidOptions,
  kInvalidInput,
  kRevisionMismatch,
  kResourceLimitExceeded,
  kPageOverflow,
  kInternalFailure,
};

struct Artifact {
  std::string scene_revision;
  // Valid vector PDF bytes with vector paths and selectable ASCII metadata.
  // Non-Latin labels are preserved verbatim in the UTF-8 SVG and manifest;
  // platform PDF font shaping is deliberately outside this portable core.
  std::string pdf;
  std::string svg;
  // UTF-8, deterministic sidecar data. It retains support/evidence IDs,
  // unknown/provisional availability and all text without making geometry.
  std::string manifest_json;
};

struct Result {
  Status status{Status::kInvalidOptions};
  std::optional<Artifact> artifact;
};

[[nodiscard]] Result build(const Input &input, const Options &options = {});
[[nodiscard]] const char *status_name(Status status);

} // namespace hss::pdf_svg_export

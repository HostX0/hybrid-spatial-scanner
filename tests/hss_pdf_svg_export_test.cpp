#include "hss/pdf_svg_export.hpp"

#include <iostream>
#include <string>

namespace {
using hss::annotation_geometry::Anchor;
using hss::annotation_geometry::Annotation;
using hss::annotation_geometry::Availability;
using hss::annotation_geometry::EvidenceSource;
using hss::annotation_geometry::Kind;
using hss::annotation_geometry::Layout;
using hss::annotation_geometry::ResolvedAnnotation;
using hss::pdf_svg_export::Input;
using hss::pdf_svg_export::Status;

int failures = 0;
void check(bool condition, const std::string &message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

Input fixture() {
  Input input;
  input.project_id = "project-42";
  input.plan.scene_revision = "42";
  input.plan.validity = hss::plan_semantics::Availability::kProvisional;
  input.plan.view = hss::plan_semantics::View::kArchitecturalCut;
  hss::plan_semantics::Fragment observed;
  observed.id = "observed-wall";
  observed.source_revision = "42";
  observed.support_ids = {"mesh-anchor-7", "depth-2"};
  observed.segment = {{{-2.0, 1.0}, {3.0, 1.0}}};
  input.plan.fragments.push_back(observed);
  hss::plan_semantics::Fragment unknown;
  unknown.id = "unknown-gap";
  unknown.source_revision = "42";
  unknown.support_ids = {"depth-gap-1"};
  input.plan.fragments.push_back(unknown);
  input.annotations.scene_revision = "42";
  Annotation annotation;
  annotation.id = "arabic-label";
  annotation.kind = Kind::kLabel;
  annotation.evidence_source = EvidenceSource::kUnknown;
  annotation.source_revision = "42";
  annotation.label = "غرفة غير مكتملة";
  annotation.anchors = {Anchor{"observed-wall", {-2.0, 1.0}}};
  input.annotations.annotations.push_back({annotation, Availability::kProvisional, std::nullopt});
  input.caller_warnings = {"Retained unknown boundary evidence."};
  return input;
}

void test_deterministic_frozen_artifact() {
  const Input input = fixture();
  const auto first = hss::pdf_svg_export::build(input);
  const auto second = hss::pdf_svg_export::build(input);
  check(first.status == Status::kOk && first.artifact, "valid frozen snapshot exports");
  check(second.status == Status::kOk && second.artifact, "same snapshot exports again");
  if (!first.artifact || !second.artifact) return;
  check(first.artifact->pdf == second.artifact->pdf && first.artifact->svg == second.artifact->svg &&
            first.artifact->manifest_json == second.artifact->manifest_json,
        "portable artifacts are deterministic for one accepted snapshot");
  check(first.artifact->pdf.starts_with("%PDF-1.4") && first.artifact->pdf.find("Print at 100%") != std::string::npos,
        "PDF contains vector document header and printing scale caveat");
  check(first.artifact->svg.find("data-y-inversion=\"exactly-one\"") != std::string::npos &&
            first.artifact->svg.find("غرفة غير مكتملة") != std::string::npos,
        "SVG declares exactly one inversion and retains UTF-8 Arabic label");
  check(first.artifact->manifest_json.find("unknown-gap") != std::string::npos &&
            first.artifact->manifest_json.find("has_explicit_segment\":false") != std::string::npos &&
            first.artifact->manifest_json.find("mesh-anchor-7") != std::string::npos,
        "manifest retains unknown geometry and provenance without creating a path");
  check(first.artifact->svg.find("fragment-unknown-gap") == std::string::npos,
        "unknown fragment with no authored segment does not gain metric geometry");
}

void test_revision_and_page_rejections_are_atomic() {
  Input stale = fixture();
  stale.annotations.scene_revision = "43";
  const auto stale_result = hss::pdf_svg_export::build(stale);
  check(stale_result.status == Status::kInvalidInput && !stale_result.artifact,
        "foreign annotation layout cannot be mixed into an export");

  Input foreign_annotation = fixture();
  foreign_annotation.annotations.annotations.front().annotation.source_revision = "41";
  const auto foreign_result = hss::pdf_svg_export::build(foreign_annotation);
  check(foreign_result.status == Status::kRevisionMismatch && !foreign_result.artifact,
        "stale annotation source revision cannot export");

  hss::pdf_svg_export::Options explicit_scale;
  explicit_scale.scale_policy = hss::pdf_svg_export::ScalePolicy::kExplicit;
  explicit_scale.explicit_scale_denominator = 1U;
  const auto overflowing = hss::pdf_svg_export::build(fixture(), explicit_scale);
  check(overflowing.status == Status::kPageOverflow && !overflowing.artifact,
        "explicit scale refuses silent PDF cropping");
}

} // namespace

int main() {
  test_deterministic_frozen_artifact();
  test_revision_and_page_rejections_are_atomic();
  if (failures != 0) { std::cerr << failures << " PDF/SVG export test(s) failed\n"; return 1; }
  std::cout << "PDF/SVG export tests passed\n";
  return 0;
}

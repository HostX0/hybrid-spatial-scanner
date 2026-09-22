#include "hss/dxf_exporter.hpp"
#include "hss/obj_mtl_exporter.hpp"
#include "hss/pdf_svg_export.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kRevision{"43"};
constexpr std::string_view kProjectId{"project-cross-export"};
constexpr std::string_view kCapturedMeshId{"mesh-7"};

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

hss::plan_semantics::PlanLayer plan() {
  hss::plan_semantics::PlanLayer value;
  value.scene_revision = std::string(kRevision);
  value.validity = hss::plan_semantics::Availability::kProvisional;
  value.view = hss::plan_semantics::View::kArchitecturalCut;

  hss::plan_semantics::Fragment observed;
  observed.id = "wall-a";
  observed.state = hss::plan_semantics::EvidenceState::kSupported;
  observed.source_revision = std::string(kRevision);
  observed.support_ids = {std::string(kCapturedMeshId)};
  observed.segment = {{{0.0, 0.0}, {1.0, 0.0}}};
  value.fragments.push_back(std::move(observed));

  hss::plan_semantics::Fragment unknown;
  unknown.id = "gap-unknown";
  unknown.state = hss::plan_semantics::EvidenceState::kUnknown;
  unknown.source_revision = std::string(kRevision);
  unknown.support_ids = {"mesh-gap"};
  value.fragments.push_back(std::move(unknown));
  return value;
}

hss::annotation_geometry::Layout layout() {
  hss::annotation_geometry::Layout value;
  value.scene_revision = std::string(kRevision);
  return value;
}

hss::pdf_svg_export::Input pdf_input() {
  hss::pdf_svg_export::Input value;
  value.project_id = std::string(kProjectId);
  value.plan = plan();
  value.annotations = layout();
  value.caller_warnings = {"unknown_region_retained"};
  return value;
}

hss::dxf::Snapshot dxf_snapshot() {
  hss::dxf::Snapshot value;
  value.plan = plan();
  value.annotations = layout();
  value.primitives = {{"wall-a-segment", std::string(kRevision),
                       hss::dxf::Layer::kBoundary,
                       hss::dxf::Provenance::kObserved,
                       {"wall-a", std::string(kCapturedMeshId)},
                       {{0.0, 0.0}, {1.0, 0.0}}, false}};
  value.warnings = {"unknown_region_retained"};
  return value;
}

hss::obj_mtl::Snapshot obj_snapshot() {
  hss::obj_mtl::Snapshot value;
  value.export_id = "export-cross-43";
  value.project_id = std::string(kProjectId);
  value.scene_revision = 43U;
  value.captured_mesh_id = std::string(kCapturedMeshId);
  value.quality_state = "provisional";
  value.vertices_m = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
  value.faces = {{"wall-a-face", {0U, 1U, 2U},
                  {"wall-a", std::string(kCapturedMeshId)}, "", std::nullopt}};
  value.warnings = {"unknown_region_retained"};
  return value;
}

void test_identical_frozen_authority_is_deterministic_across_formats() {
  const auto pdf_first = hss::pdf_svg_export::build(pdf_input());
  const auto pdf_second = hss::pdf_svg_export::build(pdf_input());
  const auto dxf_first = hss::dxf::export_snapshot(dxf_snapshot());
  const auto dxf_second = hss::dxf::export_snapshot(dxf_snapshot());
  const auto obj_first = hss::obj_mtl::export_snapshot(obj_snapshot());
  const auto obj_second = hss::obj_mtl::export_snapshot(obj_snapshot());

  check(pdf_first.status == hss::pdf_svg_export::Status::kOk && pdf_first.artifact &&
            pdf_second.status == hss::pdf_svg_export::Status::kOk && pdf_second.artifact,
        "PDF/SVG accepts the frozen cross-export fixture");
  check(dxf_first.status == hss::dxf::Status::kOk && dxf_first.artifact &&
            dxf_second.status == hss::dxf::Status::kOk && dxf_second.artifact,
        "DXF accepts the frozen cross-export fixture");
  check(obj_first.status == hss::obj_mtl::Status::kOk && obj_first.artifact &&
            obj_second.status == hss::obj_mtl::Status::kOk && obj_second.artifact,
        "OBJ/MTL accepts the frozen cross-export fixture");
  if (!pdf_first.artifact || !pdf_second.artifact || !dxf_first.artifact ||
      !dxf_second.artifact || !obj_first.artifact || !obj_second.artifact)
    return;

  check(pdf_first.artifact->pdf == pdf_second.artifact->pdf &&
            pdf_first.artifact->svg == pdf_second.artifact->svg &&
            pdf_first.artifact->manifest_json == pdf_second.artifact->manifest_json,
        "PDF, SVG, and their manifest are byte-stable for one frozen authority");
  check(dxf_first.artifact->dxf_ascii == dxf_second.artifact->dxf_ascii &&
            dxf_first.artifact->provenance_json == dxf_second.artifact->provenance_json,
        "DXF and its provenance sidecar are byte-stable for one frozen authority");
  check(obj_first.artifact->obj_utf8 == obj_second.artifact->obj_utf8 &&
            obj_first.artifact->mtl_utf8 == obj_second.artifact->mtl_utf8 &&
            obj_first.artifact->manifest_json == obj_second.artifact->manifest_json,
        "OBJ, MTL, and their manifest are byte-stable for one frozen authority");
}

void test_revision_units_and_lineage_remain_coherent() {
  const auto pdf = hss::pdf_svg_export::build(pdf_input());
  const auto dxf = hss::dxf::export_snapshot(dxf_snapshot());
  const auto obj = hss::obj_mtl::export_snapshot(obj_snapshot());
  if (!pdf.artifact || !dxf.artifact || !obj.artifact) {
    check(false, "cross-export fixture must produce every artifact before coherence checks");
    return;
  }

  check(pdf.artifact->manifest_json.find("\"scene_revision\":\"43\"") != std::string::npos &&
            dxf.artifact->provenance_json.find("\"scene_revision\":\"43\"") != std::string::npos &&
            obj.artifact->manifest_json.find("\"scene_revision\":\"43\"") != std::string::npos,
        "every artifact records the same frozen scene revision");
  check(pdf.artifact->manifest_json.find("\"units\":\"m\"") != std::string::npos &&
            pdf.artifact->svg.find("data-units=\"m\"") != std::string::npos &&
            dxf.artifact->dxf_ascii.find("$INSUNITS\n70\n6\n") != std::string::npos &&
            dxf.artifact->provenance_json.find("\"units\":\"m\"") != std::string::npos &&
            obj.artifact->manifest_json.find("\"units\":\"m\"") != std::string::npos,
        "all format boundaries retain metres as their declared coordinate unit");
  check(pdf.artifact->manifest_json.find(std::string(kCapturedMeshId)) != std::string::npos &&
            dxf.artifact->provenance_json.find(std::string(kCapturedMeshId)) != std::string::npos &&
            obj.artifact->manifest_json.find(std::string(kCapturedMeshId)) != std::string::npos,
        "every manifest preserves the captured-mesh provenance identifier");
}

void test_unknowns_do_not_gain_generated_geometry() {
  const auto pdf = hss::pdf_svg_export::build(pdf_input());
  const auto dxf = hss::dxf::export_snapshot(dxf_snapshot());
  const auto obj = hss::obj_mtl::export_snapshot(obj_snapshot());
  if (!pdf.artifact || !dxf.artifact || !obj.artifact) {
    check(false, "cross-export fixture must produce every artifact before geometry checks");
    return;
  }

  check(pdf.artifact->manifest_json.find("gap-unknown") != std::string::npos &&
            pdf.artifact->svg.find("fragment-gap-unknown") == std::string::npos,
        "PDF/SVG retains the unknown fragment in metadata without creating a path");
  check(dxf.artifact->provenance_json.find("gap-unknown") != std::string::npos &&
            dxf.artifact->dxf_ascii.find("gap-unknown") == std::string::npos,
        "DXF retains the unknown plan lineage in its sidecar without adding an unknown-region primitive");
  check(obj.artifact->obj_utf8.find("v 0 0 0\nv 1 0 0\nv 0 1 0\n") != std::string::npos &&
            obj.artifact->obj_utf8.find("f 1 2 3\n") != std::string::npos &&
            obj.artifact->obj_utf8.find("gap-unknown") == std::string::npos,
        "OBJ contains only the caller-supplied captured triangle and no unknown-region mesh");
}

} // namespace

int main() {
  test_identical_frozen_authority_is_deterministic_across_formats();
  test_revision_units_and_lineage_remain_coherent();
  test_unknowns_do_not_gain_generated_geometry();
  if (failures != 0) {
    std::cerr << failures << " cross-export regression test(s) failed\n";
    return 1;
  }
  std::cout << "Cross-export regression tests passed\n";
  return 0;
}

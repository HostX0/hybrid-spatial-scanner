#include "hss/dxf_exporter.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace {
using hss::annotation_geometry::Anchor;
using hss::annotation_geometry::Annotation;
using hss::annotation_geometry::Availability;
using hss::annotation_geometry::EvidenceSource;
using hss::annotation_geometry::Kind;
using hss::annotation_geometry::ResolvedAnnotation;
using hss::dxf::Layer;
using hss::dxf::Primitive;
using hss::dxf::Provenance;
using hss::dxf::Snapshot;
using hss::dxf::Status;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

hss::plan_semantics::PlanLayer plan() {
  hss::plan_semantics::PlanLayer value;
  value.scene_revision = "43";
  value.validity = hss::plan_semantics::Availability::kProvisional;
  value.view = hss::plan_semantics::View::kArchitecturalCut;
  value.height.cut_height_m = 1.2;
  hss::plan_semantics::Fragment fragment;
  fragment.id = "wall-a";
  fragment.source_revision = value.scene_revision;
  fragment.state = hss::plan_semantics::EvidenceState::kSupported;
  fragment.support_ids = {"mesh-7"};
  value.fragments.push_back(std::move(fragment));
  return value;
}

ResolvedAnnotation dimension() {
  Annotation annotation;
  annotation.id = "dim-one-metre";
  annotation.kind = Kind::kLength;
  annotation.evidence_source = EvidenceSource::kObserved;
  annotation.source_revision = "43";
  annotation.evidence_ids = {"mesh-7"};
  annotation.anchors = {Anchor{"wall-a", {0.0, 0.0}},
                        Anchor{"mesh-7", {1.0, 0.0}}};
  return {annotation, Availability::kProvisional, 1.0};
}

ResolvedAnnotation unknown_dimension() {
  Annotation annotation;
  annotation.id = "unknown-gap";
  annotation.kind = Kind::kLength;
  annotation.evidence_source = EvidenceSource::kUnknown;
  annotation.source_revision = "43";
  annotation.anchors = {Anchor{"wall-a", {2.0, 0.0}},
                        Anchor{"mesh-7", {3.0, 0.0}}};
  return {annotation, Availability::kProvisional, std::nullopt};
}

ResolvedAnnotation arabic_label() {
  Annotation annotation;
  annotation.id = "arabic-label";
  annotation.kind = Kind::kLabel;
  annotation.evidence_source = EvidenceSource::kObserved;
  annotation.source_revision = "43";
  annotation.anchors = {Anchor{"wall-a", {0.5, 0.5}}};
  annotation.label = "غرفة تجريبية";
  return {annotation, Availability::kReady, std::nullopt};
}

Snapshot snapshot() {
  Snapshot value;
  value.plan = plan();
  value.annotations.scene_revision = value.plan.scene_revision;
  value.annotations.annotations = {dimension(), unknown_dimension(), arabic_label()};
  value.primitives = {
      {"boundary-segment", "43", Layer::kBoundary, Provenance::kObserved,
       {"wall-a", "mesh-7"}, {{0.0, 0.0}, {1.0, 0.0}}, false},
      {"unknown-return", "43", Layer::kBoundary, Provenance::kUnknown,
       {"wall-a"}, {{1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, true},
  };
  value.warnings = {"DRAFT: unknown return retained"};
  return value;
}

void test_ac1015_metre_layers_and_exact_geometry() {
  const auto result = hss::dxf::export_snapshot(snapshot());
  check(result.status == Status::kOk && result.artifact,
        "frozen metre snapshot exports");
  if (!result.artifact)
    return;
  const std::string &dxf = result.artifact->dxf_ascii;
  check(dxf.find("$ACADVER\n1\nAC1015\n") != std::string::npos &&
            dxf.find("$INSUNITS\n70\n6\n") != std::string::npos,
        "DXF pins AC1015 and declares metres");
  check(dxf.find("8\nHSS_BOUNDARY\n") != std::string::npos &&
            dxf.find("8\nHSS_DIMS\n") != std::string::npos,
        "observed geometry and explicit dimension keep their CAD layers");
  check(dxf.find("8\nHSS_WARNINGS\n") != std::string::npos &&
            dxf.find("1\nUNKNOWN\n") != std::string::npos,
        "unknown geometry is visibly warned without a numerical claim");
  check(dxf.find("90\n3\n70\n1\n10\n1\n20\n0\n10\n1\n20\n1\n10\n0\n20\n1\n") !=
            std::string::npos,
        "closed polyline retains only caller-supplied vertices and closure flag");
}

void test_determinism_provenance_and_unicode_sidecar() {
  Snapshot first = snapshot();
  Snapshot second = first;
  std::swap(second.primitives[0], second.primitives[1]);
  std::swap(second.annotations.annotations[0], second.annotations.annotations[2]);
  const auto left = hss::dxf::export_snapshot(first);
  const auto right = hss::dxf::export_snapshot(second);
  check(left.status == Status::kOk && right.status == Status::kOk &&
            left.artifact && right.artifact &&
            left.artifact->dxf_ascii == right.artifact->dxf_ascii &&
            left.artifact->provenance_json == right.artifact->provenance_json,
        "entity order cannot change deterministic DXF or companion metadata");
  if (!left.artifact)
    return;
  check(left.artifact->provenance_json.find("\"units\":\"m\"") !=
            std::string::npos &&
            left.artifact->provenance_json.find("\"source_ids\":[\"mesh-7\",\"wall-a\"]") !=
                std::string::npos &&
            left.artifact->provenance_json.find("\"provenance\":\"unknown\"") !=
                std::string::npos &&
            left.artifact->provenance_json.find("\"metric_value_m\":null") !=
                std::string::npos,
        "sidecar retains metre axes, lineage and unknown state");
  const std::string arabic = "غرفة تجريبية";
  check(left.artifact->dxf_ascii.find(arabic) == std::string::npos &&
            left.artifact->provenance_json.find(arabic) != std::string::npos,
        "unportable Unicode stays losslessly in UTF-8 sidecar rather than unsafe AC1015 TEXT");
}

void test_stale_and_unprovenanced_geometry_are_rejected() {
  Snapshot stale = snapshot();
  stale.annotations.scene_revision = "42";
  const auto stale_result = hss::dxf::export_snapshot(stale);
  check(stale_result.status == Status::kRevisionMismatch && !stale_result.artifact,
        "a stale annotation layout cannot export against a newer scene revision");

  Snapshot foreign = snapshot();
  foreign.primitives.front().source_ids = {"missing-source"};
  const auto foreign_result = hss::dxf::export_snapshot(foreign);
  check(foreign_result.status == Status::kInvalidSnapshot && !foreign_result.artifact,
        "geometry with no retained plan lineage cannot be exported");
}

} // namespace

int main() {
  test_ac1015_metre_layers_and_exact_geometry();
  test_determinism_provenance_and_unicode_sidecar();
  test_stale_and_unprovenanced_geometry_are_rejected();
  if (failures != 0) {
    std::cerr << failures << " DXF exporter test(s) failed\n";
    return 1;
  }
  std::cout << "DXF exporter tests passed\n";
  return 0;
}

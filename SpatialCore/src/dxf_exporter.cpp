#include "hss/dxf_exporter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace hss::dxf {
namespace {

Result failure(Status status) { return {status, std::nullopt}; }

bool usable_id(std::string_view value) {
  return !value.empty() && value.size() <= 1'024U;
}

bool is_ascii_text(std::string_view value) {
  return !value.empty() && value.size() <= 255U &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return c >= 0x20U && c <= 0x7eU;
         });
}

bool valid_options(const Options &options) {
  return options.max_primitives > 0U &&
         options.max_primitives <= kHardMaxPrimitives &&
         options.max_vertices > 0U && options.max_vertices <= kHardMaxVertices &&
         options.max_warnings <= kHardMaxWarnings &&
         std::isfinite(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > 0.0;
}

bool valid_layer(Layer value) {
  return value == Layer::kBoundary || value == Layer::kFixed ||
         value == Layer::kOpenings || value == Layer::kFurniture ||
         value == Layer::kOverhead || value == Layer::kDimensions ||
         value == Layer::kLabels || value == Layer::kWarnings;
}

bool valid_provenance(Provenance value) {
  return value == Provenance::kObserved || value == Provenance::kFitted ||
         value == Provenance::kUserMeasured || value == Provenance::kInferred ||
         value == Provenance::kUnknown;
}

bool finite_point(const area::Point2d point, const Options &options) {
  return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
         std::fabs(point.x_m) <= options.max_abs_coordinate_m &&
         std::fabs(point.y_m) <= options.max_abs_coordinate_m;
}

std::string number(double value) {
  if (value == 0.0)
    value = 0.0; // Canonicalize negative zero before serializing.
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(17) << value;
  return output.str();
}

void pair(std::string &output, int code, std::string_view value) {
  output.append(std::to_string(code));
  output.push_back('\n');
  output.append(value);
  output.push_back('\n');
}

void pair(std::string &output, int code, int value) {
  pair(output, code, std::to_string(value));
}

void pair(std::string &output, int code, double value) {
  pair(output, code, number(value));
}

void polyline(std::string &output, const Primitive &primitive) {
  pair(output, 0, "LWPOLYLINE");
  pair(output, 100, "AcDbEntity");
  // Do not make uncertain geometry look like a clean metric boundary merely
  // because a caller selected a conventional architectural layer.
  const Layer effective_layer =
      primitive.provenance == Provenance::kInferred ||
              primitive.provenance == Provenance::kUnknown
          ? Layer::kWarnings
          : primitive.layer;
  pair(output, 8, layer_name(effective_layer));
  pair(output, 100, "AcDbPolyline");
  pair(output, 90, static_cast<int>(primitive.vertices_m.size()));
  pair(output, 70, primitive.closed ? 1 : 0);
  for (const area::Point2d point : primitive.vertices_m) {
    pair(output, 10, point.x_m);
    pair(output, 20, point.y_m);
  }
}

void line(std::string &output, Layer layer, area::Point2d first,
          area::Point2d second) {
  pair(output, 0, "LINE");
  pair(output, 100, "AcDbEntity");
  pair(output, 8, layer_name(layer));
  pair(output, 100, "AcDbLine");
  pair(output, 10, first.x_m);
  pair(output, 20, first.y_m);
  pair(output, 30, 0.0);
  pair(output, 11, second.x_m);
  pair(output, 21, second.y_m);
  pair(output, 31, 0.0);
}

void text(std::string &output, Layer layer, area::Point2d position,
          std::string_view value) {
  pair(output, 0, "TEXT");
  pair(output, 100, "AcDbEntity");
  pair(output, 8, layer_name(layer));
  pair(output, 100, "AcDbText");
  pair(output, 10, position.x_m);
  pair(output, 20, position.y_m);
  pair(output, 30, 0.0);
  pair(output, 40, 0.1);
  pair(output, 1, value);
}

std::string json_escape(std::string_view value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  for (unsigned char c : value) {
    switch (c) {
    case '"': output << "\\\""; break;
    case '\\': output << "\\\\"; break;
    case '\b': output << "\\b"; break;
    case '\f': output << "\\f"; break;
    case '\n': output << "\\n"; break;
    case '\r': output << "\\r"; break;
    case '\t': output << "\\t"; break;
    default:
      if (c < 0x20U) {
        output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(c) << std::dec << std::setfill(' ');
      } else {
        output << static_cast<char>(c);
      }
    }
  }
  return output.str();
}

void json_string(std::string &output, std::string_view value) {
  output.push_back('"');
  output.append(json_escape(value));
  output.push_back('"');
}

const char *availability_name(annotation_geometry::Availability value) {
  switch (value) {
  case annotation_geometry::Availability::kReady: return "ready";
  case annotation_geometry::Availability::kProvisional: return "provisional";
  case annotation_geometry::Availability::kUnavailable: return "unavailable";
  }
  return "unavailable";
}

const char *annotation_kind_name(annotation_geometry::Kind value) {
  switch (value) {
  case annotation_geometry::Kind::kLength: return "length";
  case annotation_geometry::Kind::kAngle: return "angle";
  case annotation_geometry::Kind::kHeight: return "height";
  case annotation_geometry::Kind::kLabel: return "label";
  }
  return "unknown";
}

const char *annotation_source_name(annotation_geometry::EvidenceSource value) {
  switch (value) {
  case annotation_geometry::EvidenceSource::kObserved: return "observed";
  case annotation_geometry::EvidenceSource::kFitted: return "fitted";
  case annotation_geometry::EvidenceSource::kUserMeasured: return "user_measured";
  case annotation_geometry::EvidenceSource::kInferred: return "inferred";
  case annotation_geometry::EvidenceSource::kUnknown: return "unknown";
  }
  return "unknown";
}

std::set<std::string> lineage_ids(const plan_semantics::PlanLayer &plan);

bool valid_annotation(const annotation_geometry::ResolvedAnnotation &resolved,
                      const Snapshot &snapshot, const Options &options) {
  const auto &annotation = resolved.annotation;
  if (!usable_id(annotation.id) || annotation.source_revision != snapshot.plan.scene_revision ||
      annotation.anchors.empty() ||
      !finite_point(annotation.anchors.front().position_m, options))
    return false;
  const size_t expected_anchors = annotation.kind == annotation_geometry::Kind::kAngle
                                      ? 3U
                                      : annotation.kind == annotation_geometry::Kind::kLabel
                                            ? 1U
                                            : 2U;
  if (annotation.anchors.size() != expected_anchors)
    return false;
  const std::set<std::string> lineage = lineage_ids(snapshot.plan);
  for (const auto &anchor : annotation.anchors) {
    if (!usable_id(anchor.geometry_id) || !lineage.contains(anchor.geometry_id) ||
        !finite_point(anchor.position_m, options))
      return false;
  }
  if (annotation.kind != annotation_geometry::Kind::kLabel &&
      (annotation.evidence_source == annotation_geometry::EvidenceSource::kObserved ||
       annotation.evidence_source == annotation_geometry::EvidenceSource::kFitted) &&
      (annotation.evidence_ids.empty() ||
       !std::all_of(annotation.evidence_ids.begin(), annotation.evidence_ids.end(),
                    [&lineage](const std::string &id) {
                      return usable_id(id) && lineage.contains(id);
                    })))
    return false;
  if (resolved.metric_value && (!std::isfinite(*resolved.metric_value) ||
                                *resolved.metric_value < 0.0))
    return false;
  if ((annotation.evidence_source == annotation_geometry::EvidenceSource::kUnknown ||
       annotation.evidence_source == annotation_geometry::EvidenceSource::kInferred) &&
      resolved.metric_value)
    return false;
  return true;
}

std::set<std::string> lineage_ids(const plan_semantics::PlanLayer &plan) {
  std::set<std::string> result;
  for (const auto &fragment : plan.fragments) {
    result.insert(fragment.id);
    result.insert(fragment.support_ids.begin(), fragment.support_ids.end());
  }
  return result;
}

bool valid_snapshot(const Snapshot &snapshot, const Options &options) {
  if (!usable_id(snapshot.plan.scene_revision) ||
      snapshot.annotations.scene_revision != snapshot.plan.scene_revision ||
      snapshot.primitives.size() > options.max_primitives ||
      snapshot.warnings.size() > options.max_warnings)
    return false;
  const std::set<std::string> lineage = lineage_ids(snapshot.plan);
  std::unordered_set<std::string> primitive_ids;
  size_t vertex_count = 0U;
  for (const Primitive &primitive : snapshot.primitives) {
    if (!usable_id(primitive.id) || !primitive_ids.insert(primitive.id).second ||
        primitive.source_revision != snapshot.plan.scene_revision ||
        !valid_layer(primitive.layer) || !valid_provenance(primitive.provenance) ||
        primitive.source_ids.empty() || primitive.vertices_m.size() < 2U ||
        (primitive.closed && primitive.vertices_m.size() < 3U) ||
        primitive.vertices_m.size() > options.max_vertices ||
        vertex_count > options.max_vertices - primitive.vertices_m.size())
      return false;
    vertex_count += primitive.vertices_m.size();
    for (const std::string &source : primitive.source_ids) {
      if (!usable_id(source) || !lineage.contains(source))
        return false;
    }
    if (std::set<std::string>(primitive.source_ids.begin(), primitive.source_ids.end()).size() !=
        primitive.source_ids.size())
      return false;
    for (const area::Point2d point : primitive.vertices_m) {
      if (!finite_point(point, options))
        return false;
    }
  }
  std::unordered_set<std::string> annotation_ids;
  for (const auto &annotation : snapshot.annotations.annotations) {
    if (!annotation_ids.insert(annotation.annotation.id).second ||
        !valid_annotation(annotation, snapshot, options))
      return false;
  }
  return std::all_of(snapshot.warnings.begin(), snapshot.warnings.end(), usable_id);
}

std::string annotation_text(const annotation_geometry::ResolvedAnnotation &resolved) {
  const auto &annotation = resolved.annotation;
  if (annotation.kind == annotation_geometry::Kind::kLabel)
    return annotation.label;
  if (!resolved.metric_value)
    return "UNKNOWN";
  return number(*resolved.metric_value) + " m";
}

void append_json_string_array(std::string &output,
                              const std::vector<std::string> &values) {
  output.push_back('[');
  for (size_t index = 0U; index < values.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    json_string(output, values[index]);
  }
  output.push_back(']');
}

std::vector<std::string> sorted_strings(std::vector<std::string> values) {
  std::sort(values.begin(), values.end());
  return values;
}

std::string sidecar(const Snapshot &snapshot,
                    const std::vector<Primitive> &primitives) {
  std::string output;
  output.reserve(4'096U);
  output.append("{\"format\":\"HSS-DXF-PROVENANCE-1\",\"acad_version\":\"");
  output.append(kAcadVersion);
  output.append("\",\"units\":\"m\",\"axes\":\"plan X=U,Y=V; V=-Z in the canonical floor frame\",\"scene_revision\":");
  json_string(output, snapshot.plan.scene_revision);
  output.append(",\"view\":");
  switch (snapshot.plan.view) {
  case plan_semantics::View::kFloorFootprint: json_string(output, "floor_footprint"); break;
  case plan_semantics::View::kArchitecturalCut: json_string(output, "architectural_cut"); break;
  case plan_semantics::View::kReflectedCeiling: json_string(output, "reflected_ceiling"); break;
  }
  output.append(",\"quality\":");
  switch (snapshot.plan.validity) {
  case plan_semantics::Availability::kReady: json_string(output, "ready"); break;
  case plan_semantics::Availability::kProvisional: json_string(output, "provisional"); break;
  case plan_semantics::Availability::kUnavailable: json_string(output, "unavailable"); break;
  case plan_semantics::Availability::kUnsupported: json_string(output, "unsupported"); break;
  case plan_semantics::Availability::kNotApplicable: json_string(output, "not_applicable"); break;
  }
  output.append(",\"source_is_synthetic\":");
  output.append(snapshot.plan.source_is_synthetic ? "true" : "false");
  output.append(",\"cut_height_m\":");
  if (snapshot.plan.height.cut_height_m)
    output.append(number(*snapshot.plan.height.cut_height_m));
  else
    output.append("null");
  output.append(",\"warnings\":");
  append_json_string_array(output, sorted_strings(snapshot.warnings));
  output.append(",\"plan_fragments\":[");
  std::vector<const plan_semantics::Fragment *> fragments;
  fragments.reserve(snapshot.plan.fragments.size());
  for (const auto &fragment : snapshot.plan.fragments)
    fragments.push_back(&fragment);
  std::sort(fragments.begin(), fragments.end(), [](const auto *left,
                                                   const auto *right) {
    return left->id < right->id;
  });
  for (size_t index = 0U; index < fragments.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    const auto &fragment = *fragments[index];
    output.append("{\"id\":");
    json_string(output, fragment.id);
    output.append(",\"source_revision\":");
    json_string(output, fragment.source_revision);
    output.append(",\"evidence_state\":");
    switch (fragment.state) {
    case plan_semantics::EvidenceState::kSupported: json_string(output, "supported"); break;
    case plan_semantics::EvidenceState::kUnknown: json_string(output, "unknown"); break;
    case plan_semantics::EvidenceState::kUnsupported: json_string(output, "unsupported"); break;
    }
    output.append(",\"support_ids\":");
    append_json_string_array(output, sorted_strings(fragment.support_ids));
    output.append(",\"has_explicit_segment\":");
    output.append(fragment.segment ? "true" : "false");
    output.append("}");
  }
  output.append("]");
  output.append(",\"primitives\":[");
  for (size_t primitive_index = 0U; primitive_index < primitives.size(); ++primitive_index) {
    if (primitive_index != 0U)
      output.push_back(',');
    const Primitive &primitive = primitives[primitive_index];
    output.append("{\"id\":");
    json_string(output, primitive.id);
    output.append(",\"requested_layer\":");
    json_string(output, layer_name(primitive.layer));
    output.append(",\"provenance\":");
    json_string(output, provenance_name(primitive.provenance));
    output.append(",\"source_ids\":");
    append_json_string_array(output, sorted_strings(primitive.source_ids));
    output.append(",\"closed\":");
    output.append(primitive.closed ? "true" : "false");
    output.append(",\"vertices_m\":[");
    for (size_t point_index = 0U; point_index < primitive.vertices_m.size(); ++point_index) {
      if (point_index != 0U)
        output.push_back(',');
      const area::Point2d point = primitive.vertices_m[point_index];
      output.append("[");
      output.append(number(point.x_m));
      output.push_back(',');
      output.append(number(point.y_m));
      output.push_back(']');
    }
    output.append("]}");
  }
  output.append("],\"annotations\":[");
  std::vector<annotation_geometry::ResolvedAnnotation> annotations =
      snapshot.annotations.annotations;
  std::sort(annotations.begin(), annotations.end(), [](const auto &left,
                                                         const auto &right) {
    return left.annotation.id < right.annotation.id;
  });
  for (size_t index = 0U; index < annotations.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    const auto &resolved = annotations[index];
    const auto &annotation = resolved.annotation;
    output.append("{\"id\":");
    json_string(output, annotation.id);
    output.append(",\"kind\":");
    json_string(output, annotation_kind_name(annotation.kind));
    output.append(",\"provenance\":");
    json_string(output, annotation_source_name(annotation.evidence_source));
    output.append(",\"availability\":");
    json_string(output, availability_name(resolved.availability));
    output.append(",\"metric_value_m\":");
    if (resolved.metric_value)
      output.append(number(*resolved.metric_value));
    else
      output.append("null");
    output.append(",\"label\":");
    json_string(output, annotation.label);
    output.append("}");
  }
  output.append("]}");
  return output;
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk: return "ok";
  case Status::kInvalidOptions: return "invalid_options";
  case Status::kInvalidSnapshot: return "invalid_snapshot";
  case Status::kRevisionMismatch: return "revision_mismatch";
  case Status::kInvalidPrimitive: return "invalid_primitive";
  case Status::kInvalidProvenance: return "invalid_provenance";
  case Status::kInvalidAnnotation: return "invalid_annotation";
  case Status::kResourceLimitExceeded: return "resource_limit_exceeded";
  case Status::kInternalFailure: return "internal_failure";
  }
  return "unknown";
}

const char *layer_name(Layer layer) {
  switch (layer) {
  case Layer::kBoundary: return "HSS_BOUNDARY";
  case Layer::kFixed: return "HSS_FIXED";
  case Layer::kOpenings: return "HSS_OPENINGS";
  case Layer::kFurniture: return "HSS_FURNITURE";
  case Layer::kOverhead: return "HSS_OVERHEAD";
  case Layer::kDimensions: return "HSS_DIMS";
  case Layer::kLabels: return "HSS_LABELS";
  case Layer::kWarnings: return "HSS_WARNINGS";
  }
  return "HSS_WARNINGS";
}

const char *provenance_name(Provenance provenance) {
  switch (provenance) {
  case Provenance::kObserved: return "observed";
  case Provenance::kFitted: return "fitted";
  case Provenance::kUserMeasured: return "user_measured";
  case Provenance::kInferred: return "inferred";
  case Provenance::kUnknown: return "unknown";
  }
  return "unknown";
}

Result export_snapshot(const Snapshot &snapshot, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  if (!usable_id(snapshot.plan.scene_revision) ||
      snapshot.annotations.scene_revision != snapshot.plan.scene_revision)
    return failure(Status::kRevisionMismatch);
  try {
    if (!valid_snapshot(snapshot, options))
      return failure(Status::kInvalidSnapshot);

    std::vector<Primitive> primitives = snapshot.primitives;
    std::sort(primitives.begin(), primitives.end(), [](const Primitive &left,
                                                       const Primitive &right) {
      if (left.layer != right.layer)
        return static_cast<uint8_t>(left.layer) < static_cast<uint8_t>(right.layer);
      return left.id < right.id;
    });

    Artifact artifact;
    artifact.dxf_ascii.reserve(4'096U);
    pair(artifact.dxf_ascii, 0, "SECTION");
    pair(artifact.dxf_ascii, 2, "HEADER");
    pair(artifact.dxf_ascii, 9, "$ACADVER");
    pair(artifact.dxf_ascii, 1, kAcadVersion);
    pair(artifact.dxf_ascii, 9, "$INSUNITS");
    pair(artifact.dxf_ascii, 70, kInsUnitsMetres);
    pair(artifact.dxf_ascii, 0, "ENDSEC");
    pair(artifact.dxf_ascii, 0, "SECTION");
    pair(artifact.dxf_ascii, 2, "ENTITIES");
    pair(artifact.dxf_ascii, 999, "HSS frozen snapshot; metres; companion JSON retains provenance");
    for (const Primitive &primitive : primitives)
      polyline(artifact.dxf_ascii, primitive);

    std::vector<annotation_geometry::ResolvedAnnotation> annotations =
        snapshot.annotations.annotations;
    std::sort(annotations.begin(), annotations.end(), [](const auto &left,
                                                           const auto &right) {
      return left.annotation.id < right.annotation.id;
    });
    for (const auto &resolved : annotations) {
      const auto &annotation = resolved.annotation;
      if (annotation.kind == annotation_geometry::Kind::kLabel) {
        if (options.emit_ascii_text && is_ascii_text(annotation.label))
          text(artifact.dxf_ascii, Layer::kLabels, annotation.anchors.front().position_m,
               annotation.label);
        continue;
      }
      const Layer layer = resolved.metric_value ? Layer::kDimensions : Layer::kWarnings;
      if (annotation.anchors.size() >= 2U)
        line(artifact.dxf_ascii, layer, annotation.anchors[0].position_m,
             annotation.anchors[1].position_m);
      const std::string value = annotation_text(resolved);
      if (options.emit_ascii_text && is_ascii_text(value))
        text(artifact.dxf_ascii, layer, annotation.anchors.front().position_m, value);
    }
    for (const std::string &warning : sorted_strings(snapshot.warnings)) {
      if (options.emit_ascii_text && is_ascii_text(warning))
        text(artifact.dxf_ascii, Layer::kWarnings, {0.0, 0.0}, warning);
    }
    pair(artifact.dxf_ascii, 0, "ENDSEC");
    pair(artifact.dxf_ascii, 0, "EOF");
    artifact.provenance_json = sidecar(snapshot, primitives);
    return {Status::kOk, std::move(artifact)};
  } catch (const std::bad_alloc &) {
    return failure(Status::kResourceLimitExceeded);
  } catch (...) {
    return failure(Status::kInternalFailure);
  }
}

} // namespace hss::dxf

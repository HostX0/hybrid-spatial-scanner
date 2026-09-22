#import "HSSExportBridge.h"

#include "hss/dxf_exporter.hpp"
#include "hss/obj_mtl_exporter.hpp"
#include "hss/pdf_svg_export.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

NSString *const kExportErrorDomain = @"org.hybridspatialscanner.export";

void set_error(NSError **error, NSInteger code, NSString *message) {
  if (error == nullptr)
    return;
  *error = [NSError errorWithDomain:kExportErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

std::string utf8(NSString *value) {
  if (value == nil)
    return {};
  const char *bytes = value.UTF8String;
  return bytes == nullptr ? std::string() : std::string(bytes);
}

NSString *string_value(id value) {
  return [value isKindOfClass:NSString.class] ? value : nil;
}

NSDictionary *dictionary_value(id value) {
  return [value isKindOfClass:NSDictionary.class] ? value : nil;
}

NSArray *array_value(id value) {
  return [value isKindOfClass:NSArray.class] ? value : nil;
}

bool finite_number(id value, double *result) {
  if (![value isKindOfClass:NSNumber.class])
    return false;
  const double number = [value doubleValue];
  if (!std::isfinite(number))
    return false;
  *result = number;
  return true;
}

bool point3(id value, std::array<double, 3U> *result) {
  NSArray *values = array_value(value);
  if (values == nil || values.count != 3U)
    return false;
  return finite_number(values[0], &(*result)[0]) &&
         finite_number(values[1], &(*result)[1]) &&
         finite_number(values[2], &(*result)[2]);
}

std::vector<std::string> string_array(id value) {
  std::vector<std::string> result;
  NSArray *values = array_value(value);
  if (values == nil)
    return result;
  result.reserve(values.count);
  for (id item in values) {
    NSString *text = string_value(item);
    if (text == nil || text.length == 0U)
      return {};
    result.push_back(utf8(text));
  }
  return result;
}

hss::area::GeometryOrigin geometry_origin(NSString *value) {
  if ([value isEqualToString:@"observed"])
    return hss::area::GeometryOrigin::kObserved;
  if ([value isEqualToString:@"fitted"])
    return hss::area::GeometryOrigin::kFitted;
  if ([value isEqualToString:@"user_measured"])
    return hss::area::GeometryOrigin::kUserMeasured;
  if ([value isEqualToString:@"inferred"])
    return hss::area::GeometryOrigin::kInferred;
  return hss::area::GeometryOrigin::kUnknown;
}

bool has_metric_authority(hss::area::GeometryOrigin origin) {
  return origin == hss::area::GeometryOrigin::kObserved ||
         origin == hss::area::GeometryOrigin::kFitted ||
         origin == hss::area::GeometryOrigin::kUserMeasured;
}

hss::dxf::Provenance dxf_provenance(hss::area::GeometryOrigin origin) {
  switch (origin) {
  case hss::area::GeometryOrigin::kObserved: return hss::dxf::Provenance::kObserved;
  case hss::area::GeometryOrigin::kFitted: return hss::dxf::Provenance::kFitted;
  case hss::area::GeometryOrigin::kUserMeasured: return hss::dxf::Provenance::kUserMeasured;
  case hss::area::GeometryOrigin::kInferred: return hss::dxf::Provenance::kInferred;
  case hss::area::GeometryOrigin::kUnknown: return hss::dxf::Provenance::kUnknown;
  }
  return hss::dxf::Provenance::kUnknown;
}

NSData *data(const std::string &value) {
  return [NSData dataWithBytes:value.data() length:value.size()];
}

} // namespace

@interface HSSExportArtifacts ()
@property(nonatomic, readwrite) NSData *pdfData;
@property(nonatomic, readwrite) NSData *svgData;
@property(nonatomic, readwrite) NSData *pdfSVGManifestData;
@property(nonatomic, readwrite) NSData *dxfData;
@property(nonatomic, readwrite) NSData *dxfManifestData;
@property(nonatomic, readwrite) NSData *objData;
@property(nonatomic, readwrite) NSData *mtlData;
@property(nonatomic, readwrite) NSData *objManifestData;
@end

@implementation HSSExportArtifacts
@end

@implementation HSSExportBridge

+ (HSSExportArtifacts *)buildArtifactsForProjectID:(NSString *)projectID
                                     sceneRevision:(uint64_t)sceneRevision
                                          sceneJSON:(NSData *)sceneJSON
                                         verticesXYZ:(NSData *)verticesXYZ
                                      triangleIndices:(NSData *)triangleIndices
                                              error:(NSError **)error {
  if (sceneRevision == 0U || projectID.length == 0U || sceneJSON.length == 0U ||
      verticesXYZ.length == 0U || verticesXYZ.length % (3U * sizeof(double)) != 0U ||
      triangleIndices.length == 0U || triangleIndices.length % (3U * sizeof(uint32_t)) != 0U) {
    set_error(error, 1, @"The frozen scene is incomplete or has invalid topology.");
    return nil;
  }

  NSError *jsonError = nil;
  id decoded = [NSJSONSerialization JSONObjectWithData:sceneJSON options:0 error:&jsonError];
  NSDictionary *scene = dictionary_value(decoded);
  if (scene == nil) {
    set_error(error, 2, jsonError.localizedDescription ?: @"The accepted scene JSON is invalid.");
    return nil;
  }
  NSString *sceneProjectID = string_value(scene[@"project_id"]);
  NSString *revision = string_value(scene[@"scene_revision"]);
  if (sceneProjectID == nil || ![sceneProjectID isEqualToString:projectID] ||
      revision == nil || revision.longLongValue <= 0 ||
      static_cast<uint64_t>(revision.longLongValue) != sceneRevision) {
    set_error(error, 3, @"The selected project and accepted scene revision do not match.");
    return nil;
  }

  std::map<std::string, std::array<double, 3U>> nodes;
  NSArray *sceneNodes = array_value(scene[@"nodes"]);
  for (id item in sceneNodes ?: @[]) {
    NSDictionary *node = dictionary_value(item);
    NSString *identifier = string_value(node[@"id"]);
    std::array<double, 3U> position{};
    if (identifier == nil || !point3(node[@"position_m"], &position) ||
        !nodes.emplace(utf8(identifier), position).second) {
      set_error(error, 4, @"The accepted scene contains an invalid plan node.");
      return nil;
    }
  }

  NSDictionary *basis = dictionary_value(scene[@"basis"]);
  std::array<double, 3U> origin{}, axisU{}, axisV{};
  if (basis == nil || !point3(basis[@"floor_origin_m"], &origin) ||
      !point3(basis[@"floor_u"], &axisU) || !point3(basis[@"floor_v"], &axisV)) {
    set_error(error, 5, @"The accepted scene has no finite floor coordinate basis.");
    return nil;
  }
  auto floor_point = [&](const std::array<double, 3U> &point) {
    const std::array<double, 3U> relative{point[0] - origin[0], point[1] - origin[1],
                                         point[2] - origin[2]};
    return hss::area::Point2d{
        relative[0] * axisU[0] + relative[1] * axisU[1] + relative[2] * axisU[2],
        relative[0] * axisV[0] + relative[1] * axisV[1] + relative[2] * axisV[2]};
  };

  hss::plan_semantics::PlanLayer plan;
  plan.scene_revision = utf8(revision);
  plan.source_is_synthetic = [scene[@"source_is_synthetic"] boolValue];
  plan.view = hss::plan_semantics::View::kArchitecturalCut;
  plan.boundary_state = hss::plan_semantics::BoundaryState::kOpen;
  plan.validity = hss::plan_semantics::Availability::kProvisional;

  hss::dxf::Snapshot dxfSnapshot;
  NSArray *sceneEdges = array_value(scene[@"edges"]);
  for (id item in sceneEdges ?: @[]) {
    NSDictionary *edge = dictionary_value(item);
    NSString *identifier = string_value(edge[@"id"]);
    NSString *startID = string_value(edge[@"start_node_id"]);
    NSString *endID = string_value(edge[@"end_node_id"]);
    NSDictionary *provenance = dictionary_value(edge[@"provenance"]);
    NSString *originName = string_value(provenance[@"geometry_origin"]);
    if (identifier == nil || startID == nil || endID == nil || provenance == nil ||
        originName == nil) {
      set_error(error, 6, @"The accepted scene contains an invalid plan edge.");
      return nil;
    }
    const auto start = nodes.find(utf8(startID));
    const auto end = nodes.find(utf8(endID));
    if (start == nodes.end() || end == nodes.end()) {
      set_error(error, 7, @"A plan edge references a missing accepted node.");
      return nil;
    }
    const hss::area::GeometryOrigin source = geometry_origin(originName);
    hss::plan_semantics::Fragment fragment;
    fragment.id = utf8(identifier);
    fragment.source_revision = plan.scene_revision;
    fragment.geometry_origin = source;
    fragment.state = has_metric_authority(source)
                         ? hss::plan_semantics::EvidenceState::kSupported
                         : hss::plan_semantics::EvidenceState::kUnknown;
    fragment.kind = hss::plan_semantics::EvidenceKind::kUnknownBoundary;
    fragment.support_ids = string_array(provenance[@"support_ids"]);
    if (has_metric_authority(source)) {
      const auto a = floor_point(start->second);
      const auto b = floor_point(end->second);
      if (a == b) {
        set_error(error, 8, @"The accepted plan contains a zero-length measured edge.");
        return nil;
      }
      fragment.segment = hss::plan_semantics::Segment2d{a, b};
      hss::dxf::Primitive primitive;
      primitive.id = fragment.id;
      primitive.source_revision = plan.scene_revision;
      primitive.layer = hss::dxf::Layer::kBoundary;
      primitive.provenance = dxf_provenance(source);
      primitive.source_ids = fragment.support_ids.empty()
                                 ? std::vector<std::string>{fragment.id}
                                 : fragment.support_ids;
      primitive.vertices_m = {a, b};
      dxfSnapshot.primitives.push_back(std::move(primitive));
    }
    plan.fragments.push_back(std::move(fragment));
  }
  if (dxfSnapshot.primitives.empty()) {
    set_error(error, 9, @"This revision has no observed, fitted, or user-measured plan geometry to export.");
    return nil;
  }

  hss::annotation_geometry::Layout annotations;
  annotations.scene_revision = plan.scene_revision;
  dxfSnapshot.plan = plan;
  dxfSnapshot.annotations = annotations;
  if (plan.source_is_synthetic)
    dxfSnapshot.warnings.push_back("synthetic_source");

  hss::pdf_svg_export::Input drawingInput;
  drawingInput.project_id = utf8(projectID);
  drawingInput.plan = plan;
  drawingInput.annotations = annotations;
  if (plan.source_is_synthetic)
    drawingInput.caller_warnings.push_back("Synthetic source; not physical-device evidence.");
  const auto drawing = hss::pdf_svg_export::build(drawingInput);
  if (drawing.status != hss::pdf_svg_export::Status::kOk || !drawing.artifact) {
    set_error(error, 10,
              [NSString stringWithFormat:@"PDF/SVG export failed: %s",
                                         hss::pdf_svg_export::status_name(drawing.status)]);
    return nil;
  }
  const auto dxf = hss::dxf::export_snapshot(dxfSnapshot);
  if (dxf.status != hss::dxf::Status::kOk || !dxf.artifact) {
    set_error(error, 11,
              [NSString stringWithFormat:@"DXF export failed: %s",
                                         hss::dxf::status_name(dxf.status)]);
    return nil;
  }

  hss::obj_mtl::Snapshot mesh;
  mesh.export_id = "export-" + std::to_string(sceneRevision);
  mesh.project_id = utf8(projectID);
  mesh.scene_revision = sceneRevision;
  mesh.captured_mesh_id = "accepted-mesh-" + std::to_string(sceneRevision);
  mesh.source_is_synthetic = plan.source_is_synthetic;
  NSString *qualityState = string_value(scene[@"quality_state"]);
  mesh.quality_state = qualityState.length == 0U ? "provisional" : utf8(qualityState);
  const size_t vertexCount = verticesXYZ.length / (3U * sizeof(double));
  const double *vertexBytes = static_cast<const double *>(verticesXYZ.bytes);
  mesh.vertices_m.reserve(vertexCount);
  for (size_t index = 0U; index < vertexCount; ++index) {
    mesh.vertices_m.push_back(
        {vertexBytes[index * 3U], vertexBytes[index * 3U + 1U], vertexBytes[index * 3U + 2U]});
  }
  const size_t faceCount = triangleIndices.length / (3U * sizeof(uint32_t));
  const uint32_t *indexBytes = static_cast<const uint32_t *>(triangleIndices.bytes);
  mesh.faces.reserve(faceCount);
  for (size_t index = 0U; index < faceCount; ++index) {
    mesh.faces.push_back({"face-" + std::to_string(index),
                          {indexBytes[index * 3U], indexBytes[index * 3U + 1U],
                           indexBytes[index * 3U + 2U]},
                          {mesh.captured_mesh_id}, "", std::nullopt});
  }
  if (mesh.source_is_synthetic)
    mesh.warnings.push_back("synthetic_source");
  hss::obj_mtl::Options meshOptions;
  meshOptions.include_textures = false;
  const auto obj = hss::obj_mtl::export_snapshot(mesh, meshOptions);
  if (obj.status != hss::obj_mtl::Status::kOk || !obj.artifact) {
    set_error(error, 12,
              [NSString stringWithFormat:@"OBJ/MTL export failed: %s",
                                         hss::obj_mtl::status_name(obj.status)]);
    return nil;
  }

  HSSExportArtifacts *result = [HSSExportArtifacts new];
  result.pdfData = data(drawing.artifact->pdf);
  result.svgData = data(drawing.artifact->svg);
  result.pdfSVGManifestData = data(drawing.artifact->manifest_json);
  result.dxfData = data(dxf.artifact->dxf_ascii);
  result.dxfManifestData = data(dxf.artifact->provenance_json);
  result.objData = data(obj.artifact->obj_utf8);
  result.mtlData = data(obj.artifact->mtl_utf8);
  result.objManifestData = data(obj.artifact->manifest_json);
  return result;
}

@end

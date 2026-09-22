#include "hss/surface_separation.hpp"

#include <cmath>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hss::surface_policy {
namespace {

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool valid_options(const Options &options) {
  return options.max_entities > 0U &&
         options.max_entities <= kHardMaxEntities &&
         options.max_surfaces > 0U &&
         options.max_surfaces <= kHardMaxSurfaces &&
         options.max_support_ids_per_surface > 0U &&
         options.max_support_ids_per_surface <=
             kHardMaxSupportIdsPerSurface &&
         options.max_geometry_reference_bytes > 0U &&
         options.max_geometry_reference_bytes <=
             kHardMaxGeometryReferenceBytes &&
         finite_positive(options.unit_normal_tolerance) &&
         options.unit_normal_tolerance < 1.0;
}

bool lowercase_hex(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f');
}

bool valid_uuid(std::string_view id) {
  if (id.size() != 36U)
    return false;
  bool nonzero = false;
  for (size_t index = 0U; index < id.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (id[index] != '-')
        return false;
      continue;
    }
    if (!lowercase_hex(id[index]))
      return false;
    nonzero = nonzero || id[index] != '0';
  }
  const bool supported_version = id[14] >= '1' && id[14] <= '8';
  const bool rfc_variant = id[19] == '8' || id[19] == '9' ||
                           id[19] == 'a' || id[19] == 'b';
  return nonzero && supported_version && rfc_variant;
}

bool valid_surface_kind(SurfaceKind value) {
  switch (value) {
  case SurfaceKind::kPlane:
  case SurfaceKind::kMeshPatch:
    return true;
  }
  return false;
}

bool valid_evidence_role(EvidenceRole value) {
  switch (value) {
  case EvidenceRole::kStructural:
  case EvidenceRole::kObject:
    return true;
  }
  return false;
}

bool valid_geometry_origin(GeometryOrigin value) {
  switch (value) {
  case GeometryOrigin::kObserved:
  case GeometryOrigin::kFitted:
  case GeometryOrigin::kUserMeasured:
  case GeometryOrigin::kInferred:
  case GeometryOrigin::kUnknown:
    return true;
  }
  return false;
}

bool supported_geometry_origin(GeometryOrigin value) {
  return value == GeometryOrigin::kObserved ||
         value == GeometryOrigin::kFitted ||
         value == GeometryOrigin::kUserMeasured;
}

bool valid_semantic_class(SemanticClass value) {
  switch (value) {
  case SemanticClass::kUnknown:
  case SemanticClass::kWall:
  case SemanticClass::kFloor:
  case SemanticClass::kCeiling:
  case SemanticClass::kShelf:
  case SemanticClass::kFurniture:
    return true;
  }
  return false;
}

bool valid_semantic_origin(SemanticOrigin value) {
  switch (value) {
  case SemanticOrigin::kPlatform:
  case SemanticOrigin::kLocalModel:
  case SemanticOrigin::kUser:
  case SemanticOrigin::kImported:
  case SemanticOrigin::kUnknown:
    return true;
  }
  return false;
}

bool valid_face(Face value) {
  switch (value) {
  case Face::kUnspecified:
  case Face::kTop:
  case Face::kFront:
  case Face::kUnderside:
  case Face::kRear:
  case Face::kSupport:
    return true;
  }
  return false;
}

bool valid_floor_support(FloorSupport value) {
  switch (value) {
  case FloorSupport::kNone:
  case FloorSupport::kObserved:
  case FloorSupport::kFitted:
  case FloorSupport::kUserMeasured:
  case FloorSupport::kAmbiguous:
    return true;
  }
  return false;
}

bool valid_area_policy(AreaPolicy value) {
  return value == AreaPolicy::kPreserveFloorFootprint ||
         value == AreaPolicy::kNetFixedObstructions;
}

bool structural_semantic(SemanticClass value) {
  return value == SemanticClass::kWall || value == SemanticClass::kFloor ||
         value == SemanticClass::kCeiling;
}

bool object_semantic(SemanticClass value) {
  return value == SemanticClass::kShelf ||
         value == SemanticClass::kFurniture;
}

bool has_floor_support(FloorSupport value) {
  return value == FloorSupport::kObserved ||
         value == FloorSupport::kFitted ||
         value == FloorSupport::kUserMeasured;
}

Status validate_surface(const SurfaceEvidence &surface,
                        const Options &options) {
  if (!valid_uuid(surface.id))
    return Status::kInvalidId;
  if (!valid_surface_kind(surface.kind) ||
      !valid_evidence_role(surface.evidence_role) ||
      !valid_geometry_origin(surface.geometry_origin) ||
      !valid_face(surface.face)) {
    return Status::kUnsupportedValue;
  }
  if (!supported_geometry_origin(surface.geometry_origin))
    return Status::kMissingEvidence;
  if (surface.geometry_reference.empty() ||
      surface.geometry_reference.size() > options.max_geometry_reference_bytes ||
      surface.geometry_reference.find('\0') != std::string::npos) {
    return Status::kInvalidGeometry;
  }
  if (surface.support_ids.empty())
    return Status::kMissingEvidence;
  if (surface.support_ids.size() > options.max_support_ids_per_surface)
    return Status::kResourceLimitExceeded;
  std::unordered_set<std::string_view> support_ids;
  support_ids.reserve(surface.support_ids.size());
  for (const std::string &id : surface.support_ids) {
    if (!valid_uuid(id))
      return Status::kInvalidId;
    if (!support_ids.insert(id).second)
      return Status::kDuplicateId;
  }
  if (surface.kind == SurfaceKind::kMeshPatch) {
    return surface.plane ? Status::kInvalidGeometry : Status::kOk;
  }
  if (!surface.plane)
    return Status::kInvalidGeometry;
  const Plane &plane = *surface.plane;
  for (const double component : plane.normal_unit) {
    if (!std::isfinite(component))
      return Status::kNonFiniteInput;
  }
  if (!std::isfinite(plane.offset_m))
    return Status::kNonFiniteInput;
  const double length = std::hypot(plane.normal_unit[0], plane.normal_unit[1],
                                   plane.normal_unit[2]);
  if (!std::isfinite(length))
    return Status::kNonFiniteInput;
  if (std::abs(length - 1.0) > options.unit_normal_tolerance)
    return Status::kInvalidGeometry;
  return Status::kOk;
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kResourceLimitExceeded:
    return "resource_limit_exceeded";
  case Status::kInvalidId:
    return "invalid_id";
  case Status::kDuplicateId:
    return "duplicate_id";
  case Status::kUnsupportedValue:
    return "unsupported_value";
  case Status::kNonFiniteInput:
    return "nonfinite_input";
  case Status::kInvalidGeometry:
    return "invalid_geometry";
  case Status::kMissingEvidence:
    return "missing_evidence";
  case Status::kAmbiguousInput:
    return "ambiguous_input";
  }
  return "unknown";
}

Result separate(const Request &request, const Options &options) {
  if (!valid_options(options) || !valid_area_policy(request.area_policy))
    return {Status::kInvalidOptions, std::nullopt};
  if (request.entities.empty())
    return {Status::kMissingEvidence, std::nullopt};
  if (request.entities.size() > options.max_entities)
    return {Status::kResourceLimitExceeded, std::nullopt};

  size_t surface_count = 0U;
  std::unordered_set<std::string_view> entity_ids;
  std::unordered_set<std::string_view> surface_ids;
  entity_ids.reserve(request.entities.size());
  for (const EntityCandidate &entity : request.entities) {
    if (!valid_uuid(entity.id))
      return {Status::kInvalidId, std::nullopt};
    if (!entity_ids.insert(entity.id).second)
      return {Status::kDuplicateId, std::nullopt};
    if (!valid_semantic_class(entity.semantic_class) ||
        !valid_semantic_origin(entity.semantic_origin) ||
        !valid_floor_support(entity.floor_support)) {
      return {Status::kUnsupportedValue, std::nullopt};
    }
    if (entity.floor_support == FloorSupport::kAmbiguous)
      return {Status::kAmbiguousInput, std::nullopt};
    if (entity.surfaces.empty())
      return {Status::kMissingEvidence, std::nullopt};
    if (entity.surfaces.size() > options.max_surfaces - surface_count)
      return {Status::kResourceLimitExceeded, std::nullopt};
    surface_count += entity.surfaces.size();

    const EvidenceRole entity_role = entity.surfaces.front().evidence_role;
    for (const SurfaceEvidence &surface : entity.surfaces) {
      const Status surface_status = validate_surface(surface, options);
      if (surface_status != Status::kOk)
        return {surface_status, std::nullopt};
      if (surface.evidence_role != entity_role)
        return {Status::kAmbiguousInput, std::nullopt};
      if (!surface_ids.insert(surface.id).second)
        return {Status::kDuplicateId, std::nullopt};
    }

    if ((entity_role == EvidenceRole::kStructural &&
         object_semantic(entity.semantic_class)) ||
        (entity_role == EvidenceRole::kObject &&
         structural_semantic(entity.semantic_class))) {
      return {Status::kAmbiguousInput, std::nullopt};
    }
    if (entity_role == EvidenceRole::kStructural &&
        (entity.floor_support != FloorSupport::kNone ||
         entity.explicitly_fixed)) {
      return {Status::kAmbiguousInput, std::nullopt};
    }
  }

  Decision decision;
  decision.area_policy = request.area_policy;
  decision.entities.reserve(request.entities.size());
  for (const EntityCandidate &entity : request.entities) {
    EntityDecision entity_decision;
    entity_decision.entity_id = entity.id;
    entity_decision.semantic_class = entity.semantic_class;
    entity_decision.semantic_origin = entity.semantic_origin;
    entity_decision.surfaces = entity.surfaces;

    const EvidenceRole role = entity.surfaces.front().evidence_role;
    if (role == EvidenceRole::kStructural) {
      entity_decision.planning_layer = PlanningLayer::kStructural;
      decision.entities.push_back(std::move(entity_decision));
      continue;
    }

    entity_decision.planning_layer =
        entity.semantic_class == SemanticClass::kShelf
            ? PlanningLayer::kOverhead
            : PlanningLayer::kFurniture;
    if (entity.explicitly_fixed) {
      if (!has_floor_support(entity.floor_support)) {
        entity_decision.fixed_obstruction =
            FixedObstructionDisposition::kMissingFloorSupport;
      } else if (request.area_policy != AreaPolicy::kNetFixedObstructions) {
        entity_decision.fixed_obstruction =
            FixedObstructionDisposition::kExcludedByAreaPolicy;
      } else {
        entity_decision.fixed_obstruction =
            FixedObstructionDisposition::kAccepted;
        entity_decision.planning_layer = PlanningLayer::kFixedObstruction;
      }
    }

    bool has_underside = false;
    bool has_rear = false;
    for (const SurfaceEvidence &surface : entity.surfaces) {
      has_underside = has_underside || surface.face == Face::kUnderside;
      has_rear = has_rear || surface.face == Face::kRear;
    }
    if (!has_underside)
      entity_decision.unknown_faces.push_back(Face::kUnderside);
    if (!has_rear)
      entity_decision.unknown_faces.push_back(Face::kRear);
    decision.entities.push_back(std::move(entity_decision));
  }
  return {Status::kOk, std::move(decision)};
}

} // namespace hss::surface_policy

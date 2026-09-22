#include "hss/surface_separation.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace policy = hss::surface_policy;
using policy::AreaPolicy;
using policy::EntityCandidate;
using policy::EvidenceRole;
using policy::Face;
using policy::FixedObstructionDisposition;
using policy::FloorSupport;
using policy::GeometryOrigin;
using policy::Options;
using policy::PlanningLayer;
using policy::Request;
using policy::SemanticClass;
using policy::SemanticOrigin;
using policy::Status;
using policy::SurfaceEvidence;
using policy::SurfaceKind;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ','))
    fields.push_back(field);
  return fields;
}

SemanticClass semantic_class(std::string_view value) {
  if (value == "wall")
    return SemanticClass::kWall;
  if (value == "shelf")
    return SemanticClass::kShelf;
  throw std::runtime_error("unsupported fixture semantic class");
}

SemanticOrigin semantic_origin(std::string_view value) {
  if (value == "local_model")
    return SemanticOrigin::kLocalModel;
  if (value == "platform")
    return SemanticOrigin::kPlatform;
  throw std::runtime_error("unsupported fixture semantic origin");
}

EvidenceRole evidence_role(std::string_view value) {
  if (value == "structural")
    return EvidenceRole::kStructural;
  if (value == "object")
    return EvidenceRole::kObject;
  throw std::runtime_error("unsupported fixture evidence role");
}

GeometryOrigin geometry_origin(std::string_view value) {
  if (value == "observed")
    return GeometryOrigin::kObserved;
  if (value == "fitted")
    return GeometryOrigin::kFitted;
  throw std::runtime_error("unsupported fixture geometry origin");
}

SurfaceKind surface_kind(std::string_view value) {
  if (value == "plane")
    return SurfaceKind::kPlane;
  if (value == "mesh_patch")
    return SurfaceKind::kMeshPatch;
  throw std::runtime_error("unsupported fixture surface kind");
}

Face face(std::string_view value) {
  if (value == "front")
    return Face::kFront;
  if (value == "top")
    return Face::kTop;
  throw std::runtime_error("unsupported fixture face");
}

Request load_fixture() {
  const std::string path =
      std::string(HSS_SOURCE_DIR) +
      "/fixtures/surface_policy/shelf_parallel_wall.csv";
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("unable to open surface-policy fixture");
  Request request;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    const std::vector<std::string> fields = split(line);
    if (fields[0] == "REQUEST") {
      if (fields.size() != 2U || fields[1] != "net_fixed_obstructions")
        throw std::runtime_error("invalid fixture request row");
      request.area_policy = AreaPolicy::kNetFixedObstructions;
    } else if (fields[0] == "ENTITY") {
      if (fields.size() != 6U || fields[4] != "none")
        throw std::runtime_error("invalid fixture entity row");
      EntityCandidate entity;
      entity.id = fields[1];
      entity.semantic_class = semantic_class(fields[2]);
      entity.semantic_origin = semantic_origin(fields[3]);
      entity.floor_support = FloorSupport::kNone;
      entity.explicitly_fixed = fields[5] == "1";
      request.entities.push_back(std::move(entity));
    } else if (fields[0] == "SURFACE") {
      if (fields.size() != 13U)
        throw std::runtime_error("invalid fixture surface row");
      EntityCandidate *owner = nullptr;
      for (EntityCandidate &entity : request.entities) {
        if (entity.id == fields[1])
          owner = &entity;
      }
      if (owner == nullptr)
        throw std::runtime_error("fixture surface has no entity");
      SurfaceEvidence surface;
      surface.id = fields[2];
      surface.evidence_role = evidence_role(fields[3]);
      surface.geometry_origin = geometry_origin(fields[4]);
      surface.kind = surface_kind(fields[5]);
      surface.face = face(fields[6]);
      surface.geometry_reference = fields[7];
      if (surface.kind == SurfaceKind::kPlane) {
        surface.plane = policy::Plane{{std::stod(fields[8]),
                                       std::stod(fields[9]),
                                       std::stod(fields[10])},
                                      std::stod(fields[11])};
      }
      surface.support_ids.push_back(fields[12]);
      owner->surfaces.push_back(std::move(surface));
    } else {
      throw std::runtime_error("unknown fixture row");
    }
  }
  return request;
}

Options options() {
  Options value;
  value.max_entities = 64U;
  value.max_surfaces = 256U;
  value.max_support_ids_per_surface = 16U;
  value.max_geometry_reference_bytes = 256U;
  value.unit_normal_tolerance = 1.0e-12;
  return value;
}

SurfaceEvidence object_surface() {
  SurfaceEvidence surface;
  surface.id = "92000000-0000-4000-8000-000000000001";
  surface.kind = SurfaceKind::kMeshPatch;
  surface.evidence_role = EvidenceRole::kObject;
  surface.geometry_origin = GeometryOrigin::kObserved;
  surface.face = Face::kFront;
  surface.geometry_reference = "mesh:visible-object";
  surface.support_ids = {"93000000-0000-4000-8000-000000000001"};
  return surface;
}

Request object_request() {
  EntityCandidate entity;
  entity.id = "91000000-0000-4000-8000-000000000001";
  entity.semantic_class = SemanticClass::kFurniture;
  entity.semantic_origin = SemanticOrigin::kUser;
  entity.surfaces = {object_surface()};
  return {AreaPolicy::kNetFixedObstructions, {entity}};
}

void expect_failure(const Request &request, Status expected,
                    const std::string &description,
                    const Options &test_options = options()) {
  const policy::Result result = policy::separate(request, test_options);
  check(result.status == expected,
        description + " returns " + policy::status_name(expected));
  check(!result.decision, description + " exposes no partial decision");
}

void test_fixture_preserves_parallel_wall_and_shelf_evidence() {
  const Request request = load_fixture();
  const policy::Result result = policy::separate(request, options());
  check(result.status == Status::kOk && result.decision.has_value(),
        "synthetic wall/shelf fixture is accepted");
  if (!result.decision)
    return;
  check(result.decision->entities.size() == 2U,
        "wall and shelf remain separate entities");
  const auto &wall = result.decision->entities[0];
  const auto &shelf = result.decision->entities[1];
  check(wall.planning_layer == PlanningLayer::kStructural,
        "structural evidence remains on the structural layer");
  check(shelf.planning_layer == PlanningLayer::kOverhead,
        "unsupported-by-floor shelf stays overhead");
  check(shelf.fixed_obstruction ==
            FixedObstructionDisposition::kNotRequested,
        "shelf does not become a floor obstruction by label");
  check(wall.surfaces == request.entities[0].surfaces &&
            shelf.surfaces == request.entities[1].surfaces,
        "all plane and mesh evidence is copied exactly");
  check(wall.surfaces[0].plane && shelf.surfaces[0].plane &&
            wall.surfaces[0].plane->normal_unit ==
                shelf.surfaces[0].plane->normal_unit &&
            std::abs(wall.surfaces[0].plane->offset_m -
                     shelf.surfaces[0].plane->offset_m) == 0.08,
        "close parallel wall and shelf-front planes are not merged");
  check(shelf.surfaces[1].geometry_reference == "mesh:shelf-visible-top",
        "captured shelf mesh reference persists");
  check(shelf.unknown_faces ==
            std::vector<Face>{Face::kUnderside, Face::kRear},
        "unseen underside and rear remain explicitly unknown");
}

void test_labels_cannot_reclassify_metric_evidence() {
  Request object_as_wall = object_request();
  object_as_wall.entities[0].semantic_class = SemanticClass::kWall;
  expect_failure(object_as_wall, Status::kAmbiguousInput,
                 "wall label over object evidence");

  Request structural_as_shelf = load_fixture();
  structural_as_shelf.entities[0].semantic_class = SemanticClass::kShelf;
  expect_failure(structural_as_shelf, Status::kAmbiguousInput,
                 "shelf label over structural evidence");

  Request unknown_object = object_request();
  unknown_object.entities[0].semantic_class = SemanticClass::kUnknown;
  const auto result = policy::separate(unknown_object, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].planning_layer ==
                PlanningLayer::kFurniture &&
            result.decision->entities[0].surfaces ==
                unknown_object.entities[0].surfaces,
        "unknown semantics preserve object geometry without promoting it");
}

void test_hidden_faces_require_submitted_support() {
  Request request = object_request();
  request.entities[0].semantic_class = SemanticClass::kShelf;
  SurfaceEvidence underside = object_surface();
  underside.id = "92000000-0000-4000-8000-000000000002";
  underside.face = Face::kUnderside;
  underside.geometry_reference = "mesh:observed-underside";
  underside.support_ids = {"93000000-0000-4000-8000-000000000002"};
  request.entities[0].surfaces.push_back(underside);
  const auto result = policy::separate(request, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].unknown_faces ==
                std::vector<Face>{Face::kRear},
        "observed underside clears only the underside unknown");

  Request inferred_rear = request;
  SurfaceEvidence rear = underside;
  rear.id = "92000000-0000-4000-8000-000000000003";
  rear.face = Face::kRear;
  rear.geometry_origin = GeometryOrigin::kInferred;
  rear.geometry_reference = "mesh:invented-rear";
  rear.support_ids = {"93000000-0000-4000-8000-000000000003"};
  inferred_rear.entities[0].surfaces.push_back(std::move(rear));
  expect_failure(inferred_rear, Status::kMissingEvidence,
                 "inferred hidden rear geometry");
}

void test_fixed_obstruction_requires_all_policy_gates() {
  Request accepted = object_request();
  accepted.entities[0].explicitly_fixed = true;
  accepted.entities[0].floor_support = FloorSupport::kObserved;
  auto result = policy::separate(accepted, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].planning_layer ==
                PlanningLayer::kFixedObstruction &&
            result.decision->entities[0].fixed_obstruction ==
                FixedObstructionDisposition::kAccepted,
        "explicit fixed classification, floor support, and net policy accept obstruction");

  Request no_floor = accepted;
  no_floor.entities[0].floor_support = FloorSupport::kNone;
  result = policy::separate(no_floor, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].planning_layer ==
                PlanningLayer::kFurniture &&
            result.decision->entities[0].fixed_obstruction ==
                FixedObstructionDisposition::kMissingFloorSupport,
        "explicit classification cannot replace missing floor support");

  Request no_explicit_classification = accepted;
  no_explicit_classification.entities[0].explicitly_fixed = false;
  result = policy::separate(no_explicit_classification, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].planning_layer ==
                PlanningLayer::kFurniture &&
            result.decision->entities[0].fixed_obstruction ==
                FixedObstructionDisposition::kNotRequested,
        "floor contact alone cannot create a fixed obstruction");

  Request floor_footprint = accepted;
  floor_footprint.area_policy = AreaPolicy::kPreserveFloorFootprint;
  result = policy::separate(floor_footprint, options());
  check(result.status == Status::kOk && result.decision &&
            result.decision->entities[0].planning_layer ==
                PlanningLayer::kFurniture &&
            result.decision->entities[0].fixed_obstruction ==
                FixedObstructionDisposition::kExcludedByAreaPolicy,
        "floor-footprint policy keeps even supported fixed objects separate");

  Request ambiguous = accepted;
  ambiguous.entities[0].floor_support = FloorSupport::kAmbiguous;
  expect_failure(ambiguous, Status::kAmbiguousInput,
                 "ambiguous floor support");
}

void test_invalid_unsupported_and_bounded_inputs_are_atomic() {
  Request nonfinite = load_fixture();
  nonfinite.entities[0].surfaces[0].plane->offset_m =
      std::numeric_limits<double>::infinity();
  expect_failure(nonfinite, Status::kNonFiniteInput, "nonfinite plane");

  Request nonunit = load_fixture();
  nonunit.entities[0].surfaces[0].plane->normal_unit = {0.0, 0.0, 2.0};
  expect_failure(nonunit, Status::kInvalidGeometry, "non-unit plane normal");

  Request unsupported = object_request();
  unsupported.entities[0].surfaces[0].kind =
      static_cast<SurfaceKind>(255U);
  expect_failure(unsupported, Status::kUnsupportedValue,
                 "unsupported surface kind");

  Request missing_support = object_request();
  missing_support.entities[0].surfaces[0].support_ids.clear();
  expect_failure(missing_support, Status::kMissingEvidence,
                 "surface without provenance support");

  Request duplicate = load_fixture();
  duplicate.entities[1].surfaces[0].id = duplicate.entities[0].surfaces[0].id;
  expect_failure(duplicate, Status::kDuplicateId, "duplicate surface ID");

  Request mixed_roles = object_request();
  SurfaceEvidence structural = object_surface();
  structural.id = "92000000-0000-4000-8000-000000000002";
  structural.evidence_role = EvidenceRole::kStructural;
  structural.geometry_reference = "mesh:structural-conflict";
  structural.support_ids = {"93000000-0000-4000-8000-000000000002"};
  mixed_roles.entities[0].surfaces.push_back(std::move(structural));
  expect_failure(mixed_roles, Status::kAmbiguousInput,
                 "mixed metric roles in one entity");

  Options invalid_options;
  expect_failure(object_request(), Status::kInvalidOptions,
                 "undeclared unit-normal tolerance", invalid_options);

  Options bounded = options();
  bounded.max_surfaces = 1U;
  expect_failure(load_fixture(), Status::kResourceLimitExceeded,
                 "declared surface cap", bounded);

  Request unspecified_policy = object_request();
  unspecified_policy.area_policy = AreaPolicy::kUnspecified;
  expect_failure(unspecified_policy, Status::kInvalidOptions,
                 "unspecified area policy");
}

} // namespace

int main() {
  test_fixture_preserves_parallel_wall_and_shelf_evidence();
  test_labels_cannot_reclassify_metric_evidence();
  test_hidden_faces_require_submitted_support();
  test_fixed_obstruction_requires_all_policy_gates();
  test_invalid_unsupported_and_bounded_inputs_are_atomic();
  if (failures == 0)
    std::cout << "surface separation policy tests passed\n";
  return failures == 0 ? 0 : 1;
}

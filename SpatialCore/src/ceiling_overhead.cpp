#include "hss/ceiling_overhead.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace hss::ceiling_overhead {
namespace {

bool finite(const Point3d &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool within_metric_bounds(const Point3d &point, double maximum) {
  return std::abs(point.x) <= maximum && std::abs(point.y) <= maximum &&
         std::abs(point.z) <= maximum;
}

Point3d subtract(const Point3d &left, const Point3d &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Point3d cross(const Point3d &left, const Point3d &right) {
  return {std::fma(left.y, right.z, -left.z * right.y),
          std::fma(left.z, right.x, -left.x * right.z),
          std::fma(left.x, right.y, -left.y * right.x)};
}

double norm(const Point3d &point) {
  return std::hypot(point.x, point.y, point.z);
}

bool limit_is_valid(size_t value, size_t hard_maximum) {
  return value > 0U && value <= hard_maximum;
}

bool add_within_limit(size_t amount, size_t limit, size_t *total) {
  if (amount > limit || *total > limit - amount)
    return false;
  *total += amount;
  return true;
}

bool is_metric_source(SourceKind kind) {
  return kind == SourceKind::kDepthFrame || kind == SourceKind::kMeshAnchor;
}

bool valid(Role value) {
  switch (value) {
  case Role::kCeilingPatch:
  case Role::kSoffitBottom:
  case Role::kSoffitReturn:
  case Role::kFloatingShelf:
  case Role::kUnknownRegion:
    return true;
  }
  return false;
}

bool valid(GeometryOrigin value) {
  switch (value) {
  case GeometryOrigin::kObserved:
  case GeometryOrigin::kFitted:
  case GeometryOrigin::kUnknown:
    return true;
  }
  return false;
}

bool valid(Representation value) {
  switch (value) {
  case Representation::kNone:
  case Representation::kPlane:
  case Representation::kCapturedMesh:
    return true;
  }
  return false;
}

bool valid(SourceKind value) {
  switch (value) {
  case SourceKind::kDepthFrame:
  case SourceKind::kMeshAnchor:
  case SourceKind::kNoDepthMask:
    return true;
  }
  return false;
}

bool valid(UnknownReason value) {
  switch (value) {
  case UnknownReason::kNone:
  case UnknownReason::kNoDepth:
  case UnknownReason::kOpenSky:
  case UnknownReason::kOccluded:
    return true;
  }
  return false;
}

Status validate_point(const Point3d &point, const Options &options) {
  if (!finite(point))
    return Status::kNonFiniteGeometry;
  if (!within_metric_bounds(point, options.max_abs_coordinate_m))
    return Status::kCoordinateOutOfBounds;
  return Status::kOk;
}

Status validate_provenance(const PatchInput &patch, bool require_no_depth) {
  if (patch.provenance.empty())
    return Status::kMissingProvenance;
  bool has_no_depth = false;
  for (const SourceReference &source : patch.provenance) {
    if (!valid(source.kind))
      return Status::kInvalidEnumValue;
    if (source.source_id == 0U)
      return Status::kInvalidProvenance;
    has_no_depth = has_no_depth || source.kind == SourceKind::kNoDepthMask;
  }
  if (require_no_depth && !has_no_depth)
    return Status::kInvalidUnknownRegion;
  return Status::kOk;
}

bool has_non_collinear_support(const std::vector<MetricSupport> &support) {
  if (support.size() < 3U)
    return false;
  const Point3d origin = support.front().point_m;
  for (size_t second = 1U; second + 1U < support.size(); ++second) {
    const Point3d a = subtract(support[second].point_m, origin);
    const double a_length = norm(a);
    if (!(a_length > 0.0) || !std::isfinite(a_length))
      continue;
    for (size_t third = second + 1U; third < support.size(); ++third) {
      const Point3d b = subtract(support[third].point_m, origin);
      const double b_length = norm(b);
      if (!(b_length > 0.0) || !std::isfinite(b_length))
        continue;
      const double area = norm(cross(a, b));
      const double scale = a_length * b_length;
      if (std::isfinite(area) && std::isfinite(scale) &&
          area > 64.0 * std::numeric_limits<double>::epsilon() * scale) {
        return true;
      }
    }
  }
  return false;
}

bool has_non_collinear_boundary(const std::vector<Point3d> &boundary) {
  if (boundary.size() < 3U)
    return false;
  const Point3d origin = boundary.front();
  for (size_t second = 1U; second + 1U < boundary.size(); ++second) {
    const Point3d a = subtract(boundary[second], origin);
    const double a_length = norm(a);
    if (!(a_length > 0.0) || !std::isfinite(a_length))
      continue;
    for (size_t third = second + 1U; third < boundary.size(); ++third) {
      const Point3d b = subtract(boundary[third], origin);
      const double b_length = norm(b);
      if (!(b_length > 0.0) || !std::isfinite(b_length))
        continue;
      const double area = norm(cross(a, b));
      const double scale = a_length * b_length;
      if (std::isfinite(area) && std::isfinite(scale) &&
          area > 64.0 * std::numeric_limits<double>::epsilon() * scale) {
        return true;
      }
    }
  }
  return false;
}

Status validate_plane_patch(const PatchInput &patch, const Options &options) {
  if (!patch.plane || !patch.mesh_vertices_m.empty() ||
      !patch.mesh_triangles.empty()) {
    return Status::kRoleEvidenceMismatch;
  }
  const Plane &plane = *patch.plane;
  if (!finite(plane.normal) || !std::isfinite(plane.offset_m))
    return Status::kNonFiniteGeometry;
  if (std::abs(plane.offset_m) > options.max_abs_coordinate_m)
    return Status::kCoordinateOutOfBounds;
  const double normal_length = norm(plane.normal);
  if (!std::isfinite(normal_length) ||
      std::abs(normal_length - 1.0) > options.unit_normal_tolerance) {
    return Status::kInvalidPlane;
  }
  if (patch.metric_support.size() < 3U)
    return Status::kInsufficientMetricSupport;
  for (const MetricSupport &support : patch.metric_support) {
    const Status point_status = validate_point(support.point_m, options);
    if (point_status != Status::kOk)
      return point_status;
    if (support.provenance_index >= patch.provenance.size() ||
        !is_metric_source(
            patch.provenance[support.provenance_index].kind)) {
      return Status::kInvalidProvenance;
    }
    const double distance =
        std::abs(std::fma(plane.normal.x, support.point_m.x,
                         std::fma(plane.normal.y, support.point_m.y,
                                  std::fma(plane.normal.z, support.point_m.z,
                                           plane.offset_m))));
    if (!std::isfinite(distance))
      return Status::kNonFiniteGeometry;
    if (distance > options.max_plane_support_distance_m)
      return Status::kMetricSupportMismatch;
  }
  if (!has_non_collinear_support(patch.metric_support))
    return Status::kInsufficientMetricSupport;
  return Status::kOk;
}

Status validate_mesh_patch(const PatchInput &patch, const Options &options) {
  if (patch.plane || patch.mesh_vertices_m.size() < 3U ||
      patch.mesh_triangles.empty()) {
    return Status::kInvalidMesh;
  }
  for (const Point3d &vertex : patch.mesh_vertices_m) {
    const Status status = validate_point(vertex, options);
    if (status != Status::kOk)
      return status;
  }
  for (const Triangle &triangle : patch.mesh_triangles) {
    if (triangle.a >= patch.mesh_vertices_m.size() ||
        triangle.b >= patch.mesh_vertices_m.size() ||
        triangle.c >= patch.mesh_vertices_m.size() ||
        triangle.a == triangle.b || triangle.b == triangle.c ||
        triangle.a == triangle.c) {
      return Status::kInvalidMesh;
    }
    const Point3d a = subtract(patch.mesh_vertices_m[triangle.b],
                               patch.mesh_vertices_m[triangle.a]);
    const Point3d b = subtract(patch.mesh_vertices_m[triangle.c],
                               patch.mesh_vertices_m[triangle.a]);
    const double twice_area = norm(cross(a, b));
    if (!(twice_area > 0.0) || !std::isfinite(twice_area))
      return Status::kInvalidMesh;
  }
  for (const MetricSupport &support : patch.metric_support) {
    const Status status = validate_point(support.point_m, options);
    if (status != Status::kOk)
      return status;
    if (support.provenance_index >= patch.provenance.size() ||
        !is_metric_source(
            patch.provenance[support.provenance_index].kind)) {
      return Status::kInvalidProvenance;
    }
  }
  const bool has_metric_provenance =
      std::any_of(patch.provenance.begin(), patch.provenance.end(),
                  [](const SourceReference &source) {
                    return is_metric_source(source.kind);
                  });
  return has_metric_provenance ? Status::kOk
                               : Status::kInsufficientMetricSupport;
}

Status validate_surface(const PatchInput &patch, const Options &options) {
  if (patch.geometry_origin == GeometryOrigin::kUnknown ||
      patch.unknown_reason != UnknownReason::kNone ||
      !patch.unknown_boundary_m.empty()) {
    return Status::kRoleEvidenceMismatch;
  }
  const Status provenance_status = validate_provenance(patch, false);
  if (provenance_status != Status::kOk)
    return provenance_status;
  switch (patch.representation) {
  case Representation::kPlane:
    return validate_plane_patch(patch, options);
  case Representation::kCapturedMesh:
    return validate_mesh_patch(patch, options);
  case Representation::kNone:
    return Status::kRoleEvidenceMismatch;
  }
  return Status::kInternalFailure;
}

Status validate_unknown(const PatchInput &patch, const Options &options) {
  if (patch.geometry_origin != GeometryOrigin::kUnknown ||
      patch.representation != Representation::kNone || patch.plane ||
      !patch.mesh_vertices_m.empty() || !patch.mesh_triangles.empty() ||
      !patch.metric_support.empty()) {
    return Status::kRoleEvidenceMismatch;
  }
  if (patch.unknown_reason == UnknownReason::kNone ||
      patch.unknown_boundary_m.size() < 3U) {
    return Status::kInvalidUnknownRegion;
  }
  const Status provenance_status = validate_provenance(patch, true);
  if (provenance_status != Status::kOk)
    return provenance_status;
  for (const Point3d &point : patch.unknown_boundary_m) {
    const Status status = validate_point(point, options);
    if (status != Status::kOk)
      return status;
  }
  if (!has_non_collinear_boundary(patch.unknown_boundary_m))
    return Status::kInvalidUnknownRegion;
  return Status::kOk;
}

SurfacePatch make_surface(const PatchInput &patch) {
  return {patch.patch_id,
          patch.role,
          patch.geometry_origin,
          patch.representation,
          patch.plane,
          patch.mesh_vertices_m,
          patch.mesh_triangles,
          patch.metric_support,
          patch.provenance};
}

Result classify_checked(const std::vector<PatchInput> &patches,
                        const Options &options) {
  if (!limit_is_valid(options.max_input_patches, kHardMaxPatches) ||
      !limit_is_valid(options.max_provenance_records,
                      kHardMaxProvenanceRecords) ||
      !limit_is_valid(options.max_metric_supports, kHardMaxMetricSupports) ||
      !limit_is_valid(options.max_mesh_vertices, kHardMaxMeshVertices) ||
      !limit_is_valid(options.max_mesh_triangles, kHardMaxMeshTriangles) ||
      !limit_is_valid(options.max_unknown_boundary_vertices,
                      kHardMaxUnknownBoundaryVertices) ||
      !(options.max_abs_coordinate_m > 0.0) ||
      !std::isfinite(options.max_abs_coordinate_m) ||
      !(options.unit_normal_tolerance > 0.0) ||
      !std::isfinite(options.unit_normal_tolerance) ||
      !(options.max_plane_support_distance_m > 0.0) ||
      !std::isfinite(options.max_plane_support_distance_m)) {
    return {Status::kInvalidOptions, std::nullopt};
  }
  if (patches.empty() || patches.size() > options.max_input_patches)
    return {Status::kInputLimitExceeded, std::nullopt};

  size_t provenance_count = 0U;
  size_t support_count = 0U;
  size_t vertex_count = 0U;
  size_t triangle_count = 0U;
  size_t boundary_count = 0U;
  for (size_t index = 0U; index < patches.size(); ++index) {
    const PatchInput &patch = patches[index];
    if (patch.patch_id == 0U)
      return {Status::kInvalidPatchId, std::nullopt};
    if (!valid(patch.role) || !valid(patch.geometry_origin) ||
        !valid(patch.representation) || !valid(patch.unknown_reason)) {
      return {Status::kInvalidEnumValue, std::nullopt};
    }
    for (size_t earlier = 0U; earlier < index; ++earlier) {
      if (patches[earlier].patch_id == patch.patch_id)
        return {Status::kDuplicatePatchId, std::nullopt};
    }
    if (!add_within_limit(patch.provenance.size(),
                          options.max_provenance_records, &provenance_count) ||
        !add_within_limit(patch.metric_support.size(),
                          options.max_metric_supports, &support_count) ||
        !add_within_limit(patch.mesh_vertices_m.size(),
                          options.max_mesh_vertices, &vertex_count) ||
        !add_within_limit(patch.mesh_triangles.size(),
                          options.max_mesh_triangles, &triangle_count) ||
        !add_within_limit(patch.unknown_boundary_m.size(),
                          options.max_unknown_boundary_vertices,
                          &boundary_count)) {
      return {Status::kInputLimitExceeded, std::nullopt};
    }
  }

  Model model;
  model.ceiling_patches.reserve(patches.size());
  model.soffit_bottoms.reserve(patches.size());
  model.soffit_returns.reserve(patches.size());
  model.floating_shelves.reserve(patches.size());
  model.unknown_regions.reserve(patches.size());
  for (const PatchInput &patch : patches) {
    Status status = Status::kInternalFailure;
    if (patch.role == Role::kUnknownRegion)
      status = validate_unknown(patch, options);
    else
      status = validate_surface(patch, options);
    if (status != Status::kOk)
      return {status, std::nullopt};

    switch (patch.role) {
    case Role::kCeilingPatch:
      model.ceiling_patches.push_back(make_surface(patch));
      break;
    case Role::kSoffitBottom:
      model.soffit_bottoms.push_back(make_surface(patch));
      break;
    case Role::kSoffitReturn:
      model.soffit_returns.push_back(make_surface(patch));
      break;
    case Role::kFloatingShelf:
      model.floating_shelves.push_back(make_surface(patch));
      break;
    case Role::kUnknownRegion:
      model.unknown_regions.push_back(
          {patch.patch_id, patch.unknown_reason, patch.unknown_boundary_m,
           patch.provenance});
      break;
    }
  }
  return {Status::kOk, std::move(model)};
}

} // namespace

Result classify(const std::vector<PatchInput> &patches,
                const Options &options) {
  try {
    return classify_checked(patches, options);
  } catch (const std::bad_alloc &) {
    return {Status::kResourceLimit, std::nullopt};
  } catch (...) {
    return {Status::kInternalFailure, std::nullopt};
  }
}

} // namespace hss::ceiling_overhead

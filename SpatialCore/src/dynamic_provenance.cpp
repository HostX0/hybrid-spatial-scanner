#include "hss/dynamic_provenance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace hss::dynamic_provenance {
namespace {

bool valid_uuid(std::string_view value) {
  if (value.size() != 36U)
    return false;
  for (size_t index = 0U; index < value.size(); ++index) {
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (value[index] != '-')
        return false;
      continue;
    }
    const char character = value[index];
    const bool digit = character >= '0' && character <= '9';
    const bool lower_hex = character >= 'a' && character <= 'f';
    if (!digit && !lower_hex)
      return false;
  }
  return true;
}

bool valid(SourceKind value) {
  switch (value) {
  case SourceKind::kDepthFrame:
  case SourceKind::kMeshAnchor:
    return true;
  }
  return false;
}

bool valid(EvidenceKind value) {
  switch (value) {
  case EvidenceKind::kTemporalDepthChange:
  case EvidenceKind::kTrackedMultiFrameMotion:
    return true;
  }
  return false;
}

bool finite(const Point3d &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z);
}

bool ordered(const Bounds3d &bounds) {
  return bounds.min_m.x <= bounds.max_m.x &&
         bounds.min_m.y <= bounds.max_m.y &&
         bounds.min_m.z <= bounds.max_m.z;
}

bool within_extent(const Point3d &point, double maximum) {
  return std::abs(point.x) <= maximum && std::abs(point.y) <= maximum &&
         std::abs(point.z) <= maximum;
}

bool contains(const Bounds3d &bounds, const Point3d &point) {
  return point.x >= bounds.min_m.x && point.x <= bounds.max_m.x &&
         point.y >= bounds.min_m.y && point.y <= bounds.max_m.y &&
         point.z >= bounds.min_m.z && point.z <= bounds.max_m.z;
}

bool contains(const Bounds3d &outer, const Bounds3d &inner) {
  return contains(outer, inner.min_m) && contains(outer, inner.max_m);
}

bool limit_valid(size_t value, size_t hard_maximum) {
  return value > 0U && value <= hard_maximum;
}

bool add_within_limit(size_t amount, size_t limit, size_t *total) {
  if (amount > limit || *total > limit - amount)
    return false;
  *total += amount;
  return true;
}

using SourceKey =
    std::tuple<uint8_t, std::string, std::string, uint64_t, uint64_t>;

SourceKey source_key(const SourceReference &source) {
  return {static_cast<uint8_t>(source.kind), source.source_id,
          source.capture_id, source.session_epoch, source.event_sequence};
}

Bounds3d exclusion_bounds(
    const scene_editing::DynamicExclusion &exclusion) {
  return {{exclusion.min_m[0], exclusion.min_m[1], exclusion.min_m[2]},
          {exclusion.max_m[0], exclusion.max_m[1], exclusion.max_m[2]}};
}

Status validate_bounds(const Bounds3d &bounds, const Options &options) {
  if (!finite(bounds.min_m) || !finite(bounds.max_m))
    return Status::kNonFiniteGeometry;
  if (!within_extent(bounds.min_m, options.max_abs_coordinate_m) ||
      !within_extent(bounds.max_m, options.max_abs_coordinate_m)) {
    return Status::kCoordinateOutOfBounds;
  }
  return ordered(bounds) ? Status::kOk : Status::kInvalidEvidence;
}

Status validate_source(const SourceReference &source) {
  if (!valid(source.kind) || !valid_uuid(source.source_id) ||
      !valid_uuid(source.capture_id) || source.session_epoch == 0U ||
      source.event_sequence == 0U) {
    return Status::kInvalidProvenance;
  }
  return Status::kOk;
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

Status validate_mesh(const CapturedMesh &mesh, const Options &options) {
  if (!valid_uuid(mesh.project_id) || mesh.vertices_m.size() < 3U ||
      mesh.triangles.empty() || mesh.sources.empty()) {
    return Status::kInvalidMesh;
  }
  if (mesh.vertices_m.size() > options.max_vertices ||
      mesh.triangles.size() > options.max_triangles ||
      mesh.sources.size() > options.max_source_records) {
    return Status::kInputLimitExceeded;
  }
  for (const Point3d &vertex : mesh.vertices_m) {
    if (!finite(vertex))
      return Status::kNonFiniteGeometry;
    if (!within_extent(vertex, options.max_abs_coordinate_m))
      return Status::kCoordinateOutOfBounds;
  }
  std::set<SourceKey> source_keys;
  for (const SourceReference &source : mesh.sources) {
    if (validate_source(source) != Status::kOk)
      return Status::kInvalidProvenance;
    if (!source_keys.insert(source_key(source)).second)
      return Status::kInvalidProvenance;
  }

  size_t provenance_references = 0U;
  for (const Triangle &triangle : mesh.triangles) {
    const auto &indices = triangle.vertex_indices;
    if (indices[0] >= mesh.vertices_m.size() ||
        indices[1] >= mesh.vertices_m.size() ||
        indices[2] >= mesh.vertices_m.size() || indices[0] == indices[1] ||
        indices[1] == indices[2] || indices[0] == indices[2] ||
        triangle.source_indices.empty()) {
      return Status::kInvalidMesh;
    }
    const Point3d edge_a =
        subtract(mesh.vertices_m[indices[1]], mesh.vertices_m[indices[0]]);
    const Point3d edge_b =
        subtract(mesh.vertices_m[indices[2]], mesh.vertices_m[indices[0]]);
    const double twice_area = norm(cross(edge_a, edge_b));
    if (!(twice_area > 0.0) || !std::isfinite(twice_area))
      return Status::kInvalidMesh;
    if (!add_within_limit(triangle.source_indices.size(),
                          options.max_provenance_references,
                          &provenance_references)) {
      return Status::kInputLimitExceeded;
    }
    size_t previous = 0U;
    bool have_previous = false;
    for (size_t source_index : triangle.source_indices) {
      if (source_index >= mesh.sources.size() ||
          (have_previous && source_index <= previous)) {
        return Status::kInvalidProvenance;
      }
      previous = source_index;
      have_previous = true;
    }
  }
  return Status::kOk;
}

Status validate_options(const Options &options) {
  if (!limit_valid(options.max_vertices, kHardMaxVertices) ||
      !limit_valid(options.max_triangles, kHardMaxTriangles) ||
      !limit_valid(options.max_source_records, kHardMaxSourceRecords) ||
      !limit_valid(options.max_provenance_references,
                   kHardMaxProvenanceReferences) ||
      !limit_valid(options.max_evidence_records, kHardMaxEvidenceRecords) ||
      !std::isfinite(options.max_abs_coordinate_m) ||
      options.max_abs_coordinate_m <= 0.0) {
    return Status::kInvalidOptions;
  }
  return Status::kOk;
}

bool triangle_in_bounds(const CapturedMesh &mesh, const Triangle &triangle,
                        const Bounds3d &bounds) {
  return std::all_of(
      triangle.vertex_indices.begin(), triangle.vertex_indices.end(),
      [&](uint32_t index) { return contains(bounds, mesh.vertices_m[index]); });
}

const VerifiedDynamicEvidence *matching_evidence(
    const std::map<SourceKey, const VerifiedDynamicEvidence *> &linked,
    const SourceReference &source, const CapturedMesh &mesh,
    const Triangle &triangle) {
  const auto found = linked.find(source_key(source));
  if (found == linked.end() ||
      !triangle_in_bounds(mesh, triangle, found->second->bounds_m)) {
    return nullptr;
  }
  return found->second;
}

} // namespace

Result apply_exclusion(
    const CapturedMesh &raw_captured_mesh,
    const scene_editing::DynamicExclusion &pending_exclusion,
    const std::vector<VerifiedDynamicEvidence> &verified_evidence,
    const Options &options) {
  Result result;
  try {
    result.status = validate_options(options);
    if (result.status != Status::kOk)
      return result;
    if (verified_evidence.size() > options.max_evidence_records) {
      result.status = Status::kInputLimitExceeded;
      return result;
    }

    const Bounds3d requested_bounds = exclusion_bounds(pending_exclusion);
    if (!valid_uuid(pending_exclusion.command_id) ||
        !valid_uuid(pending_exclusion.project_id) ||
        pending_exclusion.start_sequence == 0U ||
        pending_exclusion.end_sequence < pending_exclusion.start_sequence) {
      result.status = Status::kInvalidExclusion;
      return result;
    }
    result.status = validate_bounds(requested_bounds, options);
    if (result.status != Status::kOk) {
      result.status = result.status == Status::kInvalidEvidence
                          ? Status::kInvalidExclusion
                          : result.status;
      return result;
    }
    result.status = validate_mesh(raw_captured_mesh, options);
    if (result.status != Status::kOk)
      return result;
    if (raw_captured_mesh.project_id != pending_exclusion.project_id) {
      result.status = Status::kInvalidExclusion;
      return result;
    }

    std::set<std::string> evidence_ids;
    std::set<SourceKey> raw_source_keys;
    for (const SourceReference &source : raw_captured_mesh.sources)
      raw_source_keys.insert(source_key(source));
    std::map<SourceKey, const VerifiedDynamicEvidence *> linked;
    for (const VerifiedDynamicEvidence &evidence : verified_evidence) {
      if (!valid_uuid(evidence.evidence_id) ||
          !valid_uuid(evidence.exclusion_command_id) ||
          !valid_uuid(evidence.project_id) || !valid(evidence.kind) ||
          validate_source(evidence.source) != Status::kOk) {
        result.status = Status::kInvalidEvidence;
        return result;
      }
      const Status bounds_status = validate_bounds(evidence.bounds_m, options);
      if (bounds_status != Status::kOk) {
        result.status = bounds_status == Status::kInvalidEvidence
                            ? Status::kInvalidEvidence
                            : bounds_status;
        return result;
      }
      if (!evidence_ids.insert(evidence.evidence_id).second) {
        result.status = Status::kDuplicateEvidenceId;
        return result;
      }
      if (evidence.exclusion_command_id != pending_exclusion.command_id ||
          evidence.project_id != pending_exclusion.project_id) {
        continue;
      }
      if (evidence.source.event_sequence < pending_exclusion.start_sequence ||
          evidence.source.event_sequence > pending_exclusion.end_sequence ||
          !contains(requested_bounds, evidence.bounds_m)) {
        result.status = Status::kEvidenceOutsideExclusion;
        return result;
      }
      const SourceKey key = source_key(evidence.source);
      if (!raw_source_keys.contains(key)) {
        result.status = Status::kEvidenceSourceMissing;
        return result;
      }
      if (!linked.emplace(key, &evidence).second) {
        result.status = Status::kInvalidEvidence;
        return result;
      }
    }

    MaskLayer layer;
    layer.triangle_mask.assign(raw_captured_mesh.triangles.size(), uint8_t{0});
    layer.audit = {pending_exclusion.command_id,
                   pending_exclusion.project_id,
                   requested_bounds,
                   pending_exclusion.start_sequence,
                   pending_exclusion.end_sequence,
                   linked.size(),
                   0U,
                   Disposition::kNoVerifiedEffect};

    for (size_t triangle_index = 0U;
         triangle_index < raw_captured_mesh.triangles.size();
         ++triangle_index) {
      const Triangle &triangle = raw_captured_mesh.triangles[triangle_index];
      if (!triangle_in_bounds(raw_captured_mesh, triangle, requested_bounds))
        continue;

      MaskedTriangle masked;
      masked.triangle_index = triangle_index;
      masked.source_indices = triangle.source_indices;
      masked.evidence_ids.reserve(triangle.source_indices.size());
      bool fully_verified = true;
      for (size_t source_index : triangle.source_indices) {
        const SourceReference &source = raw_captured_mesh.sources[source_index];
        if (source.event_sequence < pending_exclusion.start_sequence ||
            source.event_sequence > pending_exclusion.end_sequence) {
          fully_verified = false;
          break;
        }
        const VerifiedDynamicEvidence *evidence = matching_evidence(
            linked, source, raw_captured_mesh, triangle);
        if (evidence == nullptr) {
          fully_verified = false;
          break;
        }
        masked.evidence_ids.push_back(evidence->evidence_id);
      }
      if (!fully_verified)
        continue;
      layer.triangle_mask[triangle_index] = uint8_t{1};
      layer.masked_triangles.push_back(std::move(masked));
    }

    layer.audit.masked_triangle_count = layer.masked_triangles.size();
    if (!layer.masked_triangles.empty())
      layer.audit.disposition = Disposition::kVerifiedMask;
    result.status = Status::kOk;
    result.layer = std::move(layer);
    return result;
  } catch (const std::bad_alloc &) {
    result.status = Status::kResourceLimit;
    result.layer.reset();
    return result;
  } catch (const std::length_error &) {
    result.status = Status::kResourceLimit;
    result.layer.reset();
    return result;
  } catch (...) {
    result.status = Status::kInternalFailure;
    result.layer.reset();
    return result;
  }
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kInputLimitExceeded:
    return "input_limit_exceeded";
  case Status::kInvalidExclusion:
    return "invalid_exclusion";
  case Status::kInvalidMesh:
    return "invalid_mesh";
  case Status::kInvalidProvenance:
    return "invalid_provenance";
  case Status::kNonFiniteGeometry:
    return "nonfinite_geometry";
  case Status::kCoordinateOutOfBounds:
    return "coordinate_out_of_bounds";
  case Status::kInvalidEvidence:
    return "invalid_evidence";
  case Status::kDuplicateEvidenceId:
    return "duplicate_evidence_id";
  case Status::kEvidenceOutsideExclusion:
    return "evidence_outside_exclusion";
  case Status::kEvidenceSourceMissing:
    return "evidence_source_missing";
  case Status::kResourceLimit:
    return "resource_limit";
  case Status::kInternalFailure:
    return "internal_failure";
  }
  return "unknown";
}

} // namespace hss::dynamic_provenance

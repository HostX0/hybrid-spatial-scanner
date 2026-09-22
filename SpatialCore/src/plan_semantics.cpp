#include "hss/plan_semantics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hss::plan_semantics {
namespace {

Result failure(Status status) { return {status, std::nullopt}; }

bool valid_view(View value) {
  return value == View::kFloorFootprint || value == View::kArchitecturalCut ||
         value == View::kReflectedCeiling;
}

bool valid_reference(ReferenceState value) {
  return value == ReferenceState::kConfirmedFloor ||
         value == ReferenceState::kFloorUnknown ||
         value == ReferenceState::kUnsupported ||
         value == ReferenceState::kNotApplicable;
}

bool valid_boundary(BoundaryState value) {
  return value == BoundaryState::kClosedAccepted ||
         value == BoundaryState::kOpen || value == BoundaryState::kUnknown ||
         value == BoundaryState::kUnsupported;
}

bool valid_evidence_state(EvidenceState value) {
  return value == EvidenceState::kSupported ||
         value == EvidenceState::kUnknown ||
         value == EvidenceState::kUnsupported;
}

bool valid_evidence_kind(EvidenceKind value) {
  return value == EvidenceKind::kSurfaceIntersection ||
         value == EvidenceKind::kFloorJunction ||
         value == EvidenceKind::kCeilingPatch ||
         value == EvidenceKind::kUnknownRegion ||
         value == EvidenceKind::kUnknownBoundary ||
         value == EvidenceKind::kUnsupported;
}

bool valid_geometry_origin(area::GeometryOrigin value) {
  return value == area::GeometryOrigin::kObserved ||
         value == area::GeometryOrigin::kFitted ||
         value == area::GeometryOrigin::kUserMeasured ||
         value == area::GeometryOrigin::kInferred ||
         value == area::GeometryOrigin::kUnknown;
}

bool valid_quality(area::QualityState value) {
  return value == area::QualityState::kProvisional ||
         value == area::QualityState::kNeedsReview ||
         value == area::QualityState::kReady ||
         value == area::QualityState::kInvalid;
}

bool valid_options(const Options &options) {
  return options.max_fragments > 0U &&
         options.max_fragments <= kHardMaxFragments &&
         options.max_support_ids > 0U &&
         options.max_support_ids <= kHardMaxSupportIds &&
         std::isfinite(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > 0.0 &&
         std::isfinite(options.min_fragment_length_m) &&
         options.min_fragment_length_m > 0.0 &&
         options.min_fragment_length_m < options.max_abs_coordinate_m &&
         std::isfinite(options.height_tolerance_m) &&
         options.height_tolerance_m >= 0.0 &&
         options.area_options.max_rings > 0U &&
         options.area_options.max_rings <= area::kHardMaxRings &&
         options.area_options.max_vertices >= 3U &&
         options.area_options.max_vertices <= area::kHardMaxVertices;
}

bool usable_id(std::string_view value) {
  return !value.empty() && value.size() <= 1'024U;
}

bool finite_in_extent(area::Point2d point, double extent) {
  return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
         std::fabs(point.x_m) <= extent && std::fabs(point.y_m) <= extent;
}

bool finite_in_extent(surface_proposal::Point3d point, double extent) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::isfinite(point.z) && std::fabs(point.x) <= extent &&
         std::fabs(point.y) <= extent && std::fabs(point.z) <= extent;
}

bool valid_segment(const Segment2d &segment, const Options &options) {
  if (!finite_in_extent(segment.start_m, options.max_abs_coordinate_m) ||
      !finite_in_extent(segment.end_m, options.max_abs_coordinate_m))
    return false;
  const long double dx =
      static_cast<long double>(segment.end_m.x_m) - segment.start_m.x_m;
  const long double dy =
      static_cast<long double>(segment.end_m.y_m) - segment.start_m.y_m;
  const long double minimum = options.min_fragment_length_m;
  return dx * dx + dy * dy >= minimum * minimum;
}

bool valid_height_range(const HeightRange &range) {
  return std::isfinite(range.minimum_m) && std::isfinite(range.maximum_m) &&
         range.minimum_m <= range.maximum_m;
}

bool contains(const std::unordered_set<std::string> &values,
              const std::string &value) {
  return values.find(value) != values.end();
}

bool valid_intersection_provenance(
    const surface_proposal::IntersectionProposal &intersection,
    const std::unordered_set<std::string> &support_ids,
    const Options &options) {
  if (intersection.epoch_id == 0U ||
      !finite_in_extent(intersection.start_m, options.max_abs_coordinate_m) ||
      !finite_in_extent(intersection.end_m, options.max_abs_coordinate_m) ||
      !finite_in_extent(intersection.direction, 1.0) ||
      !std::isfinite(intersection.normal_angle_rad) ||
      !std::isfinite(intersection.conditioning_sine) ||
      !std::isfinite(intersection.condition_number))
    return false;

  const auto contribution_valid = [&](const auto &contribution) {
    if (!usable_id(contribution.surface_id) ||
        contribution.all_source_ids.empty() ||
        contribution.intersection_source_ids.empty())
      return false;
    for (const std::string &id : contribution.all_source_ids) {
      if (!usable_id(id) || !contains(support_ids, id))
        return false;
    }
    for (const std::string &id : contribution.intersection_source_ids) {
      if (!usable_id(id) || !contains(support_ids, id) ||
          std::find(contribution.all_source_ids.begin(),
                    contribution.all_source_ids.end(),
                    id) == contribution.all_source_ids.end())
        return false;
    }
    return true;
  };
  return intersection.first.surface_id != intersection.second.surface_id &&
         contribution_valid(intersection.first) &&
         contribution_valid(intersection.second);
}

struct CeilingIds {
  std::unordered_set<uint64_t> surfaces;
  std::unordered_set<uint64_t> unknowns;
};

bool add_surface_ids(const std::vector<ceiling_overhead::SurfacePatch> &patches,
                     CeilingIds &ids) {
  for (const auto &patch : patches) {
    if (patch.patch_id == 0U || !ids.surfaces.insert(patch.patch_id).second ||
        ids.unknowns.find(patch.patch_id) != ids.unknowns.end())
      return false;
  }
  return true;
}

bool collect_ceiling_ids(const ceiling_overhead::Model &model,
                         CeilingIds &ids) {
  if (!add_surface_ids(model.ceiling_patches, ids) ||
      !add_surface_ids(model.soffit_bottoms, ids) ||
      !add_surface_ids(model.soffit_returns, ids) ||
      !add_surface_ids(model.floating_shelves, ids))
    return false;
  for (const auto &region : model.unknown_regions) {
    if (region.patch_id == 0U || !ids.unknowns.insert(region.patch_id).second ||
        ids.surfaces.find(region.patch_id) != ids.surfaces.end())
      return false;
  }
  return true;
}

bool provenance_valid(const area::Provenance &provenance,
                      const std::string &revision,
                      const std::unordered_set<std::string> &fragment_support,
                      size_t &support_count, const Options &options) {
  if (!valid_geometry_origin(provenance.geometry_origin) ||
      !valid_quality(provenance.quality_state) ||
      provenance.source_revision != revision || provenance.support_ids.empty())
    return false;
  for (const std::string &id : provenance.support_ids) {
    if (!usable_id(id) || !contains(fragment_support, id) ||
        support_count == options.max_support_ids)
      return false;
    ++support_count;
  }
  return true;
}

bool topology_matches(BoundaryState boundary, const area::Input &area_input) {
  bool has_incomplete =
      area_input.outer.topology_state == area::TopologyState::kIncomplete;
  bool has_unaccepted =
      area_input.outer.topology_state == area::TopologyState::kUnaccepted;
  bool all_accepted =
      area_input.outer.topology_state == area::TopologyState::kAccepted;
  for (const area::Hole &hole : area_input.holes) {
    has_incomplete = has_incomplete || hole.ring.topology_state ==
                                           area::TopologyState::kIncomplete;
    has_unaccepted = has_unaccepted || hole.ring.topology_state ==
                                           area::TopologyState::kUnaccepted;
    all_accepted = all_accepted &&
                   hole.ring.topology_state == area::TopologyState::kAccepted;
  }
  switch (boundary) {
  case BoundaryState::kClosedAccepted:
    return all_accepted;
  case BoundaryState::kOpen:
    return has_incomplete && !has_unaccepted;
  case BoundaryState::kUnknown:
  case BoundaryState::kUnsupported:
    return !all_accepted;
  }
  return false;
}

void add_issue(std::vector<Issue> &issues, IssueCode code,
               std::string target = {}) {
  issues.push_back({code, std::move(target)});
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk:
    return "ok";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kInvalidEnumValue:
    return "invalid_enum_value";
  case Status::kInvalidRevision:
    return "invalid_revision";
  case Status::kResourceLimitExceeded:
    return "resource_limit_exceeded";
  case Status::kInvalidHeightPolicy:
    return "invalid_height_policy";
  case Status::kInvalidFragment:
    return "invalid_fragment";
  case Status::kInvalidProvenance:
    return "invalid_provenance";
  case Status::kHeightEvidenceMismatch:
    return "height_evidence_mismatch";
  case Status::kViewEvidenceMismatch:
    return "view_evidence_mismatch";
  case Status::kAreaInputMismatch:
    return "area_input_mismatch";
  case Status::kBoundaryStateMismatch:
    return "boundary_state_mismatch";
  case Status::kAreaRejected:
    return "area_rejected";
  case Status::kResourceLimit:
    return "resource_limit";
  case Status::kInternalFailure:
    return "internal_failure";
  }
  return "unknown";
}

const char *issue_name(IssueCode issue) {
  switch (issue) {
  case IssueCode::kFloorReferenceUnknown:
    return "floor_reference_unknown";
  case IssueCode::kFloorReferenceUnsupported:
    return "floor_reference_unsupported";
  case IssueCode::kOpenBoundary:
    return "open_boundary";
  case IssueCode::kUnknownBoundaryEvidence:
    return "unknown_boundary_evidence";
  case IssueCode::kUnsupportedBoundaryEvidence:
    return "unsupported_boundary_evidence";
  case IssueCode::kInferredGeometry:
    return "inferred_geometry";
  case IssueCode::kQualityNotReady:
    return "quality_not_ready";
  case IssueCode::kCeilingUnknownRegion:
    return "ceiling_unknown_region";
  case IssueCode::kAreaNotApplicable:
    return "area_not_applicable";
  }
  return "unknown";
}

Result assemble(const Input &input, const Options &options) {
  try {
    if (!valid_options(options))
      return failure(Status::kInvalidOptions);
    if (!valid_view(input.view) ||
        !valid_reference(input.height.reference_state) ||
        !valid_boundary(input.boundary_state))
      return failure(Status::kInvalidEnumValue);
    if (!usable_id(input.scene_revision))
      return failure(Status::kInvalidRevision);
    if (input.fragments.empty())
      return failure(Status::kInvalidFragment);
    if (input.fragments.size() > options.max_fragments)
      return failure(Status::kResourceLimitExceeded);

    const bool ceiling_view = input.view == View::kReflectedCeiling;
    if (ceiling_view) {
      if (input.height.cut_height_m || input.height.sampling_band_m ||
          input.height.reference_state != ReferenceState::kNotApplicable ||
          !input.height.floor_reference_id.empty())
        return failure(Status::kInvalidHeightPolicy);
      if (input.area_input || !input.ceiling_model)
        return failure(Status::kViewEvidenceMismatch);
    } else {
      if (!input.height.cut_height_m || !input.height.sampling_band_m ||
          !std::isfinite(*input.height.cut_height_m) ||
          !std::isfinite(*input.height.sampling_band_m) ||
          *input.height.cut_height_m < 0.0 ||
          *input.height.sampling_band_m <= 0.0 ||
          *input.height.sampling_band_m > 100.0 ||
          input.height.reference_state == ReferenceState::kNotApplicable ||
          (input.view == View::kArchitecturalCut &&
           *input.height.cut_height_m <= 0.0))
        return failure(Status::kInvalidHeightPolicy);
      if (input.height.reference_state == ReferenceState::kConfirmedFloor) {
        if (!usable_id(input.height.floor_reference_id))
          return failure(Status::kInvalidHeightPolicy);
      } else if (!input.height.floor_reference_id.empty()) {
        return failure(Status::kInvalidHeightPolicy);
      }
      if (!input.area_input || input.ceiling_model)
        return failure(Status::kViewEvidenceMismatch);
    }

    CeilingIds ceiling_ids;
    if (ceiling_view && !collect_ceiling_ids(*input.ceiling_model, ceiling_ids))
      return failure(Status::kInvalidProvenance);

    std::unordered_set<std::string> fragment_ids;
    std::unordered_set<std::string> aggregate_support;
    size_t support_count = 0U;
    bool has_unknown = false;
    bool has_unsupported = false;
    bool has_inferred = false;

    for (const Fragment &fragment : input.fragments) {
      if (!usable_id(fragment.id) || !fragment_ids.insert(fragment.id).second ||
          !valid_evidence_state(fragment.state) ||
          !valid_evidence_kind(fragment.kind) ||
          !valid_geometry_origin(fragment.geometry_origin) ||
          fragment.source_revision != input.scene_revision)
        return failure(Status::kInvalidFragment);

      if (fragment.segment && !valid_segment(*fragment.segment, options))
        return failure(Status::kInvalidFragment);
      if (fragment.source_height_range_m &&
          !valid_height_range(*fragment.source_height_range_m))
        return failure(Status::kInvalidFragment);

      std::unordered_set<std::string> local_support;
      for (const std::string &id : fragment.support_ids) {
        if (!usable_id(id) || !local_support.insert(id).second ||
            support_count == options.max_support_ids)
          return failure(Status::kInvalidProvenance);
        ++support_count;
        aggregate_support.insert(id);
      }

      if (fragment.state == EvidenceState::kSupported) {
        if (!fragment.segment || fragment.support_ids.empty() ||
            fragment.geometry_origin == area::GeometryOrigin::kUnknown)
          return failure(Status::kInvalidProvenance);
      } else if (fragment.state == EvidenceState::kUnknown) {
        has_unknown = true;
        if (fragment.geometry_origin != area::GeometryOrigin::kUnknown)
          return failure(Status::kInvalidProvenance);
      } else {
        has_unsupported = true;
        if (fragment.segment ||
            fragment.geometry_origin != area::GeometryOrigin::kUnknown)
          return failure(Status::kInvalidProvenance);
      }
      has_inferred = has_inferred || fragment.geometry_origin ==
                                         area::GeometryOrigin::kInferred;

      if (!ceiling_view) {
        const bool valid_plan_kind =
            (fragment.state == EvidenceState::kSupported &&
             (fragment.kind == EvidenceKind::kSurfaceIntersection ||
              fragment.kind == EvidenceKind::kFloorJunction)) ||
            (fragment.state == EvidenceState::kUnknown &&
             fragment.kind == EvidenceKind::kUnknownBoundary) ||
            (fragment.state == EvidenceState::kUnsupported &&
             fragment.kind == EvidenceKind::kUnsupported);
        if (!valid_plan_kind || fragment.overhead_patch_id)
          return failure(Status::kViewEvidenceMismatch);
        if (fragment.state == EvidenceState::kSupported &&
            !fragment.source_height_range_m)
          return failure(Status::kHeightEvidenceMismatch);
        if (fragment.source_height_range_m) {
          const double half_band = *input.height.sampling_band_m * 0.5;
          const double low = *input.height.cut_height_m - half_band -
                             options.height_tolerance_m;
          const double high = *input.height.cut_height_m + half_band +
                              options.height_tolerance_m;
          if (fragment.source_height_range_m->minimum_m < low ||
              fragment.source_height_range_m->maximum_m > high)
            return failure(Status::kHeightEvidenceMismatch);
        }
      } else {
        const bool valid_ceiling_kind =
            (fragment.state == EvidenceState::kSupported &&
             fragment.kind == EvidenceKind::kCeilingPatch) ||
            (fragment.state == EvidenceState::kUnknown &&
             fragment.kind == EvidenceKind::kUnknownRegion) ||
            (fragment.state == EvidenceState::kUnsupported &&
             fragment.kind == EvidenceKind::kUnsupported);
        if (!valid_ceiling_kind || fragment.intersection)
          return failure(Status::kViewEvidenceMismatch);
        if (fragment.kind == EvidenceKind::kCeilingPatch) {
          if (!fragment.overhead_patch_id ||
              ceiling_ids.surfaces.find(*fragment.overhead_patch_id) ==
                  ceiling_ids.surfaces.end() ||
              !fragment.source_height_range_m)
            return failure(Status::kInvalidProvenance);
        } else if (fragment.kind == EvidenceKind::kUnknownRegion) {
          if (!fragment.overhead_patch_id ||
              ceiling_ids.unknowns.find(*fragment.overhead_patch_id) ==
                  ceiling_ids.unknowns.end())
            return failure(Status::kInvalidProvenance);
        } else if (fragment.overhead_patch_id) {
          return failure(Status::kInvalidProvenance);
        }
      }

      if (fragment.kind == EvidenceKind::kSurfaceIntersection) {
        if (!fragment.intersection ||
            !valid_intersection_provenance(*fragment.intersection,
                                           local_support, options))
          return failure(Status::kInvalidProvenance);
      } else if (fragment.intersection) {
        return failure(Status::kInvalidProvenance);
      }
    }

    if ((input.boundary_state == BoundaryState::kClosedAccepted &&
         (has_unknown || has_unsupported)) ||
        (input.boundary_state == BoundaryState::kUnknown && !has_unknown) ||
        (input.boundary_state == BoundaryState::kUnsupported &&
         !has_unsupported))
      return failure(Status::kBoundaryStateMismatch);

    PlanLayer layer;
    layer.scene_revision = input.scene_revision;
    layer.source_is_synthetic = input.source_is_synthetic;
    layer.view = input.view;
    layer.height = input.height;
    layer.boundary_state = input.boundary_state;
    layer.fragments = input.fragments;
    layer.ceiling_model = input.ceiling_model;

    if (ceiling_view) {
      layer.area.availability = Availability::kNotApplicable;
      layer.validity = input.ceiling_model->unknown_regions.empty() &&
                               !has_unknown && !has_unsupported
                           ? Availability::kReady
                           : Availability::kProvisional;
      add_issue(layer.issues, IssueCode::kAreaNotApplicable);
      for (const auto &region : input.ceiling_model->unknown_regions)
        add_issue(layer.issues, IssueCode::kCeilingUnknownRegion,
                  std::to_string(region.patch_id));
      if (has_unsupported) {
        layer.validity = Availability::kUnsupported;
        for (const Fragment &fragment : input.fragments)
          if (fragment.state == EvidenceState::kUnsupported)
            add_issue(layer.issues, IssueCode::kUnsupportedBoundaryEvidence,
                      fragment.id);
      }
      return {Status::kOk, std::move(layer)};
    }

    const area::Input &area_input = *input.area_input;
    layer.area.policy = area_input.policy;
    if (area_input.coordinate_unit != area::LinearUnit::kMetres ||
        (area_input.policy != area::AreaPolicy::kInternalFinishedFace &&
         area_input.policy != area::AreaPolicy::kNetFixedObstructions))
      return failure(Status::kAreaInputMismatch);
    const auto valid_topology = [](area::TopologyState state) {
      return state == area::TopologyState::kAccepted ||
             state == area::TopologyState::kIncomplete ||
             state == area::TopologyState::kUnaccepted;
    };
    if (!valid_topology(area_input.outer.topology_state))
      return failure(Status::kAreaInputMismatch);
    for (const area::Hole &hole : area_input.holes) {
      if (!valid_topology(hole.ring.topology_state) ||
          (hole.kind != area::HoleKind::kFixedObstruction &&
           hole.kind != area::HoleKind::kFurniture &&
           hole.kind != area::HoleKind::kOther) ||
          (hole.attachment != area::Attachment::kDetached &&
           hole.attachment != area::Attachment::kAttached &&
           hole.attachment != area::Attachment::kUncertain))
        return failure(Status::kAreaInputMismatch);
    }
    if (!topology_matches(input.boundary_state, area_input))
      return failure(Status::kBoundaryStateMismatch);

    size_t area_support_count = support_count;
    bool ready_quality = true;
    bool invalid_quality = false;
    bool metric_origin = true;
    const auto validate_ring = [&](const area::ValidatedRing &ring) {
      if (!provenance_valid(ring.provenance, input.scene_revision,
                            aggregate_support, area_support_count, options))
        return false;
      ready_quality = ready_quality && ring.provenance.quality_state ==
                                           area::QualityState::kReady;
      invalid_quality = invalid_quality || ring.provenance.quality_state ==
                                               area::QualityState::kInvalid;
      metric_origin =
          metric_origin &&
          ring.provenance.geometry_origin != area::GeometryOrigin::kInferred &&
          ring.provenance.geometry_origin != area::GeometryOrigin::kUnknown;
      return true;
    };
    if (!validate_ring(area_input.outer))
      return failure(Status::kInvalidProvenance);
    for (const area::Hole &hole : area_input.holes)
      if (!validate_ring(hole.ring))
        return failure(Status::kInvalidProvenance);

    std::optional<area::Result> calculated;
    if (input.boundary_state == BoundaryState::kClosedAccepted ||
        input.boundary_state == BoundaryState::kOpen) {
      calculated = area::calculate(area_input, options.area_options);
      const bool expected =
          input.boundary_state == BoundaryState::kClosedAccepted
              ? calculated->status == area::Status::kOk &&
                    calculated->area_m2.has_value()
              : calculated->status == area::Status::kTopologyIncomplete &&
                    !calculated->area_m2;
      if (!expected)
        return failure(Status::kAreaRejected);
      layer.area.calculator_status = calculated->status;
    }

    const bool floor_unknown =
        input.height.reference_state == ReferenceState::kFloorUnknown;
    const bool floor_unsupported =
        input.height.reference_state == ReferenceState::kUnsupported;
    const bool open = input.boundary_state == BoundaryState::kOpen;
    const bool unknown = input.boundary_state == BoundaryState::kUnknown;
    const bool unsupported =
        input.boundary_state == BoundaryState::kUnsupported;

    if (floor_unknown)
      add_issue(layer.issues, IssueCode::kFloorReferenceUnknown);
    if (floor_unsupported)
      add_issue(layer.issues, IssueCode::kFloorReferenceUnsupported);
    if (open)
      add_issue(layer.issues, IssueCode::kOpenBoundary);
    if (unknown)
      for (const Fragment &fragment : input.fragments)
        if (fragment.state == EvidenceState::kUnknown)
          add_issue(layer.issues, IssueCode::kUnknownBoundaryEvidence,
                    fragment.id);
    if (unsupported)
      for (const Fragment &fragment : input.fragments)
        if (fragment.state == EvidenceState::kUnsupported)
          add_issue(layer.issues, IssueCode::kUnsupportedBoundaryEvidence,
                    fragment.id);
    if (has_inferred || !metric_origin)
      add_issue(layer.issues, IssueCode::kInferredGeometry);
    if (!ready_quality)
      add_issue(layer.issues, IssueCode::kQualityNotReady);

    if (floor_unsupported || unsupported) {
      layer.validity = Availability::kUnsupported;
      layer.area.availability = Availability::kUnsupported;
    } else if (floor_unknown || unknown || has_inferred || !metric_origin ||
               invalid_quality) {
      layer.validity = Availability::kUnavailable;
      layer.area.availability = Availability::kUnavailable;
    } else if (open || !ready_quality) {
      layer.validity = Availability::kProvisional;
      layer.area.availability = Availability::kProvisional;
    } else {
      layer.validity = Availability::kReady;
      layer.area.availability = Availability::kReady;
      layer.area.area_m2 = calculated->area_m2;
      layer.area.assessments = calculated->assessments;
    }

    return {Status::kOk, std::move(layer)};
  } catch (const std::bad_alloc &) {
    return failure(Status::kResourceLimit);
  } catch (...) {
    return failure(Status::kInternalFailure);
  }
}

} // namespace hss::plan_semantics

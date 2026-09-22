#include "hss/annotation_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hss::annotation_geometry {
namespace {

Result failure(Status status) { return {status, std::nullopt}; }

bool valid_options(const Options &o) {
  return o.max_annotations > 0U && o.max_annotations <= kHardMaxAnnotations &&
         o.max_evidence_ids > 0U && o.max_evidence_ids <= kHardMaxEvidenceIds &&
         std::isfinite(o.max_abs_coordinate_m) && o.max_abs_coordinate_m > 0.0 &&
         std::isfinite(o.min_length_m) && o.min_length_m > 0.0 &&
         o.min_length_m < o.max_abs_coordinate_m;
}

bool usable_id(std::string_view value) {
  return !value.empty() && value.size() <= 1'024U;
}

bool finite_point(area::Point2d point, const Options &o) {
  return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
         std::fabs(point.x_m) <= o.max_abs_coordinate_m &&
         std::fabs(point.y_m) <= o.max_abs_coordinate_m;
}

bool valid_kind(Kind value) {
  return value == Kind::kLength || value == Kind::kAngle ||
         value == Kind::kHeight || value == Kind::kLabel;
}

bool valid_source(EvidenceSource value) {
  return value == EvidenceSource::kObserved || value == EvidenceSource::kFitted ||
         value == EvidenceSource::kUserMeasured ||
         value == EvidenceSource::kInferred || value == EvidenceSource::kUnknown;
}

bool valid_unit(DisplayUnit value) {
  return value == DisplayUnit::kMetres || value == DisplayUnit::kMillimetres ||
         value == DisplayUnit::kFeet;
}

bool metric_source(EvidenceSource value) {
  return value == EvidenceSource::kObserved || value == EvidenceSource::kFitted;
}

double distance(const area::Point2d &first, const area::Point2d &second) {
  const long double dx = static_cast<long double>(first.x_m) - second.x_m;
  const long double dy = static_cast<long double>(first.y_m) - second.y_m;
  return static_cast<double>(std::sqrt(dx * dx + dy * dy));
}

std::optional<double> angle(const Annotation &annotation, const Options &options) {
  const auto &vertex = annotation.anchors[0].position_m;
  const auto &first = annotation.anchors[1].position_m;
  const auto &second = annotation.anchors[2].position_m;
  const long double ax = static_cast<long double>(first.x_m) - vertex.x_m;
  const long double ay = static_cast<long double>(first.y_m) - vertex.y_m;
  const long double bx = static_cast<long double>(second.x_m) - vertex.x_m;
  const long double by = static_cast<long double>(second.y_m) - vertex.y_m;
  const long double first_length = std::sqrt(ax * ax + ay * ay);
  const long double second_length = std::sqrt(bx * bx + by * by);
  if (first_length < options.min_length_m || second_length < options.min_length_m)
    return std::nullopt;
  const long double cosine = (ax * bx + ay * by) / (first_length * second_length);
  return static_cast<double>(std::acos(std::clamp(cosine, -1.0L, 1.0L)));
}

bool plan_lineage(const plan_semantics::PlanLayer &plan,
                  std::unordered_set<std::string> &ids) {
  if (!usable_id(plan.scene_revision))
    return false;
  for (const auto &fragment : plan.fragments) {
    if (!usable_id(fragment.id) || fragment.source_revision != plan.scene_revision ||
        !ids.insert(fragment.id).second)
      return false;
    for (const std::string &support : fragment.support_ids) {
      if (!usable_id(support))
        return false;
      ids.insert(support);
    }
  }
  return true;
}

Availability plan_availability(plan_semantics::Availability value) {
  switch (value) {
  case plan_semantics::Availability::kReady:
    return Availability::kReady;
  case plan_semantics::Availability::kProvisional:
    return Availability::kProvisional;
  case plan_semantics::Availability::kUnavailable:
  case plan_semantics::Availability::kUnsupported:
  case plan_semantics::Availability::kNotApplicable:
    return Availability::kUnavailable;
  }
  return Availability::kUnavailable;
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk: return "ok";
  case Status::kInvalidOptions: return "invalid_options";
  case Status::kInvalidPlan: return "invalid_plan";
  case Status::kInvalidAnnotation: return "invalid_annotation";
  case Status::kInvalidProvenance: return "invalid_provenance";
  case Status::kInvalidGeometry: return "invalid_geometry";
  case Status::kInvalidMeasurement: return "invalid_measurement";
  case Status::kResourceLimitExceeded: return "resource_limit_exceeded";
  case Status::kInternalFailure: return "internal_failure";
  }
  return "unknown";
}

Result resolve(const Input &input, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  if (input.annotations.size() > options.max_annotations)
    return failure(Status::kResourceLimitExceeded);
  try {
    std::unordered_set<std::string> lineage_ids;
    if (!plan_lineage(input.plan, lineage_ids))
      return failure(Status::kInvalidPlan);
    const Availability inherited_availability = plan_availability(input.plan.validity);

    Layout layout;
    layout.scene_revision = input.plan.scene_revision;
    layout.annotations.reserve(input.annotations.size());
    std::unordered_set<std::string> annotation_ids;
    size_t evidence_count = 0U;

    for (const Annotation &value : input.annotations) {
      if (!usable_id(value.id) || !annotation_ids.insert(value.id).second ||
          !valid_kind(value.kind) || !valid_source(value.evidence_source) ||
          !valid_unit(value.display_unit) ||
          value.display_precision > options.max_display_precision ||
          value.source_revision != input.plan.scene_revision ||
          (value.uncertainty_m && (!std::isfinite(*value.uncertainty_m) ||
                                   *value.uncertainty_m < 0.0)))
        return failure(Status::kInvalidAnnotation);
      const size_t required_anchors = value.kind == Kind::kAngle ? 3U :
                                      value.kind == Kind::kLabel ? 1U : 2U;
      if (value.anchors.size() != required_anchors)
        return failure(Status::kInvalidGeometry);
      for (const Anchor &anchor : value.anchors) {
        if (!usable_id(anchor.geometry_id) ||
            !lineage_ids.contains(anchor.geometry_id) ||
            !finite_point(anchor.position_m, options))
          return failure(Status::kInvalidProvenance);
      }
      if (value.evidence_source == EvidenceSource::kUserMeasured) {
        if (!value.user_measured_value_m || !std::isfinite(*value.user_measured_value_m) ||
            *value.user_measured_value_m < 0.0 || value.evidence_ids.empty())
          return failure(Status::kInvalidMeasurement);
      } else if (value.user_measured_value_m) {
        return failure(Status::kInvalidMeasurement);
      }
      for (const std::string &evidence : value.evidence_ids) {
        if (!usable_id(evidence) || evidence_count == options.max_evidence_ids)
          return failure(Status::kInvalidProvenance);
        if (metric_source(value.evidence_source) &&
            !lineage_ids.contains(evidence))
          return failure(Status::kInvalidProvenance);
        ++evidence_count;
      }

      ResolvedAnnotation resolved;
      resolved.annotation = value;
      if (value.kind == Kind::kLabel) {
        if (value.user_measured_value_m || !value.evidence_ids.empty())
          return failure(Status::kInvalidMeasurement);
        resolved.availability = Availability::kReady;
      } else if (value.evidence_source == EvidenceSource::kUserMeasured) {
        resolved.availability = inherited_availability;
        if (inherited_availability != Availability::kUnavailable)
          resolved.metric_value = value.user_measured_value_m;
      } else if (metric_source(value.evidence_source)) {
        if (value.evidence_ids.empty())
          return failure(Status::kInvalidProvenance);
        resolved.availability = inherited_availability;
        if (inherited_availability != Availability::kUnavailable) {
          if (value.kind == Kind::kAngle) {
            resolved.metric_value = angle(value, options);
            if (!resolved.metric_value)
              return failure(Status::kInvalidGeometry);
          } else {
            resolved.metric_value = distance(value.anchors[0].position_m,
                                             value.anchors[1].position_m);
            if (*resolved.metric_value < options.min_length_m)
              return failure(Status::kInvalidGeometry);
          }
        }
      } else {
        // Retain explicit provisional geometry but publish no numerical claim.
        resolved.availability = Availability::kProvisional;
      }
      layout.annotations.push_back(std::move(resolved));
    }
    return {Status::kOk, std::move(layout)};
  } catch (const std::bad_alloc &) {
    return failure(Status::kResourceLimitExceeded);
  } catch (...) {
    return failure(Status::kInternalFailure);
  }
}

} // namespace hss::annotation_geometry

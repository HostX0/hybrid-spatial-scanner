#include "hss/appearance_isolation.hpp"

#include <cmath>
#include <map>
#include <new>
#include <string_view>
#include <utility>

namespace hss::appearance {
namespace {

struct TargetKey {
  std::string entity_id;
  uint64_t revision{0U};

  bool operator<(const TargetKey &other) const {
    return entity_id < other.entity_id ||
           (entity_id == other.entity_id && revision < other.revision);
  }
};

bool valid_identifier(std::string_view value) {
  if (value.empty() || value.size() > 128U)
    return false;
  for (const unsigned char character : value) {
    const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9');
    if (!alphanumeric && character != '-' && character != '_' &&
        character != '.') {
      return false;
    }
  }
  return true;
}

bool valid(Verification verification) {
  switch (verification) {
  case Verification::kUnknown:
  case Verification::kVerified:
  case Verification::kRejected:
    return true;
  }
  return false;
}

bool input_valid(const MetricTargetRef &target) {
  return valid_identifier(target.entity_id) && target.measured_scene_revision != 0U;
}

bool input_valid(const KeyframeAppearance &candidate) {
  return valid_identifier(candidate.keyframe_id) &&
         valid_identifier(candidate.local_asset_id) &&
         candidate.source_scene_revision != 0U && valid(candidate.calibration) &&
         valid(candidate.accepted_transform) &&
         valid(candidate.visibility_consistency) &&
         std::isfinite(candidate.visual_confidence) &&
         candidate.visual_confidence >= 0.0 && candidate.visual_confidence <= 1.0;
}

TargetKey target_key(const MetricTargetRef &target) {
  return {target.entity_id, target.measured_scene_revision};
}

Status verification_status(const KeyframeAppearance &candidate) {
  if (candidate.calibration == Verification::kUnknown)
    return Status::kUnknownCalibration;
  if (candidate.calibration == Verification::kRejected)
    return Status::kRejectedCalibration;
  if (candidate.accepted_transform == Verification::kUnknown)
    return Status::kUnknownTransform;
  if (candidate.accepted_transform == Verification::kRejected)
    return Status::kRejectedTransform;
  if (candidate.visibility_consistency == Verification::kUnknown)
    return Status::kUnknownVisibility;
  if (candidate.visibility_consistency == Verification::kRejected)
    return Status::kRejectedVisibility;
  return Status::kApplied;
}

} // namespace

struct AppearanceIsolation::Impl {
  std::map<TargetKey, AppearanceBinding> bindings;
};

AppearanceIsolation::AppearanceIsolation(Options options) : options_(options) {
  if (options_.maximum_bindings == 0U ||
      options_.maximum_bindings > kHardMaximumBindings) {
    return;
  }
  try {
    impl_ = std::make_unique<Impl>();
  } catch (const std::bad_alloc &) {
    // valid() stays false and callers receive an explicit internal failure.
  }
}

AppearanceIsolation::~AppearanceIsolation() = default;

bool AppearanceIsolation::valid() const { return static_cast<bool>(impl_); }

size_t AppearanceIsolation::binding_count() const {
  return impl_ ? impl_->bindings.size() : 0U;
}

std::optional<AppearanceBinding>
AppearanceIsolation::binding_for(const MetricTargetRef &target) const {
  if (!impl_ || !input_valid(target))
    return std::nullopt;
  const auto found = impl_->bindings.find(target_key(target));
  return found == impl_->bindings.end() ? std::nullopt
                                        : std::optional<AppearanceBinding>(found->second);
}

UpdateResult AppearanceIsolation::submit(const MetricTargetRef &target,
                                         const KeyframeAppearance &candidate) {
  UpdateResult result;
  if (!impl_) {
    result.status = options_.maximum_bindings == 0U ||
                            options_.maximum_bindings > kHardMaximumBindings
                        ? Status::kInvalidOptions
                        : Status::kInternalFailure;
    return result;
  }
  if (!input_valid(target) || !input_valid(candidate)) {
    result.status = Status::kInvalidInput;
    return result;
  }

  const TargetKey key = target_key(target);
  const auto existing = impl_->bindings.find(key);
  const auto retain_existing = [&]() {
    if (existing != impl_->bindings.end()) {
      result.retained_existing_binding = true;
      result.active_binding = existing->second;
    }
  };

  if (candidate.source_scene_revision != target.measured_scene_revision) {
    result.status = Status::kStaleSceneRevision;
    retain_existing();
    return result;
  }
  result.status = verification_status(candidate);
  if (result.status != Status::kApplied) {
    retain_existing();
    return result;
  }
  if (existing == impl_->bindings.end() &&
      impl_->bindings.size() >= options_.maximum_bindings) {
    result.status = Status::kResourceLimit;
    return result;
  }

  AppearanceBinding next{target, candidate.keyframe_id, candidate.local_asset_id,
                         candidate.visual_confidence};
  try {
    if (existing == impl_->bindings.end()) {
      const auto inserted = impl_->bindings.emplace(key, std::move(next));
      result.active_binding = inserted.first->second;
    } else {
      existing->second = std::move(next);
      result.active_binding = existing->second;
    }
  } catch (const std::bad_alloc &) {
    result.status = Status::kResourceLimit;
    retain_existing();
    return result;
  } catch (...) {
    result.status = Status::kInternalFailure;
    retain_existing();
    return result;
  }
  return result;
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kApplied:
    return "applied";
  case Status::kUnknownCalibration:
    return "unknown_calibration";
  case Status::kRejectedCalibration:
    return "rejected_calibration";
  case Status::kUnknownTransform:
    return "unknown_transform";
  case Status::kRejectedTransform:
    return "rejected_transform";
  case Status::kUnknownVisibility:
    return "unknown_visibility";
  case Status::kRejectedVisibility:
    return "rejected_visibility";
  case Status::kStaleSceneRevision:
    return "stale_scene_revision";
  case Status::kInvalidInput:
    return "invalid_input";
  case Status::kInvalidOptions:
    return "invalid_options";
  case Status::kResourceLimit:
    return "resource_limit";
  case Status::kInternalFailure:
    return "internal_failure";
  }
  return "unknown_status";
}

} // namespace hss::appearance

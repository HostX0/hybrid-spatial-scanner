#include "hss/target_tools.hpp"

#include <cmath>
#include <unordered_set>

namespace hss::target_tools {
namespace {

bool valid_id(const std::string &id) {
  return !id.empty() && id.size() <= 256U;
}

bool valid_support_ids(const std::vector<std::string> &support_ids) {
  if (support_ids.empty() || support_ids.size() > 4'096U)
    return false;
  std::unordered_set<std::string_view> unique;
  unique.reserve(support_ids.size());
  for (const std::string &id : support_ids) {
    if (!valid_id(id) || !unique.insert(id).second)
      return false;
  }
  return true;
}

bool finite_point(const ProjectPoint &point) {
  return std::isfinite(point.x_m) && std::isfinite(point.y_m) &&
         std::isfinite(point.z_m);
}

bool valid_target_shape(const Target &target) {
  if (!valid_id(target.id))
    return false;
  if (target.state == TargetState::kPending)
    return target.provenance == TargetProvenance::kPendingNoDepth &&
           !target.project_position_m && target.support_ids.empty();
  return target.state == TargetState::kConfirmed &&
         target.provenance != TargetProvenance::kPendingNoDepth &&
         target.project_position_m && finite_point(*target.project_position_m) &&
         valid_support_ids(target.support_ids);
}

bool valid_face_evidence(const RibFaceEvidence &evidence,
                         const Target &parent,
                         uint64_t current_scene_revision) {
  return evidence.target.id != parent.id &&
         evidence.target.scene_revision == current_scene_revision &&
         evidence.target.state == TargetState::kConfirmed &&
         valid_target_shape(evidence.target);
}

ToolResult result(Status status, const Target &target) {
  return {.status = status, .target = target};
}

} // namespace

ToolResult prepare_target(const Target &target, uint64_t current_scene_revision) {
  if (!valid_target_shape(target))
    return result(Status::kInvalidTarget, target);
  if (target.scene_revision != current_scene_revision)
    return result(Status::kStaleTarget, target);
  if (target.state == TargetState::kPending)
    return result(Status::kPendingTarget, target);
  return {.status = Status::kReady, .target = target,
          .command_may_be_created = true};
}

ToolResult evaluate_tool(
    Tool tool, const Target &parent, uint64_t current_scene_revision,
    const std::array<std::optional<RibFaceEvidence>, 3U> &evidence) {
  if (tool != Tool::kMarkProtrusion)
    return result(Status::kUnsupportedTool, parent);
  ToolResult parent_result = prepare_target(parent, current_scene_revision);
  if (parent_result.status != Status::kReady)
    return parent_result;

  constexpr std::array<RibFace, 3U> kRequiredFaces{
      RibFace::kFirstReturn, RibFace::kFront, RibFace::kSecondReturn};
  for (size_t index = 0U; index < kRequiredFaces.size(); ++index) {
    const std::optional<RibFaceEvidence> &candidate = evidence[index];
    if (!candidate || candidate->face != kRequiredFaces[index]) {
      return {.status = Status::kAwaitingFaceEvidence,
              .target = parent,
              .requested_face = kRequiredFaces[index]};
    }
    if (!valid_face_evidence(*candidate, parent, current_scene_revision))
      return result(Status::kInvalidEvidence, parent);
  }
  return {.status = Status::kReady, .target = parent,
          .command_may_be_created = true};
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kReady: return "ready";
  case Status::kPendingTarget: return "pending_target";
  case Status::kAwaitingFaceEvidence: return "awaiting_face_evidence";
  case Status::kStaleTarget: return "stale_target";
  case Status::kInvalidTarget: return "invalid_target";
  case Status::kInvalidEvidence: return "invalid_evidence";
  case Status::kUnsupportedTool: return "unsupported_tool";
  }
  return "unknown";
}

const char *provenance_name(TargetProvenance provenance) {
  switch (provenance) {
  case TargetProvenance::kPendingNoDepth: return "pending_no_depth";
  case TargetProvenance::kObservedSupport: return "observed_support";
  case TargetProvenance::kFittedSupport: return "fitted_support";
  }
  return "unknown";
}

const char *rib_face_name(RibFace face) {
  switch (face) {
  case RibFace::kFirstReturn: return "first_return";
  case RibFace::kFront: return "front";
  case RibFace::kSecondReturn: return "second_return";
  }
  return "unknown";
}

} // namespace hss::target_tools

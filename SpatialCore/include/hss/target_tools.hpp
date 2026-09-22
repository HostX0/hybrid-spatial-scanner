#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hss::target_tools {

// Project-space coordinates are metres in the documented right-handed basis
// (+X=U, +Y=N, -Z=V after the accepted floor reference). A pending target
// deliberately carries no coordinate.
struct ProjectPoint {
  double x_m{0.0};
  double y_m{0.0};
  double z_m{0.0};
  bool operator==(const ProjectPoint &) const = default;
};

enum class TargetProvenance : uint8_t {
  kPendingNoDepth,
  kObservedSupport,
  kFittedSupport,
};

enum class TargetState : uint8_t { kPending, kConfirmed };

// A target is selection state, not a new measurement. Confirmed coordinates
// are supplied by an accepted observation or fit and are never synthesized by
// this module.
struct Target {
  std::string id;
  uint64_t scene_revision{0U};
  TargetState state{TargetState::kPending};
  TargetProvenance provenance{TargetProvenance::kPendingNoDepth};
  std::optional<ProjectPoint> project_position_m;
  std::vector<std::string> support_ids;
  bool operator==(const Target &) const = default;
};

enum class Tool : uint8_t { kMarkProtrusion };

enum class RibFace : uint8_t {
  kFirstReturn,
  kFront,
  kSecondReturn,
};

// Support refers to separately captured/fitted evidence for one requested
// face. It carries no dimensions and does not imply equal or perpendicular
// returns.
struct RibFaceEvidence {
  RibFace face{RibFace::kFirstReturn};
  Target target;
  bool operator==(const RibFaceEvidence &) const = default;
};

enum class Status : uint8_t {
  kReady,
  kPendingTarget,
  kAwaitingFaceEvidence,
  kStaleTarget,
  kInvalidTarget,
  kInvalidEvidence,
  kUnsupportedTool,
};

struct ToolResult {
  Status status{Status::kInvalidTarget};
  Target target{};
  // Present only while the tool needs another independently supported face.
  std::optional<RibFace> requested_face;
  bool command_may_be_created{false};
};

// Validates a user selection. A no-depth tap becomes pending. A confirmed
// selection requires source support, a finite project-space point in metres,
// and an explicit observed/fitted provenance.
[[nodiscard]] ToolResult prepare_target(const Target &target,
                                        uint64_t current_scene_revision);

// Evaluates the bounded protrusion journey against a pinned parent target.
// It requires the exact current scene revision before a command can be made.
// Missing evidence is returned as one actionable face request, in order.
[[nodiscard]] ToolResult evaluate_tool(
    Tool tool, const Target &parent, uint64_t current_scene_revision,
    const std::array<std::optional<RibFaceEvidence>, 3U> &evidence);

[[nodiscard]] const char *status_name(Status status);
[[nodiscard]] const char *provenance_name(TargetProvenance provenance);
[[nodiscard]] const char *rib_face_name(RibFace face);

} // namespace hss::target_tools

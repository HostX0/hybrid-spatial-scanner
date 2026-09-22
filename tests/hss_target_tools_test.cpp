#include "hss/target_tools.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {
using namespace hss::target_tools;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Target pending(uint64_t revision = 7U) {
  return {.id = "tap-1", .scene_revision = revision,
          .state = TargetState::kPending,
          .provenance = TargetProvenance::kPendingNoDepth};
}

Target confirmed(std::string id, uint64_t revision = 7U) {
  return {.id = std::move(id),
          .scene_revision = revision,
          .state = TargetState::kConfirmed,
          .provenance = TargetProvenance::kObservedSupport,
          .project_position_m = ProjectPoint{1.25, 0.40, -2.0},
          .support_ids = {"mesh-observation-1"}};
}

RibFaceEvidence face(RibFace face, std::string id) {
  return {.face = face, .target = confirmed(std::move(id))};
}

void test_pending_tap_is_preserved_without_geometry() {
  const Target tap = pending();
  const ToolResult prepared = prepare_target(tap, 7U);
  check(prepared.status == Status::kPendingTarget &&
            prepared.target == tap && !prepared.command_may_be_created,
        "unsupported tap stays pending and cannot create a command");
  const auto tool = evaluate_tool(Tool::kMarkProtrusion, tap, 7U, {});
  check(tool.status == Status::kPendingTarget &&
            !tool.target.project_position_m && tool.target.support_ids.empty(),
        "pending target does not acquire invented metric geometry or support");
}

void test_revision_and_provenance_preconditions() {
  Target target = confirmed("parent");
  check(prepare_target(target, 8U).status == Status::kStaleTarget,
        "a selected target cannot be reused across a scene revision");
  target.provenance = TargetProvenance::kPendingNoDepth;
  check(prepare_target(target, 7U).status == Status::kInvalidTarget,
        "a confirmed target cannot claim pending provenance");
  target = confirmed("parent");
  target.project_position_m->x_m = std::numeric_limits<double>::quiet_NaN();
  check(prepare_target(target, 7U).status == Status::kInvalidTarget,
        "nonfinite project-space metre coordinates are rejected");
}

void test_protrusion_requests_missing_faces_in_order() {
  const Target parent = confirmed("parent");
  std::array<std::optional<RibFaceEvidence>, 3U> evidence{};
  auto result = evaluate_tool(Tool::kMarkProtrusion, parent, 7U, evidence);
  check(result.status == Status::kAwaitingFaceEvidence &&
            result.requested_face == RibFace::kFirstReturn &&
            !result.command_may_be_created,
        "rib tool requests the first unsupported return");
  evidence[0] = face(RibFace::kFirstReturn, "return-a");
  result = evaluate_tool(Tool::kMarkProtrusion, parent, 7U, evidence);
  check(result.requested_face == RibFace::kFront,
        "first supported return advances to the front face");
  evidence[1] = face(RibFace::kFront, "front");
  result = evaluate_tool(Tool::kMarkProtrusion, parent, 7U, evidence);
  check(result.requested_face == RibFace::kSecondReturn,
        "front support advances to the second return");
  evidence[2] = face(RibFace::kSecondReturn, "return-b");
  result = evaluate_tool(Tool::kMarkProtrusion, parent, 7U, evidence);
  check(result.status == Status::kReady && result.command_may_be_created,
        "all independently supported faces make an intent command eligible");
}

void test_invalid_evidence_does_not_retarget() {
  const Target parent = confirmed("parent");
  std::array<std::optional<RibFaceEvidence>, 3U> evidence{
      face(RibFace::kFirstReturn, "return-a"),
      face(RibFace::kFront, "front"),
      face(RibFace::kSecondReturn, "return-b")};
  evidence[1]->target.scene_revision = 6U;
  const auto result = evaluate_tool(Tool::kMarkProtrusion, parent, 7U, evidence);
  check(result.status == Status::kInvalidEvidence &&
            result.target == parent && !result.command_may_be_created,
        "stale face evidence is rejected without retargeting the parent");
}

} // namespace

int main() {
  test_pending_tap_is_preserved_without_geometry();
  test_revision_and_provenance_preconditions();
  test_protrusion_requests_missing_faces_in_order();
  test_invalid_evidence_does_not_retarget();
  return failures == 0 ? 0 : 1;
}

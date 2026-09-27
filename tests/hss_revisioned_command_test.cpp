#include "hss/revisioned_command.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using hss::command::ApplyResult;
using hss::command::CommandEnvelope;
using hss::command::CurrentTarget;
using hss::command::Outcome;
using hss::command::RevisionedCommandGate;
using hss::command::SceneState;
using hss::command::Status;

constexpr const char *kProjectId =
    "d5708cd1-a901-5786-b7f7-d422ad92be92";
constexpr const char *kCommandId =
    "b1a1e1ae-ec03-5401-af12-faa045864665";
constexpr const char *kTargetId =
    "339c43e0-f6c9-56fd-bbd2-f39bbfe6fe2b";

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string load(std::string_view relative_path) {
  std::ifstream input(std::string(HSS_SOURCE_DIR) + "/" +
                          std::string(relative_path),
                      std::ios::binary);
  if (!input)
    throw std::runtime_error("could not open revisioned-command fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

CommandEnvelope command(std::string id = kCommandId,
                        std::string revision = "3") {
  return {.command_id = std::move(id),
          .project_id = kProjectId,
          .expected_scene_revision = std::move(revision),
          .event_sequence = "109",
          .target_ids = {kTargetId},
          .allow_stale_unchanged_lineage = false};
}

SceneState scene(uint64_t revision = 3U, uint64_t lineage_revision = 2U) {
  return {.project_id = kProjectId,
          .revision = revision,
          .current_targets = {{kTargetId, lineage_revision}}};
}

ApplyResult applied_result(const std::string &result_bytes,
                           SceneState &candidate) {
  ++candidate.revision;
  return {Outcome::kApplied, result_bytes};
}

void expect_not_applied(Status expected, CommandEnvelope envelope,
                        const std::string &bytes, SceneState initial,
                        const std::string &message) {
  RevisionedCommandGate gate;
  const SceneState before = initial;
  int calls = 0;
  const auto result = gate.submit(
      envelope, bytes, initial,
      [&calls](const CommandEnvelope &, SceneState &candidate) {
        ++calls;
        ++candidate.revision;
        return ApplyResult{Outcome::kApplied, "{}"};
      });
  check(result.status == expected,
        message + " returns " + hss::command::status_name(expected));
  check(calls == 0, message + " is rejected before apply");
  check(initial == before, message + " preserves the prior accepted scene");
  check(gate.record_count() == 0U, message + " creates no accepted record");
}

void test_sha256_and_exact_byte_idempotency() {
  const std::string exact =
      load("fixtures/revisioned_commands/protect_feature.exact.json");
  const std::string different = load(
      "fixtures/revisioned_commands/protect_feature.same-json-different-bytes.json");
  const std::string result_bytes = load(
      "fixtures/revisioned_commands/protect_feature.not_committed.result.json");
  check(hss::command::sha256_hex(hss::command::sha256("abc")) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 matches the published abc test vector");

  RevisionedCommandGate gate;
  SceneState accepted = scene();
  int calls = 0;
  const auto first = gate.submit(
      command(), exact, accepted,
      [&calls, &result_bytes](const CommandEnvelope &, SceneState &candidate) {
        ++calls;
        return applied_result(result_bytes, candidate);
      });
  check(first.status == Status::kApplied &&
            first.outcome == Outcome::kApplied,
        "first exact command applies");
  check(hss::command::sha256_hex(first.command_sha256) ==
            "d10c93db8bd6336873575371731055f8eb8bbea81c837b9fe47e7d9773ccc5da",
        "fixture digest covers its exact bytes, including formatting and final newline");
  check(accepted.revision == 4U && calls == 1,
        "first application publishes one new accepted revision");
  check(!first.durably_committed,
        "volatile gate never reports durable completion");
  check(gate.record_count() == 1U, "first result is retained once");

  const SceneState after_first = accepted;
  const auto retry = gate.submit(
      command(), exact, accepted,
      [&calls](const CommandEnvelope &, SceneState &candidate) {
        ++calls;
        ++candidate.revision;
        return ApplyResult{Outcome::kApplied, "must not run"};
      });
  check(retry.status == Status::kStoredRetry &&
            retry.outcome == Outcome::kApplied,
        "byte-identical command ID retry returns the stored outcome");
  check(retry.result_utf8_bytes == result_bytes &&
            retry.command_sha256 == first.command_sha256,
        "retry returns the exact stored result bytes and original digest");
  check(calls == 1 && accepted == after_first && gate.record_count() == 1U,
        "retry neither reapplies nor advances accepted state");

  const auto mismatch = gate.submit(
      command(), different, accepted,
      [&calls](const CommandEnvelope &, SceneState &candidate) {
        ++calls;
        ++candidate.revision;
        return ApplyResult{Outcome::kApplied, "must not run"};
      });
  check(mismatch.status == Status::kDuplicateMismatch,
        "semantically equivalent JSON with different bytes is a duplicate mismatch");
  check(mismatch.command_sha256 != first.command_sha256,
        "different exact bytes retain a different SHA-256 digest");
  check(calls == 1 && accepted == after_first && gate.record_count() == 1U,
        "duplicate mismatch does not apply or overwrite the stored record");
}

void test_project_revision_and_target_validation_precede_apply() {
  const std::string bytes = "{\"synthetic\":true}";

  CommandEnvelope wrong_project = command();
  wrong_project.project_id = "11111111-1111-4111-8111-111111111111";
  expect_not_applied(Status::kProjectMismatch, wrong_project, bytes, scene(),
                     "wrong project");
  expect_not_applied(Status::kFutureRevision, command(kCommandId, "4"), bytes,
                     scene(), "future expected revision");
  expect_not_applied(Status::kStaleRevision, command(kCommandId, "2"), bytes,
                     scene(), "stale exact-revision command");

  CommandEnvelope missing_target = command();
  missing_target.target_ids = {
      "22222222-2222-4222-8222-222222222222"};
  expect_not_applied(Status::kTargetNotCurrent, missing_target, bytes, scene(),
                     "non-current target");

  CommandEnvelope duplicate_target = command();
  duplicate_target.target_ids.push_back(kTargetId);
  expect_not_applied(Status::kInvalidCommand, duplicate_target, bytes, scene(),
                     "duplicate target metadata");

  expect_not_applied(Status::kInvalidCommand, command(kCommandId, "03"),
                     bytes, scene(), "noncanonical revision spelling");
  expect_not_applied(Status::kInvalidCommand,
                     command(kCommandId, "18446744073709551616"), bytes,
                     scene(), "revision above uint64 maximum");
}

void test_explicit_stale_reconciliation_requires_unchanged_lineage() {
  const std::string bytes = "{\"stale\":\"synthetic\"}";
  const std::string result_bytes = "{\"durability\":\"not_committed\"}";

  CommandEnvelope unchanged = command(kCommandId, "3");
  unchanged.allow_stale_unchanged_lineage = true;
  SceneState current = scene(5U, 2U);
  int calls = 0;
  RevisionedCommandGate gate;
  const auto reconciled = gate.submit(
      unchanged, bytes, current,
      [&calls, &result_bytes](const CommandEnvelope &, SceneState &candidate) {
        ++calls;
        return applied_result(result_bytes, candidate);
      });
  check(reconciled.status == Status::kApplied && current.revision == 6U &&
            calls == 1,
        "explicitly permitted stale command applies when target lineage is proven unchanged");

  CommandEnvelope changed =
      command("33333333-3333-4333-8333-333333333333", "3");
  changed.allow_stale_unchanged_lineage = true;
  expect_not_applied(Status::kTargetLineageChanged, changed, bytes,
                     scene(5U, 4U), "stale changed-lineage target");

  CommandEnvelope replaced =
      command("44444444-4444-4444-8444-444444444444", "3");
  replaced.allow_stale_unchanged_lineage = true;
  SceneState replacement = scene(5U, 4U);
  replacement.current_targets = {
      {"55555555-5555-4555-8555-555555555555", 4U}};
  expect_not_applied(Status::kTargetLineageChanged, replaced, bytes,
                     replacement, "stale replaced target identity");

  CommandEnvelope targetless =
      command("66666666-6666-4666-8666-666666666666", "3");
  targetless.allow_stale_unchanged_lineage = true;
  targetless.target_ids.clear();
  expect_not_applied(Status::kTargetLineageChanged, targetless, bytes,
                     scene(5U, 2U), "targetless stale command");
}

void test_conflict_and_invalid_candidate_are_atomic() {
  const std::string bytes = "{\"conflict\":true}";
  const std::string conflict_result =
      "{\"state\":\"conflict\",\"durability\":\"not_committed\"}";
  SceneState accepted = scene();
  const SceneState before = accepted;
  RevisionedCommandGate gate;
  int calls = 0;
  const auto conflict = gate.submit(
      command(), bytes, accepted,
      [&calls, &conflict_result](const CommandEnvelope &,
                                 SceneState &candidate) {
        ++calls;
        candidate.revision = 99U;
        candidate.current_targets.clear();
        return ApplyResult{Outcome::kConflict, conflict_result};
      });
  check(conflict.status == Status::kConflict &&
            conflict.outcome == Outcome::kConflict,
        "apply conflict is explicit");
  check(accepted == before && calls == 1,
        "conflicting candidate mutations cannot replace accepted state");
  check(gate.record_count() == 1U,
        "conflict result is retained for deterministic retry");

  const auto retry = gate.submit(
      command(), bytes, accepted,
      [&calls](const CommandEnvelope &, SceneState &) {
        ++calls;
        return ApplyResult{Outcome::kApplied, "must not run"};
      });
  check(retry.status == Status::kStoredRetry &&
            retry.outcome == Outcome::kConflict &&
            retry.result_utf8_bytes == conflict_result && calls == 1,
        "conflict retry returns the exact stored conflict without applying");

  SceneState invalid_scene = scene();
  const SceneState invalid_before = invalid_scene;
  RevisionedCommandGate invalid_gate;
  const auto invalid = invalid_gate.submit(
      command("77777777-7777-4777-8777-777777777777"),
      "{\"invalid_candidate\":true}", invalid_scene,
      [](const CommandEnvelope &, SceneState &candidate) {
        candidate.revision += 2U;
        return ApplyResult{Outcome::kApplied, "{}"};
      });
  check(invalid.status == Status::kInvalidApplyResult &&
            invalid_scene == invalid_before && invalid_gate.record_count() == 0U,
        "invalid applied revision is atomic and is not recorded");
}

void test_bounds_utf8_and_record_capacity() {
  hss::command::Options tiny;
  tiny.max_command_bytes = 8U;
  tiny.max_result_bytes = 8U;
  tiny.max_records = 1U;
  tiny.max_targets = 1U;
  RevisionedCommandGate bounded(tiny);
  SceneState accepted = scene();
  const SceneState before = accepted;
  const auto oversized = bounded.submit(
      command(), "123456789", accepted,
      [](const CommandEnvelope &, SceneState &) {
        return ApplyResult{Outcome::kRejected, "{}"};
      });
  check(oversized.status == Status::kResourceLimitExceeded &&
            accepted == before,
        "command byte bound is enforced before apply");

  std::string invalid_utf8(1U, static_cast<char>(0xff));
  const auto invalid = bounded.submit(
      command(), invalid_utf8, accepted,
      [](const CommandEnvelope &, SceneState &) {
        return ApplyResult{Outcome::kRejected, "{}"};
      });
  check(invalid.status == Status::kInvalidCommand && accepted == before,
        "invalid UTF-8 command bytes are rejected");

  const auto rejected = bounded.submit(
      command(), "{}", accepted,
      [](const CommandEnvelope &, SceneState &) {
        return ApplyResult{Outcome::kRejected, "rejected"};
      });
  check(rejected.status == Status::kRejected && bounded.record_count() == 1U &&
            accepted == before,
        "bounded gate retains a rejected result without changing the scene");

  const auto full = bounded.submit(
      command("88888888-8888-4888-8888-888888888888"), "[]", accepted,
      [](const CommandEnvelope &, SceneState &candidate) {
        ++candidate.revision;
        return ApplyResult{Outcome::kApplied, "{}"};
      });
  check(full.status == Status::kResourceLimitExceeded && accepted == before,
        "record capacity prevents untracked application");
}


void test_exact_volatile_abort() {
  RevisionedCommandGate gate;
  auto accepted=scene();
  const auto before=accepted;
  const auto first=command();
  int calls=0;
  const auto apply=[&](const CommandEnvelope &, SceneState &candidate) {
    ++calls;
    return applied_result("{\"applied\":true}",candidate);
  };
  const auto one=gate.submit(first,"first exact bytes",accepted,apply);
  check(one.status==Status::kApplied,"abort fixture applies");
  auto wrong=one.command_sha256; wrong[0]^=1U;
  check(!gate.rollback_last_record(first.command_id,wrong,Outcome::kApplied,4U),
        "abort rejects wrong digest");
  check(!gate.rollback_last_record(first.command_id,one.command_sha256,Outcome::kRejected,4U),
        "abort rejects wrong outcome");
  check(!gate.rollback_last_record(first.command_id,one.command_sha256,Outcome::kApplied,3U),
        "abort rejects wrong revision");
  check(gate.record_count()==1U,"mismatched abort preserves record");
  check(gate.rollback_last_record(first.command_id,one.command_sha256,Outcome::kApplied,4U),
        "exact latest volatile abort succeeds");
  // The outer owner retains its old accepted scene until publication succeeds.
  accepted=before;
  const auto again=gate.submit(first,"first exact bytes",accepted,apply);
  check(again.status==Status::kApplied && calls==2,
        "exact retry after abort actually executes again");
  const auto second=command("b1a1e1ae-ec03-5401-af12-faa045864666","4");
  const auto two=gate.submit(second,"second exact bytes",accepted,apply);
  check(!gate.rollback_last_record(first.command_id,again.command_sha256,Outcome::kApplied,4U),
        "abort cannot remove an older record");
  check(gate.rollback_last_record(second.command_id,two.command_sha256,Outcome::kApplied,5U),
        "abort matches only latest record");
  check(gate.record_count()==1U && gate.find_record(first.command_id)!=nullptr,
        "older accepted record survives abort");
  RevisionedCommandGate pending_gate;
  auto unchanged=scene();
  const auto pending=pending_gate.submit(first,"pending",unchanged,
      [](const CommandEnvelope &,SceneState &){return ApplyResult{Outcome::kRejected,"pending"};});
  check(pending_gate.rollback_last_record(first.command_id,pending.command_sha256,
            Outcome::kRejected,3U) && pending_gate.record_count()==0U,
        "pending rejected volatile record can be aborted exactly");
}

} // namespace

int main() {
  test_exact_volatile_abort();
  test_sha256_and_exact_byte_idempotency();
  test_project_revision_and_target_validation_precede_apply();
  test_explicit_stale_reconciliation_requires_unchanged_lineage();
  test_conflict_and_invalid_candidate_are_atomic();
  test_bounds_utf8_and_record_capacity();
  if (failures == 0)
    std::cout << "revisioned command contract tests passed\n";
  return failures == 0 ? 0 : 1;
}

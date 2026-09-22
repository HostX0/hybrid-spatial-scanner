#include "hss/dynamic_provenance.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace dp = hss::dynamic_provenance;

constexpr const char *kProject = "10000000-0000-4000-8000-000000000001";
constexpr const char *kCommand = "20000000-0000-4000-8000-000000000001";
constexpr const char *kOtherCommand =
    "20000000-0000-4000-8000-000000000002";

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string uuid(uint64_t value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "30000000-0000-4000-8000-000000000000";
  for (size_t index = 0U; index < 12U; ++index) {
    result[result.size() - 1U - index] = digits[value & 0x0fU];
    value >>= 4U;
  }
  return result;
}

dp::SourceReference source(uint64_t identity, uint64_t sequence,
                           dp::SourceKind kind =
                               dp::SourceKind::kDepthFrame) {
  return {kind, uuid(identity), uuid(100U + identity), 7U, sequence};
}

dp::CapturedMesh fixture() {
  dp::CapturedMesh mesh;
  mesh.project_id = kProject;
  mesh.vertices_m = {
      {0.10, 0.10, 0.00}, {0.80, 0.10, 0.00}, {0.10, 0.80, 0.00},
      {1.10, 0.10, 0.00}, {1.80, 0.10, 0.00}, {1.10, 0.80, 0.00},
      {3.00, 0.10, 0.00}, {3.70, 0.10, 0.00}, {3.00, 0.80, 0.00},
      {0.20, 1.10, 0.00}, {0.90, 1.10, 0.00}, {0.20, 1.80, 0.00},
  };
  mesh.sources = {
      source(1U, 12U),
      source(2U, 13U, dp::SourceKind::kMeshAnchor),
      source(3U, 14U),
      source(4U, 30U),
  };
  mesh.triangles = {
      {{{0U, 1U, 2U}}, {0U, 1U}},
      {{{3U, 4U, 5U}}, {0U, 2U}},
      {{{6U, 7U, 8U}}, {0U, 1U}},
      {{{9U, 10U, 11U}}, {3U}},
  };
  return mesh;
}

hss::scene_editing::DynamicExclusion exclusion() {
  hss::scene_editing::DynamicExclusion value;
  value.command_id = kCommand;
  value.project_id = kProject;
  value.min_m = {0.0, 0.0, -0.1};
  value.max_m = {2.0, 2.0, 0.1};
  value.start_sequence = 10U;
  value.end_sequence = 20U;
  return value;
}

dp::VerifiedDynamicEvidence evidence(uint64_t identity, size_t source_index) {
  const dp::CapturedMesh mesh = fixture();
  dp::VerifiedDynamicEvidence value;
  value.evidence_id = uuid(500U + identity);
  value.exclusion_command_id = kCommand;
  value.project_id = kProject;
  value.source = mesh.sources[source_index];
  value.bounds_m = {{0.0, 0.0, -0.05}, {2.0, 2.0, 0.05}};
  value.kind = identity % 2U == 0U
                   ? dp::EvidenceKind::kTemporalDepthChange
                   : dp::EvidenceKind::kTrackedMultiFrameMotion;
  return value;
}

void expect_failure(const dp::CapturedMesh &mesh,
                    const hss::scene_editing::DynamicExclusion &request,
                    const std::vector<dp::VerifiedDynamicEvidence> &evidence,
                    const dp::Options &options, dp::Status expected,
                    const std::string &message) {
  const dp::Result result =
      dp::apply_exclusion(mesh, request, evidence, options);
  check(result.status == expected && !result.layer,
        message + " fails atomically with " + dp::status_name(expected));
}

void test_audit_marker_alone_has_no_effect() {
  const dp::CapturedMesh mesh = fixture();
  const dp::CapturedMesh before = mesh;
  const dp::Result result = dp::apply_exclusion(mesh, exclusion(), {});
  check(result.status == dp::Status::kOk && result.layer.has_value(),
        "typed pending exclusion is accepted as an audit input");
  if (!result.layer)
    return;
  check(result.layer->audit.disposition == dp::Disposition::kNoVerifiedEffect &&
            result.layer->audit.linked_evidence_count == 0U &&
            result.layer->audit.masked_triangle_count == 0U,
        "audit marker without source evidence explicitly records no effect");
  check(std::all_of(result.layer->triangle_mask.begin(),
                    result.layer->triangle_mask.end(),
                    [](uint8_t value) { return value == 0U; }) &&
            result.layer->masked_triangles.empty(),
        "audit marker alone masks no captured triangle");
  check(mesh == before, "raw captured mesh remains value-for-value unchanged");
}

void test_fully_matched_capture_provenance_masks_only_one_triangle() {
  const dp::CapturedMesh mesh = fixture();
  const dp::CapturedMesh before = mesh;
  std::vector<dp::VerifiedDynamicEvidence> verified = {
      evidence(1U, 1U), evidence(2U, 0U)};
  const dp::Result result =
      dp::apply_exclusion(mesh, exclusion(), verified);
  check(result.status == dp::Status::kOk && result.layer.has_value(),
        "fully source-linked dynamic evidence produces a mask layer");
  if (!result.layer)
    return;
  check(result.layer->audit.disposition == dp::Disposition::kVerifiedMask &&
            result.layer->audit.linked_evidence_count == 2U &&
            result.layer->audit.masked_triangle_count == 1U,
        "audit claims an effect only for a verified triangle");
  check(result.layer->triangle_mask ==
            std::vector<uint8_t>({1U, 0U, 0U, 0U}),
        "partial, spatially outside, and out-of-sequence triangles stay visible");
  check(result.layer->masked_triangles.size() == 1U &&
            result.layer->masked_triangles[0].triangle_index == 0U &&
            result.layer->masked_triangles[0].source_indices ==
                std::vector<size_t>({0U, 1U}),
        "masked triangle retains its exact captured source indices");
  check(result.layer->masked_triangles[0].evidence_ids ==
            std::vector<std::string>({uuid(502U), uuid(501U)}),
        "each captured support is paired with explicit verification evidence");
  check(mesh == before,
        "mask construction never rewrites vertices, triangles, or provenance");

  std::reverse(verified.begin(), verified.end());
  const dp::Result replay =
      dp::apply_exclusion(mesh, exclusion(), verified);
  check(replay.status == dp::Status::kOk && replay.layer == result.layer,
        "evidence input order does not change deterministic output");
}

void test_partial_or_unrelated_evidence_cannot_claim_effect() {
  const dp::CapturedMesh mesh = fixture();
  const dp::Result partial =
      dp::apply_exclusion(mesh, exclusion(), {evidence(2U, 0U)});
  check(partial.status == dp::Status::kOk && partial.layer &&
            partial.layer->audit.disposition ==
                dp::Disposition::kNoVerifiedEffect &&
            partial.layer->audit.linked_evidence_count == 1U,
        "one verified support cannot mask a multiply supported triangle");

  dp::VerifiedDynamicEvidence unrelated = evidence(2U, 0U);
  unrelated.exclusion_command_id = kOtherCommand;
  const dp::Result other =
      dp::apply_exclusion(mesh, exclusion(), {unrelated});
  check(other.status == dp::Status::kOk && other.layer &&
            other.layer->audit.disposition ==
                dp::Disposition::kNoVerifiedEffect &&
            other.layer->audit.linked_evidence_count == 0U,
        "evidence for another audit command is valid but has no effect");
}

void test_sequence_capture_epoch_and_spatial_mismatches_fail_closed() {
  const dp::CapturedMesh mesh = fixture();

  dp::VerifiedDynamicEvidence out_of_sequence = evidence(4U, 3U);
  expect_failure(mesh, exclusion(), {out_of_sequence}, dp::Options{},
                 dp::Status::kEvidenceOutsideExclusion,
                 "linked evidence sequence outside the inclusive interval");

  dp::VerifiedDynamicEvidence expanded = evidence(2U, 0U);
  expanded.bounds_m.max_m.x = 2.1;
  expect_failure(mesh, exclusion(), {expanded}, dp::Options{},
                 dp::Status::kEvidenceOutsideExclusion,
                 "verified bounds outside the requested audit region");

  dp::VerifiedDynamicEvidence wrong_epoch = evidence(2U, 0U);
  ++wrong_epoch.source.session_epoch;
  expect_failure(mesh, exclusion(), {wrong_epoch}, dp::Options{},
                 dp::Status::kEvidenceSourceMissing,
                 "evidence cannot cross a captured session epoch");

  dp::VerifiedDynamicEvidence wrong_capture = evidence(2U, 0U);
  wrong_capture.source.capture_id = uuid(9'999U);
  expect_failure(mesh, exclusion(), {wrong_capture}, dp::Options{},
                 dp::Status::kEvidenceSourceMissing,
                 "evidence cannot cross a capture identity");
}

void test_malformed_and_bounded_inputs_fail_atomically() {
  const dp::CapturedMesh mesh = fixture();
  hss::scene_editing::DynamicExclusion bad_request = exclusion();
  bad_request.start_sequence = 21U;
  expect_failure(mesh, bad_request, {}, dp::Options{},
                 dp::Status::kInvalidExclusion,
                 "reversed exclusion interval");

  dp::CapturedMesh invalid = mesh;
  invalid.vertices_m[0].x = std::numeric_limits<double>::quiet_NaN();
  expect_failure(invalid, exclusion(), {}, dp::Options{},
                 dp::Status::kNonFiniteGeometry, "nonfinite captured vertex");

  invalid = mesh;
  invalid.triangles[0].source_indices = {1U, 0U};
  expect_failure(invalid, exclusion(), {}, dp::Options{},
                 dp::Status::kInvalidProvenance,
                 "noncanonical triangle provenance order");

  std::vector<dp::VerifiedDynamicEvidence> duplicate = {
      evidence(2U, 0U), evidence(2U, 1U)};
  expect_failure(mesh, exclusion(), duplicate, dp::Options{},
                 dp::Status::kDuplicateEvidenceId,
                 "duplicate verification identity");

  std::vector<dp::VerifiedDynamicEvidence> ambiguous_source = {
      evidence(2U, 0U), evidence(3U, 0U)};
  expect_failure(mesh, exclusion(), ambiguous_source, dp::Options{},
                 dp::Status::kInvalidEvidence,
                 "multiple linked records for one captured source");

  dp::VerifiedDynamicEvidence invalid_kind = evidence(2U, 0U);
  invalid_kind.kind = static_cast<dp::EvidenceKind>(255U);
  expect_failure(mesh, exclusion(), {invalid_kind}, dp::Options{},
                 dp::Status::kInvalidEvidence,
                 "unknown evidence kind");

  dp::Options bounded;
  bounded.max_triangles = mesh.triangles.size() - 1U;
  expect_failure(mesh, exclusion(), {}, bounded,
                 dp::Status::kInputLimitExceeded,
                 "triangle collection exceeds caller budget");

  dp::Options invalid_options;
  invalid_options.max_abs_coordinate_m = 0.0;
  expect_failure(mesh, exclusion(), {}, invalid_options,
                 dp::Status::kInvalidOptions, "invalid metric extent");
}

} // namespace

int main() {
  test_audit_marker_alone_has_no_effect();
  test_fully_matched_capture_provenance_masks_only_one_triangle();
  test_partial_or_unrelated_evidence_cannot_claim_effect();
  test_sequence_capture_epoch_and_spatial_mismatches_fail_closed();
  test_malformed_and_bounded_inputs_fail_atomically();
  if (failures == 0)
    std::cout << "dynamic provenance tests passed\n";
  return failures == 0 ? 0 : 1;
}

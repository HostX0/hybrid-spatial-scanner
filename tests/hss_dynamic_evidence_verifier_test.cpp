#include "hss/dynamic_evidence_verifier.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace verifier = hss::dynamic_evidence_verifier;
namespace provenance = hss::dynamic_provenance;

constexpr const char *kProject = "10000000-0000-4000-8000-000000000001";
constexpr const char *kCommand = "20000000-0000-4000-8000-000000000001";
constexpr const char *kEvidence = "30000000-0000-4000-8000-000000000001";
constexpr const char *kCorrespondence =
    "40000000-0000-4000-8000-000000000001";
constexpr const char *kCapture = "50000000-0000-4000-8000-000000000001";

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string uuid(uint64_t value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "60000000-0000-4000-8000-000000000000";
  for (size_t index = 0U; index < 12U; ++index) {
    result[result.size() - 1U - index] = digits[value & 0x0fU];
    value >>= 4U;
  }
  return result;
}

std::array<double, 16U> translation(double x, double y, double z) {
  return {1.0, 0.0, 0.0, 0.0,
          0.0, 1.0, 0.0, 0.0,
          0.0, 0.0, 1.0, 0.0,
          x,   y,   z,   1.0};
}

verifier::Observation observation(uint64_t identity, uint64_t sequence,
                                  double time_s, double x) {
  verifier::Observation value;
  value.project_id = kProject;
  value.source = {provenance::SourceKind::kDepthFrame, uuid(identity),
                  kCapture, 7U, sequence};
  value.support_kind = verifier::SupportKind::kCalibratedDepthPoint;
  value.correspondence_id = kCorrespondence;
  value.pose_id = uuid(100U + identity);
  value.calibration_id = uuid(200U);
  value.independence_group_id = uuid(300U + identity);
  value.tracking_state = verifier::TrackingState::kNormal;
  value.calibration = verifier::Verification::kAccepted;
  value.pose = verifier::Verification::kAccepted;
  value.epoch_relationship = verifier::Verification::kAccepted;
  value.retained_source = verifier::Verification::kAccepted;
  value.epoch_transform_revision = 3U;
  value.monotonic_time_s = time_s;
  value.project_from_source = translation(x, 0.0, 0.0);
  value.source_point_m = {0.0, 0.0, 0.0};
  value.position_uncertainty_m = 0.005;
  value.correspondence_residual_m = 0.002;
  value.discontinuity_clearance_m = 0.050;
  return value;
}

std::vector<verifier::Observation> observations() {
  return {observation(1U, 10U, 1.0, 0.0),
          observation(2U, 11U, 2.0, 0.1),
          observation(3U, 12U, 3.0, 0.2)};
}

verifier::Request request() {
  const auto values = observations();
  verifier::Request value;
  value.evidence_id = kEvidence;
  value.exclusion_command_id = kCommand;
  value.project_id = kProject;
  value.correspondence_id = kCorrespondence;
  value.target_source = values[1].source;
  value.target_source_bounds_m = {{0.05, -0.05, -0.05},
                                  {0.15, 0.05, 0.05}};
  value.exclusion_bounds_m = {{-0.10, -0.10, -0.10},
                              {0.30, 0.10, 0.10}};
  value.start_sequence = 10U;
  value.end_sequence = 20U;
  return value;
}

void expect_failure(const verifier::Request &candidate,
                    const std::vector<verifier::Observation> &values,
                    verifier::Status expected, const std::string &message,
                    const verifier::Options &options = {}) {
  const verifier::Result result = verifier::verify(candidate, values, options);
  check(result.status == expected && !result.evidence && !result.diagnostics,
        message + " fails closed with " + verifier::status_name(expected));
}

void test_independent_calibrated_motion_is_verified() {
  const auto values = observations();
  const auto before = values;
  const verifier::Request candidate = request();
  const verifier::Result result = verifier::verify(candidate, values);
  check(result.status == verifier::Status::kVerified && result.evidence &&
            result.diagnostics,
        "three independently retained calibrated observations verify motion");
  if (result.evidence && result.diagnostics) {
    check(result.evidence->evidence_id == kEvidence &&
              result.evidence->exclusion_command_id == kCommand &&
              result.evidence->project_id == kProject &&
              result.evidence->source == candidate.target_source &&
              result.evidence->bounds_m == candidate.target_source_bounds_m &&
              result.evidence->kind ==
                  provenance::EvidenceKind::kTrackedMultiFrameMotion,
          "verified evidence retains exact command, project, source, and bounds linkage");
    check(result.diagnostics->observation_count == 3U &&
              result.diagnostics->motion_confirmation_count == 2U &&
              result.diagnostics->minimum_adjusted_displacement_m > 0.08 &&
              result.diagnostics->maximum_adjusted_motion_residual_m == 0.0 &&
              result.diagnostics->time_span_s == 2.0,
          "diagnostics expose the explicit motion and stability gates");
  }
  check(values == before, "verification leaves retained observations immutable");

  auto reversed = values;
  std::reverse(reversed.begin(), reversed.end());
  const verifier::Result replay = verifier::verify(candidate, reversed);
  check(replay.status == verifier::Status::kVerified &&
            replay.evidence == result.evidence &&
            replay.diagnostics == result.diagnostics,
        "observation input order does not change verified output");
}

void test_single_correlated_and_semantic_inputs_are_rejected() {
  const auto valid = observations();
  expect_failure(request(), {valid[0]}, verifier::Status::kInsufficientObservations,
                 "single-frame evidence");

  auto correlated = valid;
  correlated[1].independence_group_id = correlated[0].independence_group_id;
  expect_failure(request(), correlated,
                 verifier::Status::kCorrelatedObservations,
                 "duplicate correlation group");

  correlated = valid;
  correlated[1].pose_id = correlated[0].pose_id;
  expect_failure(request(), correlated,
                 verifier::Status::kCorrelatedObservations,
                 "duplicate calibrated pose");

  auto semantic = valid;
  semantic[0].support_kind = verifier::SupportKind::kSemanticOnly;
  expect_failure(request(), semantic, verifier::Status::kSemanticOnly,
                 "semantic-only classification");
}

void test_tracking_calibration_epoch_and_uncertainty_are_required() {
  auto values = observations();
  values[0].tracking_state = verifier::TrackingState::kLimited;
  expect_failure(request(), values, verifier::Status::kInvalidTracking,
                 "limited tracking");

  values = observations();
  values[0].calibration = verifier::Verification::kUnknown;
  expect_failure(request(), values, verifier::Status::kInvalidCalibration,
                 "unknown calibration");

  values = observations();
  values[0].project_from_source[0] = 2.0;
  expect_failure(request(), values, verifier::Status::kInvalidPose,
                 "non-rigid source pose");

  values = observations();
  values[0].epoch_transform_revision = 0U;
  expect_failure(request(), values, verifier::Status::kUnacceptedEpoch,
                 "unversioned epoch relationship");

  values = observations();
  values[2].epoch_transform_revision = 4U;
  expect_failure(request(), values, verifier::Status::kWrongEpoch,
                 "mixed accepted epoch transform revisions");

  values = observations();
  values[0].retained_source = verifier::Verification::kUnknown;
  expect_failure(request(), values, verifier::Status::kSourceNotRetained,
                 "unretained source");

  values = observations();
  values[0].position_uncertainty_m = 0.021;
  expect_failure(request(), values, verifier::Status::kUncertaintyTooLarge,
                 "position uncertainty above the explicit threshold");
}

void test_project_source_capture_epoch_and_sequence_linkage() {
  auto values = observations();
  values[0].project_id = uuid(900U);
  expect_failure(request(), values, verifier::Status::kWrongProject,
                 "observation from another project");

  values = observations();
  values[0].source.capture_id = uuid(901U);
  expect_failure(request(), values, verifier::Status::kWrongCapture,
                 "observation from another capture");

  values = observations();
  ++values[0].source.session_epoch;
  expect_failure(request(), values, verifier::Status::kWrongEpoch,
                 "observation from another epoch");

  values = observations();
  values[2].source.event_sequence = 21U;
  expect_failure(request(), values, verifier::Status::kWrongSequence,
                 "observation outside the accepted command interval");

  verifier::Request missing = request();
  missing.target_source.source_id = uuid(902U);
  expect_failure(missing, observations(), verifier::Status::kTargetSourceMissing,
                 "target source absent from retained observations");

  values = observations();
  values[1].support_kind = verifier::SupportKind::kCalibratedMeshPoint;
  expect_failure(request(), values, verifier::Status::kInvalidObservation,
                 "support kind that does not match the retained source");
}

void test_occlusion_correspondence_and_motion_fail_closed() {
  auto values = observations();
  values[0].discontinuity_clearance_m = 0.034;
  expect_failure(request(), values, verifier::Status::kOcclusionEdge,
                 "point whose uncertainty reaches an occlusion edge");

  values = observations();
  values[0].correspondence_id = uuid(903U);
  expect_failure(request(), values, verifier::Status::kUnstableCorrespondence,
                 "mixed correspondence identities");

  values = observations();
  values[0].correspondence_residual_m = 0.011;
  expect_failure(request(), values, verifier::Status::kUnstableCorrespondence,
                 "upstream geometric match above the residual threshold");

  values = observations();
  values[0].project_from_source = translation(0.0, 0.09, 0.0);
  expect_failure(request(), values, verifier::Status::kUnstableCorrespondence,
                 "track inconsistent with the bounded motion model");

  values = observations();
  values[0].project_from_source = translation(0.08, 0.0, 0.0);
  values[2].project_from_source = translation(0.12, 0.0, 0.0);
  expect_failure(request(), values, verifier::Status::kInsufficientMotion,
                 "uncertainty-adjusted displacement below the motion threshold");
}

} // namespace

int main() {
  test_independent_calibrated_motion_is_verified();
  test_single_correlated_and_semantic_inputs_are_rejected();
  test_tracking_calibration_epoch_and_uncertainty_are_required();
  test_project_source_capture_epoch_and_sequence_linkage();
  test_occlusion_correspondence_and_motion_fail_closed();
  if (failures == 0)
    std::cout << "dynamic evidence verifier tests passed\n";
  return failures == 0 ? 0 : 1;
}

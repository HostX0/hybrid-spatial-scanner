#include "hss/resource_policy.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

using hss::resource::CheckpointResult;
using hss::resource::EventAction;
using hss::resource::EventKind;
using hss::resource::MeasurementStatus;
using hss::resource::MemoryPressure;
using hss::resource::PolicyConfig;
using hss::resource::PolicyMode;
using hss::resource::PressureLevel;
using hss::resource::Reason;
using hss::resource::ResourcePolicy;
using hss::resource::ResourceSample;
using hss::resource::Status;
using hss::resource::ThermalState;
using hss::resource::kMaximumRecoverySamples;

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

PolicyConfig fixture_config() {
  return {.storage_stop_bytes = 100U,
          .storage_warning_bytes = 200U,
          .storage_recovery_bytes = 300U,
          .recovery_samples = 2U};
}

ResourceSample measured(uint64_t sequence, double time_s, ThermalState thermal,
                        MemoryPressure memory, uint64_t free_storage_bytes) {
  ResourceSample sample;
  sample.sequence = sequence;
  sample.monotonic_time_s = time_s;
  sample.thermal = {.status = MeasurementStatus::kMeasured, .state = thermal};
  sample.memory = {.status = MeasurementStatus::kMeasured, .pressure = memory};
  sample.storage = {.status = MeasurementStatus::kMeasured,
                    .free_bytes = free_storage_bytes};
  return sample;
}

void test_configuration_and_measurement_validation_are_atomic() {
  PolicyConfig invalid = fixture_config();
  invalid.storage_warning_bytes = invalid.storage_stop_bytes;
  ResourcePolicy bad_policy(invalid);
  check(!bad_policy.valid(), "equal stop/warning thresholds are invalid");
  const auto bad_config = bad_policy.observe(
      measured(1U, 1.0, ThermalState::kNominal, MemoryPressure::kNominal, 400U));
  check(bad_config.status == Status::kInvalidConfiguration &&
            bad_config.reason == Reason::kInvalidConfiguration,
        "invalid configuration has explicit status and reason");
  PolicyConfig unbounded = fixture_config();
  unbounded.recovery_samples = kMaximumRecoverySamples + 1U;
  check(!ResourcePolicy(unbounded).valid(), "recovery threshold has a finite upper bound");

  ResourcePolicy policy(fixture_config());
  ResourceSample invalid_sample =
      measured(1U, std::numeric_limits<double>::quiet_NaN(), ThermalState::kCritical,
               MemoryPressure::kCritical, 0U);
  const auto rejected = policy.observe(invalid_sample);
  check(rejected.status == Status::kInvalidMeasurement &&
            policy.mode() == PolicyMode::kNormal,
        "nonfinite measurement is rejected without pressure mutation");

  ResourceSample invalid_unmeasured;
  invalid_unmeasured.sequence = 1U;
  invalid_unmeasured.monotonic_time_s = 1.0;
  invalid_unmeasured.thermal = {.status = MeasurementStatus::kUnmeasured,
                                .state = ThermalState::kCritical};
  const auto noncanonical = policy.observe(invalid_unmeasured);
  check(noncanonical.status == Status::kInvalidMeasurement,
        "unmeasured sensor cannot smuggle a measured critical state");

  ResourceSample invalid_enum =
      measured(1U, 1.0, ThermalState::kNominal, MemoryPressure::kNominal, 400U);
  invalid_enum.memory.pressure = static_cast<MemoryPressure>(255U);
  check(policy.observe(invalid_enum).status == Status::kInvalidMeasurement,
        "out-of-range measured enum is rejected");

  const auto accepted = policy.observe(
      measured(1U, 1.0, ThermalState::kNominal, MemoryPressure::kNominal, 400U));
  check(accepted.status == Status::kOk && accepted.mode == PolicyMode::kNormal,
        "valid sample after rejection remains sequence one");
  const auto duplicate = policy.observe(
      measured(1U, 2.0, ThermalState::kCritical, MemoryPressure::kCritical, 0U));
  check(duplicate.status == Status::kOutOfOrder && policy.mode() == PolicyMode::kNormal,
        "duplicate sequence cannot mutate policy state");
  check(policy.counters().accepted_samples == 1U &&
            policy.counters().rejected_samples == 4U,
        "accepted and rejected measurement counters are explicit");
}

void test_unmeasured_is_not_nominal_or_recovery_evidence() {
  ResourcePolicy policy(fixture_config());
  ResourceSample unknown;
  unknown.sequence = 1U;
  unknown.monotonic_time_s = 1.0;
  const auto initial = policy.observe(unknown);
  check(initial.status == Status::kOk && !initial.monitoring_complete &&
            initial.reason == Reason::kMeasurementsUnavailable &&
            initial.mode == PolicyMode::kNormal,
        "unmeasured startup remains visibly distinct from measured nominal");

  const auto warning = policy.observe(
      measured(2U, 2.0, ThermalState::kFair, MemoryPressure::kNominal, 400U));
  check(warning.mode == PolicyMode::kConserving &&
            warning.pressure == PressureLevel::kConstrained,
        "measured thermal pressure enters conservation");

  unknown.sequence = 3U;
  unknown.monotonic_time_s = 3.0;
  const auto unavailable = policy.observe(unknown);
  check(unavailable.mode == PolicyMode::kConserving &&
            unavailable.reason == Reason::kMeasurementsUnavailable,
        "missing sensor readings cannot clear latched pressure");
}

void test_pressure_escalation_checkpoint_and_hysteretic_recovery() {
  ResourcePolicy policy(fixture_config());
  const auto nominal = policy.observe(
      measured(1U, 1.0, ThermalState::kNominal, MemoryPressure::kNominal, 400U));
  check(nominal.mode == PolicyMode::kNormal && nominal.validity_checks_enabled &&
            nominal.accept_metric_observations,
        "nominal sample keeps full metric path");

  const auto warning = policy.observe(
      measured(2U, 2.0, ThermalState::kSerious, MemoryPressure::kWarning, 150U));
  check(warning.mode == PolicyMode::kConserving &&
            warning.reason == Reason::kThermalSerious && warning.reduce_preview_quality &&
            warning.suspend_optional_appearance && warning.reduce_unprotected_display_lod &&
            warning.validity_checks_enabled && warning.accept_metric_observations,
        "constrained state sheds optional work without disabling validity");

  const auto unsafe = policy.observe(
      measured(3U, 3.0, ThermalState::kCritical, MemoryPressure::kCritical, 50U));
  check(unsafe.mode == PolicyMode::kCheckpointPending &&
            unsafe.reason == Reason::kThermalCritical && unsafe.request_checkpoint &&
            unsafe.pause_capture && !unsafe.accept_metric_observations &&
            unsafe.retain_existing_sources && unsafe.validity_checks_enabled,
        "unsafe state stops metric intake and requests checkpoint before validity loss");

  const auto early_clear = policy.observe(
      measured(4U, 4.0, ThermalState::kNominal, MemoryPressure::kNominal, 400U));
  check(early_clear.mode == PolicyMode::kCheckpointPending && early_clear.request_checkpoint,
        "sensor recovery cannot replace explicit checkpoint completion");

  const auto checkpoint = policy.complete_checkpoint(CheckpointResult::kSucceeded);
  check(checkpoint.status == Status::kOk && checkpoint.mode == PolicyMode::kPaused &&
            checkpoint.reason == Reason::kCheckpointSucceeded && checkpoint.pause_capture,
        "successful durable checkpoint leaves capture safely paused");

  const auto first_clear = policy.observe(
      measured(5U, 5.0, ThermalState::kNominal, MemoryPressure::kNominal, 250U));
  check(first_clear.mode == PolicyMode::kPaused &&
            first_clear.reason == Reason::kRecoveryHysteresis,
        "storage between warning and recovery reserves cannot clear pressure");
  const auto first_clear_above_reserve = policy.observe(
      measured(6U, 6.0, ThermalState::kNominal, MemoryPressure::kNominal, 350U));
  check(first_clear_above_reserve.mode == PolicyMode::kPaused &&
            first_clear_above_reserve.reason == Reason::kRecoveryHysteresis,
        "one clear sample above the reserve does not defeat sample hysteresis");
  const auto recovered = policy.observe(
      measured(7U, 7.0, ThermalState::kNominal, MemoryPressure::kNominal, 350U));
  check(recovered.mode == PolicyMode::kNormal && recovered.reason == Reason::kNominal &&
            !recovered.pause_capture && recovered.accept_metric_observations,
        "configured clear sample count resumes normal capture");
  check(policy.counters().constrained_samples == 1U &&
            policy.counters().unsafe_samples == 1U &&
            policy.counters().pressure_escalations == 2U &&
            policy.counters().pressure_recoveries == 1U &&
            policy.counters().checkpoint_requests == 1U &&
            policy.counters().checkpoint_successes == 1U,
        "pressure and checkpoint transitions are counted once");
}

void test_checkpoint_failure_stays_paused_and_sources_are_never_evicted() {
  ResourcePolicy policy(fixture_config());
  const auto unsafe = policy.observe(
      measured(1U, 1.0, ThermalState::kNominal, MemoryPressure::kNominal, 100U));
  check(unsafe.mode == PolicyMode::kCheckpointPending &&
            unsafe.reason == Reason::kStorageStop,
        "storage stop reserve is inclusive and unsafe");
  const auto failed = policy.complete_checkpoint(CheckpointResult::kFailed);
  check(failed.mode == PolicyMode::kPaused && failed.reason == Reason::kCheckpointFailed &&
            failed.pause_capture && failed.retain_existing_sources,
        "failed checkpoint pauses without an eviction escape hatch");
  check(policy.counters().checkpoint_failures == 1U,
        "checkpoint failure is explicitly counted");
}

void test_protected_events_and_preview_counters() {
  ResourcePolicy policy(fixture_config());
  (void)policy.observe(
      measured(1U, 1.0, ThermalState::kFair, MemoryPressure::kNominal, 400U));
  check(policy.decide_event(EventKind::kPreviewFrame).action == EventAction::kDropPreview,
        "preview frame is expendable under pressure");
  check(policy.decide_event(EventKind::kOptionalAppearance).action ==
            EventAction::kDeferOptional &&
            policy.decide_event(EventKind::kUnprotectedDisplayLod).action ==
                EventAction::kDeferOptional,
        "appearance and unprotected display work defer before metric work");
  check(policy.decide_event(EventKind::kMetricObservation).action == EventAction::kProcess,
        "conservation preserves valid metric observations");

  (void)policy.observe(
      measured(2U, 2.0, ThermalState::kCritical, MemoryPressure::kCritical, 50U));
  check(policy.decide_event(EventKind::kMetricObservation).action ==
            EventAction::kRetainSourceAndPauseCapture,
        "unsafe metric input retains sources and pauses upstream capture");
  for (EventKind protected_kind : {EventKind::kCommand, EventKind::kEpochTransition,
                                   EventKind::kMeshRemoval, EventKind::kDurableCommit}) {
    const auto event = policy.decide_event(protected_kind);
    check(event.status == Status::kOk && event.protected_event &&
              event.action == EventAction::kAcceptProtected &&
              event.retain_existing_sources,
          "protected event remains accepted while checkpoint is pending");
  }
  check(policy.counters().dropped_preview_events == 1U &&
            policy.counters().deferred_optional_events == 2U &&
            policy.counters().source_pause_events == 1U &&
            policy.counters().protected_events_accepted == 4U,
        "preview, optional, source-pause and protected counters are recorded");
}

}  // namespace

int main() {
  test_configuration_and_measurement_validation_are_atomic();
  test_unmeasured_is_not_nominal_or_recovery_evidence();
  test_pressure_escalation_checkpoint_and_hysteretic_recovery();
  test_checkpoint_failure_stays_paused_and_sources_are_never_evicted();
  test_protected_events_and_preview_counters();
  if (failures != 0) {
    std::cerr << failures << " resource policy test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "resource policy checks passed\n";
  return EXIT_SUCCESS;
}

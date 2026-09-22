#include "hss/resource_policy.hpp"

#include <cmath>
#include <limits>

namespace hss::resource {
namespace {

void increment(uint64_t* value) {
  if (*value != std::numeric_limits<uint64_t>::max()) ++*value;
}

bool valid_config(const PolicyConfig& config) {
  return config.storage_stop_bytes != 0U &&
         config.storage_stop_bytes < config.storage_warning_bytes &&
         config.storage_warning_bytes < config.storage_recovery_bytes &&
         config.storage_recovery_bytes <= kMaximumStorageThresholdBytes &&
         config.recovery_samples != 0U && config.recovery_samples <= kMaximumRecoverySamples;
}

bool valid_status(MeasurementStatus status) {
  return status == MeasurementStatus::kUnmeasured || status == MeasurementStatus::kMeasured;
}

bool valid_thermal(ThermalState state) {
  return state == ThermalState::kNominal || state == ThermalState::kFair ||
         state == ThermalState::kSerious || state == ThermalState::kCritical;
}

bool valid_memory(MemoryPressure pressure) {
  return pressure == MemoryPressure::kNominal || pressure == MemoryPressure::kWarning ||
         pressure == MemoryPressure::kCritical;
}

bool valid_sample(const ResourceSample& sample) {
  if (sample.sequence == 0U || !std::isfinite(sample.monotonic_time_s) ||
      sample.monotonic_time_s < 0.0 || !valid_status(sample.thermal.status) ||
      !valid_status(sample.memory.status) || !valid_status(sample.storage.status) ||
      !valid_thermal(sample.thermal.state) || !valid_memory(sample.memory.pressure)) {
    return false;
  }
  if (sample.thermal.status == MeasurementStatus::kUnmeasured &&
      sample.thermal.state != ThermalState::kNominal) {
    return false;
  }
  if (sample.memory.status == MeasurementStatus::kUnmeasured &&
      sample.memory.pressure != MemoryPressure::kNominal) {
    return false;
  }
  return sample.storage.status != MeasurementStatus::kUnmeasured || sample.storage.free_bytes == 0U;
}

bool monitoring_complete(const ResourceSample& sample) {
  return sample.thermal.status == MeasurementStatus::kMeasured &&
         sample.memory.status == MeasurementStatus::kMeasured &&
         sample.storage.status == MeasurementStatus::kMeasured;
}

struct EvaluatedPressure {
  PressureLevel level{PressureLevel::kNone};
  Reason reason{Reason::kNominal};
};

int reason_priority(Reason reason) {
  switch (reason) {
    case Reason::kThermalCritical:
      return 70;
    case Reason::kStorageStop:
      return 60;
    case Reason::kMemoryCritical:
      return 50;
    case Reason::kThermalSerious:
      return 40;
    case Reason::kStorageWarning:
      return 30;
    case Reason::kMemoryWarning:
      return 20;
    case Reason::kThermalFair:
      return 10;
    default:
      return 0;
  }
}

void consider(EvaluatedPressure* result, PressureLevel level, Reason reason) {
  if (static_cast<uint8_t>(level) > static_cast<uint8_t>(result->level) ||
      (level == result->level && reason_priority(reason) > reason_priority(result->reason))) {
    result->level = level;
    result->reason = reason;
  }
}

EvaluatedPressure evaluate(const ResourceSample& sample, const PolicyConfig& config) {
  EvaluatedPressure result;
  if (sample.thermal.status == MeasurementStatus::kMeasured) {
    switch (sample.thermal.state) {
      case ThermalState::kNominal:
        break;
      case ThermalState::kFair:
        consider(&result, PressureLevel::kConstrained, Reason::kThermalFair);
        break;
      case ThermalState::kSerious:
        consider(&result, PressureLevel::kConstrained, Reason::kThermalSerious);
        break;
      case ThermalState::kCritical:
        consider(&result, PressureLevel::kUnsafe, Reason::kThermalCritical);
        break;
    }
  }
  if (sample.memory.status == MeasurementStatus::kMeasured) {
    switch (sample.memory.pressure) {
      case MemoryPressure::kNominal:
        break;
      case MemoryPressure::kWarning:
        consider(&result, PressureLevel::kConstrained, Reason::kMemoryWarning);
        break;
      case MemoryPressure::kCritical:
        consider(&result, PressureLevel::kUnsafe, Reason::kMemoryCritical);
        break;
    }
  }
  if (sample.storage.status == MeasurementStatus::kMeasured) {
    if (sample.storage.free_bytes <= config.storage_stop_bytes) {
      consider(&result, PressureLevel::kUnsafe, Reason::kStorageStop);
    } else if (sample.storage.free_bytes <= config.storage_warning_bytes) {
      consider(&result, PressureLevel::kConstrained, Reason::kStorageWarning);
    }
  }
  return result;
}

bool clear_for_recovery(const ResourceSample& sample, const PolicyConfig& config) {
  return monitoring_complete(sample) && sample.thermal.state == ThermalState::kNominal &&
         sample.memory.pressure == MemoryPressure::kNominal &&
         sample.storage.free_bytes >= config.storage_recovery_bytes;
}

bool protected_event(EventKind kind) {
  return kind == EventKind::kCommand || kind == EventKind::kEpochTransition ||
         kind == EventKind::kMeshRemoval || kind == EventKind::kDurableCommit;
}

bool valid_event(EventKind kind) {
  switch (kind) {
    case EventKind::kPreviewFrame:
    case EventKind::kOptionalAppearance:
    case EventKind::kUnprotectedDisplayLod:
    case EventKind::kMetricObservation:
    case EventKind::kCommand:
    case EventKind::kEpochTransition:
    case EventKind::kMeshRemoval:
    case EventKind::kDurableCommit:
      return true;
  }
  return false;
}

}  // namespace

ResourcePolicy::ResourcePolicy(PolicyConfig config)
    : config_(config), configuration_valid_(valid_config(config)) {}

Decision ResourcePolicy::decision(Status status, Reason reason, bool complete) const {
  Decision result;
  result.status = status;
  result.mode = mode_;
  result.pressure = pressure_;
  result.reason = reason;
  result.monitoring_complete = complete;
  result.reduce_preview_quality = mode_ != PolicyMode::kNormal;
  result.suspend_optional_appearance = mode_ != PolicyMode::kNormal;
  result.reduce_unprotected_display_lod = mode_ != PolicyMode::kNormal;
  result.validity_checks_enabled = true;
  result.accept_metric_observations = mode_ == PolicyMode::kNormal ||
                                      mode_ == PolicyMode::kConserving;
  result.request_checkpoint = mode_ == PolicyMode::kCheckpointPending;
  result.pause_capture = mode_ == PolicyMode::kCheckpointPending ||
                         mode_ == PolicyMode::kPaused;
  result.retain_existing_sources = true;
  return result;
}

Decision ResourcePolicy::observe(const ResourceSample& sample) {
  if (!configuration_valid_) {
    increment(&counters_.rejected_samples);
    return decision(Status::kInvalidConfiguration, Reason::kInvalidConfiguration, false);
  }
  if (!valid_sample(sample)) {
    increment(&counters_.rejected_samples);
    return decision(Status::kInvalidMeasurement, Reason::kInvalidMeasurement, false);
  }
  if (has_sample_ &&
      (sample.sequence <= last_sequence_ || sample.monotonic_time_s < last_time_s_)) {
    increment(&counters_.rejected_samples);
    return decision(Status::kOutOfOrder, Reason::kOutOfOrder, false);
  }

  has_sample_ = true;
  last_sequence_ = sample.sequence;
  last_time_s_ = sample.monotonic_time_s;
  increment(&counters_.accepted_samples);
  const bool complete = monitoring_complete(sample);
  if (!complete) increment(&counters_.unmeasured_samples);

  const EvaluatedPressure current = evaluate(sample, config_);
  if (current.level == PressureLevel::kConstrained) increment(&counters_.constrained_samples);
  if (current.level == PressureLevel::kUnsafe) increment(&counters_.unsafe_samples);

  if (current.level == PressureLevel::kUnsafe) {
    recovery_count_ = 0U;
    pressure_ = current.level;
    if (mode_ == PolicyMode::kNormal || mode_ == PolicyMode::kConserving) {
      mode_ = PolicyMode::kCheckpointPending;
      increment(&counters_.pressure_escalations);
      increment(&counters_.checkpoint_requests);
    }
    return decision(Status::kOk, current.reason, complete);
  }

  if (current.level == PressureLevel::kConstrained) {
    recovery_count_ = 0U;
    if (mode_ == PolicyMode::kNormal) {
      mode_ = PolicyMode::kConserving;
      increment(&counters_.pressure_escalations);
    }
    if (mode_ == PolicyMode::kNormal || mode_ == PolicyMode::kConserving) {
      pressure_ = current.level;
    }
    return decision(Status::kOk, current.reason, complete);
  }

  if (mode_ == PolicyMode::kNormal) {
    pressure_ = PressureLevel::kNone;
    recovery_count_ = 0U;
    return decision(Status::kOk, complete ? Reason::kNominal : Reason::kMeasurementsUnavailable,
                    complete);
  }

  if (mode_ == PolicyMode::kCheckpointPending) {
    // A clear sensor sample cannot stand in for an explicit durable checkpoint
    // completion. Keep capture paused until the B-038 handshake completes.
    return decision(Status::kOk,
                    complete ? Reason::kRecoveryHysteresis : Reason::kMeasurementsUnavailable,
                    complete);
  }

  if (!clear_for_recovery(sample, config_)) {
    recovery_count_ = 0U;
    return decision(Status::kOk,
                    complete ? Reason::kRecoveryHysteresis : Reason::kMeasurementsUnavailable,
                    complete);
  }

  if (recovery_count_ < config_.recovery_samples) ++recovery_count_;
  if (recovery_count_ == config_.recovery_samples) {
    mode_ = PolicyMode::kNormal;
    pressure_ = PressureLevel::kNone;
    recovery_count_ = 0U;
    increment(&counters_.pressure_recoveries);
    return decision(Status::kOk, Reason::kNominal, true);
  }
  return decision(Status::kOk, Reason::kRecoveryHysteresis, true);
}

Decision ResourcePolicy::complete_checkpoint(CheckpointResult result) {
  if (!configuration_valid_) {
    return decision(Status::kInvalidConfiguration, Reason::kInvalidConfiguration, false);
  }
  if (mode_ != PolicyMode::kCheckpointPending) {
    return decision(Status::kInvalidTransition, Reason::kInvalidTransition, false);
  }
  if (result != CheckpointResult::kSucceeded && result != CheckpointResult::kFailed) {
    return decision(Status::kInvalidTransition, Reason::kInvalidTransition, false);
  }
  mode_ = PolicyMode::kPaused;
  recovery_count_ = 0U;
  if (result == CheckpointResult::kSucceeded) {
    increment(&counters_.checkpoint_successes);
    return decision(Status::kOk, Reason::kCheckpointSucceeded, false);
  }
  increment(&counters_.checkpoint_failures);
  return decision(Status::kOk, Reason::kCheckpointFailed, false);
}

EventDecision ResourcePolicy::decide_event(EventKind kind) {
  EventDecision result;
  result.retain_existing_sources = true;
  if (!valid_event(kind)) {
    result.status = Status::kInvalidEventKind;
    result.action = EventAction::kRetainSourceAndPauseCapture;
    return result;
  }
  result.protected_event = protected_event(kind);
  if (result.protected_event) {
    result.status = configuration_valid_ ? Status::kOk : Status::kInvalidConfiguration;
    result.action = EventAction::kAcceptProtected;
    increment(&counters_.protected_events_accepted);
    return result;
  }
  if (!configuration_valid_) {
    result.status = Status::kInvalidConfiguration;
    if (kind == EventKind::kPreviewFrame) {
      result.action = EventAction::kDropPreview;
      increment(&counters_.dropped_preview_events);
    } else if (kind == EventKind::kOptionalAppearance ||
               kind == EventKind::kUnprotectedDisplayLod) {
      result.action = EventAction::kDeferOptional;
      increment(&counters_.deferred_optional_events);
    } else {
      result.action = EventAction::kRetainSourceAndPauseCapture;
      increment(&counters_.source_pause_events);
    }
    return result;
  }

  result.status = Status::kOk;
  if (mode_ == PolicyMode::kNormal) {
    result.action = EventAction::kProcess;
  } else if (kind == EventKind::kPreviewFrame) {
    result.action = EventAction::kDropPreview;
    increment(&counters_.dropped_preview_events);
  } else if (kind == EventKind::kOptionalAppearance ||
             kind == EventKind::kUnprotectedDisplayLod) {
    result.action = EventAction::kDeferOptional;
    increment(&counters_.deferred_optional_events);
  } else if (mode_ == PolicyMode::kConserving) {
    result.action = EventAction::kProcess;
  } else {
    result.action = EventAction::kRetainSourceAndPauseCapture;
    increment(&counters_.source_pause_events);
  }
  return result;
}

}  // namespace hss::resource

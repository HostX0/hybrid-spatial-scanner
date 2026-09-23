#include "hss/bounded_ingress.hpp"
#include "hss/coordinate_basis.hpp"
#include "hss/dynamic_provenance.hpp"
#include "hss/hss_api.h"
#include "hss/live_reconstruction.hpp"
#include "hss/resource_policy.hpp"
#include "hss/scene_editing.hpp"
#include <chrono>
#include <deque>
#include <memory>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace runtime_provenance {

struct AcceptedEpoch {
  HSSEpoch epoch{};
  std::shared_ptr<hss::live::Lease> lease;
};

struct AcceptedMesh {
  hss::ingress::Event event;
  std::shared_ptr<hss::live::Lease> lease;
};

struct State {
  std::map<std::string, AcceptedEpoch> epochs;
  std::map<std::string, AcceptedMesh> active_meshes;
};

struct Inflight {
  hss::ingress::Event event;
  std::shared_ptr<hss::live::Lease> lease;
};

struct VerifiedExclusion {
  hss::scene_editing::DynamicExclusion exclusion;
  std::vector<hss::dynamic_provenance::VerifiedDynamicEvidence> evidence;
  hss::dynamic_provenance::Audit audit;
  uint64_t triangle_mask_fingerprint{0U};
  uint64_t accepted_scene_revision{0U};
  std::shared_ptr<hss::live::Lease> lease;
};

} // namespace runtime_provenance

struct HSSEngine {
  explicit HSSEngine(const HSSConfig &c)
      : config(c), ingress(c.max_working_bytes, c.max_pending_frames), live(c) {
    resource_decision.status = hss::resource::Status::kOk;
    resource_decision.mode = hss::resource::PolicyMode::kNormal;
    resource_decision.pressure = hss::resource::PressureLevel::kNone;
    resource_decision.reason = hss::resource::Reason::kMeasurementsUnavailable;
    resource_decision.validity_checks_enabled = true;
    resource_decision.accept_metric_observations = true;
    resource_decision.retain_existing_sources = true;
  }
  HSSConfig config{};
  hss::ingress::BoundedIngress ingress;
  hss::live::Reconstruction live;
  hss::scene_editing::Processor scene_editor;
  std::deque<std::shared_ptr<hss::live::Lease>> queued_leases;
  std::string command_result;
  bool has_command_result{false};
  runtime_provenance::State provenance;
  std::optional<runtime_provenance::Inflight> provenance_inflight;
  std::map<std::string, std::pair<hss::scene_editing::DynamicExclusion,
                                  uint64_t>> pending_exclusions;
  std::map<std::string, runtime_provenance::VerifiedExclusion>
      verified_exclusions;
  hss::resource::ResourcePolicy resource_policy;
  hss::resource::Decision resource_decision;
  bool resource_input_fault{false};
  uint64_t last_resource_sample_sequence{0U};
  double last_resource_sample_time_s{0.0};
  uint64_t accepted_frames{}, accepted_commands{};
  HSSStatus last_status{HSS_OK};
};
struct HSSSnapshot {
  std::shared_ptr<const hss::live::Snapshot> data;
  std::vector<uint8_t> triangle_inclusion;
  HSSDynamicMaskSummary dynamic_summary{};
  HSSPlanAnnotationSummary plan_summary{};
  std::string plan_json;
  std::string annotation_json;
};

namespace {

constexpr uint64_t kMaxJsonRecordBytes = 16U * 1024U * 1024U;
constexpr uint64_t kMaxDynamicEvidenceRecords = 8'192U;

template <typename T> HSSStatus validate_message(const T *value) {
  if (value == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  if (value->abi_version != HSS_ABI_VERSION) {
    return HSS_UNSUPPORTED_VERSION;
  }
  if (value->struct_size < sizeof(T)) {
    return HSS_INVALID_ARGUMENT;
  }
  const auto *trailing = reinterpret_cast<const uint8_t *>(value) + sizeof(T);
  const size_t trailing_size =
      static_cast<size_t>(value->struct_size - sizeof(T));
  if (!std::all_of(trailing, trailing + trailing_size,
                   [](uint8_t byte) { return byte == 0; })) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

bool finite_positive(double value) {
  return std::isfinite(value) && value > 0.0;
}

bool multiply_add_fits(uint64_t stride, uint32_t rows, uint64_t row_bytes,
                       uint64_t *required) {
  if (required == nullptr) {
    return false;
  }
  if (rows == 0U) {
    *required = 0;
    return true;
  }
  const uint64_t preceding_rows = static_cast<uint64_t>(rows - 1U);
  if (preceding_rows != 0U &&
      stride >
          (std::numeric_limits<uint64_t>::max() - row_bytes) / preceding_rows) {
    return false;
  }
  *required = stride * preceding_rows + row_bytes;
  return true;
}

HSSStatus validate_config(const HSSConfig *config) {
  const HSSStatus header_status = validate_message(config);
  if (header_status != HSS_OK) {
    return header_status;
  }
  if (!finite_positive(config->structure_voxel_m) ||
      !finite_positive(config->detail_voxel_m) ||
      !finite_positive(config->truncation_m) ||
      config->max_working_bytes == 0U || config->max_pending_frames == 0U ||
      config->flags != 0U || config->reserved != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

bool add_fits(uint64_t first, uint64_t second, uint64_t *result) {
  if (result == nullptr ||
      second > std::numeric_limits<uint64_t>::max() - first) {
    return false;
  }
  *result = first + second;
  return true;
}

HSSStatus admission_status(hss::ingress::Admission admission) {
  switch (admission) {
  case hss::ingress::Admission::accepted:
  case hss::ingress::Admission::dropped_frame:
    return HSS_OK;
  case hss::ingress::Admission::resource_limit:
    return HSS_RESOURCE_LIMIT;
  case hss::ingress::Admission::closed:
    return HSS_NOT_READY;
  }
  return HSS_INTERNAL_ERROR;
}

std::vector<uint8_t> copy_payloads(const uint8_t *first, uint64_t first_length,
                                   const uint8_t *second,
                                   uint64_t second_length) {
  uint64_t total = 0;
  if (!add_fits(first_length, second_length, &total) ||
      total > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw std::bad_alloc();
  }
  std::vector<uint8_t> result;
  result.reserve(static_cast<size_t>(total));
  if (first_length != 0U) {
    result.insert(result.end(), first,
                  first + static_cast<size_t>(first_length));
  }
  if (second_length != 0U) {
    result.insert(result.end(), second,
                  second + static_cast<size_t>(second_length));
  }
  return result;
}

hss::ingress::Admission check_admission(HSSEngine *engine,
                                        hss::ingress::EventKind kind,
                                        uint64_t bytes) {
  auto a = engine->ingress.admission(kind, bytes);
  if (a != hss::ingress::Admission::accepted)
    return a;
  auto b = engine->live.budget();
  if (bytes > UINT64_MAX - 2048 || bytes + 2048 > b->limit ||
      b->used > b->limit - (bytes + 2048))
    return hss::ingress::Admission::resource_limit;
  return a;
}
HSSStatus enqueue_owned_event(HSSEngine *engine, hss::ingress::Event &&event) {
  auto lease = hss::live::reserve(engine->live.budget(),
                                  hss::live::event_reservation(event));
  if (!lease)
    return HSS_RESOURCE_LIMIT;
  engine->queued_leases.push_back(std::move(lease));
  try {
    auto result = engine->ingress.enqueue(std::move(event));
    if (result != hss::ingress::Admission::accepted)
      engine->queued_leases.pop_back();
    else
      engine->live.seal_configuration();
    return admission_status(result);
  } catch (...) {
    engine->queued_leases.pop_back();
    throw;
  }
}

template <typename T>
HSSStatus copy_out(const T *source, uint64_t count, T *destination,
                   uint64_t capacity, uint64_t *required) {
  if (required == nullptr) {
    return HSS_INVALID_ARGUMENT;
  }
  *required = 0;
  if (destination == nullptr && capacity != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  if (capacity < count) {
    *required = count;
    return HSS_BUFFER_TOO_SMALL;
  }
  *required = count;
  if (count != 0U) {
    std::memcpy(destination, source, static_cast<size_t>(count) * sizeof(T));
  }
  return HSS_OK;
}

HSSStatus validate_bytes(HSSBytes bytes) {
  if (bytes.data == nullptr && bytes.length != 0U) {
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_OK;
}

HSSStatus validate_json_bytes(HSSBytes bytes) {
  const HSSStatus status = validate_bytes(bytes);
  if (status != HSS_OK) {
    return status;
  }
  return bytes.length > kMaxJsonRecordBytes ? HSS_RESOURCE_LIMIT : HSS_OK;
}

HSSStatus remembered(HSSEngine *engine, HSSStatus status) {
  engine->last_status = status;
  return status;
}

HSSStatus resource_status(hss::resource::Status status) {
  using Status = hss::resource::Status;
  switch (status) {
  case Status::kOk:
    return HSS_OK;
  case Status::kOutOfOrder:
    return HSS_STALE_REVISION;
  case Status::kInvalidTransition:
    return HSS_NOT_READY;
  case Status::kInvalidConfiguration:
  case Status::kInvalidMeasurement:
  case Status::kInvalidEventKind:
    return HSS_INVALID_ARGUMENT;
  }
  return HSS_INTERNAL_ERROR;
}

void fill_resource_decision(const HSSEngine &engine,
                            HSSResourceDecision *output) {
  std::memset(output, 0, sizeof(*output));
  output->abi_version = HSS_ABI_VERSION;
  output->struct_size = sizeof(*output);
  output->last_accepted_sample_sequence =
      engine.last_resource_sample_sequence;
  output->last_accepted_sample_time_s = engine.last_resource_sample_time_s;
  output->resource_status =
      static_cast<uint32_t>(engine.resource_decision.status);
  output->mode = static_cast<uint32_t>(engine.resource_decision.mode);
  output->pressure = static_cast<uint32_t>(engine.resource_decision.pressure);
  output->reason = static_cast<uint32_t>(engine.resource_decision.reason);
  output->monitoring_complete = engine.resource_decision.monitoring_complete;
  output->input_fault = engine.resource_input_fault;
  output->reduce_preview_quality =
      engine.resource_decision.reduce_preview_quality;
  output->suspend_optional_appearance =
      engine.resource_decision.suspend_optional_appearance;
  output->reduce_unprotected_display_lod =
      engine.resource_decision.reduce_unprotected_display_lod;
  output->validity_checks_enabled = true;
  output->accept_metric_observations =
      engine.resource_input_fault
          ? 0U
          : engine.resource_decision.accept_metric_observations;
  output->request_checkpoint = engine.resource_decision.request_checkpoint;
  output->pause_capture = engine.resource_input_fault
                              ? 1U
                              : engine.resource_decision.pause_capture;
  output->retain_existing_sources = true;
}

void latch_resource_input_fault(HSSEngine *engine) {
  engine->resource_input_fault = true;
  engine->resource_decision.status =
      hss::resource::Status::kInvalidMeasurement;
  engine->resource_decision.reason =
      hss::resource::Reason::kInvalidMeasurement;
  engine->resource_decision.monitoring_complete = false;
}

hss::resource::EventDecision
decide_resource_event(HSSEngine *engine, hss::resource::EventKind kind) {
  auto decision = engine->resource_policy.decide_event(kind);
  if (!engine->resource_input_fault || decision.protected_event) {
    return decision;
  }
  decision.status = hss::resource::Status::kInvalidMeasurement;
  decision.retain_existing_sources = true;
  decision.action = kind == hss::resource::EventKind::kPreviewFrame
                        ? hss::resource::EventAction::kDropPreview
                        : hss::resource::EventAction::kRetainSourceAndPauseCapture;
  return decision;
}

std::string uuid(const HSSId &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(36U);
  for (size_t index = 0U; index < 16U; ++index) {
    if (index == 4U || index == 6U || index == 8U || index == 10U)
      result.push_back('-');
    result.push_back(digits[value.bytes[index] >> 4U]);
    result.push_back(digits[value.bytes[index] & 0x0fU]);
  }
  return result;
}

bool zero_id(const HSSId &value) {
  return std::all_of(std::begin(value.bytes), std::end(value.bytes),
                     [](uint8_t byte) { return byte == 0U; });
}

std::string epoch_key(const hss::ingress::Event &event) {
  return uuid(event.capture_id) + ":" + std::to_string(event.session_epoch);
}

std::string anchor_key(const hss::ingress::Event &event) {
  return epoch_key(event) + ":" + uuid(event.anchor_id);
}

void accept_provenance_event(runtime_provenance::State *state,
                             runtime_provenance::Inflight inflight) {
  hss::ingress::Event &event = inflight.event;
  if (event.kind == hss::ingress::EventKind::epoch) {
    state->epochs[epoch_key(event)] =
        {event.epoch, std::move(inflight.lease)};
    return;
  }
  if (event.kind == hss::ingress::EventKind::mesh_add ||
      event.kind == hss::ingress::EventKind::mesh_replace) {
    state->active_meshes[anchor_key(event)] =
        {std::move(event), std::move(inflight.lease)};
  } else if (event.kind == hss::ingress::EventKind::mesh_remove) {
    state->active_meshes.erase(anchor_key(event));
  }
}

hss::dynamic_provenance::CapturedMesh
captured_mesh(const HSSEngine &engine) {
  namespace dp = hss::dynamic_provenance;
  dp::CapturedMesh result;
  result.project_id = uuid(engine.config.project_id);
  for (const auto &[key, accepted_mesh] : engine.provenance.active_meshes) {
    (void)key;
    const auto &mesh = accepted_mesh.event;
    const auto epoch = engine.provenance.epochs.find(epoch_key(mesh));
    if (epoch == engine.provenance.epochs.end() ||
        epoch->second.epoch.relationship_state != 1U) {
      continue;
    }
    const size_t source_index = result.sources.size();
    result.sources.push_back(
        {dp::SourceKind::kMeshAnchor, uuid(mesh.anchor_id),
         uuid(mesh.capture_id), mesh.session_epoch, mesh.event_sequence});
    const size_t base = result.vertices_m.size();
    for (uint64_t vertex = 0U; vertex < mesh.mesh.vertex_count; ++vertex) {
      float local[3]{};
      std::memcpy(local, mesh.payload.data() + vertex * 3U * sizeof(float),
                  sizeof(local));
      HSSVec3d point = hss::coordinate::transform_point(
          mesh.mesh.T_world_anchor, {local[0], local[1], local[2]});
      point = hss::coordinate::transform_point(
          epoch->second.epoch.T_project_world, point);
      result.vertices_m.push_back({point.x, point.y, point.z});
    }
    const size_t triangle_offset =
        static_cast<size_t>(mesh.mesh.vertex_count) * 3U * sizeof(float);
    for (uint64_t triangle = 0U; triangle < mesh.mesh.triangle_count;
         ++triangle) {
      uint32_t local[3]{};
      std::memcpy(local,
                  mesh.payload.data() + triangle_offset +
                      triangle * 3U * sizeof(uint32_t),
                  sizeof(local));
      result.triangles.push_back(
          {{{static_cast<uint32_t>(base + local[0]),
             static_cast<uint32_t>(base + local[1]),
             static_cast<uint32_t>(base + local[2])}},
           {source_index}});
    }
  }
  return result;
}

HSSStatus dynamic_status(hss::dynamic_provenance::Status status) {
  using S = hss::dynamic_provenance::Status;
  switch (status) {
  case S::kOk:
    return HSS_OK;
  case S::kInputLimitExceeded:
  case S::kResourceLimit:
    return HSS_RESOURCE_LIMIT;
  case S::kEvidenceOutsideExclusion:
  case S::kEvidenceSourceMissing:
    return HSS_INSUFFICIENT_EVIDENCE;
  case S::kInternalFailure:
    return HSS_INTERNAL_ERROR;
  default:
    return HSS_INVALID_ARGUMENT;
  }
}

HSSStatus convert_evidence(
    const HSSDynamicEvidenceBatch &batch,
    std::vector<hss::dynamic_provenance::VerifiedDynamicEvidence> *out) {
  namespace dp = hss::dynamic_provenance;
  if (batch.evidence_count == 0U || batch.evidence == nullptr ||
      batch.evidence_count > kMaxDynamicEvidenceRecords ||
      batch.evidence_count > std::numeric_limits<size_t>::max()) {
    return batch.evidence_count > kMaxDynamicEvidenceRecords
               ? HSS_RESOURCE_LIMIT
               : HSS_INVALID_ARGUMENT;
  }
  out->reserve(static_cast<size_t>(batch.evidence_count));
  const std::string command_id = uuid(batch.exclusion_command_id);
  for (uint64_t index = 0U; index < batch.evidence_count; ++index) {
    const HSSDynamicEvidence &input = batch.evidence[index];
    const HSSStatus message = validate_message(&input);
    if (message != HSS_OK || input.source_kind > 1U ||
        input.evidence_kind > 1U || input.reserved[0] != 0U ||
        input.reserved[1] != 0U || zero_id(input.evidence_id) ||
        zero_id(input.exclusion_command_id) || zero_id(input.project_id) ||
        zero_id(input.source_id) || zero_id(input.capture_id) ||
        uuid(input.exclusion_command_id) != command_id) {
      return message == HSS_OK ? HSS_INVALID_ARGUMENT : message;
    }
    dp::VerifiedDynamicEvidence value;
    value.evidence_id = uuid(input.evidence_id);
    value.exclusion_command_id = command_id;
    value.project_id = uuid(input.project_id);
    value.source = {
        input.source_kind == HSS_DYNAMIC_SOURCE_DEPTH_FRAME
            ? dp::SourceKind::kDepthFrame
            : dp::SourceKind::kMeshAnchor,
        uuid(input.source_id), uuid(input.capture_id), input.session_epoch,
        input.event_sequence};
    value.bounds_m = {{input.bounds_min_m.x, input.bounds_min_m.y,
                       input.bounds_min_m.z},
                      {input.bounds_max_m.x, input.bounds_max_m.y,
                       input.bounds_max_m.z}};
    value.kind = input.evidence_kind ==
                         HSS_DYNAMIC_EVIDENCE_TEMPORAL_DEPTH_CHANGE
                     ? dp::EvidenceKind::kTemporalDepthChange
                     : dp::EvidenceKind::kTrackedMultiFrameMotion;
    out->push_back(std::move(value));
  }
  std::sort(out->begin(), out->end(), [](const auto &left, const auto &right) {
    return std::tie(left.evidence_id, left.source.source_id,
                    left.source.event_sequence) <
           std::tie(right.evidence_id, right.source.source_id,
                    right.source.event_sequence);
  });
  return HSS_OK;
}

void fill_dynamic_audit(const HSSId &command_id, uint64_t scene_revision,
                        const hss::dynamic_provenance::Audit &source,
                        uint64_t triangle_mask_fingerprint,
                        HSSDynamicExclusionAudit *audit) {
  std::memset(audit, 0, sizeof(*audit));
  audit->abi_version = HSS_ABI_VERSION;
  audit->struct_size = sizeof(*audit);
  audit->exclusion_command_id = command_id;
  audit->scene_revision = scene_revision;
  audit->linked_evidence_count = source.linked_evidence_count;
  audit->masked_triangle_count = source.masked_triangle_count;
  audit->triangle_mask_fingerprint = triangle_mask_fingerprint;
  audit->disposition = source.disposition ==
                               hss::dynamic_provenance::Disposition::kVerifiedMask
                           ? HSS_DYNAMIC_VERIFIED_MASK
                           : HSS_DYNAMIC_NO_VERIFIED_EFFECT;
}

uint64_t triangle_mask_fingerprint(const std::vector<uint8_t> &mask) {
  uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  uint64_t size = mask.size();
  for (size_t byte = 0U; byte < sizeof(size); ++byte)
    mix(static_cast<uint8_t>(size >> (byte * 8U)));
  for (uint8_t value : mask)
    mix(value);
  return hash;
}

std::string json_quote(const std::string &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
    case '"': result += "\\\""; break;
    case '\\': result += "\\\\"; break;
    case '\b': result += "\\b"; break;
    case '\f': result += "\\f"; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default:
      if (character < 0x20U) {
        result += "\\u00";
        result.push_back(digits[character >> 4U]);
        result.push_back(digits[character & 0x0fU]);
      } else {
        result.push_back(static_cast<char>(character));
      }
    }
  }
  result.push_back('"');
  return result;
}

std::string json_number(double value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(17) << value;
  return output.str();
}

template <typename Range, typename Writer>
void json_array(std::string &output, const Range &range, Writer writer) {
  output.push_back('[');
  bool first = true;
  for (const auto &value : range) {
    if (!first)
      output.push_back(',');
    first = false;
    writer(output, value);
  }
  output.push_back(']');
}

const char *plan_view(hss::plan_semantics::View value) {
  using V = hss::plan_semantics::View;
  switch (value) {
  case V::kFloorFootprint: return "floor_footprint";
  case V::kArchitecturalCut: return "architectural_cut";
  case V::kReflectedCeiling: return "reflected_ceiling";
  }
  return "unsupported";
}

const char *reference_state(hss::plan_semantics::ReferenceState value) {
  using R = hss::plan_semantics::ReferenceState;
  switch (value) {
  case R::kConfirmedFloor: return "confirmed_floor";
  case R::kFloorUnknown: return "floor_unknown";
  case R::kUnsupported: return "unsupported";
  case R::kNotApplicable: return "not_applicable";
  }
  return "unsupported";
}

const char *boundary_state(hss::plan_semantics::BoundaryState value) {
  using B = hss::plan_semantics::BoundaryState;
  switch (value) {
  case B::kClosedAccepted: return "closed_accepted";
  case B::kOpen: return "open";
  case B::kUnknown: return "unknown";
  case B::kUnsupported: return "unsupported";
  }
  return "unsupported";
}

const char *availability(hss::plan_semantics::Availability value) {
  using A = hss::plan_semantics::Availability;
  switch (value) {
  case A::kReady: return "ready";
  case A::kProvisional: return "provisional";
  case A::kUnavailable: return "unavailable";
  case A::kUnsupported: return "unsupported";
  case A::kNotApplicable: return "not_applicable";
  }
  return "unsupported";
}

const char *evidence_state(hss::plan_semantics::EvidenceState value) {
  using E = hss::plan_semantics::EvidenceState;
  switch (value) {
  case E::kSupported: return "supported";
  case E::kUnknown: return "unknown";
  case E::kUnsupported: return "unsupported";
  }
  return "unsupported";
}

const char *evidence_kind(hss::plan_semantics::EvidenceKind value) {
  using E = hss::plan_semantics::EvidenceKind;
  switch (value) {
  case E::kSurfaceIntersection: return "surface_intersection";
  case E::kFloorJunction: return "floor_junction";
  case E::kCeilingPatch: return "ceiling_patch";
  case E::kUnknownRegion: return "unknown_region";
  case E::kUnknownBoundary: return "unknown_boundary";
  case E::kUnsupported: return "unsupported";
  case E::kCapturedSection: return "captured_section";
  }
  return "unsupported";
}

const char *geometry_origin(hss::area::GeometryOrigin value) {
  using O = hss::area::GeometryOrigin;
  switch (value) {
  case O::kObserved: return "observed";
  case O::kFitted: return "fitted";
  case O::kUserMeasured: return "user_measured";
  case O::kInferred: return "inferred";
  case O::kUnknown: return "unknown";
  }
  return "unknown";
}

const char *plan_issue(hss::plan_semantics::IssueCode value) {
  using I = hss::plan_semantics::IssueCode;
  switch (value) {
  case I::kFloorReferenceUnknown: return "floor_reference_unknown";
  case I::kFloorReferenceUnsupported: return "floor_reference_unsupported";
  case I::kOpenBoundary: return "open_boundary";
  case I::kUnknownBoundaryEvidence: return "unknown_boundary_evidence";
  case I::kUnsupportedBoundaryEvidence: return "unsupported_boundary_evidence";
  case I::kInferredGeometry: return "inferred_geometry";
  case I::kQualityNotReady: return "quality_not_ready";
  case I::kCeilingUnknownRegion: return "ceiling_unknown_region";
  case I::kAreaNotApplicable: return "area_not_applicable";
  }
  return "unknown";
}

std::string serialize_plan(const hss::plan_semantics::PlanLayer &plan) {
  std::string output = "{\"scene_revision\":" + json_quote(plan.scene_revision) +
                       ",\"source_is_synthetic\":" +
                       (plan.source_is_synthetic ? "true" : "false") +
                       ",\"view\":" + json_quote(plan_view(plan.view)) +
                       ",\"height\":{\"cut_height_m\":";
  output += plan.height.cut_height_m ? json_number(*plan.height.cut_height_m)
                                     : "null";
  output += ",\"sampling_band_m\":";
  output += plan.height.sampling_band_m
                ? json_number(*plan.height.sampling_band_m)
                : "null";
  output += ",\"reference_state\":" +
            json_quote(reference_state(plan.height.reference_state)) +
            ",\"floor_reference_id\":";
  output += plan.height.floor_reference_id.empty()
                ? "null"
                : json_quote(plan.height.floor_reference_id);
  output += "},\"boundary_state\":" +
            json_quote(boundary_state(plan.boundary_state)) +
            ",\"validity\":" + json_quote(availability(plan.validity)) +
            ",\"fragments\":";
  json_array(output, plan.fragments, [](std::string &destination,
                                        const auto &fragment) {
    destination += "{\"id\":" + json_quote(fragment.id) +
                   ",\"state\":" + json_quote(evidence_state(fragment.state)) +
                   ",\"kind\":" + json_quote(evidence_kind(fragment.kind)) +
                   ",\"geometry_origin\":" +
                   json_quote(geometry_origin(fragment.geometry_origin)) +
                   ",\"segment_m\":";
    if (fragment.segment) {
      destination += "[[" + json_number(fragment.segment->start_m.x_m) + "," +
                     json_number(fragment.segment->start_m.y_m) + "],[" +
                     json_number(fragment.segment->end_m.x_m) + "," +
                     json_number(fragment.segment->end_m.y_m) + "]]";
    } else {
      destination += "null";
    }
    destination += ",\"source_height_range_m\":";
    if (fragment.source_height_range_m) {
      destination += "[" +
                     json_number(fragment.source_height_range_m->minimum_m) +
                     "," +
                     json_number(fragment.source_height_range_m->maximum_m) +
                     "]";
    } else {
      destination += "null";
    }
    destination += ",\"source_revision\":" +
                   json_quote(fragment.source_revision) +
                   ",\"support_ids\":";
    json_array(destination, fragment.support_ids,
               [](std::string &array, const std::string &support) {
                 array += json_quote(support);
               });
    destination += "}";
  });
  output += ",\"area\":{\"policy\":" +
            json_quote(plan.area.policy == hss::area::AreaPolicy::kInternalFinishedFace
                           ? "internal_finished_face"
                           : "net_fixed_obstructions") +
            ",\"availability\":" +
            json_quote(availability(plan.area.availability)) +
            ",\"area_m2\":";
  output += plan.area.area_m2 ? json_number(*plan.area.area_m2) : "null";
  output += ",\"calculator_status\":";
  output += plan.area.calculator_status
                ? json_quote(hss::area::status_name(*plan.area.calculator_status))
                : "null";
  output += "},\"issues\":";
  json_array(output, plan.issues,
             [](std::string &destination, const auto &issue) {
               destination += "{\"code\":" +
                              json_quote(plan_issue(issue.code)) +
                              ",\"target_id\":";
               destination += issue.target_id.empty() ? "null"
                                                       : json_quote(issue.target_id);
               destination += "}";
             });
  output += "}";
  return output;
}

const char *annotation_kind(hss::annotation_geometry::Kind value) {
  using K = hss::annotation_geometry::Kind;
  switch (value) {
  case K::kLength: return "length";
  case K::kAngle: return "angle";
  case K::kHeight: return "height";
  case K::kLabel: return "label";
  }
  return "label";
}

const char *annotation_source(hss::annotation_geometry::EvidenceSource value) {
  using E = hss::annotation_geometry::EvidenceSource;
  switch (value) {
  case E::kObserved: return "observed";
  case E::kFitted: return "fitted";
  case E::kUserMeasured: return "user_measured";
  case E::kInferred: return "inferred";
  case E::kUnknown: return "unknown";
  }
  return "unknown";
}

const char *annotation_availability(
    hss::annotation_geometry::Availability value) {
  using A = hss::annotation_geometry::Availability;
  switch (value) {
  case A::kReady: return "ready";
  case A::kProvisional: return "provisional";
  case A::kUnavailable: return "unavailable";
  }
  return "unavailable";
}

const char *display_unit(hss::annotation_geometry::DisplayUnit value) {
  using U = hss::annotation_geometry::DisplayUnit;
  switch (value) {
  case U::kMetres: return "metres";
  case U::kMillimetres: return "millimetres";
  case U::kFeet: return "feet";
  }
  return "metres";
}

std::string serialize_annotations(
    const hss::annotation_geometry::Layout &layout) {
  std::string output = "{\"scene_revision\":" +
                       json_quote(layout.scene_revision) +
                       ",\"annotations\":";
  json_array(output, layout.annotations,
             [](std::string &destination, const auto &resolved) {
    const auto &annotation = resolved.annotation;
    destination += "{\"id\":" + json_quote(annotation.id) +
                   ",\"kind\":" + json_quote(annotation_kind(annotation.kind)) +
                   ",\"evidence_source\":" +
                   json_quote(annotation_source(annotation.evidence_source)) +
                   ",\"source_revision\":" +
                   json_quote(annotation.source_revision) +
                   ",\"evidence_ids\":";
    json_array(destination, annotation.evidence_ids,
               [](std::string &array, const std::string &evidence) {
                 array += json_quote(evidence);
               });
    destination += ",\"anchors\":";
    json_array(destination, annotation.anchors,
               [](std::string &array, const auto &anchor) {
                 array += "{\"geometry_id\":" +
                          json_quote(anchor.geometry_id) +
                          ",\"position_m\":[" +
                          json_number(anchor.position_m.x_m) + "," +
                          json_number(anchor.position_m.y_m) + "]}";
               });
    destination += ",\"display_unit\":" +
                   json_quote(display_unit(annotation.display_unit)) +
                   ",\"display_precision\":" +
                   std::to_string(annotation.display_precision) +
                   ",\"uncertainty_m\":";
    destination += annotation.uncertainty_m
                       ? json_number(*annotation.uncertainty_m)
                       : "null";
    destination += ",\"user_measured_value_m\":";
    destination += annotation.user_measured_value_m
                       ? json_number(*annotation.user_measured_value_m)
                       : "null";
    destination += ",\"label\":" + json_quote(annotation.label) +
                   ",\"availability\":" +
                   json_quote(annotation_availability(resolved.availability)) +
                   ",\"metric_value\":";
    destination += resolved.metric_value ? json_number(*resolved.metric_value)
                                         : "null";
    destination += "}";
  });
  output += "}";
  return output;
}

void build_plan_snapshot(const hss::live::Snapshot &source,
                         HSSSnapshot *destination) {
  auto &summary = destination->plan_summary;
  std::memset(&summary, 0, sizeof(summary));
  summary.abi_version = HSS_ABI_VERSION;
  summary.struct_size = sizeof(summary);
  summary.scene_revision = source.revision;
  if (!source.plan)
    return;
  const auto &plan = *source.plan;
  summary.plan_available = 1U;
  summary.source_is_synthetic = plan.source_is_synthetic ? 1U : 0U;
  summary.fragment_count = plan.fragments.size();
  summary.plan_issue_count = plan.issues.size();
  summary.view = static_cast<uint32_t>(plan.view);
  summary.reference_state = static_cast<uint32_t>(plan.height.reference_state);
  summary.boundary_state = static_cast<uint32_t>(plan.boundary_state);
  summary.plan_availability = static_cast<uint32_t>(plan.validity);
  summary.area_availability = static_cast<uint32_t>(plan.area.availability);
  summary.has_area = plan.area.area_m2 ? 1U : 0U;
  summary.area_m2 = plan.area.area_m2.value_or(0.0);
  summary.cut_height_m = plan.height.cut_height_m.value_or(0.0);
  summary.sampling_band_m = plan.height.sampling_band_m.value_or(0.0);
  destination->plan_json = serialize_plan(plan);
  if (source.annotations) {
    summary.annotation_layout_available = 1U;
    summary.annotation_count = source.annotations->annotations.size();
    destination->annotation_json = serialize_annotations(*source.annotations);
  }
}

void build_snapshot_mask(const HSSEngine &engine, HSSSnapshot *snapshot) {
  const uint64_t triangle_count = snapshot->data->triangles.size() / 3U;
  snapshot->triangle_inclusion.assign(static_cast<size_t>(triangle_count),
                                      uint8_t{1});
  auto &summary = snapshot->dynamic_summary;
  std::memset(&summary, 0, sizeof(summary));
  summary.abi_version = HSS_ABI_VERSION;
  summary.struct_size = sizeof(summary);
  summary.scene_revision = snapshot->data->revision;
  summary.triangle_count = triangle_count;
  const auto raw = captured_mesh(engine);
  if (raw.triangles.size() != triangle_count) {
    summary.stale_exclusion_count = engine.verified_exclusions.size();
    summary.included_triangle_count = triangle_count;
    return;
  }
  for (const auto &[command_id, record] : engine.verified_exclusions) {
    (void)command_id;
    const auto result = hss::dynamic_provenance::apply_exclusion(
        raw, record.exclusion, record.evidence);
    if (result.status != hss::dynamic_provenance::Status::kOk ||
        !result.layer || result.layer->triangle_mask.size() != triangle_count ||
        triangle_mask_fingerprint(result.layer->triangle_mask) !=
            record.triangle_mask_fingerprint) {
      ++summary.stale_exclusion_count;
      continue;
    }
    ++summary.applied_exclusion_count;
    for (size_t triangle = 0U; triangle < result.layer->triangle_mask.size();
         ++triangle) {
      if (result.layer->triangle_mask[triangle] != 0U)
        snapshot->triangle_inclusion[triangle] = uint8_t{0};
    }
  }
  summary.excluded_triangle_count = static_cast<uint64_t>(std::count(
      snapshot->triangle_inclusion.begin(), snapshot->triangle_inclusion.end(),
      uint8_t{0}));
  summary.included_triangle_count =
      triangle_count - summary.excluded_triangle_count;
}

} // namespace

extern "C" HSSStatus hss_engine_create(const HSSConfig *config,
                                       HSSEngine **out_engine) {
  if (out_engine != nullptr) {
    *out_engine = nullptr;
  }
  try {
    if (out_engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus status = validate_config(config);
    if (status != HSS_OK) {
      return status;
    }
    auto engine = std::make_unique<HSSEngine>(*config);
    *out_engine = engine.release();
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_engine_destroy(HSSEngine *engine) {
  try {
    delete engine;
  } catch (...) {
    // C linkage must not expose a C++ exception. There is no status channel.
  }
}

extern "C" HSSStatus hss_engine_observe_resource_sample(
    HSSEngine *engine, const HSSResourceSample *sample,
    HSSResourceDecision *decision) {
  if (decision != nullptr)
    std::memset(decision, 0, sizeof(*decision));
  try {
    if (engine == nullptr || decision == nullptr)
      return engine == nullptr ? HSS_INVALID_ARGUMENT
                               : remembered(engine, HSS_INVALID_ARGUMENT);
    const HSSStatus header_status = validate_message(sample);
    if (header_status != HSS_OK) {
      latch_resource_input_fault(engine);
      fill_resource_decision(*engine, decision);
      return remembered(engine, header_status);
    }
    if (sample->flags != 0U || sample->reserved != 0U ||
        sample->thermal_status > HSS_RESOURCE_MEASUREMENT_MEASURED ||
        sample->thermal_state > HSS_RESOURCE_THERMAL_CRITICAL ||
        sample->memory_status > HSS_RESOURCE_MEASUREMENT_MEASURED ||
        sample->memory_pressure > HSS_RESOURCE_MEMORY_CRITICAL ||
        sample->storage_status > HSS_RESOURCE_MEASUREMENT_MEASURED) {
      latch_resource_input_fault(engine);
      fill_resource_decision(*engine, decision);
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }

    hss::resource::ResourceSample input;
    input.sequence = sample->sequence;
    input.monotonic_time_s = sample->monotonic_time_s;
    input.thermal.status =
        static_cast<hss::resource::MeasurementStatus>(sample->thermal_status);
    input.thermal.state =
        static_cast<hss::resource::ThermalState>(sample->thermal_state);
    input.memory.status =
        static_cast<hss::resource::MeasurementStatus>(sample->memory_status);
    input.memory.pressure =
        static_cast<hss::resource::MemoryPressure>(sample->memory_pressure);
    input.storage.status =
        static_cast<hss::resource::MeasurementStatus>(sample->storage_status);
    input.storage.free_bytes = sample->free_storage_bytes;
    engine->resource_decision = engine->resource_policy.observe(input);
    if (engine->resource_decision.status == hss::resource::Status::kOk) {
      engine->last_resource_sample_sequence = sample->sequence;
      engine->last_resource_sample_time_s = sample->monotonic_time_s;
      if (engine->resource_decision.monitoring_complete)
        engine->resource_input_fault = false;
    } else {
      engine->resource_input_fault = true;
    }
    fill_resource_decision(*engine, decision);
    return remembered(engine, resource_status(engine->resource_decision.status));
  } catch (...) {
    return engine == nullptr ? HSS_INTERNAL_ERROR
                             : remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus
hss_engine_get_resource_decision(const HSSEngine *engine,
                                 HSSResourceDecision *decision) {
  if (decision != nullptr)
    std::memset(decision, 0, sizeof(*decision));
  try {
    if (engine == nullptr || decision == nullptr)
      return HSS_INVALID_ARGUMENT;
    fill_resource_decision(*engine, decision);
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_acknowledge_resource_checkpoint(
    HSSEngine *engine, uint32_t checkpoint_result,
    HSSResourceDecision *decision) {
  if (decision != nullptr)
    std::memset(decision, 0, sizeof(*decision));
  try {
    if (engine == nullptr || decision == nullptr)
      return engine == nullptr ? HSS_INVALID_ARGUMENT
                               : remembered(engine, HSS_INVALID_ARGUMENT);
    if (checkpoint_result > HSS_RESOURCE_CHECKPOINT_FAILED)
      return remembered(engine, HSS_INVALID_ARGUMENT);
    const auto durable = decide_resource_event(
        engine, hss::resource::EventKind::kDurableCommit);
    if (!durable.protected_event ||
        durable.action != hss::resource::EventAction::kAcceptProtected)
      return remembered(engine, HSS_INTERNAL_ERROR);
    engine->resource_decision = engine->resource_policy.complete_checkpoint(
        static_cast<hss::resource::CheckpointResult>(checkpoint_result));
    fill_resource_decision(*engine, decision);
    return remembered(engine, resource_status(engine->resource_decision.status));
  } catch (...) {
    return engine == nullptr ? HSS_INTERNAL_ERROR
                             : remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_set_epoch(HSSEngine *engine,
                                          const HSSEpoch *epoch) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus status = validate_message(epoch);
    if (status != HSS_OK) {
      return remembered(engine, status);
    }
    if (epoch->relationship_state > 1U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!hss::coordinate::is_rigid_transform(epoch->T_project_world)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    const auto resource = decide_resource_event(
        engine, hss::resource::EventKind::kEpochTransition);
    if (!resource.protected_event ||
        resource.action != hss::resource::EventAction::kAcceptProtected)
      return remembered(engine, HSS_INTERNAL_ERROR);
    hss::ingress::Event event{};
    event.kind = hss::ingress::EventKind::epoch;
    event.epoch = *epoch;
    event.capture_id = epoch->capture_id;
    event.event_sequence = epoch->event_sequence;
    event.session_epoch = epoch->session_epoch;
    const hss::ingress::Admission admission =
        check_admission(engine, event.kind, 0U);
    if (admission != hss::ingress::Admission::accepted) {
      return remembered(engine, admission_status(admission));
    }
    return remembered(engine, enqueue_owned_event(engine, std::move(event)));
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_frame(HSSEngine *engine,
                                             const HSSFrame *frame) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(frame);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (frame->tracking_state > 2U || frame->depth_source > 1U ||
        frame->reserved != 0U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!std::isfinite(frame->timestamp_s) ||
        !hss::coordinate::is_rigid_transform(frame->T_world_camera)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    if (!finite_positive(frame->depth_intrinsics.fx) ||
        !finite_positive(frame->depth_intrinsics.fy) ||
        !std::isfinite(frame->depth_intrinsics.cx) ||
        !std::isfinite(frame->depth_intrinsics.cy)) {
      return remembered(engine, HSS_INVALID_CALIBRATION);
    }
    if (frame->depth_width == 0U || frame->depth_height == 0U ||
        frame->depth_row_stride_bytes <
            static_cast<uint64_t>(frame->depth_width) * 4U ||
        frame->depth_bytes == nullptr) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    uint64_t required_depth = 0;
    if (!multiply_add_fits(frame->depth_row_stride_bytes, frame->depth_height,
                           static_cast<uint64_t>(frame->depth_width) * 4U,
                           &required_depth) ||
        frame->depth_byte_length < required_depth) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (frame->confidence_bytes == nullptr) {
      if (frame->confidence_row_stride_bytes != 0U ||
          frame->confidence_byte_length != 0U) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    } else {
      if (frame->confidence_row_stride_bytes < frame->depth_width) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
      uint64_t required_confidence = 0;
      if (!multiply_add_fits(frame->confidence_row_stride_bytes,
                             frame->depth_height, frame->depth_width,
                             &required_confidence) ||
          frame->confidence_byte_length < required_confidence) {
        return remembered(engine, HSS_INVALID_ARGUMENT);
      }
    }
    uint64_t payload_bytes = required_depth;
    if (frame->confidence_bytes != nullptr) {
      uint64_t required_confidence = 0;
      if (!multiply_add_fits(frame->confidence_row_stride_bytes,
                             frame->depth_height, frame->depth_width,
                             &required_confidence) ||
          !add_fits(payload_bytes, required_confidence, &payload_bytes)) {
        return remembered(engine, HSS_RESOURCE_LIMIT);
      }
    }
    const auto resource = decide_resource_event(
        engine, frame->depth_source == 1U
                    ? hss::resource::EventKind::kPreviewFrame
                    : hss::resource::EventKind::kMetricObservation);
    if (resource.action == hss::resource::EventAction::kDropPreview) {
      engine->ingress.record_dropped_frame();
      return remembered(engine, HSS_OK);
    }
    if (resource.action ==
        hss::resource::EventAction::kRetainSourceAndPauseCapture)
      return remembered(engine, HSS_NOT_READY);
    if (resource.action != hss::resource::EventAction::kProcess)
      return remembered(engine, HSS_INTERNAL_ERROR);
    const hss::ingress::Admission admission =
        check_admission(engine, hss::ingress::EventKind::frame, payload_bytes);
    if (admission == hss::ingress::Admission::dropped_frame) {
      engine->ingress.record_dropped_frame();
      return remembered(engine, HSS_OK);
    }
    if (admission != hss::ingress::Admission::accepted) {
      return remembered(engine, admission_status(admission));
    }
    hss::ingress::Event event{};
    event.kind = hss::ingress::EventKind::frame;
    event.frame = *frame;
    event.frame.depth_bytes = nullptr;
    event.frame.depth_byte_length = required_depth;
    event.frame.confidence_bytes = nullptr;
    event.frame.confidence_byte_length = payload_bytes - required_depth;
    event.capture_id = frame->capture_id;
    event.event_sequence = frame->event_sequence;
    event.session_epoch = frame->session_epoch;
    event.payload_bytes = payload_bytes;
    event.payload =
        copy_payloads(frame->depth_bytes, required_depth,
                      frame->confidence_bytes, payload_bytes - required_depth);
    const HSSStatus enqueue_status =
        enqueue_owned_event(engine, std::move(event));
    if (enqueue_status == HSS_OK) {
      ++engine->accepted_frames;
    }
    return remembered(engine, enqueue_status);
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_mesh_event(HSSEngine *engine,
                                                  const HSSMeshEvent *event) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus header_status = validate_message(event);
    if (header_status != HSS_OK) {
      return remembered(engine, header_status);
    }
    if (event->kind > 2U) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (!hss::coordinate::is_rigid_transform(event->T_world_anchor)) {
      return remembered(engine, HSS_INVALID_COORDINATE_FRAME);
    }
    const bool remove = event->kind == 2U;
    if ((remove &&
         (event->vertices_xyz != nullptr ||
          event->triangle_indices != nullptr || event->vertex_count != 0U ||
          event->triangle_count != 0U)) ||
        (!remove &&
         ((event->vertex_count != 0U && event->vertices_xyz == nullptr) ||
          (event->triangle_count != 0U &&
           event->triangle_indices == nullptr)))) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
    if (event->vertex_count > UINT64_MAX / (3U * sizeof(float)) ||
        event->triangle_count > UINT64_MAX / (3U * sizeof(uint32_t))) {
      return remembered(engine, HSS_RESOURCE_LIMIT);
    }
    if (event->vertex_count > 65536 || event->triangle_count > 65536)
      return remembered(engine, HSS_RESOURCE_LIMIT);
    for (uint64_t i = 0; i < event->vertex_count * 3; ++i)
      if (!std::isfinite(event->vertices_xyz[i]) ||
          std::abs(event->vertices_xyz[i]) > 10000)
        return remembered(engine, HSS_INVALID_ARGUMENT);
    for (uint64_t i = 0; i < event->triangle_count * 3; ++i)
      if (event->triangle_indices[i] >= event->vertex_count)
        return remembered(engine, HSS_INVALID_ARGUMENT);
    const uint64_t vertex_bytes = event->vertex_count * 3U * sizeof(float);
    const uint64_t triangle_bytes =
        event->triangle_count * 3U * sizeof(uint32_t);
    uint64_t payload_bytes = 0;
    if (!add_fits(vertex_bytes, triangle_bytes, &payload_bytes)) {
      return remembered(engine, HSS_RESOURCE_LIMIT);
    }
    const hss::ingress::EventKind kind =
        event->kind == 0U
            ? hss::ingress::EventKind::mesh_add
            : (event->kind == 1U ? hss::ingress::EventKind::mesh_replace
                                 : hss::ingress::EventKind::mesh_remove);
    const auto resource = decide_resource_event(
        engine, remove ? hss::resource::EventKind::kMeshRemoval
                       : hss::resource::EventKind::kMetricObservation);
    if (remove) {
      if (!resource.protected_event ||
          resource.action != hss::resource::EventAction::kAcceptProtected)
        return remembered(engine, HSS_INTERNAL_ERROR);
    } else {
      if (resource.action ==
          hss::resource::EventAction::kRetainSourceAndPauseCapture)
        return remembered(engine, HSS_NOT_READY);
      if (resource.action != hss::resource::EventAction::kProcess)
        return remembered(engine, HSS_INTERNAL_ERROR);
    }
    const hss::ingress::Admission admission =
        check_admission(engine, kind, payload_bytes);
    if (admission != hss::ingress::Admission::accepted) {
      // Mesh removals/replacements are structural control events.  They must
      // surface retry/backpressure instead of becoming a best-effort drop.
      return remembered(engine, admission_status(admission));
    }
    hss::ingress::Event queued{};
    queued.kind = kind;
    queued.mesh = *event;
    queued.mesh.vertices_xyz = nullptr;
    queued.mesh.triangle_indices = nullptr;
    queued.capture_id = event->capture_id;
    queued.anchor_id = event->anchor_id;
    queued.event_sequence = event->event_sequence;
    queued.anchor_revision = event->anchor_revision;
    queued.session_epoch = event->session_epoch;
    queued.payload_bytes = payload_bytes;
    queued.payload = copy_payloads(
        reinterpret_cast<const uint8_t *>(event->vertices_xyz), vertex_bytes,
        reinterpret_cast<const uint8_t *>(event->triangle_indices),
        triangle_bytes);
    return remembered(engine, enqueue_owned_event(engine, std::move(queued)));
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_submit_room_proposal_json(HSSEngine *engine,
                                                          HSSBytes proposal) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_json_bytes(proposal);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_apply_command_json(HSSEngine *engine,
                                                   HSSBytes command) {
  try {
    if (engine == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    const HSSStatus bytes_status = validate_json_bytes(command);
    if (bytes_status != HSS_OK) {
      return remembered(engine, bytes_status);
    }
    if (engine->live.busy() || engine->ingress.pending_events() != 0U)
      return remembered(engine, HSS_NOT_READY);
    const auto resource = decide_resource_event(
        engine, hss::resource::EventKind::kCommand);
    if (!resource.protected_event ||
        resource.action != hss::resource::EventAction::kAcceptProtected)
      return remembered(engine, HSS_INTERNAL_ERROR);
    const std::string exact(reinterpret_cast<const char *>(command.data),
                            static_cast<size_t>(command.length));
    const auto processed = engine->scene_editor.process(
        engine->live.snapshot()->json, exact);
    if (processed.exact_result_json.size() > kMaxJsonRecordBytes)
      return remembered(engine, HSS_RESOURCE_LIMIT);
    engine->command_result = processed.exact_result_json;
    engine->has_command_result = true;
    if (processed.pending_dynamic_exclusion) {
      engine->pending_exclusions[processed.pending_dynamic_exclusion->command_id] =
          {*processed.pending_dynamic_exclusion,
           engine->live.snapshot()->revision};
    }
    if (processed.scene_changed) {
      const auto counts =
          hss::scene_editing::scene_counts(processed.candidate_scene_json);
      if (!counts)
        return remembered(engine, HSS_INTERNAL_ERROR);
      const HSSStatus published = engine->live.publish_edited_scene(
          processed.candidate_scene_json,
          engine->live.snapshot()->revision + 1U, counts->node_count,
          counts->edge_count, counts->issue_count);
      if (published != HSS_OK)
        return remembered(engine, published);
      ++engine->accepted_commands;
    }
    switch (processed.status) {
    case hss::scene_editing::Status::kApplied:
    case hss::scene_editing::Status::kStoredRetry:
      return remembered(engine, HSS_OK);
    case hss::scene_editing::Status::kConflict:
      return remembered(engine, HSS_CONSTRAINT_CONFLICT);
    case hss::scene_editing::Status::kDuplicateMismatch:
      return remembered(engine, HSS_DUPLICATE_MISMATCH);
    case hss::scene_editing::Status::kStaleRevision:
    case hss::scene_editing::Status::kFutureRevision:
    case hss::scene_editing::Status::kTargetNotCurrent:
    case hss::scene_editing::Status::kTargetLineageChanged:
      return remembered(engine, HSS_STALE_REVISION);
    case hss::scene_editing::Status::kPendingUnsupported:
      return remembered(engine, HSS_UNSUPPORTED_CAPABILITY);
    case hss::scene_editing::Status::kResourceLimitExceeded:
      return remembered(engine, HSS_RESOURCE_LIMIT);
    default:
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }
  } catch (const std::bad_alloc &) {
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_apply_dynamic_evidence(
    HSSEngine *engine, const HSSDynamicEvidenceBatch *batch,
    HSSDynamicExclusionAudit *audit) {
  if (audit != nullptr)
    std::memset(audit, 0, sizeof(*audit));
  try {
    if (engine == nullptr || audit == nullptr)
      return engine == nullptr ? HSS_INVALID_ARGUMENT
                               : remembered(engine, HSS_INVALID_ARGUMENT);
    HSSStatus status = validate_message(batch);
    if (status != HSS_OK)
      return remembered(engine, status);
    if (batch->flags != 0U || batch->reserved != 0U ||
        zero_id(batch->exclusion_command_id) ||
        engine->live.busy() || engine->ingress.pending_events() != 0U)
      return remembered(engine, engine->live.busy() ||
                                    engine->ingress.pending_events() != 0U
                                ? HSS_NOT_READY
                                : HSS_INVALID_ARGUMENT);
    const auto resource = decide_resource_event(
        engine, hss::resource::EventKind::kCommand);
    if (!resource.protected_event ||
        resource.action != hss::resource::EventAction::kAcceptProtected)
      return remembered(engine, HSS_INTERNAL_ERROR);
    const std::string command_id = uuid(batch->exclusion_command_id);
    const auto pending = engine->pending_exclusions.find(command_id);
    if (pending == engine->pending_exclusions.end())
      return remembered(engine, HSS_INVALID_ARGUMENT);
    if (batch->expected_scene_revision != engine->live.snapshot()->revision ||
        batch->expected_scene_revision != pending->second.second)
      return remembered(engine, HSS_STALE_REVISION);

    std::vector<hss::dynamic_provenance::VerifiedDynamicEvidence> evidence;
    status = convert_evidence(*batch, &evidence);
    if (status != HSS_OK)
      return remembered(engine, status);
    if (!std::all_of(evidence.begin(), evidence.end(), [&](const auto &item) {
          return item.project_id == uuid(engine->config.project_id);
        })) {
      return remembered(engine, HSS_INVALID_ARGUMENT);
    }

    const auto existing = engine->verified_exclusions.find(command_id);
    if (existing != engine->verified_exclusions.end()) {
      if (existing->second.accepted_scene_revision !=
              batch->expected_scene_revision ||
          existing->second.evidence != evidence)
        return remembered(engine, HSS_DUPLICATE_MISMATCH);
      fill_dynamic_audit(batch->exclusion_command_id,
                         existing->second.accepted_scene_revision,
                         existing->second.audit,
                         existing->second.triangle_mask_fingerprint, audit);
      return remembered(engine, HSS_OK);
    }

    const auto result = hss::dynamic_provenance::apply_exclusion(
        captured_mesh(*engine), pending->second.first, evidence);
    status = dynamic_status(result.status);
    if (status != HSS_OK || !result.layer)
      return remembered(engine,
                        status == HSS_OK ? HSS_INTERNAL_ERROR : status);
    runtime_provenance::VerifiedExclusion record;
    const uint64_t evidence_charge =
        4096U + static_cast<uint64_t>(evidence.size()) * 1024U;
    record.lease = hss::live::reserve(engine->live.budget(), evidence_charge);
    if (!record.lease)
      return remembered(engine, HSS_RESOURCE_LIMIT);
    record.exclusion = pending->second.first;
    record.evidence = std::move(evidence);
    record.audit = result.layer->audit;
    record.triangle_mask_fingerprint =
        triangle_mask_fingerprint(result.layer->triangle_mask);
    record.accepted_scene_revision = batch->expected_scene_revision;
    const uint64_t fingerprint = record.triangle_mask_fingerprint;
    engine->verified_exclusions.emplace(command_id, std::move(record));
    fill_dynamic_audit(batch->exclusion_command_id,
                       batch->expected_scene_revision, result.layer->audit,
                       fingerprint, audit);
    return remembered(engine, HSS_OK);
  } catch (const std::bad_alloc &) {
    return engine == nullptr ? HSS_RESOURCE_LIMIT
                             : remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    return engine == nullptr ? HSS_INTERNAL_ERROR
                             : remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus
hss_engine_copy_command_result_json(const HSSEngine *engine,
                                    uint8_t *destination, uint64_t capacity,
                                    uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (engine == nullptr || required == nullptr ||
        (destination == nullptr && capacity != 0U)) {
      return HSS_INVALID_ARGUMENT;
    }
    if (!engine->has_command_result)
      return HSS_NOT_READY;
    return copy_out(reinterpret_cast<const uint8_t *>(engine->command_result.data()),
                    engine->command_result.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_step(HSSEngine *engine,
                                     uint32_t work_budget_us) {
  try {
    if (engine == nullptr || work_budget_us == 0U) {
      return engine == nullptr ? HSS_INVALID_ARGUMENT
                               : remembered(engine, HSS_INVALID_ARGUMENT);
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(work_budget_us);
    // At least one bounded quantum makes forward progress even for a 1 us
    // request. A quantum is one vertex, triangle, <=96-point fit, <=128-edge
    // contour validation, bounded metadata setup or one JSON record.
    do {
      if (engine->live.busy()) {
        engine->live.advance();
        if (!engine->live.busy() && engine->live.status() == HSS_OK &&
            engine->provenance_inflight) {
          accept_provenance_event(&engine->provenance,
                                  std::move(*engine->provenance_inflight));
          engine->provenance_inflight.reset();
        }
        if (engine->live.status() != HSS_OK) {
          engine->provenance_inflight.reset();
          return remembered(engine, engine->live.status());
        }
      } else {
        // Allocate before popping: allocation failure must not desynchronize
        // the event queue and its parallel reservation leases.
        auto owned = std::make_shared<hss::live::Evidence>();
        hss::ingress::Event event{};
        if (!engine->ingress.pop(&event))
          break;
        owned->lease = std::move(engine->queued_leases.front());
        engine->queued_leases.pop_front();
        owned->event = std::move(event);
        std::optional<runtime_provenance::Inflight> provenance_event;
        const bool tracks_provenance =
            owned->event.kind == hss::ingress::EventKind::epoch ||
            owned->event.kind == hss::ingress::EventKind::mesh_add ||
            owned->event.kind == hss::ingress::EventKind::mesh_replace ||
            owned->event.kind == hss::ingress::EventKind::mesh_remove;
        if (tracks_provenance) {
          auto lease = hss::live::reserve(
              engine->live.budget(),
              hss::live::event_reservation(owned->event));
          if (!lease)
            return remembered(engine, HSS_RESOURCE_LIMIT);
          provenance_event =
              runtime_provenance::Inflight{owned->event, std::move(lease)};
        }
        auto status = engine->live.begin(std::move(owned));
        if (status != HSS_OK)
          return remembered(engine, status);
        engine->provenance_inflight = std::move(provenance_event);
      }
    } while (std::chrono::steady_clock::now() < deadline);
    return remembered(engine, HSS_OK);
  } catch (const std::bad_alloc &) {
    engine->live.cancel(HSS_RESOURCE_LIMIT);
    return remembered(engine, HSS_RESOURCE_LIMIT);
  } catch (...) {
    engine->live.cancel(HSS_INTERNAL_ERROR);
    return remembered(engine, HSS_INTERNAL_ERROR);
  }
}

extern "C" HSSStatus hss_engine_get_stats(const HSSEngine *engine,
                                          HSSEngineStats *stats) {
  if (stats != nullptr) {
    std::memset(stats, 0, sizeof(*stats));
  }
  try {
    if (engine == nullptr || stats == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    stats->abi_version = HSS_ABI_VERSION;
    stats->struct_size = sizeof(*stats);
    stats->accepted_frames = engine->accepted_frames;
    stats->dropped_frames = engine->ingress.dropped_frames();
    stats->accepted_commands = engine->accepted_commands;
    stats->working_bytes = engine->live.budget()->used;
    stats->pending_frames = engine->ingress.pending_frames();
    stats->scene_revision = engine->live.snapshot()->revision;
    stats->lifecycle_state =
        engine->live.busy() || engine->ingress.pending_events() != 0 ? 2 : 1;
    stats->last_status = static_cast<uint32_t>(engine->last_status);
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_acquire_snapshot(const HSSEngine *engine,
                                                 HSSSnapshot **out_snapshot) {
  if (out_snapshot != nullptr) {
    *out_snapshot = nullptr;
  }
  try {
    if (engine == nullptr || out_snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    auto *snapshot = new HSSSnapshot();
    snapshot->data = engine->live.snapshot();
    build_snapshot_mask(*engine, snapshot);
    build_plan_snapshot(*snapshot->data, snapshot);
    *out_snapshot = snapshot;
    return HSS_OK;
  } catch (const std::bad_alloc &) {
    return HSS_RESOURCE_LIMIT;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" void hss_snapshot_release(HSSSnapshot *snapshot) {
  try {
    delete snapshot;
  } catch (...) {
  }
}

extern "C" HSSStatus hss_snapshot_get_counts(const HSSSnapshot *snapshot,
                                             HSSCounts *counts) {
  if (counts != nullptr) {
    std::memset(counts, 0, sizeof(*counts));
  }
  try {
    if (snapshot == nullptr || counts == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    counts->abi_version = HSS_ABI_VERSION;
    counts->struct_size = sizeof(*counts);
    counts->scene_revision = snapshot->data->revision;
    counts->vertex_count = snapshot->data->vertices.size();
    counts->triangle_count = snapshot->data->triangles.size() / 3U;
    counts->boundary_node_count = snapshot->data->node_count;
    counts->boundary_edge_count = snapshot->data->edge_count;
    counts->issue_count = snapshot->data->issue_count;
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_vertices(const HSSSnapshot *snapshot,
                                                HSSVec3d *destination,
                                                uint64_t capacity,
                                                uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->data->vertices.data(),
                    snapshot->data->vertices.size(), destination, capacity,
                    required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_triangles(const HSSSnapshot *snapshot,
                                                 uint32_t *destination,
                                                 uint64_t capacity,
                                                 uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(snapshot->data->triangles.data(),
                    snapshot->data->triangles.size(), destination, capacity,
                    required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_triangle_inclusion(
    const HSSSnapshot *snapshot, uint8_t *destination, uint64_t capacity,
    uint64_t *required) {
  if (required != nullptr)
    *required = 0U;
  try {
    if (snapshot == nullptr)
      return HSS_INVALID_ARGUMENT;
    return copy_out(snapshot->triangle_inclusion.data(),
                    snapshot->triangle_inclusion.size(), destination, capacity,
                    required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_get_dynamic_mask_summary(
    const HSSSnapshot *snapshot, HSSDynamicMaskSummary *summary) {
  if (summary != nullptr)
    std::memset(summary, 0, sizeof(*summary));
  try {
    if (snapshot == nullptr || summary == nullptr)
      return HSS_INVALID_ARGUMENT;
    *summary = snapshot->dynamic_summary;
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot *snapshot,
                                                  uint8_t *destination,
                                                  uint64_t capacity,
                                                  uint64_t *required) {
  if (required != nullptr) {
    *required = 0;
  }
  try {
    if (snapshot == nullptr) {
      return HSS_INVALID_ARGUMENT;
    }
    return copy_out(
        reinterpret_cast<const uint8_t *>(snapshot->data->json.data()),
        snapshot->data->json.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_get_plan_annotation_summary(
    const HSSSnapshot *snapshot, HSSPlanAnnotationSummary *summary) {
  if (summary != nullptr)
    std::memset(summary, 0, sizeof(*summary));
  try {
    if (snapshot == nullptr || summary == nullptr)
      return HSS_INVALID_ARGUMENT;
    *summary = snapshot->plan_summary;
    return HSS_OK;
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_plan_json(
    const HSSSnapshot *snapshot, uint8_t *destination, uint64_t capacity,
    uint64_t *required) {
  if (required != nullptr)
    *required = 0U;
  try {
    if (snapshot == nullptr || required == nullptr ||
        (destination == nullptr && capacity != 0U))
      return HSS_INVALID_ARGUMENT;
    if (snapshot->plan_summary.plan_available == 0U)
      return HSS_UNSUPPORTED_CAPABILITY;
    return copy_out(
        reinterpret_cast<const uint8_t *>(snapshot->plan_json.data()),
        snapshot->plan_json.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_snapshot_copy_annotation_json(
    const HSSSnapshot *snapshot, uint8_t *destination, uint64_t capacity,
    uint64_t *required) {
  if (required != nullptr)
    *required = 0U;
  try {
    if (snapshot == nullptr || required == nullptr ||
        (destination == nullptr && capacity != 0U))
      return HSS_INVALID_ARGUMENT;
    if (snapshot->plan_summary.annotation_layout_available == 0U)
      return HSS_UNSUPPORTED_CAPABILITY;
    return copy_out(
        reinterpret_cast<const uint8_t *>(snapshot->annotation_json.data()),
        snapshot->annotation_json.size(), destination, capacity, required);
  } catch (...) {
    return HSS_INTERNAL_ERROR;
  }
}

extern "C" HSSStatus hss_engine_set_source_is_synthetic(HSSEngine *e,
                                                        uint32_t value) {
  if (!e)
    return HSS_INVALID_ARGUMENT;
  try {
    return remembered(e, e->live.set_synthetic(value));
  } catch (...) {
    return remembered(e, HSS_RESOURCE_LIMIT);
  }
}
extern "C" HSSStatus
hss_engine_set_plan_reference(HSSEngine *e, const HSSMat4d *t, double cut) {
  if (!e || !t)
    return HSS_INVALID_ARGUMENT;
  if (e->ingress.pending_events() != 0 || e->live.busy())
    return remembered(e, HSS_NOT_READY);
  try {
    const auto resource =
        decide_resource_event(e, hss::resource::EventKind::kCommand);
    if (!resource.protected_event ||
        resource.action != hss::resource::EventAction::kAcceptProtected)
      return remembered(e, HSS_INTERNAL_ERROR);
    return remembered(e, e->live.set_plan_reference(*t, cut));
  } catch (...) {
    e->live.cancel(HSS_RESOURCE_LIMIT);
    return remembered(e, HSS_RESOURCE_LIMIT);
  }
}

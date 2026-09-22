#pragma once

#include "hss/hss_api.h"
#include "hss/normalized_replay.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hss::vertical_slice {

enum class Status : uint8_t {
  kOk,
  kInvalidArgument,
  kReplayRejected,
  kFacadeRejected,
  kResourceLimit,
  kInsufficientEvidence,
  kInvalidBoundary,
};

struct Limits {
  uint64_t max_working_bytes{64U * 1024U * 1024U};
  uint64_t max_live_anchors{64U};
  uint64_t max_boundary_vertices{4096U};
  double floor_planarity_tolerance_m{1.0e-5};
};

struct SourceIdentity {
  replay::Id capture_id{};
  uint32_t session_epoch{0};
  replay::Id anchor_id{};
  uint64_t generation{0};
  uint64_t anchor_revision{0};
  replay::Provenance provenance{replay::Provenance::kCaptured};
  bool operator==(const SourceIdentity&) const = default;
};

struct PlanVertex {
  double u_m{0.0};
  double v_m{0.0};
  uint32_t source_vertex_index{0};
  bool operator==(const PlanVertex&) const = default;
};

// This is deliberately a synthetic, provisional verification product. It is
// not an HSSSnapshot and cannot be presented as capture or physical evidence.
struct PlanSnapshot {
  bool source_is_synthetic{true};
  bool provisional{true};
  bool derived_from_source_mesh_only{true};
  bool hidden_geometry_synthesized{false};
  std::string units{"m"};
  std::string handedness{"right"};
  std::string bridge{"hss_c_abi_v1"};
  uint64_t facade_events_forwarded{0};
  uint64_t last_event_sequence{0};
  double signed_area_m2{0.0};
  SourceIdentity source;
  std::vector<PlanVertex> outer_ring;
  bool operator==(const PlanSnapshot&) const = default;
};

struct BuildResult {
  Status status{Status::kInvalidArgument};
  replay::Status replay_status{replay::Status::kOk};
  HSSStatus facade_status{HSS_OK};
  PlanSnapshot snapshot;
};

// Replays a completely validated normalized stream through the representable
// subset of the existing C ABI facade, then derives one closed outer ring from
// the still-owned normalized mesh boundary edges. The output is assigned only
// on complete success. No edge, closure, or hidden surface is inferred when
// source mesh evidence is absent or ambiguous.
BuildResult build_provisional_plan(
    const std::vector<std::vector<uint8_t>>& records,
    const replay::Limits& replay_limits = {},
    const Limits& plan_limits = {});

std::string snapshot_json(const PlanSnapshot& snapshot);

}  // namespace hss::vertical_slice

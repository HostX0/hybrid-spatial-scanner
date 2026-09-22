#include "hss/provisional_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace {

using hss::replay::DepthFrameEvent;
using hss::replay::EpochEvent;
using hss::replay::Event;
using hss::replay::EventMetadata;
using hss::replay::Id;
using hss::replay::Matrix4d;
using hss::replay::MeshAnchorEvent;
using hss::replay::MeshEventKind;
using ReplayStatus = hss::replay::Status;
using PlanStatus = hss::vertical_slice::Status;

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Matrix4d identity() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Id capture_id() {
  Id id{};
  id[0] = 0x10;
  id[15] = 0x7a;
  return id;
}

Id anchor_id() {
  Id id{};
  id[0] = 0xa0;
  id[15] = 0x35;
  return id;
}

EventMetadata metadata(uint64_t sequence, double time_s, uint32_t epoch = 3U) {
  EventMetadata value;
  value.capture_id = capture_id();
  value.session_epoch = epoch;
  value.event_sequence = sequence;
  value.monotonic_time_s = time_s;
  return value;
}

EpochEvent epoch(hss::replay::RelationshipState relationship =
                     hss::replay::RelationshipState::kAccepted) {
  EpochEvent value;
  value.metadata = metadata(1U, 10.0);
  value.relationship_state = relationship;
  value.transform_revision = 1U;
  value.project_from_world = identity();
  return value;
}

DepthFrameEvent depth() {
  DepthFrameEvent value;
  value.metadata = metadata(2U, 10.1);
  value.rgb_layout = {4, 4, 111, {{4, 4, 4}}};
  value.depth_layout = {2, 2, 222, {{2, 2, 8}}};
  value.confidence_was_supplied = true;
  value.confidence_layout = {2, 2, 333, {{2, 2, 2}}};
  value.calibration = {4, 4, 2.0, 2.0, 0.5, 0.5, 0, 0, 4, 4};
  value.world_from_camera = identity();
  value.depth_meters = {1.0F, 1.1F, 1.2F, 1.3F};
  value.confidence = {2, 2, 1, 0};
  value.valid_depth_mask = {1, 1, 1, 1};
  value.metric_fusion_mask = {1, 1, 1, 0};
  value.provisional_preview_mask = {0, 0, 0, 1};
  return value;
}

hss::replay::MeshPayload irregular_floor() {
  // The source mesh is a six-sided oblique floor patch. Projecting through the
  // documented floor basis preserves these exact (u,v) points; no right-angle
  // or rectangular prior is involved.
  const std::array<std::array<float, 2>, 6> plan{{
      {0.0F, 0.0F}, {4.0F, 0.0F}, {5.0F, 1.5F},
      {3.2F, 3.4F}, {1.1F, 2.8F}, {-0.6F, 1.1F},
  }};
  hss::replay::MeshPayload payload;
  for (const auto& point : plan) {
    payload.vertices_xyz.insert(payload.vertices_xyz.end(), {point[0], 0.0F, -point[1]});
    payload.normals_xyz.insert(payload.normals_xyz.end(), {0.0F, 1.0F, 0.0F});
  }
  payload.triangle_indices = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 5};
  payload.classifications = {2, 2, 2, 2};
  payload.local_bounds_min = {-0.6F, 0.0F, -3.4F};
  payload.local_bounds_max = {5.0F, 0.0F, 0.0F};
  return payload;
}

MeshAnchorEvent mesh(uint32_t session_epoch = 3U) {
  MeshAnchorEvent value;
  value.metadata = metadata(3U, 10.2, session_epoch);
  value.kind = MeshEventKind::kAdd;
  value.anchor_id = anchor_id();
  value.generation = 1U;
  value.anchor_revision = 1U;
  value.world_from_anchor = identity();
  value.payload = irregular_floor();
  return value;
}

std::vector<Event> fixture_events() { return {epoch(), depth(), mesh()}; }

std::vector<std::vector<uint8_t>> encode(const std::vector<Event>& events) {
  std::vector<std::vector<uint8_t>> records;
  for (const Event& event : events) {
    std::vector<uint8_t> record;
    const ReplayStatus status = hss::replay::encode_record(event, {}, &record);
    check(status == ReplayStatus::kOk, "fixture event encodes");
    if (status != ReplayStatus::kOk) return {};
    records.push_back(std::move(record));
  }
  return records;
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(0U - (crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

void update_crc(std::vector<uint8_t>* record) {
  const uint32_t value = crc32(record->data() + 32, record->size() - 32);
  for (size_t index = 0; index < 4U; ++index) {
    (*record)[24U + index] = static_cast<uint8_t>((value >> (8U * index)) & 0xffU);
  }
}

std::string expected_json() {
  std::ifstream input(std::string(HSS_SOURCE_DIR) +
                      "/fixtures/replay/irregular_provisional_plan.expected.json");
  check(input.good(), "plan JSON oracle opens");
  std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  while (!contents.empty() && (contents.back() == '\n' || contents.back() == '\r')) contents.pop_back();
  return contents;
}

void test_deterministic_irregular_plan() {
  const auto records = encode(fixture_events());
  const auto first = hss::vertical_slice::build_provisional_plan(records);
  const auto second = hss::vertical_slice::build_provisional_plan(records);
  check(first.status == PlanStatus::kOk && second.status == PlanStatus::kOk,
        "normalized stream crosses facade and produces plan");
  check(first.snapshot == second.snapshot, "two replay runs produce identical snapshots");
  check(first.snapshot.provisional && first.snapshot.source_is_synthetic &&
            first.snapshot.units == "m" && first.snapshot.handedness == "right",
        "snapshot is explicitly provisional synthetic metric/right-handed output");
  check(first.snapshot.facade_events_forwarded == 3U && first.snapshot.bridge == "hss_c_abi_v1",
        "epoch, depth, and mesh traverse existing C boundary");
  check(first.snapshot.outer_ring.size() == 6U &&
            std::abs(first.snapshot.signed_area_m2 - 13.155) < 1.0e-5,
        "closed observed boundary has deterministic finite area");
  check(first.snapshot.source.capture_id == capture_id() &&
            first.snapshot.source.anchor_id == anchor_id() &&
            first.snapshot.source.session_epoch == 3U &&
            first.snapshot.source.generation == 1U &&
            first.snapshot.source.anchor_revision == 1U &&
            first.snapshot.source.provenance == hss::replay::Provenance::kCaptured,
        "capture, epoch, anchor lineage, and provenance survive derivation");
  check(first.snapshot.derived_from_source_mesh_only &&
            !first.snapshot.hidden_geometry_synthesized,
        "geometry policy records no hidden geometry synthesis");
  const bool preserves_oblique_vertex = std::any_of(
      first.snapshot.outer_ring.begin(), first.snapshot.outer_ring.end(), [](const auto& vertex) {
        return std::abs(vertex.u_m - 5.0) < 1.0e-6 && std::abs(vertex.v_m - 1.5) < 1.0e-6;
      });
  const bool preserves_oblique_edge = std::any_of(
      first.snapshot.outer_ring.begin(), first.snapshot.outer_ring.end(), [&](const auto& vertex) {
        const size_t index = static_cast<size_t>(&vertex - first.snapshot.outer_ring.data());
        const auto& next = first.snapshot.outer_ring[(index + 1U) % first.snapshot.outer_ring.size()];
        const double du = std::abs(next.u_m - vertex.u_m);
        const double dv = std::abs(next.v_m - vertex.v_m);
        return du > 0.1 && dv > 0.1 && std::abs(du - dv) > 0.1;
      });
  check(preserves_oblique_vertex && preserves_oblique_edge,
        "arbitrary source angles survive without Manhattan snapping");
  const std::string actual = hss::vertical_slice::snapshot_json(first.snapshot);
  const std::string expected = expected_json();
  if (actual != expected) std::cerr << "ACTUAL_JSON=" << actual << '\n';
  check(actual == expected, "snapshot matches independent deterministic JSON oracle");
}

void test_replay_rejections_are_atomic() {
  auto records = encode(fixture_events());
  std::swap(records[1], records[2]);
  auto result = hss::vertical_slice::build_provisional_plan(records);
  check(result.status == PlanStatus::kReplayRejected &&
            result.replay_status == ReplayStatus::kOutOfOrder &&
            result.snapshot.outer_ring.empty(),
        "invalid sequence is rejected without a partial snapshot");

  records = encode({epoch(), depth(), mesh(4U)});
  result = hss::vertical_slice::build_provisional_plan(records);
  check(result.status == PlanStatus::kReplayRejected &&
            result.replay_status == ReplayStatus::kOutOfOrder &&
            result.snapshot.outer_ring.empty(),
        "undeclared epoch is rejected without cross-epoch geometry");

  records = encode(fixture_events());
  records[0][36] = 2U;
  update_crc(&records[0]);
  result = hss::vertical_slice::build_provisional_plan(records);
  check(result.status == PlanStatus::kReplayRejected &&
            result.replay_status == ReplayStatus::kIncompatibleProvenance &&
            result.snapshot.outer_ring.empty(),
        "non-captured provenance is rejected without a partial snapshot");
}

void test_no_synthesis_finite_and_bounds() {
  auto result = hss::vertical_slice::build_provisional_plan(encode({epoch(), depth()}));
  check(result.status == PlanStatus::kInsufficientEvidence && result.snapshot.outer_ring.empty(),
        "depth without source mesh does not synthesize plan geometry");

  result = hss::vertical_slice::build_provisional_plan(
      encode({epoch(hss::replay::RelationshipState::kUnresolved), depth(), mesh()}));
  check(result.status == PlanStatus::kInsufficientEvidence && result.snapshot.outer_ring.empty(),
        "unresolved epoch does not emit project-space geometry");

  hss::vertical_slice::Limits tight;
  tight.max_boundary_vertices = 5U;
  result = hss::vertical_slice::build_provisional_plan(encode(fixture_events()), {}, tight);
  check(result.status == PlanStatus::kResourceLimit && result.snapshot.outer_ring.empty(),
        "boundary cap rejects oversized output atomically");

  auto nonfinite_events = fixture_events();
  std::get<MeshAnchorEvent>(nonfinite_events.back()).payload.vertices_xyz[0] =
      std::numeric_limits<float>::quiet_NaN();
  std::vector<uint8_t> rejected_record;
  check(hss::replay::encode_record(nonfinite_events.back(), {}, &rejected_record) ==
            ReplayStatus::kInvalidArgument,
        "normalized source rejects nonfinite mesh coordinates before replay");

  result = hss::vertical_slice::build_provisional_plan(encode(fixture_events()));
  const bool all_finite = std::isfinite(result.snapshot.signed_area_m2) &&
      std::all_of(result.snapshot.outer_ring.begin(), result.snapshot.outer_ring.end(),
                  [](const auto& vertex) {
                    return std::isfinite(vertex.u_m) && std::isfinite(vertex.v_m);
                  });
  check(result.status == PlanStatus::kOk && all_finite,
        "successful snapshot contains only finite metric values");
}

}  // namespace

int main() {
  test_deterministic_irregular_plan();
  test_replay_rejections_are_atomic();
  test_no_synthesis_finite_and_bounds();
  if (failures != 0) {
    std::cerr << failures << " provisional-plan test(s) failed\n";
    return 1;
  }
  std::cout << "synthetic normalized replay -> C facade -> provisional plan checks passed\n";
  return 0;
}

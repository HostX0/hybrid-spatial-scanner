#include "hss/durable_source_store.hpp"
#include "hss/normalized_replay.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include <unistd.h>

namespace {

using hss::replay::DepthFrameEvent;
using hss::replay::EpochEvent;
using hss::replay::Event;
using hss::replay::EventMetadata;
using hss::replay::Id;
using hss::replay::Limits;
using hss::replay::Matrix4d;
using hss::replay::MeshAnchorEvent;
using hss::replay::MeshEventKind;
using hss::replay::Status;

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
  id[15] = 7;
  return id;
}

Id anchor_id() {
  Id id{};
  id[15] = 42;
  return id;
}

EventMetadata metadata(uint64_t sequence, double time) {
  EventMetadata result;
  result.capture_id = capture_id();
  result.session_epoch = 4;
  result.event_sequence = sequence;
  result.monotonic_time_s = time;
  return result;
}

EpochEvent epoch() {
  EpochEvent result;
  result.metadata = metadata(1, 1.0);
  result.relationship_state = hss::replay::RelationshipState::kAccepted;
  result.transform_revision = 1;
  result.project_from_world = identity();
  return result;
}

DepthFrameEvent depth() {
  DepthFrameEvent result;
  result.metadata = metadata(2, 1.1);
  result.rgb_layout = {12, 8, 111, {{12, 8, 16}, {6, 4, 8}}};
  result.depth_layout = {2, 2, 222, {{2, 2, 12}}};
  result.confidence_was_supplied = true;
  result.confidence_layout = {2, 2, 333, {{2, 2, 4}}};
  result.calibration = {12, 8, 4, 3, 1, 1, 2, 2, 8, 4};
  result.world_from_camera = identity();
  result.world_from_camera[12] = 0.5;
  result.world_from_camera[13] = 0.25;
  result.world_from_camera[14] = -1.0;
  result.depth_meters = {1.0F, 2.0F, 0.0F, 3.0F};
  result.confidence = {2, 0, hss::replay::kConfidenceUnavailable, 1};
  result.valid_depth_mask = {1, 1, 0, 1};
  result.metric_fusion_mask = {1, 0, 0, 1};
  result.provisional_preview_mask = {0, 1, 0, 0};
  return result;
}

hss::replay::MeshPayload quad(float x) {
  return {
      .vertices_xyz = {x, 0, 0, x + 1, 0, 0, x + 1, 1, 0, x, 1, 0},
      .normals_xyz = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1},
      .triangle_indices = {0, 1, 2, 0, 2, 3},
      .classifications = {2, 2},
      .local_bounds_min = {x, 0, 0},
      .local_bounds_max = {x + 1, 1, 0},
  };
}

hss::replay::MeshPayload triangle(float x) {
  return {
      .vertices_xyz = {x, 0, 0, x + 1, 0, 0, x, 1, 0},
      .normals_xyz = {0, 0, 1, 0, 0, 1, 0, 0, 1},
      .triangle_indices = {0, 1, 2},
      .classifications = {2},
      .local_bounds_min = {x, 0, 0},
      .local_bounds_max = {x + 1, 1, 0},
  };
}

MeshAnchorEvent mesh(uint64_t sequence, MeshEventKind kind, uint64_t generation,
                     uint64_t revision, hss::replay::MeshPayload payload = {}) {
  MeshAnchorEvent result;
  result.metadata = metadata(sequence, 1.0 + static_cast<double>(sequence) / 10.0);
  result.kind = kind;
  result.anchor_id = anchor_id();
  result.generation = generation;
  result.anchor_revision = revision;
  result.world_from_anchor = identity();
  result.payload = std::move(payload);
  return result;
}

std::vector<Event> fixture_events() {
  return {
      epoch(),
      depth(),
      mesh(3, MeshEventKind::kAdd, 1, 1, quad(0)),
      mesh(4, MeshEventKind::kReplace, 1, 2, triangle(10)),
      mesh(5, MeshEventKind::kRemove, 1, 2),
      mesh(6, MeshEventKind::kAdd, 2, 1, quad(20)),
  };
}

std::vector<std::vector<uint8_t>> encode(const std::vector<Event>& events, const Limits& limits = {}) {
  std::vector<std::vector<uint8_t>> records;
  for (const Event& event : events) {
    std::vector<uint8_t> bytes;
    check(hss::replay::encode_record(event, limits, &bytes) == Status::kOk, "fixture event encodes");
    records.push_back(std::move(bytes));
  }
  return records;
}

std::string hex(const Id& id) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (uint8_t byte : id) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 0x0fU]);
  }
  return result;
}

struct TopologyState {
  uint64_t generation{0};
  uint64_t revision{0};
  uint64_t vertices{0};
  uint64_t triangles{0};
};

class RecordingSink final : public hss::replay::EventSink {
 public:
  bool on_epoch(const EpochEvent& event) override {
    events.emplace_back(event);
    std::ostringstream line;
    line << "epoch seq=" << event.metadata.event_sequence << " epoch=" << event.metadata.session_epoch
         << " relation=" << (event.relationship_state == hss::replay::RelationshipState::kAccepted ? "accepted" : "unresolved")
         << " transform_revision=" << event.transform_revision;
    trace.push_back(line.str());
    return accept;
  }

  bool on_depth_frame(const DepthFrameEvent& event) override {
    events.emplace_back(event);
    const uint64_t valid = std::count(event.valid_depth_mask.begin(), event.valid_depth_mask.end(), 1);
    const uint64_t low = std::count(event.confidence.begin(), event.confidence.end(), 0);
    const uint64_t unavailable = std::count(event.confidence.begin(), event.confidence.end(), hss::replay::kConfidenceUnavailable);
    std::ostringstream line;
    line << "depth seq=" << event.metadata.event_sequence << " epoch=" << event.metadata.session_epoch
         << " samples=" << event.depth_meters.size() << " valid=" << valid
         << " low_confidence=" << low << " unavailable_confidence=" << unavailable;
    trace.push_back(line.str());
    return accept;
  }

  bool on_mesh_anchor(const MeshAnchorEvent& event) override {
    events.emplace_back(event);
    const uint64_t vertices = event.payload.vertices_xyz.size() / 3U;
    const uint64_t triangles = event.payload.triangle_indices.size() / 3U;
    std::ostringstream line;
    const char* kind = event.kind == MeshEventKind::kAdd ? "add" :
                       event.kind == MeshEventKind::kReplace ? "replace" : "remove";
    line << "mesh seq=" << event.metadata.event_sequence << " epoch=" << event.metadata.session_epoch
         << " kind=" << kind << " anchor=" << hex(event.anchor_id)
         << " generation=" << event.generation << " revision=" << event.anchor_revision
         << " vertices=" << vertices << " triangles=" << triangles;
    trace.push_back(line.str());
    if (event.kind == MeshEventKind::kRemove) {
      topology.erase(event.anchor_id);
    } else {
      topology[event.anchor_id] = {event.generation, event.anchor_revision, vertices, triangles};
    }
    return accept;
  }

  void append_topology_summary() {
    uint64_t vertices = 0;
    uint64_t triangles = 0;
    uint64_t generation = 0;
    uint64_t revision = 0;
    for (const auto& [id, state] : topology) {
      static_cast<void>(id);
      vertices += state.vertices;
      triangles += state.triangles;
      generation = state.generation;
      revision = state.revision;
    }
    std::ostringstream line;
    line << "topology live_anchors=" << topology.size() << " vertices=" << vertices
         << " triangles=" << triangles << " generation=" << generation << " revision=" << revision;
    trace.push_back(line.str());
  }

  bool accept{true};
  std::vector<Event> events;
  std::vector<std::string> trace;
  std::map<Id, TopologyState> topology;
};

std::vector<std::string> expected_trace() {
  std::ifstream stream(std::string(HSS_SOURCE_DIR) + "/fixtures/replay/synthetic_trace.expected");
  check(stream.good(), "synthetic replay oracle opens");
  std::vector<std::string> lines;
  for (std::string line; std::getline(stream, line);) lines.push_back(line);
  return lines;
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
  for (size_t index = 0; index < 4; ++index) {
    (*record)[24 + index] = static_cast<uint8_t>((value >> (8U * index)) & 0xffU);
  }
}

void test_round_trip_and_deterministic_topology() {
  const std::vector<Event> events = fixture_events();
  auto records = encode(events);
  auto epoch_decoded = hss::replay::decode_record(records[0], {});
  check(epoch_decoded.status == Status::kOk,
        "epoch round trip status=" + std::to_string(static_cast<int>(epoch_decoded.status)));
  auto decoded = hss::replay::decode_record(records[1], {});
  check(decoded.status == Status::kOk && decoded.event == events[1],
        "depth calibration, pose, masks, and confidence round trip status=" +
            std::to_string(static_cast<int>(decoded.status)) + " equal=" +
            std::to_string(decoded.event == events[1]));
  decoded = hss::replay::decode_record(records[3], {});
  check(decoded.status == Status::kOk && decoded.event == events[3],
        "mesh UUID, generation, revision, and topology round trip status=" +
            std::to_string(static_cast<int>(decoded.status)) + " equal=" +
            std::to_string(decoded.event == events[3]));

  RecordingSink first;
  RecordingSink second;
  const Status first_status = hss::replay::replay_records(records, {}, first);
  check(first_status == Status::kOk, "first replay succeeds status=" + std::to_string(static_cast<int>(first_status)));
  check(hss::replay::replay_records(records, {}, second) == Status::kOk, "repeated replay succeeds");
  first.append_topology_summary();
  second.append_topology_summary();
  check(first.events == second.events, "repeated playback emits identical typed owned events");
  check(first.trace == second.trace, "repeated playback emits identical trace");
  check(first.trace == expected_trace(), "captured normalized trace matches synthetic topology oracle");
}

void test_corruption_versions_bounds_and_provenance() {
  const auto records = encode(fixture_events());
  auto corrupt = records[1];
  corrupt.back() ^= 0x80U;
  check(hss::replay::decode_record(corrupt, {}).status == Status::kCorrupt, "checksum rejects corrupt payload");

  auto future = records[0];
  future[8] = 2;
  check(hss::replay::decode_record(future, {}).status == Status::kUnsupportedVersion, "record major version rejected");

  auto wrong_type = records[0];
  wrong_type[12] = 99;
  check(hss::replay::decode_record(wrong_type, {}).status == Status::kUnsupportedType, "unknown event type rejected");

  auto wrong_provenance = records[0];
  wrong_provenance[32 + 4] = 2;
  update_crc(&wrong_provenance);
  check(hss::replay::decode_record(wrong_provenance, {}).status == Status::kIncompatibleProvenance,
        "non-captured provenance rejected explicitly");

  auto future_schema = records[0];
  future_schema[32] = 2;
  update_crc(&future_schema);
  check(hss::replay::decode_record(future_schema, {}).status == Status::kUnsupportedVersion,
        "incompatible normalized schema rejected");

  auto truncated = records[1];
  truncated.pop_back();
  check(hss::replay::decode_record(truncated, {}).status == Status::kCorrupt, "declared length mismatch rejected");

  Limits tight;
  tight.max_depth_samples = 3;
  check(hss::replay::decode_record(records[1], tight).status == Status::kResourceLimit,
        "depth allocation bound checked before resize");
  tight = {};
  tight.max_record_bytes = records[0].size() - 1U;
  check(hss::replay::decode_record(records[0], tight).status == Status::kResourceLimit,
        "record byte bound enforced");
  tight = {};
  tight.max_stream_bytes = records[0].size();
  RecordingSink bounded_sink;
  check(hss::replay::replay_records(records, tight, bounded_sink) == Status::kResourceLimit &&
            bounded_sink.events.empty(),
        "aggregate replay memory bound enforced before callbacks");
}

void test_stream_order_lineage_and_atomic_validation() {
  auto records = encode(fixture_events());
  std::swap(records[1], records[2]);
  RecordingSink sink;
  check(hss::replay::replay_records(records, {}, sink) == Status::kOutOfOrder, "event sequence disorder rejected");
  check(sink.events.empty(), "whole stream validates before any sink callback");

  records = encode(fixture_events());
  records.erase(records.begin());
  check(hss::replay::replay_records(records, {}, sink) == Status::kOutOfOrder, "event without declared epoch rejected");

  auto bad_events = fixture_events();
  std::get<MeshAnchorEvent>(bad_events[3]).anchor_revision = 7;
  records = encode(bad_events);
  check(hss::replay::replay_records(records, {}, sink) == Status::kOutOfOrder, "mesh replacement revision jump rejected");

  records = encode(fixture_events());
  RecordingSink rejecting;
  rejecting.accept = false;
  check(hss::replay::replay_records(records, {}, rejecting) == Status::kSinkRejected, "typed sink rejection propagated");
}

void test_recovered_source_payloads_replay_in_manifest_order() {
  char directory_template[] = "/tmp/hss-b008-replay-XXXXXX";
  char* directory = mkdtemp(directory_template);
  check(directory != nullptr, "temporary replay store created");
  if (directory == nullptr) return;

  const auto events = fixture_events();
  const auto records = encode(events);
  hss::storage::NativeFileSystem file_system(directory);
  hss::storage::RetentionLimits limits;
  limits.max_depth_records = 4;
  limits.max_mesh_records = 8;
  limits.max_retained_bytes = 1024U * 1024U;
  limits.max_chunk_bytes = 1024U * 1024U;
  hss::storage::DurableSourceStore store(file_system, limits);

  auto source = [&](size_t index, hss::storage::SourceKind kind) {
    hss::storage::SourceRecord record;
    record.kind = kind;
    record.capture_id = capture_id();
    if (kind == hss::storage::SourceKind::kMesh) record.anchor_id = anchor_id();
    record.session_epoch = 4;
    record.event_sequence = std::visit([](const auto& value) { return value.metadata.event_sequence; }, events[index]);
    record.monotonic_time_s = std::visit([](const auto& value) { return value.metadata.monotonic_time_s; }, events[index]);
    record.source_revision = kind == hss::storage::SourceKind::kMesh
        ? std::get<MeshAnchorEvent>(events[index]).anchor_revision : 0;
    record.payload_version = 1;
    record.payload = records[index];
    return record;
  };

  check(store.checkpoint({source(1, hss::storage::SourceKind::kDepth),
                          source(2, hss::storage::SourceKind::kMesh)}) == hss::storage::StoreStatus::kOk,
        "encoded normalized depth and mesh checkpoint");
  const auto recovered = store.recover();
  check(recovered.status == hss::storage::StoreStatus::kOk && recovered.records.size() == 2,
        "encoded source records recover");
  std::vector<std::vector<uint8_t>> replay_records{records[0]};
  for (const auto& stored : recovered.records) {
    std::vector<uint8_t> payload;
    check(store.read_payload(stored, &payload) == hss::storage::StoreStatus::kOk, "bounded payload read succeeds");
    replay_records.push_back(std::move(payload));
  }
  RecordingSink replayed;
  check(hss::replay::replay_records(replay_records, {}, replayed) == Status::kOk,
        "recovered manifest order feeds typed replay sink");
  check(replayed.events.size() == 3 && std::holds_alternative<EpochEvent>(replayed.events[0]) &&
        std::holds_alternative<DepthFrameEvent>(replayed.events[1]) &&
        std::holds_alternative<MeshAnchorEvent>(replayed.events[2]),
        "recovered event order is epoch, depth, mesh");

  std::error_code error;
  std::filesystem::remove_all(directory, error);
  check(!error, "temporary replay store removed");
}

}  // namespace

int main() {
  test_round_trip_and_deterministic_topology();
  test_corruption_versions_bounds_and_provenance();
  test_stream_order_lineage_and_atomic_validation();
  test_recovered_source_payloads_replay_in_manifest_order();
  if (failures != 0) {
    std::cerr << failures << " replay test(s) failed\n";
    return 1;
  }
  std::cout << "normalized replay tests passed\n";
  return 0;
}

#include "hss/provisional_plan.hpp"

#include "hss/coordinate_basis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace hss::vertical_slice {
namespace {

using Edge = std::pair<uint32_t, uint32_t>;

struct EpochState {
  replay::RelationshipState relationship_state{replay::RelationshipState::kUnresolved};
  replay::Matrix4d project_from_world{};
};

struct LiveMesh {
  replay::MeshAnchorEvent event;
};

struct EngineDeleter {
  void operator()(HSSEngine* engine) const { hss_engine_destroy(engine); }
};

HSSId abi_id(const replay::Id& id) {
  HSSId result{};
  std::copy(id.begin(), id.end(), result.bytes);
  return result;
}

HSSMat4d abi_matrix(const replay::Matrix4d& matrix) {
  HSSMat4d result{};
  std::copy(matrix.begin(), matrix.end(), result.column_major);
  return result;
}

bool finite(double value) { return std::isfinite(value); }

std::string hex_id(const replay::Id& id) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(32);
  for (uint8_t byte : id) {
    result.push_back(kDigits[byte >> 4U]);
    result.push_back(kDigits[byte & 0x0fU]);
  }
  return result;
}

double cross(const PlanVertex& a, const PlanVertex& b, const PlanVertex& c) {
  return (b.u_m - a.u_m) * (c.v_m - a.v_m) -
         (b.v_m - a.v_m) * (c.u_m - a.u_m);
}

bool ranges_overlap(double a0, double a1, double b0, double b1) {
  constexpr double kEpsilon = 1.0e-12;
  const auto [amin, amax] = std::minmax(a0, a1);
  const auto [bmin, bmax] = std::minmax(b0, b1);
  return std::max(amin, bmin) <= std::min(amax, bmax) + kEpsilon;
}

int orientation(const PlanVertex& a, const PlanVertex& b, const PlanVertex& c) {
  constexpr double kEpsilon = 1.0e-12;
  const double value = cross(a, b, c);
  if (value > kEpsilon) return 1;
  if (value < -kEpsilon) return -1;
  return 0;
}

bool segments_intersect(const PlanVertex& a, const PlanVertex& b,
                        const PlanVertex& c, const PlanVertex& d) {
  const int o1 = orientation(a, b, c);
  const int o2 = orientation(a, b, d);
  const int o3 = orientation(c, d, a);
  const int o4 = orientation(c, d, b);
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && ranges_overlap(a.u_m, b.u_m, c.u_m, c.u_m) &&
      ranges_overlap(a.v_m, b.v_m, c.v_m, c.v_m)) return true;
  if (o2 == 0 && ranges_overlap(a.u_m, b.u_m, d.u_m, d.u_m) &&
      ranges_overlap(a.v_m, b.v_m, d.v_m, d.v_m)) return true;
  if (o3 == 0 && ranges_overlap(c.u_m, d.u_m, a.u_m, a.u_m) &&
      ranges_overlap(c.v_m, d.v_m, a.v_m, a.v_m)) return true;
  if (o4 == 0 && ranges_overlap(c.u_m, d.u_m, b.u_m, b.u_m) &&
      ranges_overlap(c.v_m, d.v_m, b.v_m, b.v_m)) return true;
  return false;
}

class SliceSink final : public replay::EventSink {
 public:
  explicit SliceSink(const Limits& limits) : limits_(limits) {
    HSSConfig config{};
    config.abi_version = HSS_ABI_VERSION;
    config.struct_size = sizeof(config);
    // Normalized replay v1.1 carries capture identity but no project identity.
    // Leave the ephemeral facade project ID unset instead of conflating them.
    config.structure_voxel_m = 0.02;
    config.detail_voxel_m = 0.01;
    config.truncation_m = 0.04;
    config.max_working_bytes = limits_.max_working_bytes;
    config.max_pending_frames = 1U;
    config.deterministic_seed = 0U;
    HSSEngine* raw_engine = nullptr;
    facade_status_ = hss_engine_create(&config, &raw_engine);
    engine_.reset(raw_engine);
    if (facade_status_ != HSS_OK) status_ = Status::kFacadeRejected;
  }

  bool ready() const { return engine_ != nullptr && status_ == Status::kOk; }
  Status status() const { return status_; }
  HSSStatus facade_status() const { return facade_status_; }

  bool on_epoch(const replay::EpochEvent& event) override {
    if (!validate_metadata(event.metadata)) return false;
    HSSEpoch epoch{};
    epoch.abi_version = HSS_ABI_VERSION;
    epoch.struct_size = sizeof(epoch);
    epoch.capture_id = abi_id(event.metadata.capture_id);
    epoch.event_sequence = event.metadata.event_sequence;
    epoch.session_epoch = event.metadata.session_epoch;
    epoch.relationship_state = static_cast<uint32_t>(event.relationship_state);
    epoch.T_project_world = abi_matrix(event.project_from_world);
    epoch.transform_revision = event.transform_revision;
    if (!forward(hss_engine_set_epoch(engine_.get(), &epoch))) return false;
    epochs_[event.metadata.session_epoch] = {event.relationship_state, event.project_from_world};
    last_sequence_ = event.metadata.event_sequence;
    return true;
  }

  bool on_depth_frame(const replay::DepthFrameEvent& event) override {
    if (!validate_metadata(event.metadata) || !epochs_.contains(event.metadata.session_epoch)) return false;
    if (event.depth_layout.width == 0U || event.depth_layout.height == 0U ||
        event.depth_meters.size() != static_cast<uint64_t>(event.depth_layout.width) *
                                         event.depth_layout.height) {
      status_ = Status::kInvalidArgument;
      return false;
    }
    HSSFrame frame{};
    frame.abi_version = HSS_ABI_VERSION;
    frame.struct_size = sizeof(frame);
    frame.capture_id = abi_id(event.metadata.capture_id);
    frame.event_sequence = event.metadata.event_sequence;
    frame.session_epoch = event.metadata.session_epoch;
    frame.tracking_state = static_cast<uint32_t>(event.tracking_state);
    frame.timestamp_s = event.metadata.monotonic_time_s;
    frame.T_world_camera = abi_matrix(event.world_from_camera);
    frame.depth_intrinsics = {event.calibration.fx, event.calibration.fy,
                              event.calibration.cx, event.calibration.cy};
    frame.depth_width = event.depth_layout.width;
    frame.depth_height = event.depth_layout.height;
    frame.depth_row_stride_bytes = static_cast<uint64_t>(frame.depth_width) * sizeof(float);
    frame.depth_byte_length = static_cast<uint64_t>(event.depth_meters.size()) * sizeof(float);
    frame.depth_bytes = reinterpret_cast<const uint8_t*>(event.depth_meters.data());
    if (event.confidence_was_supplied) {
      frame.confidence_row_stride_bytes = frame.depth_width;
      frame.confidence_byte_length = event.confidence.size();
      frame.confidence_bytes = event.confidence.data();
    }
    frame.depth_source = static_cast<uint32_t>(event.depth_source);
    if (!forward(hss_engine_submit_frame(engine_.get(), &frame))) return false;
    last_sequence_ = event.metadata.event_sequence;
    return true;
  }

  bool on_mesh_anchor(const replay::MeshAnchorEvent& event) override {
    if (!validate_metadata(event.metadata) || !epochs_.contains(event.metadata.session_epoch)) return false;
    HSSMeshEvent mesh{};
    mesh.abi_version = HSS_ABI_VERSION;
    mesh.struct_size = sizeof(mesh);
    mesh.capture_id = abi_id(event.metadata.capture_id);
    mesh.anchor_id = abi_id(event.anchor_id);
    mesh.event_sequence = event.metadata.event_sequence;
    mesh.anchor_revision = event.anchor_revision;
    mesh.session_epoch = event.metadata.session_epoch;
    mesh.kind = static_cast<uint32_t>(event.kind);
    mesh.T_world_anchor = abi_matrix(event.world_from_anchor);
    if (event.kind != replay::MeshEventKind::kRemove) {
      mesh.vertices_xyz = event.payload.vertices_xyz.data();
      mesh.vertex_count = event.payload.vertices_xyz.size() / 3U;
      mesh.triangle_indices = event.payload.triangle_indices.data();
      mesh.triangle_count = event.payload.triangle_indices.size() / 3U;
    }
    if (!forward(hss_engine_submit_mesh_event(engine_.get(), &mesh))) return false;

    if (event.kind == replay::MeshEventKind::kRemove) {
      live_meshes_.erase(event.anchor_id);
    } else {
      if (!live_meshes_.contains(event.anchor_id) && live_meshes_.size() >= limits_.max_live_anchors) {
        status_ = Status::kResourceLimit;
        return false;
      }
      live_meshes_[event.anchor_id] = {event};
    }
    last_sequence_ = event.metadata.event_sequence;
    return true;
  }

  BuildResult finish() const {
    BuildResult result;
    result.status = status_;
    result.facade_status = facade_status_;
    if (status_ != Status::kOk) return result;
    if (live_meshes_.empty()) {
      result.status = Status::kInsufficientEvidence;
      return result;
    }
    // The minimal slice emits exactly one supported outer ring. Multiple live
    // anchors are not silently merged or bridged with invented edges.
    if (live_meshes_.size() != 1U) {
      result.status = Status::kInsufficientEvidence;
      return result;
    }
    const replay::MeshAnchorEvent& mesh = live_meshes_.begin()->second.event;
    const auto epoch = epochs_.find(mesh.metadata.session_epoch);
    if (epoch == epochs_.end() ||
        epoch->second.relationship_state != replay::RelationshipState::kAccepted) {
      result.status = Status::kInsufficientEvidence;
      return result;
    }

    std::vector<PlanVertex> projected;
    const uint64_t vertex_count = mesh.payload.vertices_xyz.size() / 3U;
    if (vertex_count > limits_.max_boundary_vertices ||
        vertex_count > std::numeric_limits<uint32_t>::max()) {
      result.status = Status::kResourceLimit;
      return result;
    }
    projected.reserve(static_cast<size_t>(vertex_count));
    const HSSMat4d world_from_anchor = abi_matrix(mesh.world_from_anchor);
    const HSSMat4d project_from_world = abi_matrix(epoch->second.project_from_world);
    const coordinate::FloorFrame floor = coordinate::default_floor_frame();
    for (uint32_t index = 0; index < vertex_count; ++index) {
      const size_t offset = static_cast<size_t>(index) * 3U;
      HSSVec3d local{mesh.payload.vertices_xyz[offset], mesh.payload.vertices_xyz[offset + 1U],
                     mesh.payload.vertices_xyz[offset + 2U]};
      const HSSVec3d world = coordinate::transform_point(world_from_anchor, local);
      const HSSVec3d project = coordinate::transform_point(project_from_world, world);
      const coordinate::PlanPoint plan = coordinate::project_to_floor(floor, project);
      if (!finite(project.x) || !finite(project.y) || !finite(project.z) ||
          !finite(plan.u_m) || !finite(plan.v_m) ||
          std::abs(project.y - floor.origin_m.y) > limits_.floor_planarity_tolerance_m) {
        result.status = Status::kInvalidBoundary;
        return result;
      }
      projected.push_back({plan.u_m, plan.v_m, index});
    }

    std::map<Edge, uint8_t> edge_counts;
    const auto& indices = mesh.payload.triangle_indices;
    for (size_t offset = 0; offset < indices.size(); offset += 3U) {
      const std::array<uint32_t, 3> triangle{indices[offset], indices[offset + 1U], indices[offset + 2U]};
      if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
        result.status = Status::kInvalidBoundary;
        return result;
      }
      for (size_t side = 0; side < 3U; ++side) {
        const uint32_t a = triangle[side];
        const uint32_t b = triangle[(side + 1U) % 3U];
        if (a >= projected.size() || b >= projected.size()) {
          result.status = Status::kInvalidBoundary;
          return result;
        }
        const Edge edge = std::minmax(a, b);
        uint8_t& count = edge_counts[edge];
        if (++count > 2U) {
          result.status = Status::kInvalidBoundary;
          return result;
        }
      }
    }

    std::map<uint32_t, std::vector<uint32_t>> adjacency;
    uint64_t boundary_edges = 0;
    for (const auto& [edge, count] : edge_counts) {
      if (count != 1U) continue;
      adjacency[edge.first].push_back(edge.second);
      adjacency[edge.second].push_back(edge.first);
      ++boundary_edges;
    }
    if (boundary_edges < 3U || boundary_edges > limits_.max_boundary_vertices ||
        adjacency.size() != boundary_edges ||
        !std::all_of(adjacency.begin(), adjacency.end(),
                     [](const auto& item) { return item.second.size() == 2U; })) {
      result.status = boundary_edges > limits_.max_boundary_vertices
                          ? Status::kResourceLimit
                          : Status::kInvalidBoundary;
      return result;
    }

    const auto start_it = std::min_element(
        adjacency.begin(), adjacency.end(), [&](const auto& lhs, const auto& rhs) {
          const PlanVertex& a = projected[lhs.first];
          const PlanVertex& b = projected[rhs.first];
          if (a.u_m != b.u_m) return a.u_m < b.u_m;
          if (a.v_m != b.v_m) return a.v_m < b.v_m;
          return lhs.first < rhs.first;
        });
    const uint32_t start = start_it->first;
    std::vector<uint32_t> ring_indices;
    ring_indices.reserve(static_cast<size_t>(boundary_edges));
    uint32_t previous = std::numeric_limits<uint32_t>::max();
    uint32_t current = start;
    for (uint64_t count = 0; count < boundary_edges; ++count) {
      ring_indices.push_back(current);
      const auto& neighbours = adjacency.at(current);
      const uint32_t next = neighbours[0] == previous ? neighbours[1] : neighbours[0];
      previous = current;
      current = next;
      if (current == start && count + 1U != boundary_edges) {
        result.status = Status::kInvalidBoundary;
        return result;
      }
    }
    if (current != start || std::set<uint32_t>(ring_indices.begin(), ring_indices.end()).size() != boundary_edges) {
      result.status = Status::kInvalidBoundary;
      return result;
    }

    std::vector<PlanVertex> ring;
    ring.reserve(ring_indices.size());
    for (uint32_t index : ring_indices) ring.push_back(projected[index]);
    const double area_origin_u = ring.front().u_m;
    const double area_origin_v = ring.front().v_m;
    double twice_area = 0.0;
    double area_compensation = 0.0;
    for (size_t index = 0; index < ring.size(); ++index) {
      const PlanVertex& a = ring[index];
      const PlanVertex& b = ring[(index + 1U) % ring.size()];
      const double du = b.u_m - a.u_m;
      const double dv = b.v_m - a.v_m;
      if (!finite(du) || !finite(dv) || du * du + dv * dv <= 1.0e-24) {
        result.status = Status::kInvalidBoundary;
        return result;
      }
      const double area_term = (a.u_m - area_origin_u) * (b.v_m - area_origin_v) -
                               (b.u_m - area_origin_u) * (a.v_m - area_origin_v);
      const double corrected = area_term - area_compensation;
      const double next_area = twice_area + corrected;
      area_compensation = (next_area - twice_area) - corrected;
      twice_area = next_area;
    }
    if (!finite(twice_area) || std::abs(twice_area) <= 1.0e-12) {
      result.status = Status::kInvalidBoundary;
      return result;
    }
    if (twice_area < 0.0) {
      std::reverse(ring.begin() + 1, ring.end());
      twice_area = -twice_area;
    }
    for (size_t first = 0; first < ring.size(); ++first) {
      const size_t first_next = (first + 1U) % ring.size();
      for (size_t second = first + 1U; second < ring.size(); ++second) {
        const size_t second_next = (second + 1U) % ring.size();
        if (first == second || first_next == second || second_next == first) continue;
        if (segments_intersect(ring[first], ring[first_next], ring[second], ring[second_next])) {
          result.status = Status::kInvalidBoundary;
          return result;
        }
      }
    }

    PlanSnapshot snapshot;
    snapshot.facade_events_forwarded = forwarded_;
    snapshot.last_event_sequence = last_sequence_;
    snapshot.signed_area_m2 = twice_area / 2.0;
    snapshot.source = {mesh.metadata.capture_id, mesh.metadata.session_epoch, mesh.anchor_id,
                       mesh.generation, mesh.anchor_revision, mesh.metadata.provenance};
    snapshot.outer_ring = std::move(ring);
    result.status = Status::kOk;
    result.snapshot = std::move(snapshot);
    return result;
  }

 private:
  bool validate_metadata(const replay::EventMetadata& metadata) {
    if (metadata.provenance != replay::Provenance::kCaptured ||
        (have_sequence_ && metadata.event_sequence <= last_sequence_)) {
      status_ = Status::kInvalidArgument;
      return false;
    }
    have_sequence_ = true;
    return true;
  }

  bool forward(HSSStatus status) {
    facade_status_ = status;
    if (status != HSS_OK) {
      status_ = status == HSS_RESOURCE_LIMIT ? Status::kResourceLimit : Status::kFacadeRejected;
      return false;
    }
    facade_status_ = hss_engine_step(engine_.get(), 1U);
    if (facade_status_ != HSS_OK) {
      status_ = Status::kFacadeRejected;
      return false;
    }
    ++forwarded_;
    return true;
  }

  Limits limits_;
  std::unique_ptr<HSSEngine, EngineDeleter> engine_;
  Status status_{Status::kOk};
  HSSStatus facade_status_{HSS_OK};
  bool have_sequence_{false};
  uint64_t last_sequence_{0};
  uint64_t forwarded_{0};
  std::map<uint32_t, EpochState> epochs_;
  std::map<replay::Id, LiveMesh> live_meshes_;
};

}  // namespace

BuildResult build_provisional_plan(const std::vector<std::vector<uint8_t>>& records,
                                   const replay::Limits& replay_limits,
                                   const Limits& plan_limits) {
  BuildResult result;
  try {
    if (records.empty() || plan_limits.max_working_bytes == 0U ||
        plan_limits.max_live_anchors == 0U || plan_limits.max_boundary_vertices < 3U ||
        !finite(plan_limits.floor_planarity_tolerance_m) ||
        plan_limits.floor_planarity_tolerance_m < 0.0) {
      result.status = Status::kInvalidArgument;
      return result;
    }
    SliceSink sink(plan_limits);
    if (!sink.ready()) {
      result.status = sink.status();
      result.facade_status = sink.facade_status();
      return result;
    }
    result.replay_status = replay::replay_records(records, replay_limits, sink);
    if (result.replay_status != replay::Status::kOk) {
      result.status = result.replay_status == replay::Status::kSinkRejected
                          ? (sink.status() == Status::kOk ? Status::kFacadeRejected : sink.status())
                          : Status::kReplayRejected;
      result.facade_status = sink.facade_status();
      return result;
    }
    return sink.finish();
  } catch (const std::bad_alloc&) {
    result.status = Status::kResourceLimit;
    return result;
  }
}

std::string snapshot_json(const PlanSnapshot& snapshot) {
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << std::setprecision(17);
  json << "{\"kind\":\"synthetic_host_provisional_plan\",\"source_is_synthetic\":"
       << (snapshot.source_is_synthetic ? "true" : "false")
       << ",\"quality_state\":\"provisional\",\"units\":\"" << snapshot.units
       << "\",\"basis\":{\"handedness\":\"" << snapshot.handedness
       << "\",\"up\":\"+Y\",\"floor_u\":[1,0,0],\"floor_v\":[0,0,-1]},"
       << "\"bridge\":\"" << snapshot.bridge << "\",\"facade_events_forwarded\":"
       << snapshot.facade_events_forwarded << ",\"last_event_sequence\":"
       << snapshot.last_event_sequence << ",\"geometry_policy\":{"
       << "\"derived_from_source_mesh_only\":"
       << (snapshot.derived_from_source_mesh_only ? "true" : "false")
       << ",\"hidden_geometry_synthesized\":"
       << (snapshot.hidden_geometry_synthesized ? "true" : "false") << "},"
       << "\"source\":{\"capture_id\":\"" << hex_id(snapshot.source.capture_id)
       << "\",\"session_epoch\":" << snapshot.source.session_epoch
       << ",\"anchor_id\":\"" << hex_id(snapshot.source.anchor_id)
       << "\",\"generation\":" << snapshot.source.generation
       << ",\"anchor_revision\":" << snapshot.source.anchor_revision
       << ",\"provenance\":\"captured\"},\"signed_area_m2\":"
       << snapshot.signed_area_m2 << ",\"outer_ring\":[";
  for (size_t index = 0; index < snapshot.outer_ring.size(); ++index) {
    if (index != 0U) json << ',';
    const PlanVertex& vertex = snapshot.outer_ring[index];
    json << "{\"u_m\":" << vertex.u_m << ",\"v_m\":" << vertex.v_m
         << ",\"source_vertex_index\":" << vertex.source_vertex_index << '}';
  }
  json << "]}";
  return json.str();
}

}  // namespace hss::vertical_slice

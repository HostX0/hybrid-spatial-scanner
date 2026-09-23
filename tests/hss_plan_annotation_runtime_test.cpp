#include "hss/hss_api.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

HSSId identity(uint8_t first) {
  HSSId value{};
  value.bytes[0] = first;
  return value;
}

HSSMat4d identity_matrix() {
  HSSMat4d value{};
  value.column_major[0] = 1.0;
  value.column_major[5] = 1.0;
  value.column_major[10] = 1.0;
  value.column_major[15] = 1.0;
  return value;
}

HSSConfig config() {
  HSSConfig value{};
  value.abi_version = HSS_ABI_VERSION;
  value.struct_size = sizeof(value);
  value.project_id = identity(1U);
  value.structure_voxel_m = 0.02;
  value.detail_voxel_m = 0.01;
  value.truncation_m = 0.04;
  value.max_working_bytes = 64U * 1024U * 1024U;
  value.max_pending_frames = 2U;
  return value;
}

HSSEngineStats stats(HSSEngine *engine) {
  HSSEngineStats value{};
  require(hss_engine_get_stats(engine, &value) == HSS_OK, "read stats");
  return value;
}

void drain(HSSEngine *engine) {
  for (size_t index = 0U; index < 100'000U; ++index) {
    if (stats(engine).lifecycle_state == 1U)
      return;
    require(hss_engine_step(engine, 50U) == HSS_OK, "drain engine");
  }
  require(false, "bounded reconstruction completed");
}

HSSSnapshot *snapshot(HSSEngine *engine) {
  HSSSnapshot *value = nullptr;
  require(hss_engine_acquire_snapshot(engine, &value) == HSS_OK,
          "acquire snapshot");
  return value;
}

HSSPlanAnnotationSummary summary(HSSSnapshot *snapshot_value) {
  HSSPlanAnnotationSummary value{};
  require(hss_snapshot_get_plan_annotation_summary(snapshot_value, &value) ==
              HSS_OK,
          "read plan summary");
  return value;
}

std::string copy_json(HSSSnapshot *snapshot_value, bool annotations) {
  uint64_t required = 0U;
  const auto copy = annotations ? hss_snapshot_copy_annotation_json
                                : hss_snapshot_copy_plan_json;
  require(copy(snapshot_value, nullptr, 0U, &required) == HSS_BUFFER_TOO_SMALL &&
              required != 0U,
          "query semantic JSON");
  std::string value(static_cast<size_t>(required), ' ');
  require(copy(snapshot_value, reinterpret_cast<uint8_t *>(value.data()),
               required, &required) == HSS_OK,
          "copy semantic JSON");
  return value;
}

struct Mesh {
  std::vector<float> vertices;
  std::vector<uint32_t> triangles;
};

Mesh room(bool closed) {
  const std::vector<std::pair<float, float>> outline{
      {0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}};
  Mesh result;
  for (const auto [x, z] : outline)
    result.vertices.insert(result.vertices.end(), {x, 0.0F, -z, x, 3.0F, -z});
  const size_t edge_count = closed ? outline.size() : outline.size() - 1U;
  for (size_t edge = 0U; edge < edge_count; ++edge) {
    const uint32_t first = static_cast<uint32_t>(edge * 2U);
    const uint32_t second =
        static_cast<uint32_t>(((edge + 1U) % outline.size()) * 2U);
    result.triangles.insert(result.triangles.end(),
                            {first, second, second + 1U, first, second + 1U,
                             first + 1U});
  }
  return result;
}

HSSMeshEvent mesh_event(const Mesh &mesh, uint64_t sequence, uint32_t kind,
                        uint64_t revision) {
  HSSMeshEvent value{};
  value.abi_version = HSS_ABI_VERSION;
  value.struct_size = sizeof(value);
  value.capture_id = identity(2U);
  value.anchor_id = identity(3U);
  value.event_sequence = sequence;
  value.anchor_revision = revision;
  value.kind = kind;
  value.T_world_anchor = identity_matrix();
  value.vertices_xyz = mesh.vertices.data();
  value.vertex_count = mesh.vertices.size() / 3U;
  value.triangle_indices = mesh.triangles.data();
  value.triangle_count = mesh.triangles.size() / 3U;
  return value;
}

} // namespace

int main() {
  HSSConfig configuration = config();
  HSSEngine *engine = nullptr;
  require(hss_engine_create(&configuration, &engine) == HSS_OK,
          "create engine");
  require(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
          "set source provenance");

  HSSSnapshot *empty = snapshot(engine);
  require(summary(empty).scene_revision == 0U &&
              summary(empty).plan_available == 0U,
          "empty snapshot exposes no invented plan");
  uint64_t required = 99U;
  require(hss_snapshot_copy_plan_json(empty, nullptr, 0U, &required) ==
              HSS_UNSUPPORTED_CAPABILITY &&
              required == 0U,
          "unrepresentable plan is explicitly unsupported");
  hss_snapshot_release(empty);

  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = identity(2U);
  epoch.event_sequence = 1U;
  epoch.relationship_state = 1U;
  epoch.T_project_world = identity_matrix();
  epoch.transform_revision = 1U;
  require(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "queue epoch");
  drain(engine);

  const Mesh closed = room(true);
  HSSMeshEvent event = mesh_event(closed, 2U, 0U, 1U);
  require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK,
          "queue captured room");
  drain(engine);
  HSSSnapshot *unconfirmed = snapshot(engine);
  const HSSPlanAnnotationSummary unconfirmed_summary = summary(unconfirmed);
  require(unconfirmed_summary.plan_available == 1U &&
              unconfirmed_summary.annotation_layout_available == 1U &&
              unconfirmed_summary.reference_state ==
                  HSS_PLAN_REFERENCE_FLOOR_UNKNOWN &&
              unconfirmed_summary.boundary_state == HSS_PLAN_BOUNDARY_UNKNOWN &&
              unconfirmed_summary.plan_availability ==
                  HSS_PLAN_AVAILABILITY_UNAVAILABLE &&
              unconfirmed_summary.has_area == 0U,
          "unknown floor suppresses clean plan measurements");
  const std::string unconfirmed_plan = copy_json(unconfirmed, false);
  const std::string unconfirmed_annotations = copy_json(unconfirmed, true);
  require(unconfirmed_plan.find("\"kind\":\"captured_section\"") !=
              std::string::npos &&
              unconfirmed_plan.find("\"reference_state\":\"floor_unknown\"") !=
                  std::string::npos &&
              unconfirmed_annotations.find("\"metric_value\":null") !=
                  std::string::npos,
          "unknown plan retains exact section lineage without numeric claims");

  const HSSMat4d floor = identity_matrix();
  require(hss_engine_set_plan_reference(engine, &floor, 1.2) == HSS_OK,
          "confirm floor reference");
  drain(engine);
  HSSSnapshot *confirmed = snapshot(engine);
  const HSSPlanAnnotationSummary confirmed_summary = summary(confirmed);
  require(confirmed_summary.scene_revision ==
              unconfirmed_summary.scene_revision + 1U &&
              confirmed_summary.reference_state ==
                  HSS_PLAN_REFERENCE_CONFIRMED_FLOOR &&
              confirmed_summary.boundary_state ==
                  HSS_PLAN_BOUNDARY_CLOSED_ACCEPTED &&
              confirmed_summary.plan_availability ==
                  HSS_PLAN_AVAILABILITY_PROVISIONAL &&
              confirmed_summary.area_availability ==
                  HSS_PLAN_AVAILABILITY_PROVISIONAL &&
              confirmed_summary.has_area == 0U &&
              confirmed_summary.fragment_count != 0U &&
              confirmed_summary.annotation_count ==
                  confirmed_summary.fragment_count,
          "confirmed captured cut remains provisional with bound dimensions");
  const std::string confirmed_plan = copy_json(confirmed, false);
  const std::string confirmed_annotations = copy_json(confirmed, true);
  require(confirmed_plan.find("\"area_m2\":null") != std::string::npos &&
              confirmed_plan.find("\"calculator_status\":\"ok\"") !=
                  std::string::npos &&
              confirmed_annotations.find("\"availability\":\"provisional\"") !=
                  std::string::npos &&
              confirmed_annotations.find("\"metric_value\":null") ==
                  std::string::npos,
          "B-031 quality and B-032 explicit lengths survive serialization");

  const Mesh open = room(false);
  event = mesh_event(open, 3U, 1U, 2U);
  require(hss_engine_submit_mesh_event(engine, &event) == HSS_OK,
          "queue open replacement");
  drain(engine);
  HSSSnapshot *open_snapshot = snapshot(engine);
  const HSSPlanAnnotationSummary open_summary = summary(open_snapshot);
  require(open_summary.boundary_state == HSS_PLAN_BOUNDARY_OPEN &&
              open_summary.plan_availability ==
                  HSS_PLAN_AVAILABILITY_PROVISIONAL &&
              open_summary.has_area == 0U &&
              copy_json(open_snapshot, false).find("\"code\":\"open_boundary\"") !=
                  std::string::npos,
          "open replacement cannot publish a clean area");
  require(summary(confirmed).scene_revision == confirmed_summary.scene_revision &&
              copy_json(confirmed, false) == confirmed_plan,
          "historical semantic snapshot remains immutable");

  hss_snapshot_release(open_snapshot);
  hss_snapshot_release(confirmed);
  hss_snapshot_release(unconfirmed);
  hss_engine_destroy(engine);
  std::cout << "runtime plan and annotation snapshots preserve revision and provenance\n";
  return 0;
}

// Host-only fixture generator: actual source/store/engine, visibly synthetic.
// Refuses any nonempty destination, so it cannot replace a user's library.
#include "hss/hss_api.h"
#include "hss/hss_project_store.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
HSSId id(uint8_t first) {
  HSSId value{};
  value.bytes[0] = first;
  value.bytes[6] = 0x50U;
  value.bytes[8] = 0x80U;
  return value;
}
HSSMat4d identity() {
  HSSMat4d value{};
  value.column_major[0] = value.column_major[5] =
      value.column_major[10] = value.column_major[15] = 1.0;
  return value;
}
struct Owners {
  HSSEngine* engine{nullptr};
  HSSProjectStore* store{nullptr};
  ~Owners() { hss_project_store_destroy(store); hss_engine_destroy(engine); }
};
void drain(HSSEngine* engine, uint64_t revision) {
  for (size_t iteration = 0; iteration < 100000; ++iteration) {
    HSSEngineStats stats{};
    require(hss_engine_get_stats(engine, &stats) == HSS_OK, "Cannot read fixture engine");
    if (stats.lifecycle_state == 1U) {
      require(stats.scene_revision == revision, "Fixture revision mismatch");
      return;
    }
    require(hss_engine_step(engine, 5000) == HSS_OK, "Cannot drain fixture engine");
  }
  throw std::runtime_error("Fixture did not drain within its work bound");
}
HSSProjectEventContext context(uint64_t sequence) {
  HSSProjectEventContext value{};
  value.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  value.struct_size = sizeof(value);
  value.capture_id = id(2);
  value.event_sequence = sequence;
  value.accepted_scene_revision = sequence;
  value.session_epoch = 1;
  value.monotonic_time_s = static_cast<double>(sequence - 1) * 0.1;
  return value;
}
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "Usage: hss_native_boundary_fixture <empty trusted library root>");
    const std::filesystem::path root(argv[1]);
    require(root.is_absolute() && std::filesystem::is_directory(root) &&
            !std::filesystem::is_symlink(root) && std::filesystem::is_empty(root),
            "Destination must be an existing empty nonsymlink directory");
    const std::string path = root.string();
    Owners owners;
    HSSConfig config{};
    config.abi_version = HSS_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.project_id = id(1);
    config.structure_voxel_m = 0.02;
    config.detail_voxel_m = 0.01;
    config.truncation_m = 0.04;
    config.max_working_bytes = 512U * 1024U * 1024U;
    config.max_pending_frames = 2;
    config.deterministic_seed = 0x48535331;
    require(hss_engine_create(&config, &owners.engine) == HSS_OK, "Cannot create fixture engine");
    require(hss_engine_set_source_is_synthetic(owners.engine, 1) == HSS_OK, "Cannot label fixture source");
    HSSProjectStoreConfig storage{};
    storage.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
    storage.struct_size = sizeof(storage);
    storage.project_id = id(1);
    storage.trusted_root_directory_utf8 = {reinterpret_cast<const uint8_t*>(path.data()), path.size()};
    storage.open_mode = HSS_PROJECT_STORE_CREATE_NEW;
    storage.source_is_synthetic = 1;
    storage.max_epoch_records = 1024;
    storage.max_depth_records = storage.max_mesh_records = 8192;
    storage.max_command_records = 1024;
    storage.max_retained_bytes = 8ULL * 1024 * 1024 * 1024;
    storage.max_record_bytes = 256U * 1024U * 1024U;
    storage.max_pending_bytes = 512U * 1024U * 1024U;
    require(hss_project_store_create(&storage, &owners.store) == HSS_OK, "Cannot create fixture store");

    HSSEpoch epoch{};
    epoch.abi_version = HSS_ABI_VERSION;
    epoch.struct_size = sizeof(epoch);
    epoch.capture_id = id(2);
    epoch.event_sequence = 1;
    epoch.session_epoch = 1;
    epoch.relationship_state = 1;
    epoch.T_project_world = identity();
    epoch.transform_revision = 1;
    auto c1 = context(1);
    HSSProjectStoreReceipt receipt{};
    require(hss_engine_set_epoch(owners.engine, &epoch) == HSS_OK, "Cannot accept fixture epoch");
    drain(owners.engine, 1);
    require(hss_project_store_append_epoch(owners.store, &c1, &epoch, &receipt) == HSS_OK, "Cannot retain fixture epoch");

    // Four vertical walls, +Y up, floor plan uses X/-Z. No private capture.
    const std::array<float, 24> vertices{0,0,0, 4,0,0, 4,0,-5, 0,0,-5,
                                       0,2.8F,0, 4,2.8F,0, 4,2.8F,-5, 0,2.8F,-5};
    const std::array<uint32_t, 24> triangles{0,1,5, 0,5,4, 1,2,6, 1,6,5,
                                           2,3,7, 2,7,6, 3,0,4, 3,4,7};
    HSSMeshEvent mesh{};
    mesh.abi_version = HSS_ABI_VERSION;
    mesh.struct_size = sizeof(mesh);
    mesh.capture_id = id(2);
    mesh.anchor_id = id(3);
    mesh.event_sequence = 2;
    mesh.session_epoch = 1;
    mesh.anchor_revision = 1;
    mesh.T_world_anchor = identity();
    mesh.vertices_xyz = vertices.data();
    mesh.vertex_count = vertices.size() / 3;
    mesh.triangle_indices = triangles.data();
    mesh.triangle_count = triangles.size() / 3;
    auto c2 = context(2);
    require(hss_engine_submit_mesh_event(owners.engine, &mesh) == HSS_OK, "Cannot accept fixture walls");
    drain(owners.engine, 2);
    require(hss_project_store_append_mesh_event(owners.store, &c2, &mesh, &receipt) == HSS_OK, "Cannot retain fixture walls");

    HSSProjectPlanReference floor{};
    floor.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
    floor.struct_size = sizeof(floor);
    floor.T_project_floor = identity();
    floor.cut_height_m = 1.2;
    floor.before_scene_revision = 2;
    floor.after_scene_revision = 3;
    auto c3 = context(3);
    require(hss_engine_set_plan_reference(owners.engine, &floor.T_project_floor, floor.cut_height_m) == HSS_OK, "Cannot set fixture floor");
    drain(owners.engine, 3);
    require(hss_project_store_append_plan_reference(owners.store, &c3, &floor, &receipt) == HSS_OK, "Cannot retain fixture floor");
    HSSSnapshot* snapshot = nullptr;
    require(hss_engine_acquire_snapshot(owners.engine, &snapshot) == HSS_OK, "Cannot acquire fixture scene");
    HSSCounts counts{};
    const auto countStatus = hss_snapshot_get_counts(snapshot, &counts);
    hss_snapshot_release(snapshot);
    std::cout << "Fixture scene: " << counts.boundary_edge_count << " boundary edges, "
              << counts.vertex_count << " raw vertices.\n";
    require(countStatus == HSS_OK && counts.boundary_edge_count == 8 && counts.vertex_count == 8,
            "Fixture must expose eight triangle-supported cut segments and eight raw captured vertices");
    HSSProjectCheckpointResult checkpoint{};
    require(hss_project_store_checkpoint(owners.store, &checkpoint) == HSS_OK &&
            checkpoint.durability_state == 1 && checkpoint.durable_event_count == 3, "Fixture checkpoint failed");

    const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto staging = root / "workspace-catalog.json.new";
    std::ofstream catalog(staging);
    catalog << "{\"projects\":[{\"id\":\"01000000-0000-5000-8000-000000000000\","
               "\"captureID\":\"02000000-0000-5000-8000-000000000000\","
               "\"name\":\"Reference room\",\"updatedAt\":" << unixSeconds - 978307200 <<
               ",\"sceneRevision\":3,\"state\":\"frozen\",\"sourceIsSynthetic\":true}]}";
    catalog.close();
    require(!catalog.fail(), "Fixture catalog write failed");
    std::filesystem::rename(staging, root / "workspace-catalog.json");
    std::cout << "Synthetic reference room checkpointed: 3 source/control events, scene revision 3.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

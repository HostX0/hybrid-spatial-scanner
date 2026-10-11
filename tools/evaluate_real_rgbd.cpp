// Host-only evaluation of external RGB-D observations. Not a SLAM benchmark:
// poses are supplied by the dataset adapter, not estimated by this engine.
#include "hss/hss_api.h"
#include "hss/hss_project_store.h"
#include "hss/coordinate_basis.hpp"
#include "hss/obj_mtl_exporter.hpp"
#include "hss/calibrated_appearance.hpp"
#include "hss/guidance_policy.hpp"
#include "hss/revisioned_command.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void ok(HSSStatus status, const std::string& operation) {
  require(status == HSS_OK, operation + ": status=" + std::to_string(status));
}
HSSId id(uint8_t n) {
  HSSId value{}; value.bytes[0] = n; value.bytes[6] = 0x50; value.bytes[8] = 0x80; return value;
}
HSSMat4d identity() {
  HSSMat4d m{}; m.column_major[0] = m.column_major[5] = m.column_major[10] = m.column_major[15] = 1; return m;
}
template<class T> std::vector<T> read(const std::filesystem::path& path, size_t maximum) {
  require(std::filesystem::is_regular_file(path) && !std::filesystem::is_symlink(path), "Invalid input file");
  const auto bytes = std::filesystem::file_size(path);
  require(bytes % sizeof(T) == 0 && bytes / sizeof(T) <= maximum, "Input bound exceeded");
  std::vector<T> result(bytes / sizeof(T));
  std::ifstream stream(path, std::ios::binary);
  stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(bytes));
  require(static_cast<bool>(stream), "Truncated input");
  return result;
}
void write(const std::filesystem::path& path, const std::string& body) {
  std::ofstream out(path, std::ios::binary); out << body; out.close(); require(!out.fail(), "Output failed");
}
struct Owners {
  HSSEngine* engine{}; HSSProjectStore* store{};
  ~Owners() { hss_project_store_destroy(store); hss_engine_destroy(engine); }
};
HSSEngineStats drain(HSSEngine* engine) {
  for (size_t n = 0; n < 100000; ++n) {
    HSSEngineStats stats{}; ok(hss_engine_get_stats(engine, &stats), "stats");
    if (stats.lifecycle_state == 1) { ok(static_cast<HSSStatus>(stats.last_status), "drained transaction"); return stats; }
    ok(hss_engine_step(engine, 5000), "step");
  }
  throw std::runtime_error("Work bound exhausted");
}
struct View {
  uint64_t revision{}; uint64_t issues{};
  std::vector<HSSVec3d> vertices; std::vector<uint32_t> triangles; std::string scene;
};
View snapshot(HSSEngine* engine) {
  HSSSnapshot* handle{}; ok(hss_engine_acquire_snapshot(engine, &handle), "snapshot");
  struct Release { HSSSnapshot* h; ~Release() { hss_snapshot_release(h); } } release{handle};
  HSSCounts counts{}; ok(hss_snapshot_get_counts(handle, &counts), "counts");
  View view; view.revision = counts.scene_revision; view.issues = counts.issue_count;
  view.vertices.resize(counts.vertex_count); view.triangles.resize(counts.triangle_count * 3);
  uint64_t required{};
  ok(hss_snapshot_copy_vertices(handle, view.vertices.data(), view.vertices.size(), &required), "vertices");
  ok(hss_snapshot_copy_triangles(handle, view.triangles.data(), view.triangles.size(), &required), "triangles");
  require(hss_snapshot_copy_scene_json(handle, nullptr, 0, &required) == HSS_BUFFER_TOO_SMALL, "scene query");
  view.scene.resize(required);
  ok(hss_snapshot_copy_scene_json(handle, reinterpret_cast<uint8_t*>(view.scene.data()), required, &required), "scene");
  return view;
}
HSSProjectEventContext context(uint64_t sequence, uint64_t revision, double time) {
  HSSProjectEventContext c{}; c.abi_version = HSS_PROJECT_STORE_ABI_VERSION; c.struct_size = sizeof(c);
  c.capture_id = id(2); c.event_sequence = sequence; c.accepted_scene_revision = revision;
  c.session_epoch = 1; c.monotonic_time_s = time; return c;
}
bool same_vertices(const std::vector<HSSVec3d>& a, const std::vector<HSSVec3d>& b, double tolerance) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::abs(a[i].x-b[i].x) > tolerance || std::abs(a[i].y-b[i].y) > tolerance || std::abs(a[i].z-b[i].z) > tolerance) return false;
  return true;
}
}

int main(int argc, char** argv) {
  try {
    static_assert(std::endian::native == std::endian::little, "Adapter files are explicit little-endian");
    require(argc == 3 || (argc == 4 && std::string(argv[3]) == "--appearance"), "Usage: hss_real_rgbd_evaluate <adapted input> <empty output> [--appearance]");
    const std::filesystem::path input(argv[1]), output(argv[2]);
    require(input.is_absolute() && output.is_absolute() && std::filesystem::is_directory(output) &&
            !std::filesystem::is_symlink(output) && std::filesystem::is_empty(output), "Empty trusted output required");
    std::ifstream index(input / "frames.txt");
    size_t frame_count{}; index >> frame_count;
    require(index && frame_count > 0 && frame_count <= 24, "Frame bound");
    HSSConfig config{}; config.abi_version = HSS_ABI_VERSION; config.struct_size = sizeof(config);
    config.project_id = id(1); config.structure_voxel_m = .02; config.detail_voxel_m = .01; config.truncation_m = .04;
    config.max_working_bytes = 512ULL * 1024 * 1024; config.max_pending_frames = 2; config.deterministic_seed = 0x48535331;
    Owners owner; ok(hss_engine_create(&config, &owner.engine), "create");
    auto storage_root = output / "retained"; std::filesystem::create_directory(storage_root);
    const auto root = storage_root.string();
    HSSProjectStoreConfig storage{}; storage.abi_version = HSS_PROJECT_STORE_ABI_VERSION; storage.struct_size = sizeof(storage);
    storage.project_id = id(1); storage.trusted_root_directory_utf8 = {reinterpret_cast<const uint8_t*>(root.data()), root.size()};
    storage.open_mode = HSS_PROJECT_STORE_CREATE_NEW; storage.max_epoch_records = 8;
    storage.max_depth_records = storage.max_mesh_records = 64; storage.max_command_records = 32;
    storage.max_retained_bytes = 512ULL * 1024 * 1024; storage.max_record_bytes = 8 * 1024 * 1024;
    storage.max_pending_bytes = 64 * 1024 * 1024;
    ok(hss_project_store_create(&storage, &owner.store), "store");
    HSSEpoch epoch{}; epoch.abi_version = HSS_ABI_VERSION; epoch.struct_size = sizeof(epoch);
    epoch.capture_id = id(2); epoch.event_sequence = 1; epoch.session_epoch = 1; epoch.relationship_state = 1;
    epoch.T_project_world = identity(); epoch.transform_revision = 1;
    ok(hss_engine_set_epoch(owner.engine, &epoch), "epoch"); auto stats = drain(owner.engine);
    auto c = context(1, stats.scene_revision, 0); HSSProjectStoreReceipt receipt{};
    ok(hss_project_store_append_epoch(owner.store, &c, &epoch, &receipt), "retain epoch");
    std::vector<HSSVec3d> expected_vertices; std::vector<uint32_t> expected_triangles;
    std::vector<std::string> face_sources;
    uint64_t sequence = 1, peak = stats.working_bytes;
    const auto started = std::chrono::steady_clock::now();
    std::ofstream progress(output / "observations.csv");
    progress << "frame,time_s,revision,vertices,triangles,issues,reserved_working_bytes\n";
    for (size_t i = 0; i < frame_count; ++i) {
      size_t frame_index{}; double time{}; index >> frame_index >> time;
      require(index && frame_index == i && std::isfinite(time) && time >= 0, "Frame index/time");
      auto frame_root = input / std::to_string(i);
      auto matrix = read<double>(frame_root / "pose.f64", 16); require(matrix.size() == 16, "Pose size");
      HSSMat4d pose{}; std::copy(matrix.begin(), matrix.end(), pose.column_major);
      require(hss::coordinate::is_rigid_transform(pose), "Adapter pose must be rigid");
      auto depth = read<uint8_t>(frame_root / "depth.f32", 640U * 480U * 4U);
      require(depth.size() == 640U * 480U * 4U, "Registered depth dimensions");
      HSSFrame frame{}; frame.abi_version = HSS_ABI_VERSION; frame.struct_size = sizeof(frame);
      frame.capture_id = id(2); frame.event_sequence = ++sequence; frame.session_epoch = 1; frame.timestamp_s = time;
      frame.T_world_camera = pose; frame.depth_intrinsics = {525,525,319.5,239.5}; frame.depth_width = 640; frame.depth_height = 480;
      frame.depth_row_stride_bytes = 640 * 4; frame.depth_byte_length = depth.size(); frame.depth_bytes = depth.data();
      ok(hss_engine_submit_frame(owner.engine, &frame), "depth frame " + std::to_string(i)); stats = drain(owner.engine);
      c = context(sequence, stats.scene_revision, time);
      ok(hss_project_store_append_frame(owner.store, &c, &frame, &receipt), "retain depth");
      auto xyz = read<float>(frame_root / "vertices.f32", 20000);
      auto triangles = read<uint32_t>(frame_root / "triangles.u32", 20000);
      require(xyz.size() % 3 == 0 && triangles.size() % 3 == 0 && !triangles.empty(), "Mesh layout");
      const uint32_t base = static_cast<uint32_t>(expected_vertices.size());
      for (size_t v = 0; v < xyz.size(); v += 3)
        expected_vertices.push_back(hss::coordinate::transform_point(pose, {xyz[v],xyz[v+1],xyz[v+2]}));
      for (auto t : triangles) { require(t < xyz.size()/3, "Mesh index"); expected_triangles.push_back(base + t); }
      face_sources.insert(face_sources.end(), triangles.size()/3, "dataset-frame-" + std::to_string(i));
      HSSMeshEvent mesh{}; mesh.abi_version = HSS_ABI_VERSION; mesh.struct_size = sizeof(mesh);
      mesh.capture_id = id(2); mesh.anchor_id = id(static_cast<uint8_t>(i + 3)); mesh.event_sequence = ++sequence;
      mesh.anchor_revision = 1; mesh.session_epoch = 1; mesh.T_world_anchor = pose;
      mesh.vertices_xyz = xyz.data(); mesh.vertex_count = xyz.size()/3; mesh.triangle_indices = triangles.data(); mesh.triangle_count = triangles.size()/3;
      ok(hss_engine_submit_mesh_event(owner.engine, &mesh), "mesh frame " + std::to_string(i)); stats = drain(owner.engine);
      c = context(sequence, stats.scene_revision, time);
      ok(hss_project_store_append_mesh_event(owner.store, &c, &mesh, &receipt), "retain mesh");
      const auto view = snapshot(owner.engine);
      require(same_vertices(view.vertices, expected_vertices, 1e-12) && view.triangles == expected_triangles, "Captured source topology changed");
      peak = std::max(peak, stats.working_bytes);
      progress << i << ',' << time << ',' << view.revision << ',' << view.vertices.size() << ',' << view.triangles.size()/3 << ',' << view.issues << ',' << stats.working_bytes << '\n'; progress.flush();
      std::cout << "FRAME=" << i << " revision=" << view.revision << " raw_vertices=" << view.vertices.size() << " issues=" << view.issues << std::endl;
    }
    const auto before = snapshot(owner.engine); write(output / "scene-before-reopen.json", before.scene);
    HSSProjectCheckpointResult durable{}; ok(hss_project_store_checkpoint(owner.store, &durable), "checkpoint");
    require(durable.durability_state == 1 && durable.durable_event_count == sequence, "Durable event mismatch");
    hss_project_store_destroy(owner.store); owner.store = nullptr;
    hss_engine_destroy(owner.engine); owner.engine = nullptr;
    storage.open_mode = HSS_PROJECT_STORE_OPEN_EXISTING;
    ok(hss_project_store_create(&storage, &owner.store), "reopen store");
    ok(hss_engine_create(&config, &owner.engine), "reopen engine");
    HSSProjectReplayResult replay{}; ok(hss_project_store_replay(owner.store, owner.engine, &replay), "replay");
    const auto after = snapshot(owner.engine); write(output / "scene-after-reopen.json", after.scene);
    require(before.scene == after.scene && same_vertices(before.vertices, after.vertices, 0) && before.triangles == after.triangles, "Reopen changed accepted scene");
    hss::obj_mtl::Snapshot exported;
    exported.export_id = "real-rgbd-evaluation"; exported.project_id = "external-licensed-dataset"; exported.scene_revision = after.revision;
    exported.captured_mesh_id = "rgbd-derived-source-mesh"; exported.quality_state = "provisional-no-confirmed-floor";
    exported.warnings = {"HSS_EXTERNAL_KINECT_REFERENCE_POSES", "HSS_EVALUATION_DEPTH_TRIANGULATION", "HSS_FLOOR_REFERENCE_UNCONFIRMED"};
    for (const auto& v : after.vertices) exported.vertices_m.push_back({v.x,v.y,v.z});
    for (size_t f = 0; f < after.triangles.size()/3; ++f) {
      hss::obj_mtl::Face face; face.id = "face-" + std::to_string(f);
      face.vertex_indices = {after.triangles[f*3],after.triangles[f*3+1],after.triangles[f*3+2]};
      face.source_ids = {face_sources[f]}; exported.faces.push_back(face);
    }
    hss::obj_mtl::Options options; options.include_textures = false;
    auto artifact = hss::obj_mtl::export_snapshot(exported, options);
    require(artifact.status == hss::obj_mtl::Status::kOk && artifact.artifact,
            std::string("Real OBJ export: ") + hss::obj_mtl::status_name(artifact.status));
    write(output / "capture.obj", artifact.artifact->obj_utf8); write(output / "capture.mtl", artifact.artifact->mtl_utf8);
    write(output / "export-manifest.json", artifact.artifact->manifest_json);
    if (argc == 4) {
      // TUM spatially registers depth to the RGB pixel grid, but exposure
      // timestamps can differ. Retain both actual reference poses: spatial
      // registration alone cannot certify temporal alignment or static objects.
      std::vector<std::vector<float>> depths;
      std::vector<std::vector<uint8_t>> images;
      std::vector<hss::appearance::CalibratedView> views;
      depths.reserve(frame_count); images.reserve(frame_count); views.reserve(frame_count);
      hss::guidance::State coach;
      const hss::guidance::Tuning tuning{.8,2,8,.4,.5,.7,.6};
      std::ofstream guidance(output / "capture-coach.csv");
      guidance << "frame,quality_revision,valid_depth_fraction,issue_code,evidence_revision,expires_before_revision\n";
      for (size_t i=0; i<frame_count; ++i) {
        const auto folder=input/std::to_string(i);
        depths.push_back(read<float>(folder/"depth.f32",640*480));
        images.push_back(read<uint8_t>(folder/"rgb.png",8*1024*1024));
        const auto pose=read<double>(folder/"pose.f64",16);
        const auto rgb_pose=read<double>(folder/"rgb-pose.f64",16);
        require(pose.size()==16 && rgb_pose.size()==16,"Separate retained depth/RGB reference poses");
        hss::appearance::CalibratedView v;
        v.keyframe_id="dataset-frame-"+std::to_string(i);
        const std::string bytes(reinterpret_cast<const char*>(images.back().data()),images.back().size());
        v.texture={"image-"+std::to_string(i),"textures/image-"+std::to_string(i)+".png",
                   hss::command::sha256_hex(hss::command::sha256(bytes))};
        v.measured_scene_revision=after.revision; v.accepted_transform_revision=1;
        v.calibration=v.accepted_transform=v.registered_color_depth=v.retained_asset_checksum=hss::appearance::Verification::kVerified;
        std::copy(pose.begin(),pose.end(),v.project_from_depth_camera.column_major);
        // Provider spatial registration is not evidence of temporal pose
        // equality. Respect both sampled reference poses; unsupported camera
        // offsets fail closed in the projector instead of being overwritten.
        std::copy(rgb_pose.begin(),rgb_pose.end(),v.project_from_color_camera.column_major);
        v.color_intrinsics=v.depth_intrinsics={525,525,319.5,239.5};
        v.color_width=v.depth_width=640; v.color_height=v.depth_height=480; v.axial_depth_m=depths.back();
        views.push_back(v);
        hss::guidance::QualityVector q; q.revision=i+1; q.tracking=hss::guidance::TrackingState::kNormal;
        q.measured_facts=hss::guidance::kDepthMeasured;
        q.valid_depth_fraction=static_cast<double>(std::count_if(depths.back().begin(),depths.back().end(),[](float d){return std::isfinite(d)&&d>0;}))/depths.back().size();
        // Unavailable support/fit/stability/target-coverage metrics are not
        // replaced with optimistic values to make the policy return success.
        const auto recommendation=hss::guidance::evaluate(q,tuning,coach,static_cast<double>(i));
        require(recommendation.status==hss::guidance::Status::kOk,"Measured guidance evaluation");
        coach=recommendation.next_state;
        guidance<<i<<','<<q.revision<<','<<q.valid_depth_fraction<<',';
        if (recommendation.issue) {
          require(recommendation.issue->evidence_revision==q.revision && recommendation.issue->expires_before_revision>q.revision,"Expired coach evidence");
          guidance<<static_cast<int>(recommendation.issue->code)<<','<<recommendation.issue->evidence_revision<<','<<recommendation.issue->expires_before_revision;
        } else guidance<<"none,0,0";
        guidance<<'\n';
      }
      auto layer=hss::appearance::project(exported,views,1);
      require(layer.status==hss::appearance::ProjectionStatus::kOk,"Calibrated real-data projection");
      auto colored=exported;
      for (const auto& projected:layer.faces) {
        auto& face=colored.faces[projected.face_index];
        face.appearance_target_id=face.id;
        const auto base=static_cast<uint32_t>(colored.texture_coordinates.size());
        colored.texture_coordinates.insert(colored.texture_coordinates.end(),projected.uv.begin(),projected.uv.end());
        face.texture_coordinate_indices=std::array<uint32_t,3>{base,base+1,base+2};
        const auto& ref=views[projected.view_index].texture;
        colored.appearance_slots.push_back({face.id,hss::obj_mtl::AppearanceState::kVerified,ref.local_asset_id,projected.binding,ref});
      }
      const auto textured=hss::obj_mtl::export_snapshot(colored);
      require(textured.status==hss::obj_mtl::Status::kOk && textured.artifact,"Textured real-data bundle");
      require(colored.vertices_m==exported.vertices_m && colored.faces.size()==exported.faces.size(),"Color altered measured mesh");
      for (size_t i=0;i<colored.faces.size();++i)
        require(colored.faces[i].vertex_indices==exported.faces[i].vertex_indices && colored.faces[i].source_ids==exported.faces[i].source_ids,"Color altered face source/topology");
      const auto destination=output/"textured";
      std::filesystem::create_directory(destination); std::filesystem::create_directory(destination/"textures");
      write(destination/"capture.obj",textured.artifact->obj_utf8); write(destination/"capture.mtl",textured.artifact->mtl_utf8);
      write(destination/"manifest.json",textured.artifact->manifest_json);
      for (const auto& texture:textured.artifact->texture_references) {
        const auto found=std::find_if(views.begin(),views.end(),[&](const auto& v){return v.texture==texture;});
        require(found!=views.end(),"Texture reference missing retained image");
        const auto i=static_cast<size_t>(std::distance(views.begin(),found));
        const std::string bytes(reinterpret_cast<const char*>(images[i].data()),images[i].size());
        require(hss::command::sha256_hex(hss::command::sha256(bytes))==texture.sha256_hex,"Retained image checksum changed");
        write(destination/texture.bundle_relative_path,bytes);
      }
      write(output/"appearance-result.json","{\"metric_geometry_changed\":false,\"registered_pair_common_pose_assumption\":false,\"separate_rgb_depth_reference_poses_used\":true,\"rgb_depth_timestamp_equality_verified\":false,\"textured_faces\":"+std::to_string(layer.faces.size())+",\"total_faces\":"+std::to_string(colored.faces.size())+",\"depth_work\":"+std::to_string(layer.depth_checks)+",\"rejected_candidates\":"+std::to_string(layer.rejected_candidates)+",\"unknown_candidates\":"+std::to_string(layer.unknown_candidates)+",\"packaged_images\":"+std::to_string(textured.artifact->texture_references.size())+"}\n");
      std::cout<<"TEXTURED_FACES="<<layer.faces.size()<<'/'<<colored.faces.size()<<" metric_geometry_changed=false\n";
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    write(output / "result.json", "{\"passed\":true,\"source_is_synthetic\":false,\"reference_poses_supplied\":true,\"floor_confirmed\":false,\"frames\":" + std::to_string(frame_count) + ",\"durable_events\":" + std::to_string(sequence) + ",\"revision\":" + std::to_string(after.revision) + ",\"vertices\":" + std::to_string(after.vertices.size()) + ",\"triangles\":" + std::to_string(after.triangles.size()/3) + ",\"issues\":" + std::to_string(after.issues) + ",\"peak_reserved_working_bytes\":" + std::to_string(peak) + ",\"host_elapsed_s\":" + std::to_string(elapsed) + ",\"source_geometry_preserved\":true,\"reopen_exact\":true}\n");
    std::cout << "REAL_RGBD_EVALUATION_PASSED frames=" << frame_count << " host_elapsed_s=" << elapsed << '\n';
    return 0;
  } catch (const std::exception& error) { std::cerr << "REAL_RGBD_EVALUATION_FAILED: " << error.what() << '\n'; return 1; }
}

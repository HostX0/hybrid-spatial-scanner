#include "hss/obj_mtl_exporter.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace exporter = hss::obj_mtl;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

exporter::AppearanceSlot verified_slot(uint64_t revision = 17U) {
  exporter::AppearanceSlot slot;
  slot.target_id = "surface-wall-a";
  slot.state = exporter::AppearanceState::kVerified;
  slot.candidate_asset_id = "asset-wall-a";
  slot.binding = {{{"surface-wall-a", revision}, "keyframe-wall-a", "asset-wall-a", 0.9}};
  slot.texture = {"asset-wall-a", "textures/wall-a.png",
                  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
  return slot;
}

exporter::Snapshot snapshot() {
  exporter::Snapshot value;
  value.export_id = "export-17";
  value.project_id = "project-17";
  value.scene_revision = 17U;
  value.captured_mesh_id = "mesh-revision-4";
  value.source_is_synthetic = true;
  value.quality_state = "provisional";
  value.vertices_m = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                      {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
  value.texture_coordinates = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}};
  value.faces = {
      {"face-observed", {0U, 1U, 2U}, {"depth-11", "mesh-anchor-4"},
       "surface-wall-a", std::array<uint32_t, 3U>{0U, 1U, 2U}},
      {"face-unknown", {0U, 2U, 3U}, {"depth-12"}, "surface-gap-b", std::nullopt},
  };
  value.appearance_slots = {verified_slot(),
                            {"surface-gap-b", exporter::AppearanceState::kUnknown,
                             "asset-gap-b", std::nullopt, std::nullopt}};
  value.warnings = {"synthetic_fixture", "unknown_region_retained"};
  return value;
}

std::vector<std::string> geometry_lines(std::string_view obj) {
  std::vector<std::string> result;
  size_t start = 0U;
  while (start < obj.size()) {
    const size_t end = obj.find('\n', start);
    const std::string_view line = obj.substr(start, end - start);
    if (line.starts_with("v ")) {
      result.emplace_back(line);
    } else if (line.starts_with("f ")) {
      std::istringstream input{std::string(line)};
      std::string token;
      std::string canonical = "f";
      (void)(input >> token);
      while (input >> token) {
        const size_t texture_separator = token.find('/');
        canonical.push_back(' ');
        canonical.append(token.substr(0U, texture_separator));
      }
      result.push_back(std::move(canonical));
    }
    start = end == std::string_view::npos ? obj.size() : end + 1U;
  }
  return result;
}

void test_textured_bundle_freezes_raw_topology_and_provenance() {
  const exporter::Snapshot frozen = snapshot();
  const exporter::Snapshot before = frozen;
  const exporter::Result result = exporter::export_snapshot(frozen);
  check(result.status == exporter::Status::kOk && result.artifact,
        "frozen captured snapshot creates an OBJ/MTL bundle");
  check(frozen.vertices_m == before.vertices_m && frozen.faces == before.faces &&
            frozen.appearance_slots == before.appearance_slots,
        "export leaves the caller-owned captured topology and appearance inputs unchanged");
  if (!result.artifact)
    return;
  const exporter::Artifact &artifact = *result.artifact;
  check(artifact.obj_utf8.find("v 0 0 0\nv 1 0 0") != std::string::npos &&
            artifact.obj_utf8.find("f 1/1 2/2 3/3") != std::string::npos &&
            artifact.obj_utf8.find("f 1 3 4") != std::string::npos,
        "OBJ preserves supplied metre vertices, face order, and 1 m reference edge");
  check(artifact.mtl_utf8.find("map_Kd textures/wall-a.png") != std::string::npos &&
            artifact.obj_utf8.find("usemtl hss_appearance_surface-wall-a") !=
                std::string::npos &&
            artifact.obj_utf8.find("usemtl hss_untextured") != std::string::npos,
        "only an admitted local appearance binding reaches the MTL texture map");
  check(artifact.manifest_json.find("\"scene_revision\":\"17\"") !=
                std::string::npos &&
            artifact.manifest_json.find("\"captured_mesh_id\":\"mesh-revision-4\"") !=
                std::string::npos &&
            artifact.manifest_json.find("\"keyframe_id\":\"keyframe-wall-a\"") !=
                std::string::npos &&
            artifact.manifest_json.find("\"source_ids\":[\"depth-11\",\"mesh-anchor-4\"]") !=
                std::string::npos,
        "manifest freezes revision, captured mesh identity, units, and source lineage");
  check(artifact.manifest_json.find("\"state\":\"unknown\"") != std::string::npos &&
            artifact.manifest_json.find("\"candidate_asset_id\":\"asset-gap-b\"") !=
                std::string::npos &&
            artifact.manifest_json.find("textures/gap-b") == std::string::npos,
        "unknown visual data is retained explicitly without inventing a texture binding");
  check(artifact.texture_references == std::vector<exporter::TextureReference>{*verified_slot().texture},
        "only verified local texture references are returned for an explicit bundle write");
}

void test_texture_free_bundle_keeps_metric_geometry_identical() {
  const exporter::Snapshot frozen = snapshot();
  const exporter::Result textured = exporter::export_snapshot(frozen);
  const exporter::Result texture_free = exporter::export_snapshot(
      frozen, {.include_textures = false});
  check(textured.status == exporter::Status::kOk && texture_free.status == exporter::Status::kOk &&
            textured.artifact && texture_free.artifact,
        "both textured and texture-free frozen exports are valid");
  if (!textured.artifact || !texture_free.artifact)
    return;
  check(geometry_lines(textured.artifact->obj_utf8) ==
            geometry_lines(texture_free.artifact->obj_utf8),
        "removing optional appearance preserves every OBJ vertex and face index");
  check(texture_free.artifact->mtl_utf8.find("map_Kd") == std::string::npos &&
            texture_free.artifact->texture_references.empty() &&
            texture_free.artifact->manifest_json.find("\"texture_mode\":\"texture_free\"") !=
                std::string::npos,
        "texture-free mode contains no image path while retaining an explicit manifest mode");
}

void test_revision_and_appearance_contract_fail_closed() {
  exporter::Snapshot stale = snapshot();
  stale.appearance_slots.front().binding->target.measured_scene_revision = 18U;
  const exporter::Result stale_result = exporter::export_snapshot(stale);
  check(stale_result.status == exporter::Status::kRevisionMismatch && !stale_result.artifact,
        "a visual binding from another frozen revision cannot be exported");

  exporter::Snapshot rejected = snapshot();
  rejected.appearance_slots[1].state = exporter::AppearanceState::kRejected;
  const exporter::Result rejected_result = exporter::export_snapshot(rejected);
  check(rejected_result.status == exporter::Status::kOk && rejected_result.artifact &&
            rejected_result.artifact->obj_utf8.find("textures/gap-b") == std::string::npos &&
            rejected_result.artifact->manifest_json.find("\"state\":\"rejected\"") !=
                std::string::npos,
        "rejected data remains auditable but never becomes a material mapping");

  exporter::Snapshot malformed = snapshot();
  malformed.faces.front().vertex_indices[1] = malformed.faces.front().vertex_indices[0];
  const exporter::Result malformed_result = exporter::export_snapshot(malformed);
  check(malformed_result.status == exporter::Status::kInvalidTopology &&
            !malformed_result.artifact,
        "invalid raw topology is rejected instead of repaired or regenerated");

  malformed = snapshot();
  malformed.appearance_slots.front().texture->bundle_relative_path = "../escape.png";
  const exporter::Result path_result = exporter::export_snapshot(malformed);
  check(path_result.status == exporter::Status::kInvalidTextureReference && !path_result.artifact,
        "unsafe texture paths cannot enter an OBJ/MTL bundle");
}

void test_bounds_and_invalid_visual_inputs_are_explicit() {
  exporter::Options invalid_options;
  invalid_options.max_faces = exporter::kHardMaximumFaces + 1U;
  check(exporter::export_snapshot(snapshot(), invalid_options).status ==
            exporter::Status::kInvalidOptions,
        "unbounded exporter options fail before any output is assembled");

  exporter::Snapshot malformed = snapshot();
  malformed.appearance_slots.front().binding->visual_confidence =
      std::numeric_limits<double>::quiet_NaN();
  check(exporter::export_snapshot(malformed).status == exporter::Status::kInvalidAppearance,
        "malformed visual confidence is rejected without being interpreted as metric evidence");
}

void test_multiple_targets_can_share_one_verified_local_image() {
  auto value=snapshot();
  auto second=verified_slot();
  second.target_id="surface-gap-b";
  second.binding->target.entity_id=second.target_id;
  value.appearance_slots[1]=second;
  value.faces[1].texture_coordinate_indices=std::array<uint32_t,3>{0,1,2};
  const auto result=exporter::export_snapshot(value);
  check(result.status==exporter::Status::kOk && result.artifact,
        "different measured targets may reuse one exact local image without duplicating source pixels");
  if (result.artifact)
    check(result.artifact->texture_references.size()==1,
          "bundle references shared local asset once while preserving per-target admission");
  value.appearance_slots[1].texture->sha256_hex=std::string(64,'a');
  const auto mismatch=exporter::export_snapshot(value);
  check(mismatch.status==exporter::Status::kInvalidTextureReference && !mismatch.artifact,
        "shared path with different checksum fails closed without partial artifact");
}

void test_companion_filename_matches_the_actual_bundle() {
  const auto input=snapshot();
  exporter::Options options; options.material_library_filename="scene.mtl";
  const auto output=exporter::export_snapshot(input,options);
  const auto baseline=exporter::export_snapshot(input);
  check(output.artifact && baseline.artifact && output.status==exporter::Status::kOk &&
        output.artifact->obj_utf8.find("mtllib scene.mtl\n")!=std::string::npos &&
        output.artifact->obj_utf8.find("mtllib capture.mtl")==std::string::npos &&
        geometry_lines(output.artifact->obj_utf8)==geometry_lines(baseline.artifact->obj_utf8),
        "native scene.obj references its actual scene.mtl without changing geometry");
  for (const std::string name:{"../escape.mtl","scene.mtl\nusemtl injected","a/b.mtl",".hidden.mtl","a..mtl","scene.obj",""}) {
    options.material_library_filename=name;
    const auto invalid=exporter::export_snapshot(input,options);
    check(invalid.status==exporter::Status::kInvalidOptions && !invalid.artifact,
          "unsafe or non-MTL companion filenames fail without a partial artifact");
  }
}

} // namespace

int main() {
  test_textured_bundle_freezes_raw_topology_and_provenance();
  test_texture_free_bundle_keeps_metric_geometry_identical();
  test_revision_and_appearance_contract_fail_closed();
  test_bounds_and_invalid_visual_inputs_are_explicit();
  test_multiple_targets_can_share_one_verified_local_image();
  test_companion_filename_matches_the_actual_bundle();
  if (failures == 0)
    std::cout << "OBJ/MTL exporter tests passed\n";
  return failures == 0 ? 0 : 1;
}

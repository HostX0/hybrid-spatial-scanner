#include "hss/hss_api.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HSSConfig valid_config() {
  HSSConfig config{};
  config.abi_version = HSS_ABI_VERSION;
  config.struct_size = sizeof(config);
  config.structure_voxel_m = 0.02;
  config.detail_voxel_m = 0.01;
  config.truncation_m = 0.04;
  config.max_working_bytes = 1024U * 1024U;
  config.max_pending_frames = 2;
  return config;
}

}  // namespace

int main() {
  HSSEngine* engine = nullptr;
  HSSConfig invalid = valid_config();
  invalid.abi_version = HSS_ABI_VERSION + 1U;
  check(hss_engine_create(&invalid, &engine) == HSS_UNSUPPORTED_VERSION, "invalid ABI version maps safely");
  check(engine == nullptr, "failed create clears handle");

  HSSConfig config = valid_config();
  check(hss_engine_create(&config, &engine) == HSS_OK && engine != nullptr, "create");
  try {
    check(hss_engine_submit_frame(engine, nullptr) == HSS_INVALID_ARGUMENT,
          "invalid C input returns a status rather than escaping an exception");
  } catch (...) {
    check(false, "C facade allowed an exception to escape");
  }

  HSSSnapshot* snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK && snapshot != nullptr, "acquire snapshot");
  hss_engine_destroy(engine);

  HSSCounts counts{};
  check(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK, "snapshot survives engine destroy");
  check(counts.vertex_count == 0U && counts.triangle_count == 0U, "empty facade snapshot has no scanner geometry");

  uint64_t required = 0;
  check(hss_snapshot_copy_scene_json(snapshot, nullptr, 0, &required) == HSS_BUFFER_TOO_SMALL && required > 0U,
        "scene JSON size query");
  std::vector<uint8_t> undersized(required, 0xa5U);
  check(hss_snapshot_copy_scene_json(snapshot, undersized.data(), required - 1U, &required) == HSS_BUFFER_TOO_SMALL,
        "undersized scene JSON rejected");
  for (uint8_t value : undersized) {
    check(value == 0xa5U, "undersized copy does not partially write");
  }
  std::vector<uint8_t> scene(required);
  check(hss_snapshot_copy_scene_json(snapshot, scene.data(), scene.size(), &required) == HSS_OK,
        "scene JSON complete copy");
  check(std::memcmp(scene.data(), "{\"schema_version\"", 17) == 0, "schema-shaped empty snapshot");
  hss_snapshot_release(snapshot);
  hss_snapshot_release(nullptr);

  std::cout << "HSS engine facade ownership and copy-out checks passed (no scanner output)\n";
  return EXIT_SUCCESS;
}

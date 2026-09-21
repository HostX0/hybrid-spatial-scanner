#include "hss/coordinate_basis.hpp"
#include "hss/hss_api.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void check(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool close(double lhs, double rhs) {
  return std::abs(lhs - rhs) < 1.0e-12;
}

HSSMat4d identity() {
  HSSMat4d result{};
  result.column_major[0] = 1.0;
  result.column_major[5] = 1.0;
  result.column_major[10] = 1.0;
  result.column_major[15] = 1.0;
  return result;
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

HSSEpoch valid_epoch(uint32_t epoch_number, uint32_t relationship_state) {
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.session_epoch = epoch_number;
  epoch.relationship_state = relationship_state;
  epoch.T_project_world = identity();
  return epoch;
}

}  // namespace

int main() {
  using hss::coordinate::FloorFrame;
  using hss::coordinate::Pixel;
  using hss::coordinate::PresentationOrientation;

  const FloorFrame floor = hss::coordinate::default_floor_frame();
  check(hss::coordinate::is_valid_floor_frame(floor), "default floor is orthonormal right-handed");
  const auto plan = hss::coordinate::project_to_floor(floor, {2.5, 7.0, -3.25});
  check(close(plan.u_m, 2.5) && close(plan.v_m, 3.25), "floor projection is metre based");
  FloorFrame reflected = floor;
  reflected.v.z = 1.0;
  check(!hss::coordinate::is_valid_floor_frame(reflected), "left-handed floor frame rejected");

  const HSSIntrinsics intrinsics{100.0, 80.0, 50.0, 40.0};
  const HSSVec3d camera_point =
      hss::coordinate::unproject_axial_depth_m(75.0, 60.0, 2.0, intrinsics);
  check(close(camera_point.x, 0.5) && close(camera_point.y, -0.5) && close(camera_point.z, -2.0),
        "axial metres map to documented AR camera convention");
  HSSMat4d translated = identity();
  translated.column_major[12] = 1.0;
  translated.column_major[13] = 2.0;
  translated.column_major[14] = 3.0;
  const HSSVec3d project_point = hss::coordinate::transform_point(translated, camera_point);
  check(close(project_point.x, 1.5) && close(project_point.y, 1.5) && close(project_point.z, 1.0),
        "column-major transform applies translation once");

  Pixel sensor{};
  check(hss::coordinate::presentation_to_sensor_pixel(PresentationOrientation::kLandscapeRight, 640, 480,
                                                       {100.0, 200.0}, &sensor) &&
            close(sensor.x, 100.0) && close(sensor.y, 200.0),
        "landscape sensor coordinates are unchanged");
  check(hss::coordinate::presentation_to_sensor_pixel(PresentationOrientation::kPortrait, 640, 480,
                                                       {200.0, 100.0}, &sensor) &&
            close(sensor.x, 539.0) && close(sensor.y, 200.0),
        "portrait presentation maps once to sensor coordinates");
  check(hss::coordinate::presentation_to_sensor_pixel(PresentationOrientation::kPortraitUpsideDown, 640, 480,
                                                       {200.0, 100.0}, &sensor) &&
            close(sensor.x, 100.0) && close(sensor.y, 279.0),
        "upside-down portrait mapping remains deterministic");
  check(!hss::coordinate::presentation_to_sensor_pixel(PresentationOrientation::kPortrait, 640, 480,
                                                        {480.0, 0.0}, &sensor),
        "out-of-bounds presentation coordinate rejected");

  HSSMat4d rigid = identity();
  check(hss::coordinate::is_rigid_transform(rigid), "identity is rigid");
  HSSMat4d scaled = rigid;
  scaled.column_major[0] = 1.001;
  check(!hss::coordinate::is_rigid_transform(scaled), "embedded scale rejected");
  HSSMat4d reflection = rigid;
  reflection.column_major[0] = -1.0;
  check(!hss::coordinate::is_rigid_transform(reflection), "reflection rejected");
  HSSMat4d malformed = rigid;
  malformed.column_major[15] = std::numeric_limits<double>::quiet_NaN();
  check(!hss::coordinate::is_rigid_transform(malformed), "nonfinite transform rejected");

  HSSEngine* engine = nullptr;
  const HSSConfig config = valid_config();
  check(hss_engine_create(&config, &engine) == HSS_OK, "create facade for semantic errors");
  HSSEpoch nonrigid_epoch = valid_epoch(1, 1);
  nonrigid_epoch.T_project_world = scaled;
  check(hss_engine_set_epoch(engine, &nonrigid_epoch) == HSS_INVALID_COORDINATE_FRAME,
        "facade preserves coordinate-frame status for nonrigid epoch");
  // Equal numeric transforms do not make an unresolved epoch accepted: each
  // relationship state is supplied explicitly by the adapter and this facade
  // emits no geometry from either epoch.
  HSSEpoch unresolved = valid_epoch(2, 0);
  HSSEpoch accepted = valid_epoch(3, 1);
  check(hss_engine_set_epoch(engine, &unresolved) == HSS_OK,
        "unresolved epoch accepted without compatibility inference");
  check(hss_engine_set_epoch(engine, &accepted) == HSS_OK, "accepted epoch requires explicit state");
  hss_engine_destroy(engine);

  std::cout << "Coordinate, floor-basis, projection, and orientation checks passed (synthetic host evidence)\n";
  return EXIT_SUCCESS;
}

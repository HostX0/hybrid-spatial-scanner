#pragma once

#include "hss/hss_api.h"

#include <cstdint>

namespace hss::coordinate {

// Canonical physical values are metres. These utilities deliberately model
// only the documented project/camera convention, not an ARKit viewport.
constexpr double kRigidTolerance = 1.0e-6;

struct FloorFrame {
  HSSVec3d origin_m;
  HSSVec3d u;
  HSSVec3d v;
  HSSVec3d n;
};

struct PlanPoint {
  double u_m;
  double v_m;
};

struct Pixel {
  double x;
  double y;
};

enum class PresentationOrientation : uint32_t {
  kLandscapeRight = 0,
  kLandscapeLeft = 1,
  kPortrait = 2,
  kPortraitUpsideDown = 3,
};

bool is_rigid_transform(const HSSMat4d& transform);
bool is_valid_floor_frame(const FloorFrame& frame);
FloorFrame default_floor_frame();
PlanPoint project_to_floor(const FloorFrame& frame, HSSVec3d project_point_m);
HSSVec3d unproject_axial_depth_m(double sensor_u, double sensor_v, double depth_m,
                                 const HSSIntrinsics& intrinsics);
HSSVec3d transform_point(const HSSMat4d& transform, HSSVec3d point);

// Converts a presentation pixel to the recorded sensor pixel. Intrinsics stay
// tied to the sensor grid; this mapping must be applied at most once.
bool presentation_to_sensor_pixel(PresentationOrientation orientation,
                                  uint32_t sensor_width, uint32_t sensor_height,
                                  Pixel presentation, Pixel* sensor);

}  // namespace hss::coordinate

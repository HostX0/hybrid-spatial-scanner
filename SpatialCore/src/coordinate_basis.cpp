#include "hss/coordinate_basis.hpp"

#include <array>
#include <cmath>

namespace hss::coordinate {
namespace {

bool finite(HSSVec3d point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

double dot(HSSVec3d lhs, HSSVec3d rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

HSSVec3d cross(HSSVec3d lhs, HSSVec3d rhs) {
  return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

bool close(double lhs, double rhs) {
  return std::abs(lhs - rhs) <= kRigidTolerance;
}

bool valid_pixel(Pixel pixel, uint32_t width, uint32_t height) {
  return std::isfinite(pixel.x) && std::isfinite(pixel.y) && pixel.x >= 0.0 && pixel.y >= 0.0 &&
         pixel.x < static_cast<double>(width) && pixel.y < static_cast<double>(height);
}

}  // namespace

bool is_rigid_transform(const HSSMat4d& transform) {
  const double* m = transform.column_major;
  for (size_t index = 0; index < 16; ++index) {
    if (!std::isfinite(m[index])) {
      return false;
    }
  }
  if (!close(m[3], 0.0) || !close(m[7], 0.0) || !close(m[11], 0.0) || !close(m[15], 1.0)) {
    return false;
  }
  const std::array<HSSVec3d, 3> columns{{{m[0], m[1], m[2]}, {m[4], m[5], m[6]},
                                          {m[8], m[9], m[10]}}};
  for (size_t column = 0; column < columns.size(); ++column) {
    if (!close(dot(columns[column], columns[column]), 1.0)) {
      return false;
    }
    for (size_t other = column + 1; other < columns.size(); ++other) {
      if (!close(dot(columns[column], columns[other]), 0.0)) {
        return false;
      }
    }
  }
  return close(dot(cross(columns[0], columns[1]), columns[2]), 1.0);
}

bool is_valid_floor_frame(const FloorFrame& frame) {
  if (!finite(frame.origin_m) || !finite(frame.u) || !finite(frame.v) || !finite(frame.n)) {
    return false;
  }
  return close(dot(frame.u, frame.u), 1.0) && close(dot(frame.v, frame.v), 1.0) &&
         close(dot(frame.n, frame.n), 1.0) && close(dot(frame.u, frame.v), 0.0) &&
         close(dot(frame.u, frame.n), 0.0) && close(dot(frame.v, frame.n), 0.0) &&
         close(dot(cross(frame.u, frame.v), frame.n), 1.0);
}

FloorFrame default_floor_frame() {
  return {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}};
}

PlanPoint project_to_floor(const FloorFrame& frame, HSSVec3d project_point_m) {
  const HSSVec3d delta{project_point_m.x - frame.origin_m.x, project_point_m.y - frame.origin_m.y,
                       project_point_m.z - frame.origin_m.z};
  return {dot(delta, frame.u), dot(delta, frame.v)};
}

HSSVec3d unproject_axial_depth_m(double sensor_u, double sensor_v, double depth_m,
                                 const HSSIntrinsics& intrinsics) {
  const double x_m = (sensor_u - intrinsics.cx) * depth_m / intrinsics.fx;
  const double y_m = (sensor_v - intrinsics.cy) * depth_m / intrinsics.fy;
  return {x_m, -y_m, -depth_m};
}

HSSVec3d transform_point(const HSSMat4d& transform, HSSVec3d point) {
  const double* m = transform.column_major;
  return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
          m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
          m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

bool presentation_to_sensor_pixel(PresentationOrientation orientation, uint32_t sensor_width,
                                  uint32_t sensor_height, Pixel presentation, Pixel* sensor) {
  if (sensor == nullptr || sensor_width == 0U || sensor_height == 0U) {
    return false;
  }
  const bool rotated = orientation == PresentationOrientation::kPortrait ||
                       orientation == PresentationOrientation::kPortraitUpsideDown;
  const uint32_t presentation_width = rotated ? sensor_height : sensor_width;
  const uint32_t presentation_height = rotated ? sensor_width : sensor_height;
  if (!valid_pixel(presentation, presentation_width, presentation_height)) {
    return false;
  }
  const double max_x = static_cast<double>(sensor_width - 1U);
  const double max_y = static_cast<double>(sensor_height - 1U);
  switch (orientation) {
    case PresentationOrientation::kLandscapeRight:
      *sensor = presentation;
      return true;
    case PresentationOrientation::kLandscapeLeft:
      *sensor = {max_x - presentation.x, max_y - presentation.y};
      return true;
    case PresentationOrientation::kPortrait:
      *sensor = {max_x - presentation.y, presentation.x};
      return true;
    case PresentationOrientation::kPortraitUpsideDown:
      *sensor = {presentation.y, max_y - presentation.x};
      return true;
  }
  return false;
}

}  // namespace hss::coordinate

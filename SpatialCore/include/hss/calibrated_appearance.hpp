#pragma once

#include "hss/coordinate_basis.hpp"
#include "hss/obj_mtl_exporter.hpp"

#include <span>

namespace hss::appearance {

// Read-only calibrated source views. Initial implementation supports only
// provider-registered/coincident camera geometry. Different viewpoints remain
// unknown: depth visibility in one camera does not prove RGB visibility in another.
// RGB is never read by this module; a caller retains/checksums local bytes.
struct CalibratedView {
  std::string keyframe_id;
  obj_mtl::TextureReference texture;
  uint64_t measured_scene_revision{};
  uint64_t accepted_transform_revision{};
  Verification calibration{Verification::kUnknown};
  Verification accepted_transform{Verification::kUnknown};
  Verification registered_color_depth{Verification::kUnknown};
  Verification retained_asset_checksum{Verification::kUnknown};
  HSSMat4d project_from_color_camera{};
  HSSMat4d project_from_depth_camera{};
  HSSIntrinsics color_intrinsics{}, depth_intrinsics{};
  uint32_t color_width{}, color_height{}, depth_width{}, depth_height{};
  std::span<const float> axial_depth_m;
};

struct ProjectionOptions {
  size_t maximum_views{24};
  size_t maximum_faces{16'384};
  size_t maximum_depth_pixels_per_face{4096};
  // Includes all raster iterations, even pixels outside a triangle.
  uint64_t maximum_total_depth_checks{16'000'000};
  // Appearance-only visibility gates. They never change metric tolerances,
  // positions, topology or support. Unknown/occluded areas remain neutral.
  double depth_agreement_absolute_m{0.03};
  double depth_agreement_relative{0.01};
  double image_margin_pixels{1};
  double minimum_projected_area_pixels{1};
};

enum class ProjectionStatus : uint8_t {
  kOk, kInvalidInput, kInvalidOptions, kResourceLimit, kInternalFailure
};
struct ProjectedFace {
  size_t face_index{}, view_index{};
  std::array<obj_mtl::TextureCoordinate,3> uv{};
  AppearanceBinding binding;
  uint64_t checked_depth_pixels{};
};
struct ProjectionResult {
  ProjectionStatus status{ProjectionStatus::kInvalidInput};
  std::vector<ProjectedFace> faces;
  uint64_t depth_checks{}, rejected_candidates{}, unknown_candidates{};
  bool metric_geometry_changed{false};
};

// Projects an immutable source mesh and verifies every covered depth pixel of
// each candidate (bounded). No geometry or image warping, nearest-neighbor
// infill, pose optimization, smoothing or hole completion is performed.
ProjectionResult project(const obj_mtl::Snapshot& measured,
                         std::span<const CalibratedView> views,
                         uint64_t expected_accepted_transform_revision,
                         const ProjectionOptions& options = {});

} // namespace hss::appearance

#include "hss/calibrated_appearance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hss::appearance {
namespace {
bool positive(double v) { return std::isfinite(v) && v > 0; }
bool intrinsics(const HSSIntrinsics& k, uint32_t w, uint32_t h) {
  return positive(k.fx) && positive(k.fy) && std::isfinite(k.cx) && std::isfinite(k.cy) &&
         w > 1 && h > 1 && w <= 8192 && h <= 8192 && k.cx >= 0 && k.cx < w && k.cy >= 0 && k.cy < h;
}
HSSVec3d camera_point(const HSSMat4d& pose, const obj_mtl::Vertex& p) {
  const auto& m = pose.column_major;
  const double x = p.x_m-m[12], y = p.y_m-m[13], z = p.z_m-m[14];
  return {m[0]*x+m[1]*y+m[2]*z, m[4]*x+m[5]*y+m[6]*z, m[8]*x+m[9]*y+m[10]*z};
}
struct Pixel { double x,y,z; };
bool pixel(const obj_mtl::Vertex& p, const HSSMat4d& pose, const HSSIntrinsics& k, uint32_t w, uint32_t h,
           double margin, Pixel& out) {
  const auto c = camera_point(pose,p);
  out = {k.fx*c.x/-c.z+k.cx, k.fy*-c.y/-c.z+k.cy, -c.z};
  return positive(out.z) && std::isfinite(out.x) && std::isfinite(out.y) &&
         out.x >= margin && out.y >= margin && out.x <= w-1-margin && out.y <= h-1-margin;
}
double area(const std::array<Pixel,3>& p) {
  return (p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
}
enum class Visibility { kVerified, kRejected, kUnknown, kLimit };
Visibility visible(const std::array<Pixel,3>& p, const CalibratedView& view, const ProjectionOptions& options,
                   uint64_t& total, uint64_t& checked) {
  const double a = area(p);
  if (std::abs(a) < 1e-12) return Visibility::kUnknown;
  const int minx = static_cast<int>(std::floor(std::min({p[0].x,p[1].x,p[2].x})));
  const int maxx = static_cast<int>(std::ceil(std::max({p[0].x,p[1].x,p[2].x})));
  const int miny = static_cast<int>(std::floor(std::min({p[0].y,p[1].y,p[2].y})));
  const int maxy = static_cast<int>(std::ceil(std::max({p[0].y,p[1].y,p[2].y})));
  if (static_cast<uint64_t>(maxx-minx+1)*static_cast<uint64_t>(maxy-miny+1) > options.maximum_depth_pixels_per_face)
    return Visibility::kUnknown;
  auto test = [&](double depth, size_t index) {
    ++checked;
    const double observed = view.axial_depth_m[index];
    if (!positive(observed)) return Visibility::kUnknown;
    const double tolerance = std::max(options.depth_agreement_absolute_m, options.depth_agreement_relative*depth);
    return std::abs(observed-depth) <= tolerance ? Visibility::kVerified : Visibility::kRejected;
  };
  // Vertex support is required even for a subpixel triangle.
  for (const auto& v : p) {
    if (++total > options.maximum_total_depth_checks) return Visibility::kLimit;
    const auto index = static_cast<size_t>(std::lround(v.y))*view.depth_width + static_cast<size_t>(std::lround(v.x));
    const auto status = test(v.z,index);
    if (status != Visibility::kVerified) return status;
  }
  for (int y=miny; y<=maxy; ++y) for (int x=minx; x<=maxx; ++x) {
    if (++total > options.maximum_total_depth_checks) return Visibility::kLimit;
    const double b1 = ((x-p[0].x)*(p[2].y-p[0].y)-(y-p[0].y)*(p[2].x-p[0].x))/a;
    const double b2 = ((p[1].x-p[0].x)*(y-p[0].y)-(p[1].y-p[0].y)*(x-p[0].x))/a;
    const double b0 = 1-b1-b2;
    if (std::min({b0,b1,b2}) < -1e-10) continue;
    const double depth = 1/(b0/p[0].z+b1/p[1].z+b2/p[2].z);
    const auto index = static_cast<size_t>(y)*view.depth_width + static_cast<size_t>(x);
    const auto status = test(depth,index);
    if (status != Visibility::kVerified) return status;
  }
  return Visibility::kVerified;
}
}

ProjectionResult project(const obj_mtl::Snapshot& measured, std::span<const CalibratedView> views,
                         uint64_t expected_accepted_transform_revision,
                         const ProjectionOptions& options) {
  ProjectionResult result;
  if (options.maximum_views == 0 || options.maximum_views > 128 || options.maximum_faces == 0 ||
      options.maximum_faces > kHardMaximumBindings || options.maximum_depth_pixels_per_face == 0 ||
      options.maximum_depth_pixels_per_face > 1'048'576 || options.maximum_total_depth_checks == 0 ||
      options.maximum_total_depth_checks > 100'000'000 || !positive(options.depth_agreement_absolute_m) ||
      !positive(options.depth_agreement_relative) || !positive(options.minimum_projected_area_pixels) ||
      !std::isfinite(options.image_margin_pixels) || options.image_margin_pixels < 0) {
    result.status = ProjectionStatus::kInvalidOptions; return result;
  }
  if (measured.scene_revision == 0 || expected_accepted_transform_revision == 0 || measured.vertices_m.empty() || measured.faces.empty() || views.empty()) return result;
  if (views.size() > options.maximum_views || measured.faces.size() > options.maximum_faces ||
      measured.vertices_m.size() > obj_mtl::kHardMaximumVertices) { result.status = ProjectionStatus::kResourceLimit; return result; }
  try {
    // Reuse the existing provenance/topology/export boundary rather than a
    // second validator. No appearance is present on this immutable input.
    if (!measured.appearance_slots.empty() || !measured.texture_coordinates.empty()) return result;
    obj_mtl::Options validation; validation.include_textures = false;
    validation.max_vertices = obj_mtl::kHardMaximumVertices;
    if (obj_mtl::export_snapshot(measured, validation).status != obj_mtl::Status::kOk) return result;
    std::vector<bool> usable(views.size(),false);
    for (size_t i=0; i<views.size(); ++i) {
      const auto& v = views[i];
      usable[i] = v.measured_scene_revision == measured.scene_revision && v.accepted_transform_revision == expected_accepted_transform_revision &&
                  v.calibration == Verification::kVerified && v.accepted_transform == Verification::kVerified &&
                  v.registered_color_depth == Verification::kVerified && v.retained_asset_checksum == Verification::kVerified &&
                  obj_mtl::is_valid_texture_reference(v.texture) &&
                  std::equal(std::begin(v.project_from_color_camera.column_major),std::end(v.project_from_color_camera.column_major),std::begin(v.project_from_depth_camera.column_major)) &&
                  coordinate::is_rigid_transform(v.project_from_color_camera) && coordinate::is_rigid_transform(v.project_from_depth_camera) &&
                  intrinsics(v.color_intrinsics,v.color_width,v.color_height) && intrinsics(v.depth_intrinsics,v.depth_width,v.depth_height) &&
                  v.axial_depth_m.size() == static_cast<uint64_t>(v.depth_width)*v.depth_height;
    }
    AppearanceIsolation isolation({options.maximum_faces});
    for (size_t f=0; f<measured.faces.size(); ++f) {
      const auto& face = measured.faces[f];
      std::optional<ProjectedFace> best;
      double best_area = -1;
      for (size_t i=0; i<views.size(); ++i) {
        const auto& view = views[i];
        if (!usable[i]) { ++result.unknown_candidates; continue; }
        std::array<Pixel,3> rgb{}, depth{};
        bool inside = true;
        for (size_t n=0; n<3; ++n) {
          const auto& p = measured.vertices_m[face.vertex_indices[n]];
          inside = pixel(p,view.project_from_color_camera,view.color_intrinsics,view.color_width,view.color_height,options.image_margin_pixels,rgb[n]) &&
                   pixel(p,view.project_from_depth_camera,view.depth_intrinsics,view.depth_width,view.depth_height,options.image_margin_pixels,depth[n]) && inside;
        }
        if (!inside) { ++result.rejected_candidates; continue; }
        const double projected_area = std::abs(area(rgb))/2;
        if (projected_area < options.minimum_projected_area_pixels || projected_area <= best_area) continue;
        uint64_t checked{};
        const auto visibility = visible(depth,view,options,result.depth_checks,checked);
        if (visibility == Visibility::kLimit) { result.faces.clear(); result.status = ProjectionStatus::kResourceLimit; return result; }
        if (visibility == Visibility::kUnknown) { ++result.unknown_candidates; continue; }
        if (visibility == Visibility::kRejected) { ++result.rejected_candidates; continue; }
        const MetricTargetRef target{face.id,measured.scene_revision};
        KeyframeAppearance candidate{view.keyframe_id,view.texture.local_asset_id,measured.scene_revision,
                                    Verification::kVerified,Verification::kVerified,Verification::kVerified,
                                    std::min(1.0,projected_area/(view.color_width*static_cast<double>(view.color_height)))};
        const auto admission = isolation.submit(target,candidate);
        if (admission.status != Status::kApplied || !admission.active_binding) { ++result.unknown_candidates; continue; }
        ProjectedFace projected; projected.face_index=f; projected.view_index=i; projected.binding=*admission.active_binding;
        projected.checked_depth_pixels=checked;
        for (size_t n=0; n<3; ++n)
          projected.uv[n] = {(rgb[n].x+.5)/view.color_width, 1-(rgb[n].y+.5)/view.color_height};
        best=std::move(projected); best_area=projected_area;
      }
      if (best) result.faces.push_back(std::move(*best));
    }
    result.status=ProjectionStatus::kOk;
  } catch (...) { result.faces.clear(); result.status=ProjectionStatus::kInternalFailure; }
  return result;
}
} // namespace hss::appearance

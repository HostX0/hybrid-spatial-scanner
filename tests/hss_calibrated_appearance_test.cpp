#include "hss/calibrated_appearance.hpp"
#include <iostream>
#include <limits>

namespace {
using namespace hss::appearance;
int failures=0;
void check(bool condition,const char* message) { if (!condition) { std::cerr<<"FAIL: "<<message<<'\n'; ++failures; } }
HSSMat4d identity() { HSSMat4d p{}; p.column_major[0]=p.column_major[5]=p.column_major[10]=p.column_major[15]=1; return p; }
hss::obj_mtl::Snapshot mesh() {
  hss::obj_mtl::Snapshot s; s.export_id="export"; s.project_id="project"; s.scene_revision=3;
  s.captured_mesh_id="captured"; s.quality_state="provisional";
  s.vertices_m={{-.5,.5,-2},{.5,.5,-2},{0,-.5,-2}};
  s.faces={{"face",{0,1,2},{"source-frame"},"",std::nullopt}}; return s;
}
CalibratedView view(std::span<const float> depth) {
  CalibratedView v; v.keyframe_id="frame"; v.texture={"image","textures/image.png",std::string(64,'a')};
  v.measured_scene_revision=3; v.accepted_transform_revision=7;
  v.calibration=v.accepted_transform=v.registered_color_depth=v.retained_asset_checksum=Verification::kVerified;
  v.project_from_color_camera=v.project_from_depth_camera=identity();
  v.color_width=v.depth_width=v.color_height=v.depth_height=32;
  v.color_intrinsics=v.depth_intrinsics={32,32,15.5,15.5}; v.axial_depth_m=depth; return v;
}
ProjectionResult run(const hss::obj_mtl::Snapshot& s,const CalibratedView& v,const ProjectionOptions& o={}) {
  return project(s,std::span<const CalibratedView>(&v,1),7,o);
}
void test_occlusion_and_unknowns() {
  auto s=mesh(); const auto vertices=s.vertices_m; const auto faces=s.faces;
  std::vector<float> depth(32*32,2); auto v=view(depth);
  auto r=run(s,v);
  check(r.status==ProjectionStatus::kOk && r.faces.size()==1 && !r.metric_geometry_changed,"supported face receives calibrated UV binding");
  check(s.vertices_m==vertices && s.faces==faces,"appearance leaves immutable metric buffers byte-equivalent");
  depth[16*32+16]=1; r=run(s,v);
  check(r.faces.empty() && r.rejected_candidates==1,"foreground at triangle interior rejects entire image candidate");
  depth[16*32+16]=0; r=run(s,v);
  check(r.faces.empty() && r.unknown_candidates==1,"missing depth inside triangle remains neutral, not inpainted");
  depth[16*32+16]=2;
  for (auto member : {&CalibratedView::calibration,&CalibratedView::accepted_transform,&CalibratedView::registered_color_depth,&CalibratedView::retained_asset_checksum}) {
    auto unknown=v; unknown.*member=Verification::kUnknown;
    check(run(s,unknown).faces.empty(),"missing explicit verification cannot decorate geometry");
  }
  auto stale=v; stale.measured_scene_revision=4; check(run(s,stale).faces.empty(),"stale scene rejected");
  stale=v; stale.accepted_transform_revision=6; check(run(s,stale).faces.empty(),"stale transform rejected against immutable accepted revision");
  auto displaced=v; displaced.project_from_color_camera.column_major[12]=.1;
  check(run(s,displaced).faces.empty(),"other-camera visibility is unsupported even when depth rays agree");
  auto invalid=v; invalid.texture.bundle_relative_path="../outside.png";
  check(run(s,invalid).faces.empty(),"unsafe retained texture reference never admitted");
  invalid=v; invalid.texture.sha256_hex="bad";
  check(run(s,invalid).faces.empty(),"invalid digest never admitted");
  s.vertices_m[0].z_m=2; check(run(s,v).faces.empty(),"behind-camera face rejected");
  s=mesh(); s.vertices_m[0].x_m=-3; check(run(s,v).faces.empty(),"image-border/out-of-frame face rejected");
}
void test_work_budget_is_atomic() {
  auto s=mesh(); std::vector<float> depth(32*32,2); auto v=view(depth);
  auto options=ProjectionOptions{}; options.maximum_total_depth_checks=5;
  auto r=run(s,v,options);
  check(r.status==ProjectionStatus::kResourceLimit && r.faces.empty(),"all raster work charged; resource exhaustion publishes no partial layer");
  options.maximum_faces=0;
  check(run(s,v,options).status==ProjectionStatus::kInvalidOptions,"invalid work bounds rejected");
}
}
int main() { test_occlusion_and_unknowns(); test_work_budget_is_atomic(); return failures?1:0; }

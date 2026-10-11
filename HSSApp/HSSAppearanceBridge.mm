#import "HSSAppearanceBridge.h"
#include "hss/calibrated_appearance.hpp"
#include "hss/revisioned_command.hpp"
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

@interface HSSAppearanceArtifacts ()
@property(nonatomic,readwrite) NSData *faceJSON;
@property(nonatomic,readwrite) NSData *objData;
@property(nonatomic,readwrite) NSData *mtlData;
@property(nonatomic,readwrite) NSData *manifestData;
@property(nonatomic,readwrite) NSArray<NSString*> *usedAssetIDs;
@end
@implementation HSSAppearanceArtifacts
@end

namespace {
std::string text(NSString* s) { return s.UTF8String ? std::string(s.UTF8String) : std::string(); }
NSData* data(const std::string& s) { return [NSData dataWithBytes:s.data() length:s.size()]; }
void fail(NSError** out,NSString* message) {
  if (out) *out=[NSError errorWithDomain:@"org.hybridspatialscanner.appearance" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
bool number(NSDictionary* d,NSString* key,double& out) {
  id value=d[key]; if (![value isKindOfClass:NSNumber.class]) return false;
  out=[value doubleValue]; return std::isfinite(out);
}
bool dimension(NSDictionary* d,NSString* key,uint32_t& out) {
  double v{}; if (!number(d,key,v) || v<2 || v>8192 || std::floor(v)!=v) return false;
  out=static_cast<uint32_t>(v); return true;
}
}

@implementation HSSAppearanceBridge
+ (HSSAppearanceArtifacts*)buildForProjectID:(NSString*)projectID sceneRevision:(uint64_t)revision
                         sourceIsSynthetic:(BOOL)synthetic
                         transformRevision:(uint64_t)transformRevision verticesXYZ:(NSData*)vertices
                           triangleIndices:(NSData*)triangles views:(NSArray<NSDictionary<NSString*,id>*>*)packets error:(NSError**)error {
  try {
    if (!revision || !transformRevision || vertices.length==0 || vertices.length%(3*sizeof(double)) ||
        triangles.length==0 || triangles.length%(3*sizeof(uint32_t)) || vertices.length>65536*3*sizeof(double) ||
        triangles.length>65536*3*sizeof(uint32_t) || packets.count==0 || packets.count>24) {
      fail(error,@"Appearance input exceeds its supported source bounds."); return nil;
    }
    hss::obj_mtl::Snapshot measured;
    measured.export_id="appearance-"+std::to_string(revision); measured.project_id=text(projectID);
    measured.scene_revision=revision; measured.captured_mesh_id="accepted-mesh-"+std::to_string(revision);
    measured.source_is_synthetic=synthetic;
    measured.quality_state="provisional";
    measured.warnings={"HSS_OPTIONAL_APPEARANCE_NOT_METRIC_AUTHORITY","HSS_FROZEN_SNAPSHOT_SOURCE_LINKS"};
    // NSData base addresses need not be aligned: copy each scalar, never cast
    // a borrowed byte pointer into a potentially unaligned typed buffer.
    for (size_t i=0;i<vertices.length/(3*sizeof(double));++i) {
      double xyz[3]; std::memcpy(xyz,static_cast<const uint8_t*>(vertices.bytes)+i*sizeof(xyz),sizeof(xyz));
      measured.vertices_m.push_back({xyz[0],xyz[1],xyz[2]});
    }
    for (size_t i=0;i<triangles.length/(3*sizeof(uint32_t));++i) {
      std::array<uint32_t,3> indices{};
      std::memcpy(indices.data(),static_cast<const uint8_t*>(triangles.bytes)+i*sizeof(uint32_t)*3,sizeof(uint32_t)*3);
      measured.faces.push_back({"face-"+std::to_string(i),indices,{measured.captured_mesh_id},"",std::nullopt});
    }
    std::vector<std::vector<float>> depths; depths.reserve(packets.count);
    std::vector<hss::appearance::CalibratedView> views; views.reserve(packets.count);
    for (NSDictionary* packet in packets) {
      if (![packet isKindOfClass:NSDictionary.class]) { fail(error,@"Invalid calibrated image record."); return nil; }
      NSData* depth=packet[@"depth"]; NSData* pose=packet[@"pose"]; NSData* jpeg=packet[@"jpeg"];
      NSString* keyframe=packet[@"keyframeID"]; NSString* asset=packet[@"assetID"]; NSString* checksum=packet[@"sha256"];
      if (![depth isKindOfClass:NSData.class] || ![pose isKindOfClass:NSData.class] || pose.length!=16*sizeof(double) ||
          ![jpeg isKindOfClass:NSData.class] || jpeg.length==0 || jpeg.length>4*1024*1024 ||
          ![keyframe isKindOfClass:NSString.class] || ![asset isKindOfClass:NSString.class] || ![checksum isKindOfClass:NSString.class]) {
        fail(error,@"Calibrated image bytes or identity are invalid."); return nil;
      }
      hss::appearance::CalibratedView v;
      v.keyframe_id=text(keyframe); v.texture={text(asset),"textures/"+text(asset)+".jpg",text(checksum)};
      const std::string bytes(static_cast<const char*>(jpeg.bytes),jpeg.length);
      if (hss::command::sha256_hex(hss::command::sha256(bytes))!=v.texture.sha256_hex ||
          !dimension(packet,@"colorWidth",v.color_width) || !dimension(packet,@"colorHeight",v.color_height) ||
          !dimension(packet,@"depthWidth",v.depth_width) || !dimension(packet,@"depthHeight",v.depth_height) ||
          !number(packet,@"fx",v.color_intrinsics.fx) || !number(packet,@"fy",v.color_intrinsics.fy) ||
          !number(packet,@"cx",v.color_intrinsics.cx) || !number(packet,@"cy",v.color_intrinsics.cy) ||
          !number(packet,@"dfx",v.depth_intrinsics.fx) || !number(packet,@"dfy",v.depth_intrinsics.fy) ||
          !number(packet,@"dcx",v.depth_intrinsics.cx) || !number(packet,@"dcy",v.depth_intrinsics.cy) ||
          depth.length!=static_cast<uint64_t>(v.depth_width)*v.depth_height*sizeof(float) || depth.length>4*1024*1024) {
        fail(error,@"Retained appearance checksum/calibration validation failed."); return nil;
      }
      depths.emplace_back(depth.length/sizeof(float)); std::memcpy(depths.back().data(),depth.bytes,depth.length);
      v.axial_depth_m=depths.back(); std::memcpy(v.project_from_depth_camera.column_major,pose.bytes,pose.length);
      v.project_from_color_camera=v.project_from_depth_camera;
      v.measured_scene_revision=revision; v.accepted_transform_revision=transformRevision;
      v.calibration=v.accepted_transform=v.registered_color_depth=v.retained_asset_checksum=hss::appearance::Verification::kVerified;
      views.push_back(std::move(v));
    }
    // Large snapshots retain all metric faces. A bounded evenly distributed
    // subset is considered for optional decoration, not for metric analysis.
    auto candidates=measured; candidates.faces.clear();
    std::vector<size_t> source_faces;
    const size_t selected=std::min(measured.faces.size(),hss::appearance::kHardMaximumBindings);
    for (size_t i=0;i<selected;++i) {
      const size_t f=i*measured.faces.size()/selected;
      candidates.faces.push_back(measured.faces[f]); source_faces.push_back(f);
    }
    const auto projected=hss::appearance::project(candidates,views,transformRevision);
    if (projected.status!=hss::appearance::ProjectionStatus::kOk) { fail(error,@"Appearance could not be verified within its bounded work budget."); return nil; }
    NSMutableArray* faces=[NSMutableArray array];
    auto colored=measured;
    for (const auto& p:projected.faces) {
      const size_t f=source_faces[p.face_index]; const auto& v=views[p.view_index];
      auto& face=colored.faces[f]; face.appearance_target_id=face.id;
      const auto base=static_cast<uint32_t>(colored.texture_coordinates.size());
      colored.texture_coordinates.insert(colored.texture_coordinates.end(),p.uv.begin(),p.uv.end());
      face.texture_coordinate_indices=std::array<uint32_t,3>{base,base+1,base+2};
      colored.appearance_slots.push_back({face.id,hss::obj_mtl::AppearanceState::kVerified,v.texture.local_asset_id,p.binding,v.texture});
      [faces addObject:@{@"faceIndex":@(f),@"assetID":[NSString stringWithUTF8String:v.texture.local_asset_id.c_str()],
                         @"uv":@[@[@(p.uv[0].u),@(p.uv[0].v)],@[@(p.uv[1].u),@(p.uv[1].v)],@[@(p.uv[2].u),@(p.uv[2].v)]]}];
    }
    hss::obj_mtl::Options exportOptions; exportOptions.material_library_filename="scene.mtl";
    const auto exported=hss::obj_mtl::export_snapshot(colored,exportOptions);
    if (exported.status!=hss::obj_mtl::Status::kOk || !exported.artifact) { fail(error,@"Verified appearance export failed closed."); return nil; }
    NSError* jsonError=nil;
    NSData* faceJSON=[NSJSONSerialization dataWithJSONObject:faces options:0 error:&jsonError];
    if (!faceJSON) { if (error) *error=jsonError; return nil; }
    HSSAppearanceArtifacts* result=[HSSAppearanceArtifacts new]; result.faceJSON=faceJSON;
    result.objData=data(exported.artifact->obj_utf8); result.mtlData=data(exported.artifact->mtl_utf8); result.manifestData=data(exported.artifact->manifest_json);
    NSMutableArray<NSString*>* used=[NSMutableArray array];
    for (const auto& reference:exported.artifact->texture_references)
      [used addObject:[NSString stringWithUTF8String:reference.local_asset_id.c_str()]];
    result.usedAssetIDs=used; return result;
  } catch (...) { fail(error,@"Optional appearance failed; measured geometry remains available."); return nil; }
}
@end

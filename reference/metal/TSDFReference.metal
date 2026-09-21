// Reference kernel: authored for this specification, NOT compiled or run here.
// Each work item has a unique voxel index: host must deduplicate before dispatch.
// Pixel calibration must match the depth texture, NOT the screen viewport.
#include <metal_stdlib>
using namespace metal;
struct Voxel { float distance; float weight; };
struct FusionParams {
    float4x4 T_camera_submap;
    float4 intrinsics; // fx,fy,cx,cy for depth texture
    float truncation_m;
    float weight_cap;
    uint item_count;
    uint reserved;
};
kernel void hss_integrate_reference(
    device Voxel* voxels [[buffer(0)]],
    const device uint* unique_indices [[buffer(1)]],
    const device float4* centers_submap [[buffer(2)]],
    constant FusionParams& p [[buffer(3)]],
    texture2d<float,access::read> depth [[texture(0)]],
    texture2d<float,access::read> eligibility_weight [[texture(1)]],
    uint id [[thread_position_in_grid]]) {
    if(id>=p.item_count || p.truncation_m<=0 || p.weight_cap<=0) return;
    float4 camera=p.T_camera_submap*centers_submap[id];
    float z=-camera.z;
    if(!isfinite(z) || z<=0) return;
    float u=p.intrinsics.x*camera.x/z+p.intrinsics.z;
    float v=p.intrinsics.y*(-camera.y)/z+p.intrinsics.w;
    if(!isfinite(u)||!isfinite(v)) return;
    int2 pixel=int2(floor(float2(u,v)+0.5f));
    if(pixel.x<0||pixel.y<0||pixel.x>=int(depth.get_width())||pixel.y>=int(depth.get_height()))return;
    uint2 uv=uint2(pixel);float d=depth.read(uv).x;
    float sample=eligibility_weight.read(uv).x;
    if(!isfinite(d)||d<=0||!isfinite(sample)||sample<=0)return;
    float sdf=d-z;
    if(sdf < -p.truncation_m)return; // not observed: behind first surface
    uint index=unique_indices[id];Voxel old=voxels[index];
    float w=min(sample,max(0.0f,p.weight_cap-old.weight));
    if(w<=0)return;
    float f=clamp(sdf/p.truncation_m,-1.0f,1.0f);
    float total=old.weight+w;
    voxels[index]={ (old.weight*old.distance+w*f)/total,total };
}
// Eligibility texture must already reject invalid tracking, bad depth confidence,
// motion regions and ambiguous edge samples. No such classification is performed
// by this kernel. Weight=0 means unknown, not an observed zero-distance surface.

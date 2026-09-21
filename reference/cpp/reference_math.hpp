#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace hss_reference {
using P2=std::array<double,2>;
inline double signed_area(const std::vector<P2>& points) {
 if(points.size()<3) throw std::invalid_argument("polygon needs at least 3 points");
 const auto origin=points.front(); double sum=0, correction=0;
 for(size_t i=0;i<points.size();++i) {
  auto a=points[i],b=points[(i+1)%points.size()];
  for(auto v:a) if(!std::isfinite(v)) throw std::invalid_argument("nonfinite point");
  double term=(a[0]-origin[0])*(b[1]-origin[1])-(b[0]-origin[0])*(a[1]-origin[1]);
  double y=term-correction, t=sum+y; correction=(t-sum)-y; sum=t;
 }
 return sum*0.5;
}
inline std::array<double,3> unproject(double u,double v,double z,double fx,double fy,double cx,double cy) {
 if(!std::isfinite(z)||z<=0||!std::isfinite(fx)||fx<=0||!std::isfinite(fy)||fy<=0)
  throw std::invalid_argument("invalid depth or focal length");
 for(double x:{u,v,cx,cy}) if(!std::isfinite(x)) throw std::invalid_argument("nonfinite calibration");
 return {(u-cx)*z/fx,-(v-cy)*z/fy,-z};
}
struct Tsdf {double value=0, weight=0;};
inline Tsdf fuse(Tsdf old,double signed_distance,double sample_weight,double mu,double cap) {
 if(!std::isfinite(mu)||mu<=0||!std::isfinite(cap)||cap<=0||!std::isfinite(sample_weight)||sample_weight<0)
  throw std::invalid_argument("invalid fusion configuration");
 if(!std::isfinite(old.value)||!std::isfinite(old.weight)||old.weight<0||old.weight>cap)
  throw std::invalid_argument("invalid stored voxel");
 if(!std::isfinite(signed_distance)||signed_distance < -mu || sample_weight==0) return old;
 // Saturation freezes additional evidence instead of silently inventing an EMA.
 double w=std::fmin(sample_weight,cap-old.weight); if(w<=0)return old;
 double f=std::fmax(-1.0,std::fmin(1.0,signed_distance/mu));
 return {(old.weight*old.value+w*f)/(old.weight+w),old.weight+w};
}
}

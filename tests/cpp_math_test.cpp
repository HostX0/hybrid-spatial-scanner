#include "reference_math.hpp"
#include "hss/hss_api.h"
#include <cstdlib>
#include <iostream>
void check(bool ok,const char* name){if(!ok){std::cerr<<name<<"\n";std::exit(1);}}
int main(){using namespace hss_reference;
 std::vector<P2> rib={{0,0},{2,0},{2,.5},{2.15,.5},{2.15,0},{6,0},{6,5},{0,5}};
 check(std::abs(signed_area(rib)-29.925)<1e-10,"rib area");
 auto p=unproject(50,50,2,100,100,50,50);check(p[2]==-2,"camera sign");
 auto voxel=fuse({},0,1,.04,64);check(voxel.weight==1&&voxel.value==0,"fusion zero");
 auto hidden=fuse(voxel,-1,1,.04,64);check(hidden.weight==1,"occluded voxel");
 try{unproject(0,0,-1,100,100,0,0);return 1;}catch(const std::invalid_argument&){}
 std::cout<<"C++ reference checks passed (not scanner implementation)\n";
}

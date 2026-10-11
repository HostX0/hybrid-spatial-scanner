#include "hss/hss_api.h"
#include "hss/hss_guidance.h"
_Static_assert(HSS_GUIDANCE_DEPTH_MEASURED == 1, "Depth fact availability bit");
_Static_assert(HSS_GUIDANCE_ALL_FACTS == 127, "Known guidance facts only");
int main(void) {
 HSSConfig c={0}; c.abi_version=HSS_ABI_VERSION; c.struct_size=sizeof c;
 HSSGuidanceQuality q={0}; q.abi_version=HSS_ABI_VERSION; q.struct_size=sizeof q;
 q.measured_facts=HSS_GUIDANCE_DEPTH_MEASURED;
 return c.abi_version!=1 || q.measured_facts!=1;
}

#ifndef HSS_GUIDANCE_H
#define HSS_GUIDANCE_H
#include "hss/hss_api.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive presentation-only owner. No geometry, capture or durability
 * authority. Calls on one owner must be serialized. No exception crosses C.
 * v1 requires exact known struct size and zero reserved fields. */
typedef struct HSSGuidanceOwner HSSGuidanceOwner;
/* Explicit availability, not a quality score. Zero means all scalar facts
 * are unavailable. Unknown bits are invalid. Unmeasured scalar values are
 * ignored by policy; zero-initialize them, not guessed optimistic scores. */
enum {
 HSS_GUIDANCE_DEPTH_MEASURED=1U<<0,
 HSS_GUIDANCE_SUPPORT_MEASURED=1U<<1,
 HSS_GUIDANCE_VIEWS_MEASURED=1U<<2,
 HSS_GUIDANCE_FIT_MEASURED=1U<<3,
 HSS_GUIDANCE_STABILITY_MEASURED=1U<<4,
 HSS_GUIDANCE_COVERAGE_MEASURED=1U<<5,
 HSS_GUIDANCE_OCCLUSION_MEASURED=1U<<6,
 HSS_GUIDANCE_ALL_FACTS=(1U<<7)-1U
};
typedef struct HSSGuidanceTuning {
 uint32_t abi_version,struct_size;
 double debounce_s,minimum_display_s,repeated_haptic_s;
 double low_enter,low_exit,high_enter,high_exit;
} HSSGuidanceTuning;
typedef struct HSSGuidanceQuality {
 uint32_t abi_version,struct_size;
 uint64_t quality_revision;
 double monotonic_time_s;
 uint32_t tracking,resources; /* guidance enums: normal=0, available=0 */
 uint32_t relocalization_unresolved,measured_facts; /* HSS_GUIDANCE_*_MEASURED mask */
 double valid_depth_fraction,support_extent,view_diversity,fit_quality,change_stability;
 uint64_t target_domain_revision;
 double target_coverage,occlusion_suspicion;
 uint32_t has_conflict,processing_pending;
 uint32_t reserved[2];
} HSSGuidanceQuality;
typedef struct HSSGuidancePresentation {
 uint32_t abi_version,struct_size;
 uint64_t evidence_revision,expires_before_quality_revision;
 uint32_t code,has_issue,hide_precise_overlays,haptic_allowed;
} HSSGuidancePresentation;
HSSStatus hss_guidance_create(const HSSGuidanceTuning*,HSSGuidanceOwner**);
HSSStatus hss_guidance_evaluate(HSSGuidanceOwner*,const HSSGuidanceQuality*,HSSGuidancePresentation*);
void hss_guidance_reset(HSSGuidanceOwner*);
void hss_guidance_destroy(HSSGuidanceOwner*);
#ifdef __cplusplus
}
#endif
#endif

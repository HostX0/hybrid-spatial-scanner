#include "hss/hss_guidance.h"
#include "hss/guidance_policy.hpp"
#include <new>

struct HSSGuidanceOwner { hss::guidance::Tuning tuning; hss::guidance::State state; };
extern "C" HSSStatus hss_guidance_create(const HSSGuidanceTuning* input,HSSGuidanceOwner** out) {
  if (!out) return HSS_INVALID_ARGUMENT;
  *out=nullptr;
  if (!input || input->abi_version!=HSS_ABI_VERSION || input->struct_size!=sizeof(*input)) return HSS_INVALID_ARGUMENT;
  const hss::guidance::Tuning tuning{input->debounce_s,input->minimum_display_s,input->repeated_haptic_s,input->low_enter,input->low_exit,input->high_enter,input->high_exit};
  hss::guidance::QualityVector sample; sample.revision=1;
  if (hss::guidance::evaluate(sample,tuning,{},0).status!=hss::guidance::Status::kOk) return HSS_INVALID_ARGUMENT;
  try { *out=new HSSGuidanceOwner{tuning,{}}; return HSS_OK; }
  catch (const std::bad_alloc&) { return HSS_RESOURCE_LIMIT; }
  catch (...) { return HSS_INTERNAL_ERROR; }
}
extern "C" HSSStatus hss_guidance_evaluate(HSSGuidanceOwner* owner,const HSSGuidanceQuality* input,HSSGuidancePresentation* out) {
  if (!owner || !input || !out || input->abi_version!=HSS_ABI_VERSION || input->struct_size!=sizeof(*input) ||
      input->tracking>3 || input->resources>2 || input->relocalization_unresolved>1 || input->has_conflict>1 || input->processing_pending>1 || input->reserved[0] || input->reserved[1]) return HSS_INVALID_ARGUMENT;
  try {
    hss::guidance::QualityVector q;
    q.revision=input->quality_revision; q.tracking=static_cast<hss::guidance::TrackingState>(input->tracking);
    q.resources=static_cast<hss::guidance::ResourceState>(input->resources); q.relocalization_unresolved=input->relocalization_unresolved!=0;
    q.measured_facts=input->measured_facts; q.valid_depth_fraction=input->valid_depth_fraction;
    q.support_extent=input->support_extent; q.independent_view_diversity=input->view_diversity;
    q.fit_quality=input->fit_quality; q.change_stability=input->change_stability;
    q.target_domain_revision=input->target_domain_revision; q.target_coverage=input->target_coverage;
    q.occlusion_suspicion=input->occlusion_suspicion; q.has_conflict=input->has_conflict!=0; q.processing_pending=input->processing_pending!=0;
    const auto result=hss::guidance::evaluate(q,owner->tuning,owner->state,input->monotonic_time_s);
    if (result.status!=hss::guidance::Status::kOk) return HSS_INVALID_ARGUMENT;
    HSSGuidancePresentation presentation{}; presentation.abi_version=HSS_ABI_VERSION; presentation.struct_size=sizeof(presentation);
    if (result.issue) {
      presentation.has_issue=1; presentation.code=static_cast<uint32_t>(result.issue->code);
      presentation.evidence_revision=result.issue->evidence_revision; presentation.expires_before_quality_revision=result.issue->expires_before_revision;
      presentation.hide_precise_overlays=result.issue->precise_overlays_hidden; presentation.haptic_allowed=result.issue->haptic_allowed;
    }
    owner->state=result.next_state; *out=presentation; return HSS_OK;
  } catch (...) { return HSS_INTERNAL_ERROR; }
}
extern "C" void hss_guidance_reset(HSSGuidanceOwner* owner) { if (owner) owner->state={}; }
extern "C" void hss_guidance_destroy(HSSGuidanceOwner* owner) { delete owner; }

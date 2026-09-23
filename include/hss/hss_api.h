#ifndef HSS_API_H
#define HSS_API_H
/* HSS ABI v1.1: normative declarations, not an implementation of the scanner.
 * Scalars in this host ABI use native endianness. Never serialize raw structs.
 * See docs/D16_Contracts.md for storage encoding, ownership and lifecycle rules. */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HSS_ABI_VERSION 1u

typedef struct HSSEngine HSSEngine;
typedef struct HSSSnapshot HSSSnapshot;
typedef int32_t HSSStatus;
enum {
 HSS_OK=0, HSS_INVALID_ARGUMENT=1, HSS_UNSUPPORTED_VERSION=2,
 HSS_UNSUPPORTED_CAPABILITY=3, HSS_INVALID_CALIBRATION=4,
 HSS_INVALID_COORDINATE_FRAME=5, HSS_STALE_REVISION=6,
 HSS_CONSTRAINT_CONFLICT=7, HSS_INSUFFICIENT_EVIDENCE=8,
 HSS_BUFFER_TOO_SMALL=9, HSS_RESOURCE_LIMIT=10, HSS_CANCELED=11,
 HSS_INTERNAL_ERROR=12, HSS_NOT_READY=13, HSS_DUPLICATE_MISMATCH=14
};
/* Fixed-size math and identity values do not contain version prefixes. */
typedef struct HSSId { uint8_t bytes[16]; } HSSId;
typedef struct HSSVec3d { double x,y,z; } HSSVec3d;
typedef struct HSSMat4d { double column_major[16]; } HSSMat4d;
typedef struct HSSIntrinsics { double fx,fy,cx,cy; } HSSIntrinsics;
typedef struct HSSBytes { const uint8_t* data; uint64_t length; } HSSBytes;
/* Every extensible message starts with these two uint32 fields.
 * Version 1 rejects a struct_size smaller than its full known size, accepts a
 * larger size only if all trailing bytes are zero, and ignores no flags. */
typedef struct HSSConfig {
 uint32_t abi_version, struct_size;
 HSSId project_id;
 double structure_voxel_m, detail_voxel_m, truncation_m;
 uint64_t max_working_bytes;
 uint32_t max_pending_frames, deterministic_seed;
 uint32_t flags; /* v1: must be 0; explicit backend is set by platform adapter. */
 uint32_t reserved;
} HSSConfig;

typedef struct HSSFrame {
 uint32_t abi_version, struct_size;
 HSSId capture_id;
 uint64_t event_sequence;
 uint32_t session_epoch, tracking_state; /* 0=normal,1=limited,2=unavailable */
 double timestamp_s;
 HSSMat4d T_world_camera;
 HSSIntrinsics depth_intrinsics;
 uint32_t depth_width, depth_height;
 uint64_t depth_row_stride_bytes, depth_byte_length;
 const uint8_t* depth_bytes; /* Float32 axial metres, little-endian in v1 */
 uint64_t confidence_row_stride_bytes, confidence_byte_length;
 const uint8_t* confidence_bytes; /* optional uint8 0/1/2,255=unknown */
 uint32_t depth_source; /* 0=sceneDepth; 1=smoothedSceneDepth: preview only */
 uint32_t reserved;
} HSSFrame;

typedef struct HSSEpoch {
 uint32_t abi_version, struct_size;
 HSSId capture_id;
 uint64_t event_sequence;
 uint32_t session_epoch, relationship_state; /* 0=unresolved,1=accepted */
 HSSMat4d T_project_world;
 uint64_t transform_revision;
} HSSEpoch;

typedef struct HSSMeshEvent {
 uint32_t abi_version, struct_size;
 HSSId capture_id, anchor_id;
 uint64_t event_sequence, anchor_revision;
 uint32_t session_epoch, kind; /* 0=add,1=replace,2=remove */
 HSSMat4d T_world_anchor;
 const float* vertices_xyz; /* tightly packed XYZ Float32 anchor-local metres */
 uint64_t vertex_count;
 const uint32_t* triangle_indices;
 uint64_t triangle_count;
 /* remove requires null pointers and zero counts. */
} HSSMeshEvent;

enum {
 HSS_DYNAMIC_SOURCE_DEPTH_FRAME=0,
 HSS_DYNAMIC_SOURCE_MESH_ANCHOR=1
};
enum {
 HSS_DYNAMIC_EVIDENCE_TEMPORAL_DEPTH_CHANGE=0,
 HSS_DYNAMIC_EVIDENCE_TRACKED_MULTI_FRAME_MOTION=1
};
enum {
 HSS_DYNAMIC_NO_VERIFIED_EFFECT=0,
 HSS_DYNAMIC_VERIFIED_MASK=1
};

/* Independently verified captured-source evidence for one pending
 * exclude_dynamic_region command. source_id names the exact frame or mesh
 * anchor revision; capture/epoch/sequence prevent evidence from crossing a
 * source identity boundary. Bounds are canonical project metres. */
typedef struct HSSDynamicEvidence {
 uint32_t abi_version, struct_size;
 HSSId evidence_id, exclusion_command_id, project_id;
 HSSId source_id, capture_id;
 uint64_t session_epoch, event_sequence;
 HSSVec3d bounds_min_m, bounds_max_m;
 uint32_t source_kind, evidence_kind;
 uint32_t reserved[2];
} HSSDynamicEvidence;

typedef struct HSSDynamicEvidenceBatch {
 uint32_t abi_version, struct_size;
 HSSId exclusion_command_id;
 /* Must equal the immutable snapshot revision against which the pending
  * command was validated. A stale batch has no effect. */
 uint64_t expected_scene_revision;
 const HSSDynamicEvidence* evidence;
 uint64_t evidence_count;
 uint32_t flags; /* v1: must be zero. */
 uint32_t reserved;
} HSSDynamicEvidenceBatch;

typedef struct HSSDynamicExclusionAudit {
 uint32_t abi_version, struct_size;
 HSSId exclusion_command_id;
 uint64_t scene_revision;
 uint64_t linked_evidence_count, masked_triangle_count;
 uint64_t triangle_mask_fingerprint;
 uint32_t disposition;
 uint32_t reserved;
} HSSDynamicExclusionAudit;

typedef struct HSSDynamicMaskSummary {
 uint32_t abi_version, struct_size;
 uint64_t scene_revision, triangle_count;
 uint64_t included_triangle_count, excluded_triangle_count;
 uint64_t applied_exclusion_count, stale_exclusion_count;
} HSSDynamicMaskSummary;

typedef struct HSSCounts {
 uint32_t abi_version, struct_size;
 uint64_t scene_revision, vertex_count, triangle_count;
 uint64_t boundary_node_count, boundary_edge_count, issue_count;
} HSSCounts;

enum {
 HSS_PLAN_VIEW_FLOOR_FOOTPRINT=0,
 HSS_PLAN_VIEW_ARCHITECTURAL_CUT=1,
 HSS_PLAN_VIEW_REFLECTED_CEILING=2
};
enum {
 HSS_PLAN_REFERENCE_CONFIRMED_FLOOR=0,
 HSS_PLAN_REFERENCE_FLOOR_UNKNOWN=1,
 HSS_PLAN_REFERENCE_UNSUPPORTED=2,
 HSS_PLAN_REFERENCE_NOT_APPLICABLE=3
};
enum {
 HSS_PLAN_BOUNDARY_CLOSED_ACCEPTED=0,
 HSS_PLAN_BOUNDARY_OPEN=1,
 HSS_PLAN_BOUNDARY_UNKNOWN=2,
 HSS_PLAN_BOUNDARY_UNSUPPORTED=3
};
enum {
 HSS_PLAN_AVAILABILITY_READY=0,
 HSS_PLAN_AVAILABILITY_PROVISIONAL=1,
 HSS_PLAN_AVAILABILITY_UNAVAILABLE=2,
 HSS_PLAN_AVAILABILITY_UNSUPPORTED=3,
 HSS_PLAN_AVAILABILITY_NOT_APPLICABLE=4
};

/* Revision-bound B-031/B-032 runtime state. plan_available==0 means this
 * snapshot cannot be represented by the semantic contract without guessing;
 * the JSON copy functions then return HSS_UNSUPPORTED_CAPABILITY. has_area is
 * independent of geometric closure: provisional/unavailable plans never
 * publish a clean numeric area. */
typedef struct HSSPlanAnnotationSummary {
 uint32_t abi_version, struct_size;
 uint64_t scene_revision;
 uint64_t fragment_count, plan_issue_count, annotation_count;
 uint32_t plan_available, annotation_layout_available;
 uint32_t view, reference_state, boundary_state;
 uint32_t plan_availability, area_availability;
 uint32_t source_is_synthetic, has_area;
 double area_m2, cut_height_m, sampling_band_m;
} HSSPlanAnnotationSummary;

enum {
 HSS_RESOURCE_MEASUREMENT_UNMEASURED=0,
 HSS_RESOURCE_MEASUREMENT_MEASURED=1
};
enum {
 HSS_RESOURCE_THERMAL_NOMINAL=0,
 HSS_RESOURCE_THERMAL_FAIR=1,
 HSS_RESOURCE_THERMAL_SERIOUS=2,
 HSS_RESOURCE_THERMAL_CRITICAL=3
};
enum {
 HSS_RESOURCE_MEMORY_NOMINAL=0,
 HSS_RESOURCE_MEMORY_WARNING=1,
 HSS_RESOURCE_MEMORY_CRITICAL=2
};
enum {
 HSS_RESOURCE_STATUS_OK=0,
 HSS_RESOURCE_STATUS_INVALID_CONFIGURATION=1,
 HSS_RESOURCE_STATUS_INVALID_MEASUREMENT=2,
 HSS_RESOURCE_STATUS_OUT_OF_ORDER=3,
 HSS_RESOURCE_STATUS_INVALID_TRANSITION=4,
 HSS_RESOURCE_STATUS_INVALID_EVENT_KIND=5
};
enum {
 HSS_RESOURCE_MODE_NORMAL=0,
 HSS_RESOURCE_MODE_CONSERVING=1,
 HSS_RESOURCE_MODE_CHECKPOINT_PENDING=2,
 HSS_RESOURCE_MODE_PAUSED=3
};
enum {
 HSS_RESOURCE_PRESSURE_NONE=0,
 HSS_RESOURCE_PRESSURE_CONSTRAINED=1,
 HSS_RESOURCE_PRESSURE_UNSAFE=2
};
enum {
 HSS_RESOURCE_REASON_NOMINAL=0,
 HSS_RESOURCE_REASON_MEASUREMENTS_UNAVAILABLE=1,
 HSS_RESOURCE_REASON_THERMAL_FAIR=2,
 HSS_RESOURCE_REASON_THERMAL_SERIOUS=3,
 HSS_RESOURCE_REASON_THERMAL_CRITICAL=4,
 HSS_RESOURCE_REASON_MEMORY_WARNING=5,
 HSS_RESOURCE_REASON_MEMORY_CRITICAL=6,
 HSS_RESOURCE_REASON_STORAGE_WARNING=7,
 HSS_RESOURCE_REASON_STORAGE_STOP=8,
 HSS_RESOURCE_REASON_RECOVERY_HYSTERESIS=9,
 HSS_RESOURCE_REASON_CHECKPOINT_SUCCEEDED=10,
 HSS_RESOURCE_REASON_CHECKPOINT_FAILED=11,
 HSS_RESOURCE_REASON_INVALID_CONFIGURATION=12,
 HSS_RESOURCE_REASON_INVALID_MEASUREMENT=13,
 HSS_RESOURCE_REASON_OUT_OF_ORDER=14,
 HSS_RESOURCE_REASON_INVALID_TRANSITION=15
};
enum {
 HSS_RESOURCE_CHECKPOINT_SUCCEEDED=0,
 HSS_RESOURCE_CHECKPOINT_FAILED=1
};

/* Runtime measurements are supplied by the serialized native owner. The core
 * does not query iOS APIs. Every sensor has an independent status; unmeasured
 * thermal/memory values use their nominal placeholder and unmeasured storage
 * uses zero bytes. flags and reserved must be zero. */
typedef struct HSSResourceSample {
 uint32_t abi_version, struct_size;
 uint64_t sequence;
 double monotonic_time_s;
 uint32_t thermal_status, thermal_state;
 uint32_t memory_status, memory_pressure;
 uint32_t storage_status, flags;
 uint64_t free_storage_bytes;
 uint64_t reserved;
} HSSResourceSample;

/* Snapshot of the resource admission decision. input_fault is latched after
 * an invalid/out-of-order sample and blocks new metric intake until a complete
 * valid ordered sample is accepted. Preview degradation never disables metric
 * validity checks. */
typedef struct HSSResourceDecision {
 uint32_t abi_version, struct_size;
 uint64_t last_accepted_sample_sequence;
 double last_accepted_sample_time_s;
 uint32_t resource_status, mode, pressure, reason;
 uint32_t monitoring_complete, input_fault;
 uint32_t reduce_preview_quality, suspend_optional_appearance;
 uint32_t reduce_unprotected_display_lod, validity_checks_enabled;
 uint32_t accept_metric_observations, request_checkpoint;
 uint32_t pause_capture, retain_existing_sources;
} HSSResourceDecision;

typedef struct HSSEngineStats {
 uint32_t abi_version, struct_size;
 uint64_t accepted_frames, dropped_frames, accepted_commands;
 uint64_t working_bytes, pending_frames, scene_revision;
 uint32_t lifecycle_state, last_status;
} HSSEngineStats;

/* Creation copies config. On failure *out_engine=NULL. All engine calls must be
 * serialized by the owner. No callbacks or background thread are created by
 * this ABI. Mutation and polling may not race with destroy.
 * submit_* validates and copies required arrays before returning; caller can
 * release/reuse input memory after return. Copy is NOT completion of solve.
 * HSS_OK from apply_command means a command result is available, not fsync.
 * Durability belongs to the Swift journal transaction, never a C return value. */
HSSStatus hss_engine_create(const HSSConfig*, HSSEngine** out_engine);
/* Additive live reconstruction controls; existing layouts are unchanged.
 * Source origin defaults to captured (0). Set 0/1 before first successful
 * evidence submission or plan-reference control. Synthetic input MUST set 1; this is source provenance,
 * not a claim that capture accuracy has been physically validated.
 * Plan reference is caller-confirmed, rigid T_project_floor: local +X is U,
 * +Y is N and -Z is V. cut_height_m is positive (normally 1.20 m).
 * Call only when lifecycle_state==1 (idle), otherwise NOT_READY; acceptance
 * starts a bounded transactional rebuild and publishes a new scene revision.
 * A floor change never moves captured mesh. Persist/replay controls in order.
 * Without a reference, project-Y slicing is provisional and area unavailable. */
HSSStatus hss_engine_set_source_is_synthetic(HSSEngine*, uint32_t source_is_synthetic);
HSSStatus hss_engine_set_plan_reference(HSSEngine*, const HSSMat4d* T_project_floor, double cut_height_m);
/* Default reserves are the provisional B-041 policy values. observe validates
 * and applies one ordered owner-supplied sample. acknowledge records only the
 * result of a durable checkpoint completed elsewhere; it performs no I/O. */
HSSStatus hss_engine_observe_resource_sample(HSSEngine*, const HSSResourceSample*, HSSResourceDecision*);
HSSStatus hss_engine_get_resource_decision(const HSSEngine*, HSSResourceDecision*);
HSSStatus hss_engine_acknowledge_resource_checkpoint(HSSEngine*, uint32_t checkpoint_result, HSSResourceDecision*);
void hss_engine_destroy(HSSEngine*); /* NULL allowed; other double destroy invalid */
HSSStatus hss_engine_set_epoch(HSSEngine*, const HSSEpoch*);
HSSStatus hss_engine_submit_frame(HSSEngine*, const HSSFrame*);
HSSStatus hss_engine_submit_mesh_event(HSSEngine*, const HSSMeshEvent*);
HSSStatus hss_engine_submit_room_proposal_json(HSSEngine*, HSSBytes);
HSSStatus hss_engine_apply_command_json(HSSEngine*, HSSBytes);
/* Applies independently verified evidence to a previously journaled pending
 * exclusion. Raw vertices/indices and scene revision remain unchanged. The
 * batch is copied on success and is replayable; invalid or stale input has no
 * partial effect. Calls must be serialized while the engine is idle. */
HSSStatus hss_engine_apply_dynamic_evidence(HSSEngine*, const HSSDynamicEvidenceBatch*, HSSDynamicExclusionAudit*);
/* Poll copies most recent command result, UTF-8 WITHOUT trailing NUL.
 * NULL destination with capacity=0 is a size query: BUFFER_TOO_SMALL and required
 * length, unless the result has zero bytes. Never writes partial JSON. */
HSSStatus hss_engine_copy_command_result_json(const HSSEngine*, uint8_t*, uint64_t capacity, uint64_t* required);
HSSStatus hss_engine_step(HSSEngine*, uint32_t work_budget_us); /* bounded slice */
HSSStatus hss_engine_get_stats(const HSSEngine*, HSSEngineStats*);
HSSStatus hss_engine_acquire_snapshot(const HSSEngine*, HSSSnapshot**);
/* Exactly one owner per acquired handle. Release NULL allowed. Copies of the
 * pointer are borrows, not additional owners. A snapshot outlives the engine. */
void hss_snapshot_release(HSSSnapshot*);
HSSStatus hss_snapshot_get_counts(const HSSSnapshot*, HSSCounts*);
HSSStatus hss_snapshot_copy_vertices(const HSSSnapshot*, HSSVec3d*, uint64_t capacity, uint64_t* required);
HSSStatus hss_snapshot_copy_triangles(const HSSSnapshot*, uint32_t*, uint64_t index_capacity, uint64_t* required_indices);
/* One byte per raw triangle: 1=included in the derived view, 0=excluded by a
 * currently valid verified mask. This never changes copy_triangles output. */
HSSStatus hss_snapshot_copy_triangle_inclusion(const HSSSnapshot*, uint8_t*, uint64_t capacity, uint64_t* required);
HSSStatus hss_snapshot_get_dynamic_mask_summary(const HSSSnapshot*, HSSDynamicMaskSummary*);
HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot*, uint8_t*, uint64_t capacity, uint64_t* required_bytes);
HSSStatus hss_snapshot_get_plan_annotation_summary(const HSSSnapshot*, HSSPlanAnnotationSummary*);
/* Complete value-owned semantic snapshots, UTF-8 without a trailing NUL.
 * Coordinates remain metres in the accepted floor frame. These documents are
 * derived views, not replacements for scene.json or durable source records. */
HSSStatus hss_snapshot_copy_plan_json(const HSSSnapshot*, uint8_t*, uint64_t capacity, uint64_t* required_bytes);
HSSStatus hss_snapshot_copy_annotation_json(const HSSSnapshot*, uint8_t*, uint64_t capacity, uint64_t* required_bytes);
/* No function permits C++ exceptions to cross this C boundary. Pointer/count
 * validation cannot make arbitrary dangling pointers safe; valid ownership is
 * a caller precondition. Errors never overwrite the last accepted snapshot. */
#ifdef __cplusplus
}
static_assert(sizeof(HSSId)==16, "HSSId layout");
static_assert(sizeof(HSSVec3d)==24, "HSSVec3d layout");
static_assert(offsetof(HSSFrame, struct_size)==4, "message prefix");
static_assert(offsetof(HSSPlanAnnotationSummary, struct_size)==4, "plan summary prefix");
static_assert(offsetof(HSSResourceSample, struct_size)==4, "resource sample prefix");
static_assert(offsetof(HSSResourceDecision, struct_size)==4, "resource decision prefix");
#else
_Static_assert(sizeof(HSSId)==16, "HSSId layout");
_Static_assert(sizeof(HSSVec3d)==24, "HSSVec3d layout");
_Static_assert(offsetof(HSSFrame, struct_size)==4, "message prefix");
_Static_assert(offsetof(HSSPlanAnnotationSummary, struct_size)==4, "plan summary prefix");
_Static_assert(offsetof(HSSResourceSample, struct_size)==4, "resource sample prefix");
_Static_assert(offsetof(HSSResourceDecision, struct_size)==4, "resource decision prefix");
#endif
#endif

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

typedef struct HSSCounts {
 uint32_t abi_version, struct_size;
 uint64_t scene_revision, vertex_count, triangle_count;
 uint64_t boundary_node_count, boundary_edge_count, issue_count;
} HSSCounts;

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
void hss_engine_destroy(HSSEngine*); /* NULL allowed; other double destroy invalid */
HSSStatus hss_engine_set_epoch(HSSEngine*, const HSSEpoch*);
HSSStatus hss_engine_submit_frame(HSSEngine*, const HSSFrame*);
HSSStatus hss_engine_submit_mesh_event(HSSEngine*, const HSSMeshEvent*);
HSSStatus hss_engine_submit_room_proposal_json(HSSEngine*, HSSBytes);
HSSStatus hss_engine_apply_command_json(HSSEngine*, HSSBytes);
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
HSSStatus hss_snapshot_copy_scene_json(const HSSSnapshot*, uint8_t*, uint64_t capacity, uint64_t* required_bytes);
/* No function permits C++ exceptions to cross this C boundary. Pointer/count
 * validation cannot make arbitrary dangling pointers safe; valid ownership is
 * a caller precondition. Errors never overwrite the last accepted snapshot. */
#ifdef __cplusplus
}
static_assert(sizeof(HSSId)==16, "HSSId layout");
static_assert(sizeof(HSSVec3d)==24, "HSSVec3d layout");
static_assert(offsetof(HSSFrame, struct_size)==4, "message prefix");
#else
_Static_assert(sizeof(HSSId)==16, "HSSId layout");
_Static_assert(sizeof(HSSVec3d)==24, "HSSVec3d layout");
_Static_assert(offsetof(HSSFrame, struct_size)==4, "message prefix");
#endif
#endif

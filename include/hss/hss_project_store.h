#ifndef HSS_PROJECT_STORE_H
#define HSS_PROJECT_STORE_H

/* Additive local project persistence ABI. This header intentionally builds on
 * hss_api.h without changing that engine ABI. Persisted records use explicit
 * encodings; no C struct or pointer is written to disk. */
#include "hss/hss_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HSS_PROJECT_STORE_ABI_VERSION 1u

typedef struct HSSProjectStore HSSProjectStore;

enum {
  HSS_PROJECT_STORE_CREATE_NEW = 0,
  HSS_PROJECT_STORE_OPEN_EXISTING = 1,
  HSS_PROJECT_STORE_OPEN_OR_CREATE = 2
};

/* Store-specific failures occupy an additive range outside hss_api.h v1.
 * Other validation/resource/engine failures use the ordinary HSS_* values. */
enum {
  HSS_PROJECT_STORE_IO_ERROR = 100,
  HSS_PROJECT_STORE_CORRUPT = 101,
  HSS_PROJECT_STORE_PROJECT_MISMATCH = 102
};

enum {
  HSS_PROJECT_COMMAND_APPLIED = 0,
  HSS_PROJECT_COMMAND_CONFLICT = 1,
  HSS_PROJECT_COMMAND_REJECTED = 2,
  HSS_PROJECT_COMMAND_PENDING_EXCLUSION = 3
};

typedef struct HSSProjectStoreConfig {
  uint32_t abi_version, struct_size;
  HSSId project_id;
  /* UTF-8 path of an already trusted app-owned directory. The store creates a
   * child named from project_id. Bytes need not be NUL-terminated, embedded
   * NUL is rejected, and v1 accepts at most 4096 bytes. */
  HSSBytes trusted_root_directory_utf8;
  uint32_t open_mode;
  /* 0=captured/user project, 1=synthetic fixture. Immutable after creation. */
  uint32_t source_is_synthetic;
  uint64_t max_epoch_records;
  uint64_t max_depth_records;
  uint64_t max_mesh_records;
  uint64_t max_command_records;
  uint64_t max_retained_bytes;
  uint64_t max_record_bytes;
  uint64_t max_pending_bytes;
  uint32_t flags; /* v1: must be zero. */
  uint32_t reserved;
} HSSProjectStoreConfig;

typedef struct HSSProjectEventContext {
  uint32_t abi_version, struct_size;
  HSSId capture_id;
  uint64_t event_sequence;
  /* Scene revision observed after this accepted event/control fully drained. */
  uint64_t accepted_scene_revision;
  uint32_t session_epoch;
  uint32_t reserved;
  double monotonic_time_s;
} HSSProjectEventContext;

typedef struct HSSProjectStoreReceipt {
  uint32_t abi_version, struct_size;
  uint64_t event_sequence;
  uint64_t pending_event_count;
  uint64_t pending_bytes;
  uint64_t durable_revision;
  /* Always 0 on successful append: copied/received, not durable. */
  uint32_t durability_state; /* 0=received, 1=durable */
  uint32_t reserved;
} HSSProjectStoreReceipt;

typedef struct HSSProjectCheckpointResult {
  uint32_t abi_version, struct_size;
  uint64_t durable_revision;
  uint64_t durable_event_count;
  uint64_t durable_bytes;
  uint32_t durability_state; /* 1 only after manifest-last commit succeeds. */
  uint32_t reserved;
} HSSProjectCheckpointResult;

typedef struct HSSProjectReplayResult {
  uint32_t abi_version, struct_size;
  uint64_t durable_revision;
  uint64_t durable_event_count;
  uint64_t applied_event_count;
  uint64_t failed_event_sequence;
  HSSStatus engine_status;
  uint32_t used_previous_manifest;
} HSSProjectReplayResult;

typedef struct HSSProjectCommandRecord {
  uint32_t abi_version, struct_size;
  HSSProjectEventContext context;
  uint64_t before_scene_revision;
  uint64_t after_scene_revision;
  /* Applies one revision only for APPLIED. Every other durable audit outcome
   * preserves the revision and exact result bytes. */
  uint32_t disposition;
  uint32_t reserved;
  /* Exact validated UTF-8 bytes. result_json may be empty only when the engine
   * contract produced an accepted zero-byte result. */
  HSSBytes command_json;
  HSSBytes result_json;
} HSSProjectCommandRecord;

typedef struct HSSProjectPlanReference {
  uint32_t abi_version, struct_size;
  HSSMat4d T_project_floor;
  double cut_height_m;
  uint64_t before_scene_revision;
  uint64_t after_scene_revision;
} HSSProjectPlanReference;

/* Creation copies config and path bytes. It creates or validates immutable
 * checksummed project metadata according to open_mode. On failure *out_store is
 * NULL. Calls on one store must be serialized; destroy during a call is invalid. */
HSSStatus hss_project_store_create(const HSSProjectStoreConfig* config,
                                   HSSProjectStore** out_store);
void hss_project_store_destroy(HSSProjectStore* store); /* NULL allowed. */

/* Append validates, normalizes, encodes and copies an event into bounded
 * pending memory. HSS_OK here is receipt only. The input may be released after
 * return. Epoch/mesh timestamps come from context because the engine ABI does
 * not carry them; identity/sequence/epoch must exactly match the event. */
HSSStatus hss_project_store_append_epoch(HSSProjectStore* store,
                                         const HSSProjectEventContext* context,
                                         const HSSEpoch* epoch,
                                         HSSProjectStoreReceipt* receipt);
HSSStatus hss_project_store_append_frame(HSSProjectStore* store,
                                         const HSSProjectEventContext* context,
                                         const HSSFrame* frame,
                                         HSSProjectStoreReceipt* receipt);
HSSStatus hss_project_store_append_mesh_event(HSSProjectStore* store,
                                              const HSSProjectEventContext* context,
                                              const HSSMeshEvent* event,
                                              HSSProjectStoreReceipt* receipt);
/* Accepted-command journal entry. It retains exact command/result
 * bytes and before/after revision evidence; undo/redo remain ordinary appended
 * commands and never delete history. */
HSSStatus hss_project_store_append_command_record(HSSProjectStore* store,
                                                  const HSSProjectCommandRecord* record,
                                                  HSSProjectStoreReceipt* receipt);
/* Retains a caller-confirmed floor reference as an ordered control. Replay
 * drains prior engine work, applies this reference, then continues. It changes
 * interpretation and cut height, never captured coordinates or provenance. */
HSSStatus hss_project_store_append_plan_reference(HSSProjectStore* store,
                                                  const HSSProjectEventContext* context,
                                                  const HSSProjectPlanReference* reference,
                                                  HSSProjectStoreReceipt* receipt);

/* Commits every pending event as one new durable revision. Success means the
 * immutable chunks and checksummed manifest are flushed and published. Failure
 * preserves the prior committed revision and keeps pending events retryable. */
HSSStatus hss_project_store_checkpoint(HSSProjectStore* store,
                                       HSSProjectCheckpointResult* result);

/* Recovers the newest valid committed revision (or its valid predecessor),
 * validates all records before the first engine mutation, then replays them in
 * original sequence. Unresolved epochs remain unresolved. Pending memory is
 * never replayed. The caller owns and serializes engine. */
HSSStatus hss_project_store_replay(HSSProjectStore* store,
                                   HSSEngine* engine,
                                   HSSProjectReplayResult* result);

#ifdef __cplusplus
}
static_assert(offsetof(HSSProjectStoreConfig, struct_size) == 4,
              "project store message prefix");
#else
_Static_assert(offsetof(HSSProjectStoreConfig, struct_size) == 4,
               "project store message prefix");
#endif

#endif

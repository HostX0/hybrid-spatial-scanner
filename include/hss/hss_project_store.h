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
                                         const HSSFrame* frame,
                                         HSSProjectStoreReceipt* receipt);
HSSStatus hss_project_store_append_mesh_event(HSSProjectStore* store,
                                              const HSSProjectEventContext* context,
                                              const HSSMeshEvent* event,
                                              HSSProjectStoreReceipt* receipt);
/* The exact validated UTF-8 bytes are retained. Replay submits them through
 * hss_engine_apply_command_json in original event order. */
HSSStatus hss_project_store_append_command_json(HSSProjectStore* store,
                                                const HSSProjectEventContext* context,
                                                HSSBytes command_json,
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

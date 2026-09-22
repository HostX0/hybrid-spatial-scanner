# HSS local project store contract

`include/hss/hss_project_store.h` is an additive C ABI over the existing engine,
B-007 durable source store, and B-008 normalized replay. It does not change
`hss_api.h` and does not serialize native C or Swift structs.

The caller supplies a bounded UTF-8 path for a trusted app-owned directory and a
nonzero project ID. The implementation creates a project-ID child directory, so
capture-provided names never become paths. Creation writes immutable checksummed
JSON metadata containing the project ID, format version, and
`source_is_synthetic`. Reopen validates those fields before exposing records.
There is no account, cloud, or automatic upload behavior.

Epoch, normalized scene-depth frame, mesh add/replace/remove, and exact UTF-8
command bytes share one strictly increasing event sequence. Append validates and
copies into bounded pending memory. Its receipt is deliberately not a save
acknowledgement. Only a successful checkpoint returns `durability_state=1`,
after immutable records and the active manifest have been flushed and published.
A failed checkpoint leaves both the prior committed revision and pending events
available for recovery or retry. Capacity exhaustion returns
`HSS_RESOURCE_LIMIT`; retained source is never silently evicted.

On reopen, replay validates project metadata, the complete manifest, checksums,
record bounds, event ordering, epoch declarations, and mesh lineage before its
first engine call. It then calls the existing epoch, frame, mesh, and command
entry points in stored order. An unresolved epoch is replayed as unresolved and
does not acquire a fabricated transform. The result reports the durable revision,
whether recovery used the previous manifest, the applied count, and the exact
failing event sequence/status when the engine rejects an otherwise valid record.

The default implementation limits remain within the D16 package bounds: paths
at 4096 bytes, individual records at 256 MiB, JSON commands at 16 MiB, and total
retained source at 8 GiB. Caller-selected limits may be smaller. Every declared
size/count/product is checked before allocation. No successful receipt, engine
application, progress animation, or in-memory state may be labeled Saved.

Swift should import this header through the native module, wrap the opaque store
in one serialized owner, retain no borrowed buffers after a call, and display a
saved state only after `hss_project_store_checkpoint` returns `HSS_OK` with
`durability_state=1`. Use `OPEN_EXISTING` for reopen so a missing or mismatched
project cannot be replaced by an empty one.

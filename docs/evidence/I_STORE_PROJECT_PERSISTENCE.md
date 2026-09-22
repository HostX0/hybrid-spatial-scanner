# I-STORE evidence — native project persistence facade

Date: 2026-09-22

Branch: `hss/project-persistence`

Baseline: `ae9b61e`

## Delivered contract and implementation

`include/hss/hss_project_store.h` is an additive C/Swift-importable facade. It
creates or opens one project below a trusted app-owned root using the immutable
project UUID as the directory name. `project.json` stores the UUID, format, and
captured-versus-synthetic source flag as checksummed JSON. No captured name is a
path and there is no account or cloud behavior.

The facade converts owned ABI inputs to portable records rather than dumping C
structs. B-008 encoding remains authoritative for epochs, normalized calibrated
scene-depth frames, and mesh add/replace/remove events. The new control encoder
retains accepted commands with exact command/result UTF-8 bytes and before/after
scene revisions, plus confirmed floor-reference matrices and metre cut heights.
Every record carries the original capture ID, epoch, sequence, monotonic time,
and accepted scene revision.

The v1 engine frame ABI has no RGB fields. Its normalized replay adapter records
the explicit `NONE` RGB-layout sentinel and no RGB bytes; it does not invent an
image. Depth values, confidence availability, intrinsics, pose, dimensions, and
source timing remain authoritative.

Append returns a bounded in-memory receipt with `durability_state=0`. Checkpoint
is the only API that returns `durability_state=1`; it reuses B-007 immutable
chunks and manifest-last publication. A failed checkpoint preserves the prior
commit and leaves pending records retryable. Disk retention and pending heap
limits are separate. Hitting either limit returns `HSS_RESOURCE_LIMIT` and does
not evict retained evidence.

Reopen validates immutable project metadata, the complete manifest, chunk
lengths/checksums, normalized/control record bounds, source metadata agreement,
strict event order, epoch declarations, mesh lineage, and exact revision
progression before mutating an engine. Replay then performs a second bounded
pass, reading one record at a time. It sets source provenance before evidence,
drains after every source event, drains around controls, and verifies the stored
scene revision and accepted command result. An unresolved epoch is replayed
unchanged.

## Native integration

Import `HSSContracts`; `include/module.modulemap` exports both C headers. Create
an `HSSProjectStoreConfig` with an absolute app-owned root, project ID, open mode,
and explicit disk/pending limits. After each engine event has fully drained,
append the matching event plus `HSSProjectEventContext.accepted_scene_revision`.
For commands, append `HSSProjectCommandRecord` only after acceptance and result
copy-out. Call checkpoint before displaying Saved. On reopen, create a fresh
engine with the same project ID, then call `hss_project_store_replay` before
continuing capture.

The final integrated native target must also contain I-CORE commit
`be548c8b9c8dc6293419c6b8464665261a48efa3`, which supplies the canonical
`hss_engine_set_source_is_synthetic` and `hss_engine_set_plan_reference`
declarations/implementations. This branch intentionally does not edit its owned
`hss_api.h` or `hss_engine.cpp` files. Weak host references let the independent
store slice build; synthetic or floor-control replay returns
`HSS_UNSUPPORTED_CAPABILITY` until that core commit is present.

## Focused host evidence

Configured and built with AppleClang 21.0.0.21000334 and ASan/UBSan:

```text
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B /tmp/hss-project-store-build \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build /tmp/hss-project-store-build \
  --target hss_project_store_test hss_durable_source_store_test hss_normalized_replay_test --parallel
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir /tmp/hss-project-store-build --output-on-failure \
  -R 'hss_(project_store|durable_source_store|normalized_replay)$'
xcrun swiftc -typecheck -I include \
  -module-cache-path /tmp/hss-project-store-swift-cache -
```

Observed result: 3/3 tests passed. The project-store test uses a deterministic
engine-contract fixture because the core owner commit is intentionally outside
this branch. It proves two checkpoints, process-style destroy/reopen, exact
seven-event replay order and revision progression, unresolved epoch retention,
mesh removal, command/result association, floor control, explicit pending-memory
resource pause, corrupt-active fallback, and survival of the prior two-event
checkpoint. Existing B-007/B-008 tests continue to pass.
The inline Swift smoke imported `HSSProjectStoreConfig`,
`HSSProjectEventContext.accepted_scene_revision`, create, and destroy through
`HSSContracts` with no diagnostics. `git diff --check` produced no output.

## Limits

This is host/POSIX and synthetic contract evidence. It is not an iPhone
protected-data, low-storage, process-kill, ARKit, physical metrology, or device
thermal result. It does not serialize derived/native snapshots: retained source
and accepted controls are canonical and rebuild the scene. The native owner must
run the combined simulator build after integrating I-CORE and then reserve
physical save/reopen evidence for the owner-authorized iPhone 15 Pro Max run.

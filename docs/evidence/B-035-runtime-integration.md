# B-035 runtime integration evidence

Date: 2026-09-22

Branch: `hss/b035-runtime-integration`

Baseline: `811022708fab3152ad8c0e704d3984eeb38e69fd`

## Runtime behavior

The live engine now retains a bounded provenance mirror only after the matching
epoch or mesh event has committed to the immutable reconstruction snapshot. A
typed `HSSDynamicEvidenceBatch` can target a previously journaled
`exclude_dynamic_region` command while the engine is idle. The batch must match
the command, project, immutable scene revision, captured source identity,
capture identity, epoch, event sequence, and project-space bounds before the
portable B-035 module can produce an exclusion.

The engine never replaces the captured vertex or triangle buffers. Snapshot
consumers continue to receive every raw triangle through
`hss_snapshot_copy_triangles` and separately receive one inclusion byte per raw
triangle through `hss_snapshot_copy_triangle_inclusion`. A mask summary reports
included, excluded, applied, and stale counts. If source replacement or any
other validation failure makes a retained verification stale, the derived view
returns those triangles as included and increments the stale count. It does not
reuse an old triangle index or hide the replacement source.

Evidence input is normalized by evidence identity before evaluation. Successful
application retains the normalized typed records, counts, disposition, and a
deterministic FNV-1a fingerprint over the complete per-triangle mask. A
byte-identical retry returns the prior audit. A differing retry for the same
exclusion command returns `HSS_DUPLICATE_MISMATCH`.

## Durable audit and replay

`hss_project_store_append_dynamic_exclusion_record` appends a nonmutating,
accepted-control record after the pending command. Control encoding major 1 now
has a critical minor-2 dynamic-exclusion record type. It stores the typed
evidence identities, source linkage, bounds, evidence kinds, scene revision,
audit counts, disposition, and mask fingerprint. Checkpoint remains
manifest-last.

Reopen validates the whole typed record before the first engine call. Replay
rebuilds source geometry, replays the pending command, reapplies the dynamic
evidence at the recorded revision, and compares revision, counts, disposition,
and mask fingerprint. A mismatch fails replay instead of publishing a guessed
mask. Existing projects without this record remain byte-compatible. A reader
that does not implement control minor 2 rejects a project containing the new
critical record; there is no lossy downgrade that drops its audit.

## Additive contract impact

The existing ABI-v1 layouts and functions are unchanged. `hss_api.h` adds the
fixed-layout evidence, batch, audit, and mask-summary messages plus three entry
points:

- `hss_engine_apply_dynamic_evidence`
- `hss_snapshot_copy_triangle_inclusion`
- `hss_snapshot_get_dynamic_mask_summary`

`hss_project_store.h` adds one fixed-layout durable record and
`hss_project_store_append_dynamic_exclusion_record`. Input arrays are copied;
no borrowed buffer survives a call. Dynamic verification does not increment
the scene revision because it changes a derived visibility layer only.

## Focused synthetic coverage

`hss_dynamic_runtime_integration_test` authors two captured triangles backed by
one accepted mesh-anchor revision. It verifies that a pending command alone
excludes nothing, wrong-capture evidence has no effect, verified evidence masks
only the spatially and temporally matched triangle, raw vertices and indices
remain value-for-value unchanged, and stale revision submission has no partial
effect. It checkpoints and reopens the typed audit, compares the same raw mesh
and inclusion vector after deterministic replay, then replaces the source
revision and verifies the old mask fails safe to fully included geometry with a
stale count.

The focused sanitizer run also includes the isolated B-035 module, engine
contract, live scene-edit adapter, project store, and C ABI smoke tests.

## Host verification

AppleClang 21.0.0.21000334 and CMake 3.31.6 were used with AddressSanitizer and
UndefinedBehaviorSanitizer enabled:

```sh
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B /tmp/hss-b035-runtime-asan \
  -DHSS_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build /tmp/hss-b035-runtime-asan \
  --target hss_dynamic_runtime_integration_test hss_dynamic_provenance_test \
           hss_engine_scene_editing_adapter_test hss_project_store_test \
           hss_engine_contract_test abi_smoke --parallel
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir /tmp/hss-b035-runtime-asan \
  -R '^(hss_dynamic_runtime_integration|hss_dynamic_provenance|hss_engine_scene_editing_adapter|hss_project_store|hss_engine_contract|abi_smoke)$' \
  --output-on-failure
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build /tmp/hss-b035-runtime-asan --parallel
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir /tmp/hss-b035-runtime-asan --output-on-failure
git diff --check
```

Observed result: configuration and focused build exited 0 with warnings treated
as errors. Focused CTest passed 6/6 under ASan and UBSan. The complete host
build then exited 0 and complete CTest passed 36/36 under the same sanitizer
configuration. `git diff --check` exited 0 with no output.

## Unverified layers

No simulator or iPhone build ran. No ARKit motion classifier, physical moving
person/curtain/screen capture, renderer adoption, exporter adoption, memory or
thermal profile, metrology trial, or native app save/reopen flow was exercised.
The test evidence is synthetic host geometry and validates runtime contracts,
not device sensing accuracy or production dynamic-object detection quality.

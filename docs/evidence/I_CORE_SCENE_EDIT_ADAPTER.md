# I-CORE scene-edit adapter evidence

This change connects the existing revisioned scene-edit processor to the live C facade without changing `hss_api.h` layouts or function signatures. One serialized processor belongs to each engine. It receives the immutable current scene JSON and exact command bytes. Only an applied candidate publishes a new immutable snapshot; the captured Float64 vertices, triangle indices, and source provenance are retained separately and copied unchanged.

`hss_engine_copy_command_result_json` now returns the exact processor result after every processed command, including conflicts, rejections and pending exclusions. Before any command has been processed it remains `HSS_NOT_READY`. Commands submitted while source work is queued or rebuilding return `HSS_NOT_READY`, avoiding an edit against an incoherent snapshot.

The adapter maps applied/stored retry to `HSS_OK`, conflicts to `HSS_CONSTRAINT_CONFLICT`, duplicate mismatches to `HSS_DUPLICATE_MISMATCH`, stale/target lineage errors to `HSS_STALE_REVISION`, pending typed dynamic exclusions to `HSS_UNSUPPORTED_CAPABILITY`, and resource exhaustion to `HSS_RESOURCE_LIMIT`. Pending exclusions remain typed processor records only; B035 source masking is deliberately not implemented.

The durable replay path accepts nonmutating command dispositions as audit controls: it verifies the exact result bytes and that scene JSON is unchanged even when the engine returns its normal conflict/rejection/pending status. It still rejects a status without an exact matching stored result. This permits reopen replay to reconstruct accepted source first and then replay each committed command exactly once.

Focused host validation used the bundled host CMake with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B work/build -DHSS_ENABLE_SANITIZERS=ON -DHSS_BUILD_TESTS=ON
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build work/build --target hss_engine_scene_editing_adapter_test \
  hss_engine_contract_test hss_live_reconstruction_test hss_project_store_test -j4
work/build/hss_engine_scene_editing_adapter_test
work/build/hss_engine_contract_test
work/build/hss_live_reconstruction_test
work/build/hss_project_store_test
```

All checks passed with no sanitizer report. The adapter regression covers captured epoch/mesh/floor baseline, an auditable pending dynamic exclusion that leaves the snapshot unchanged, an accepted `mark_unknown` edit, exact-result retry, captured-mesh equality before/after edit, checkpoint/reopen, and deterministic source-then-command replay. The live reconstruction test remains synthetic and reported a maximum observed 50 µs requested host-ASan slice of 1,840.38 µs; it is cooperative work-quantum evidence, not a device latency or metrology claim.

No iPhone build, physical capture, device thermal/memory test, B035 dynamic masking, or reconciliation of a later source rebuild against an existing edited overlay was performed here. A later source mutation must use the command lineage/reconciliation path rather than silently treating edited model geometry as captured mesh.

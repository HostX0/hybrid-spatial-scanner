# B-041 runtime resource-policy integration evidence

Date: 2026-09-24

Branch: `hss/i-resource-core`

Baseline: `8a36c25`

## Delivered boundary

The existing portable `hss::resource::ResourcePolicy` now controls engine
admission through an additive ABI. The single serialized owner can submit
ordered measured/unmeasured thermal, memory, and storage samples, query the
current decision, and acknowledge a checkpoint result completed by its durable
storage layer. No existing ABI layout or function changed.

Constrained mode drops smoothed preview frames but keeps calibrated depth and
mesh metric intake with validity checks enabled. Checkpoint-pending and paused
modes stop new calibrated depth and mesh add/replace events with
`HSS_NOT_READY`. Already queued work continues to drain and accepted source is
retained. Epoch transitions, mesh removals, commands, plan-reference controls,
dynamic-evidence commands, and durable checkpoint acknowledgements remain
protected. Invalid or out-of-order samples latch a fail-closed input fault;
unmeasured samples cannot clear it.

The core uses the provisional B-041 default storage reserves. It does not read
iOS resource APIs, schedule sampling, perform checkpoint I/O, claim that a
checkpoint is durable, or evict retained source.

## Focused host verification

Configured and built with AppleClang 21.0.0.21000334, AddressSanitizer and
UndefinedBehaviorSanitizer:

```text
CC=clang CXX=clang++ .../host-venv/bin/cmake -S . \
  -B /tmp/hss-i-resource-core-build-2 \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
.../host-venv/bin/cmake --build /tmp/hss-i-resource-core-build-2 \
  --target hss_resource_runtime_integration_test hss_resource_policy_test \
  hss_engine_contract_test hss_live_reconstruction_test \
  hss_engine_scene_editing_adapter_test abi_smoke --parallel 4
UBSAN_OPTIONS=halt_on_error=1 .../host-venv/bin/ctest \
  --test-dir /tmp/hss-i-resource-core-build-2 --output-on-failure \
  -R '^(hss_resource_runtime_integration|hss_resource_policy|hss_engine_contract|hss_live_reconstruction|hss_engine_scene_editing_adapter|abi_smoke)$'
```

Result: 6/6 focused tests passed. The integration test covers initial
unmeasured status, constrained preview degradation, metric validity/intake,
unsafe gating, pre-pressure queue drain, source retention, protected removal
and epoch transitions, protected command/plan controls, checkpoint
acknowledgement, recovery hysteresis, and fail-closed stale, invalid, and
unmeasured samples. macOS ASan leak detection is unsupported, so
`detect_leaks` was omitted; ASan/UBSan instrumentation remained enabled.

## Remaining device dependency

No iPhone test was run. Native adoption must sample and serialize actual iOS
thermal, memory-pressure, and available-capacity signals, execute and fsync the
B-038 checkpoint before acknowledging it, and verify ordering, backgrounding,
protected-data, low-storage, and real-pressure behavior on declared devices.

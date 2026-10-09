# Independent seam characterization (ENG-INDEPENDENT-SEAM-CHARACTERIZATION-01)

Status: locally committed characterization in an isolated source workspace; it is not published or merged. It is not a device, capture, or physical-accuracy result.

## Scope and frozen inputs

This experiment's isolated checkout started at integration commit `eb774607c248cdd8e7fe46a82094dd4903d54c04`; the harness and this evidence are a local commit pending remote review/publication. It does not modify `SpatialCore/src/live_reconstruction.cpp`, `kJoin`, or any production fitting or topology threshold.

The executable accepts `--independent-seams` and submits all observations through the public C ABI. Each profile uses one mesh anchor per face, retains the anchor-local Float32 payload, and derives expected project coordinates from the submitted `T_world_anchor` translation. Reference geometry is used only by the post-reconstruction scorer; it is never passed into the engine.

The predeclared profiles are:

| Profile | Purpose | Observation model |
| --- | --- | --- |
| `clean-translation-control` | Transform-only control | Exact shared seams; nonidentity anchor translations; no face bias |
| `independent-development` | Development case | Independent face/anchor planar and range-dependent biases |
| `independent-predeclared-variant` | Predeclared synthetic variant | Different seed, camera/range relation, tessellation, and independent biases; not an independent physical dataset |
| `negative-controls` | Safety control | A real 0.45 mm gap plus disconnected near-parallel faces and fixture-side multiple candidates inside 1 mm |
| `high-frequency-nonplanarity` | Separate fallback characterization | Deterministic nonplanar wall; it is not used as seam evidence |

Every seam profile is run with 1 µs and 50 µs cooperative work budgets both before and after a deterministic triangle permutation. The test requires exact derived Scene equality across all four scheduling/permutation cases, exact raw vertex and triangle retention, valid Scene JSON, no unmatched reference edge, no non-manifold boundary node, and at least 98% coverage of every declared reference cut. Its scorer tolerance is derived from the actual maximum `abs(camera-to-face range − 2 m)` among the declared reference faces and a two-face displacement bound; it is a fixture-scoring bound, not an engine tolerance.

The negative control also measures graph connectivity. It requires four separate boundary components, so an erroneous degree-two connection across the real 0.45 mm gap cannot pass merely because every resulting edge remains near a reference line.

The implementation deliberately labels fixture candidate counts as `fixture_ambiguous_endpoints`. The engine has no correspondence-candidate stage, so its candidate-ambiguity metric is explicitly reported as `unavailable` rather than invented.

## Local execution and results

Command:

```sh
./build-engine-macos/hss_live_permutation_regression_test --independent-seams
```

The local AppleClang Debug build used ASan and UBSan. The run completed with exit status zero on 2026-10-10. Timing is host-only diagnostic information, not iPhone latency evidence.

| Profile | Boundary result | Safety/coverage result |
| --- | --- | --- |
| clean translation control | 0 endpoints, 1 component, 1 region | 0 false bridges; minimum cut coverage `0.999999904633` |
| independent development | 16 endpoints, 8 components, 0 regions | 0 false bridges; minimum cut coverage `0.998653729757` |
| independent predeclared variant | 16 endpoints, 8 components, 0 regions | 0 false bridges; minimum cut coverage `0.998288154602` |
| negative controls | 8 endpoints, 4 components, 0 regions | 0 false bridges; fixture max candidate count 3 |

The measured maximum reference deviations were approximately 0.284 mm for the development profile and 0.372 mm for the predeclared variant. The clean control closed correctly through nonidentity anchor translations. Independent observations remained conservatively open; no false bridge or false closure was observed in the frozen negative case.

The high-frequency nonplanar input emitted 75 planes, 2 mesh patches, and one `partial_plane_support` issue. That result is recorded as a conservative fallback characterization, not an accuracy claim or a seam defect.

Limitations are deliberate: the seam fixture is translation-only (no anchor rotations), uses a whole-face deterministic planar bias rather than per-point sensor noise, and contains no real ARKit tracking, motion, or physical capture. It is evidence about this bounded synthetic observation model only.

## Durable C ABI, project-store, and export coverage

The clean translation control and independent predeclared variant were also each passed through:

1. C ABI epoch, plan-reference, and all mesh-anchor events;
2. exact raw-coordinate/index verification and OBJ/MTL export;
3. a zero-target `mark_unknown` command with exact command/result bytes;
4. project-store checkpoint and resume-cursor verification;
5. store reopen and replay into a new engine; and
6. byte-equivalent Scene, raw geometry, command result, and OBJ/MTL artifact comparison after replay.

Both streams completed 12 durable events successfully. The exporter verified that it retained only declared observed source ranges; the fixture's one absent ceiling panel was not fabricated as raw geometry. This is local persistence and export evidence only. It is not a claim that an iPhone capture was installed, launched, or physically verified.

## Pending-store transaction regression

Final review found that `append_encoded` accounted for `pending_bytes` before its fallible `pending.push_back`. An allocation failure could therefore return `HSS_RESOURCE_LIMIT` with phantom pending capacity consumed. The production order now publishes the vector record before its byte accounting.

`hss_project_store_test` compiles a test-only fault point at that precise publication boundary. It establishes the encoded size in a baseline store, sizes a second store exactly to that size, injects the append failure, then retries the identical epoch. The retry succeeds with one pending record and one checkpoint; it would fail with a phantom byte count. The hook is not compiled into `SpatialCore` production builds.

## Decision

This work does **not** demonstrate a production seam-tolerance defect. It demonstrates the current conservative behavior under independently displaced synthetic face observations: unmatched endpoints stay open. It also demonstrates that broadening a join tolerance cannot be assumed safe, because the frozen negative control contains close but genuinely disconnected geometry. The 0.45 mm gap is not a measured unsafe threshold: this experiment does not run a widened production threshold and must not be interpreted as one.

The engine presently has no cross-anchor correspondence search, adjacent-cell candidate stage, or ambiguity decision to tune. A future attempt to connect independent seams would need a separately specified correspondence feature with explicit ambiguity, provenance, and false-closure acceptance criteria. It is not justified as a one-line tolerance increase.

## Readiness consequence

Engine investigation is finite for this handoff milestone: no engine rewrite and no production threshold change are warranted by this evidence. The paired native implementation slice exposes the existing safe, zero-target `mark_unknown` command from frozen review and retains its exact command/result bytes for checkpoint/reopen. After the transaction correction, the portable CTest suite ran 59/59 and the complete XCTest suite ran on an iPhone Simulator with 72 passing tests, including the exact command/checkpoint/reopen/replay and whitespace-rejection cases. A generic iPhoneOS build also succeeded with code signing disabled. Device installation remains separately blocked by the missing authorized development signing team and matching provisioning profile.

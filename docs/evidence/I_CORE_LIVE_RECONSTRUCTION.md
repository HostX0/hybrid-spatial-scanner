# I-CORE live C facade reconstruction — host evidence

Branch: `hss/live-core`, starting at `ae9b61e`. Executed 2026-09-22 on the macOS host with AppleClang 21.0.0.21000334, C++20, AddressSanitizer and UndefinedBehaviorSanitizer. These are synthetic source-shape integration results, not iPhone metrology, ARKit capture quality, sustained mobile latency, visual SLAM or TSDF evidence. The task deliberately uses platform captured-mesh evidence instead of introducing another reconstruction stack.

## Implemented path

The existing submit/step/acquire/copy C facade now publishes actual accepted mesh evidence transformed anchor-local → world → project. Calibrated depth metadata/payloads are retained separately; smoothed depth is preview evidence and neither stream creates independent geometry in this backend. Current anchor revisions replace atomically, removals retain tombstones, new generations remain distinct, and unresolved epochs retain local evidence without contributing project geometry. Accepted transform revisions rebuild retained source coordinates.

A cooperative state machine builds candidates and publishes immutable coherent mesh/scene snapshots. Each accepted event/control adds exactly one scene revision regardless of work-budget scheduling. Failed input, stale lifecycle, invalid transformed extent and resource exhaustion preserve the previously published state. Snapshot leases outlive engine destruction and keep conservative memory reservations until release.

Connected near-coplanar triangle patches feed the existing `robust_plane_fit` module (bounded 96 samples, 24 candidates, four refinement iterations, 4,096 residual evaluations). A 0.002 m inlier threshold is an implementation tuning policy, not a sensor-accuracy claim. Measured triangle intersections at the selected cut produce supported fragments with arbitrary angles and narrow returns. The existing `boundary_graph`, `topology_protection` and `ring_net_area` modules validate eligible closed contours and calculate their provisional section area. No convex hull, gap extension, Manhattan snapping or mesh flattening is performed. Unclassified 3D patches and every accepted triangle remain available even if metric extraction cannot classify them.

Two coordinator-approved additive C functions configure source provenance and confirmed floor/cut reference. Existing structs/layouts/functions are unchanged; `docs/D16_Contracts.md` records lifecycle, scheduling and replay semantics. A runtime floor change rebuilds the plan and never moves captured mesh.

## Commands executed and observed results

From the assigned isolated worktree, using `HOST=/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin` below as a descriptive shorthand:

```sh
$HOST/cmake -S . -B work/build -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
$HOST/cmake --build work/build --target hss_live_reconstruction_test hss_engine_contract_test hss_bounded_ingress_test -j4
work/build/hss_live_reconstruction_test work/live-scene.json
work/build/hss_engine_contract_test
work/build/hss_bounded_ingress_test
$HOST/python tests/check_live_scene.py work/live-scene.json
git diff --check
```

All listed focused tests/checks passed. No sanitizer diagnostics were emitted. The final live test reported maximum observed 50 µs requested slice duration of **1,193.42 µs** under host ASan. This confirms cooperative bounded quanta, **not** a 50 µs hard deadline. Clock checks occur between quanta; fit/contour validation and bounded setup/cleanup can overrun the request. A one-microsecond step was verified to leave a nonempty mesh reconstruction pending instead of publishing the whole scene synchronously.

The final emitted `scene.json` passed Draft 2020-12 schema validation with UUID format checks, unique IDs, node/surface references, unit fitted normals, explicit synthetic provenance and exact revision `3`. Independent fixture shoelace area: **28.925 m²**. Emitted area: **28.924999952316284 m²**, consistent with the Float32 mesh input; the test tolerance is a host arithmetic check, not a field accuracy threshold.

The first compile found an admission-helper/local-variable name collision and then an unused JSON-copy helper after unsupported-command handling was made explicit. Both were repaired before the focused run. Later focused repetitions were limited to actual implementation/test changes: the protected area calculator, exception-safe queue lease transfer, source-control ordering, nested-loop ambiguity, and additional captured topology cases. No broad suite or reviewer agents were run.

## Exercised acceptance cases

- Synthetic oblique room with a 0.15 m narrow front and 0.50 m return: exact triangle topology copied through the C ABI, nonempty live plane/slice output, concavity retained and independent section area checked.
- Caller buffers changed after submission: published vertices retain the copied source. Undersized triangle and JSON copies return required capacity without partial output.
- Partial/open replacement: old triangles disappear from the new snapshot, no closing edge/region appears, old snapshot stays unchanged.
- Bad index/nonfinite input and skipped revision: explicit rejection, no accepted-state corruption.
- Remove-current-revision, stale replacement of a tombstone, and re-add revision 1: correct live lifecycle and generation provenance.
- Unresolved separate epoch excluded from project geometry; accepted transform later restores the retained anchor at the exact composed coordinate; out-of-range accepted-transform candidate rejected atomically.
- Floor confirmation after capture enables provisional section area; changing floor reference changes only section interpretation, while raw shelf/sloped coffer heights stay intact.
- Nested closed patches retain empty hole lists, explicit unresolved classification and null area. No missing semantic classification becomes an invented fixed-obstruction subtraction.
- Calibrated sceneDepth and smoothed preview payloads remain retained separately and create no fabricated mesh.
- Same source events plus floor control with 50 µs versus 100,000 µs scheduling produce byte-identical scene JSON, revisions and IDs.
- Snapshots remain readable after engine destruction. Pinned historical versions eventually cause explicit memory backpressure without exceeding the configured conservative reservation counter.

## Limits and next integration

This is functional provisional mesh-to-section reconstruction, not a completed scanner acceptance gate. The mesh source has no surface-classification field; surfaces remain semantically unknown. A single fully validated closed section after confirmed floor reference may expose provisional section area, not a verified finished-room floor footprint. Multiple/ambiguous/open/over-limit contours expose unavailable area. No automatic hole subtraction is justified yet. There is no sensor covariance estimate, so uncertainty remains null.

The runtime caps are explicit: 32 captures/epochs, 128 anchor identities including tombstones, 256 retained depth frames, 65,536 active project vertices/triangles, 2,048 metric patches/edges and 128 edges per contour-validation quantum. Native should submit selected calibrated keyframes, and persistence owns full history. Backpressure is required instead of silently evicting irreplaceable evidence. Reservations include conservative source, scratch and pinned snapshot allowances; fixed facade bookkeeping is excluded and the reported figure is not measured RSS.

Node coordinates and source identities generate deterministic derived UUIDs; replaced measured geometry is a new proposal. The forthcoming command processor must preserve accepted edited-model overlays or report stale/conflicting lineage instead of silently overwriting edits during capture rebuild. Command and room-proposal execution are currently explicitly unsupported, not accepted and discarded. Next dependencies are coordinator integration with I-STORE/I-NATIVE, then the verified I-EDIT processor seam and device validation. No iOS/device build, iPhone 15 Pro Max physical capture, ARKit accuracy, sustained memory/thermal run, held-out metrology or full broad host suite was executed here.

# ENG-REAL-RGBD-APPEARANCE-GUIDANCE-01

Engineering evidence, 2026-10-11. This is not a customer product preview or a
claim of physical measurement accuracy. The owner authorized server work and
light source editing, but explicitly paused all Mac compilation and simulators.
No Mac build, simulator, phone installation or private-capture upload was used.

## Finite objective and verdict

Evaluate the existing engine with licensed real observations, fix demonstrated
defects, then connect the existing appearance and guidance components to the
offline application. Do not reopen the completed seam investigation, widen its
tolerances, replace the engine or turn this into an indefinite synthetic loop.

The real-data source/replay/export gate passed on one development and one held-out
sequence. Portable appearance/guidance repairs passed the complete Linux suite.
Native color capture, local persistence, review and opt-in export are connected
in source, as is current-frame depth guidance. Native compilation, raster/UV
alignment, end-to-end UI, packaged privacy and exact Release acceptance remain
unexecuted for this new stack. This is a dependency-ready source milestone, not
an App Store-ready scanner or a completed native textured-twin acceptance.

## Lawful data and retained provenance

The [TUM RGB-D Benchmark](https://cvg.cit.tum.de/data/datasets/rgbd-dataset) is
licensed CC BY 4.0. Attribution: J. Sturm, N. Engelhard, F. Endres, W. Burgard,
D. Cremers, *A Benchmark for the Evaluation of RGB-D SLAM Systems*, IROS 2012.
The [provider format](https://cvg.cit.tum.de/data/datasets/rgbd-dataset/file_formats)
describes 640 x 480 RGB/depth, depth divisor 5000, missing depth zero, registered
depth and default registered-image intrinsics (525,525,319.5,239.5). The provider's
depth correction is already applied; no second scaling or one-sided undistortion
was applied here.

Server workspace: `/home/abdulazeez/hss-work/codex-real-capture-0vqlhzyw`.
Original archives, saved provider/license pages and `provenance.json` remain
outside Git. No images or source archives are committed. HTTPS/TLS validation,
archive bounds and member checks precede extraction; only the predeclared
selected pairs and required metadata were extracted.

| Sequence and role | Archive bytes | SHA-256 | Predeclared pairs |
| --- | ---: | --- | ---: |
| freiburg1_xyz, development | 448204271 | a0236d97b8c30cd93b653656d2b6c293ff7c982a4130ef2a1a8beecdb124ef98 | 24 |
| freiburg1_room, held out | 782381450 | 5ace47a1d2e53696bc939a84999293a04a7226958e848a609666691fd3cc38da | 24 |

Pairs are uniformly selected before engine evaluation, RGB association is bounded
to 20 ms, and separate RGB/depth poses use bounded reference interpolation with
no extrapolation. CV optical coordinates are converted to AR camera convention.
The project frame is the first accepted camera, not a confirmed gravity/floor
reference. `tools/prepare_tum_rgbd.py` emits an evaluation-only coarse source mesh
at stride 32, rejects missing/depth-discontinuous cells and retains source pixel
links. It does not reconstruct a native ARKit mesh, fill holes or estimate SLAM.

This data is real Kinect observation data with supplied motion-capture poses;
it is not iPhone LiDAR, surveyed architectural truth or an engine tracking test.
Other candidate datasets were researched but not silently relabeled/acquired as
commercially usable real evidence. In particular, noncommercial-only data is not
used for this product's development.

## Executed engine observations

`tools/evaluate_real_rgbd.cpp` invokes the actual engine and project store. For
each scene it accepts 24 depth observations and 24 separately derived source
anchors, checks all emitted positions against transformed source coordinates
within 1e-12 m and exact triangle indices, checkpoints 49 source events, destroys
both owners, reopens/replays and compares exact JSON and raw geometry. An actual
OBJ/MTL bundle is produced. Independent source anchors can overlap; they are not
native anchor replacement or a fused, surveyed room mesh.

| Strict-pose run | Vertices / triangles | Reserved working bytes peak | Host elapsed s | Source/reopen/consumer |
| --- | ---: | ---: | ---: | --- |
| freiburg1_xyz | 5111 / 3014 | 48618360 | 125.532268 | passed |
| freiburg1_room | 5118 / 1624 | 44400528 | 98.401674 | passed |

These are GCC 13.3 Debug ASan+UBSan end-to-end evaluation/replay times, not phone
frame latency or process RSS. The development scene reached `metric_work_limit`
(2072 candidate patches versus the existing 2048 cap); raw geometry was retained
and unavailable metric output remained unavailable. The held-out scene reported
`open_or_ambiguous_slice`. Neither had a confirmed floor. No numeric area/accuracy
or closed finished-room claim was invented. These bounded limitations are part
of the readiness assessment, not evidence for widening geometry tolerances.

An initial evaluation failure concerned evaluator warning-code syntax and its
MTL companion naming, not an engine reconstruction defect. Its baseline log is
retained. The corrected evaluator passed both scenes before appearance work.

## Color admission: preserved failure, not optimistic success

Early exploratory colored exports used a common-camera-pose assumption. They
decorated 2968/3014 and 1335/1624 faces and passed saved-file topology/material
checks. That does **not** establish physically aligned texture. Original results
remain under `engine-freiburg1_xyz-appearance` and
`engine-freiburg1_room-appearance`, explicitly superseded for admission evidence.

`tools/measure_tum_pair_motion.py` independently used the separately retained
reference poses. The paired cameras were not coincident:

| Sequence | Max time gap ms | Max translation mm | Max rotation deg | p95 / max reprojection shift px |
| --- | ---: | ---: | ---: | ---: |
| freiburg1_xyz | 15.7499 | 6.7729 | 0.2890 | 2.8342 / 4.9613 |
| freiburg1_room | 17.0519 | 8.6189 | 1.0043 | 7.4003 / 11.9147 |

This is a public-data pairing diagnostic, not calibrated image or device accuracy.
The evaluator now passes both actual poses rather than overwriting the color
pose. The portable projector only supports proven coincident camera centers and
orientations; unsupported cross-camera visibility becomes unknown. Both strict
outputs therefore correctly contain **zero colored faces and zero packaged
images**, with all source geometry and replay intact. No pose epsilon was widened.
This proves rejection of unsupported data, not native RGB/depth alignment.

The production portable projector validates source revision and accepted
transform revision, calibration and asset identity/digest, rigid camera frames,
image bounds and perspective-correct depth support across the entire triangle.
Missing or foreground depth remains neutral. Every visited bounding-box pixel
is charged to a finite total-work budget; exhaustion returns no partial claimed
verified result. Multiple faces may share one exactly matching texture reference;
conflicting identities, paths or digests fail. UVs never move metric geometry.

## Demonstrated repairs and application changes

1. Stale guidance: old evidence could survive debounce/minimum-display transition
   after current quality changed. The policy now invalidates it immediately and
   binds a new recommendation to the current quality revision. Partial adapters
   explicitly mark available facts; unknown fit/coverage/view metrics are not
   invented. Native source only reports measured current-frame depth support,
   clears advice on rejection/tracking loss/stop and expires it after 1.5 s without
   fresh evidence. No haptic or capture-control authority was added.
2. Shared-image export: valid bindings of multiple faces to the same exact image
   were rejected by the existing texture uniqueness check. Identical references
   are now deduplicated, while identity/path/digest conflicts still fail closed.
3. Native OBJ naming: existing native output wrote `scene.mtl` while its OBJ named
   `capture.mtl`. An additive, strictly validated material-companion option lets
   both native export paths name the actual file. Legacy portable default remains.
4. Evaluation admission: separate real RGB/depth poses are now respected; the
   exploratory color result is not promoted to a verified native product result.
5. Native persistence safety, reviewed in source: photos are staged in bounded
   owned memory only after accepted source events and committed only after the
   canonical checkpoint's last durable **sequence**, not its event count. An
   ephemeral runtime token prevents old pending images rebinding on reopen.
   Previous-manifest recovery removes bindings above the recovered cursor before
   event identities can be reused. Failure quarantines optional appearance only.

The new native path samples at most 24 owned JPEGs, no faster than 0.8 s, at most
960 pixels on either side and 4 MiB each. Metadata is stripped; visibility uses
accepted metric-depth support rather than smoothed preview or missing samples.
Sidecar v1 is local, project-bound, immutable-assets/manifest-last and protected.
Projection only uses one verified epoch cohort and an exact frozen project,
revision and raw topology. Resource policy can suspend color independently.

Review includes a Source color / Measured mesh switch, neutral unsupported faces,
localized accessible guidance and an explicit default-off source-image export
toggle. Renderer UV-seam duplication affects disposable presentation buffers
only and preserves the existing derived triangle inclusion mask. Opt-in 3D bundles
contain only referenced, hashed relative JPEG assets. Metric PDF/SVG/DXF and raw
mesh topology stay unchanged. Portable room archives remain colorless; legacy
projects without the optional sidecar continue to work. Permanent project deletion
includes these local images; separately shared copies cannot be revoked.

## Exact executed checks and durable evidence

Portable repaired source `e38e259cdc04e9f26f8a81d0f9ea9b779806a369`:
60/60 Linux GCC Debug ASan+UBSan CTest, 76.97 s.
Additive C-owner/material-companion source
`fe41ec11a553bc201ba3be7d843b8998e1dfd060`:
60/60 in 76.49 s. RED regressions were preserved before repair under
`appearance-guidance-red-20261011`; no thresholds were relaxed.

Strict real-data evaluator/source checkpoint
`5b62439c3712c91e54a3909138b1be1e11f4cf8d`:
`real-rgbd-strict-pose-20261011/result.json` completed successfully.
The independent consumer parses saved OBJ/MTL, exact topology/source links,
UV indices, local material/image paths, image digests, manifests and exact reopen
JSON; strict outputs passed with no images. Earlier colored-file checks prove
consumer integrity only. Their color-alignment assumption is not accepted.

Representative actual commands (run on the server, with absolute data paths):

```sh
cmake --build build-gcc-asan-ubsan --parallel 2
ctest --test-dir build-gcc-asan-ubsan --output-on-failure --output-junit ctest.xml
hss_real_rgbd_evaluate adapted-freiburg1_xyz engine-freiburg1_xyz-strict-pose --appearance
hss_real_rgbd_evaluate adapted-freiburg1_room engine-freiburg1_room-strict-pose --appearance
python3 tools/measure_tum_pair_motion.py adapted-freiburg1_xyz adapted-freiburg1_room
python3 tools/verify_rgbd_artifacts.py engine-freiburg1_xyz-strict-pose engine-freiburg1_room-strict-pose
```

Job descriptions and logs retain actual absolute commands/outcomes in bounded
job evidence. On `50b3feece8b1ef38ede3e10bac96e8dab6987846`, the new C11 header
smoke and C++ guidance owner both passed. A source-audit wrapper then referenced
an incorrect motion-tool basename and stopped before the consumer step; its
failure is retained in `appearance-source-final-20261011`. The wrapper and this
document's tool name were corrected; no engine code or acceptance threshold was
changed. The corrected audit/consumer job is retained separately.

Publication hit an authenticated boundary: the server OAuth app cannot create or
update GitHub workflow files without the `workflow` scope. The rejected push did
not change the remote branch. No permission/token/security setting was changed.
The optional new portable-CI definition is retained in the isolated local history;
the deliverable omits it and leaves every existing remote workflow unchanged.
An aggregate publication commit preserves the previously published ancestry and
exact reviewed product code without importing that prohibited workflow change.
Checkpoint commit IDs above remain retained provenance, not an assertion that
each unpublished intermediate commit became part of remote history. The paired
handoff and PR record the actual publication commit/tree. Full portable CTest on
the exact deliverable is executed on the server; existing narrower hosted checks
are reported separately and do not stand in for that complete suite.

Source transfer, verification and checkpoints use the original `run.lock`
inode 12106462 and paired durable HANDOFF-first/state-last publication. Leases are
cleared and the kernel lock released after each finite job. Original dirty
checkouts and private captures are untouched. The canceled hourly task remains
deleted; no new scheduler was created.

New Swift/Objective-C++ integration tests are authored but **not executed** during
the Mac pause. Existing earlier native XCTest/archive evidence is not transferred
to this new tree. Remaining gates include Swift import/type checking, checkpoint
and rollback integration, actual CoreImage half/quarter-scale asymmetric raster
and UV orientation, colored/default-off export and SceneKit mask/position behavior,
all native/UI journeys, exact arm64 Release/privacy packaging and physical sensor,
storage, thermal, recovery and metrology acceptance. Team ID is deferred to
signing; it is not why these software checks are pending. See the finite release
assessment in `docs/release/software-readiness.md`.

# ENG-PERTURBED-TOPOLOGY-VALIDATION-01

Date: 2026-10-02. Base: `94042e29858290083b592c03a034b2f9159b5120`.
Scope: B-015/B-016/B-018/B-021/B-044, actual portable engine/correction/durability path. No native/physical accuracy claim.

## Product effect and reproduced failures

An independently parameterized, rotated and tessellated room with small declared coordinate perturbations exposed two production defects, in order:

1. The actual C ABI published a geometrically coherent Scene which the actual scene-edit validator rejected. The source had 630 vertices/840 triangles in three anchors, 21 fitted planes, 104 cut nodes/edges and two loops. All 239 entity IDs were unique. Three pairs of nonadjacent column edges had disjoint bounding boxes, but `segments_intersect` treated tolerance-zero orientation as an opposite side. One exact pair yielded orientation states `[-1,0,1,0]`; neither finite segment touched the other. Initial CTest `hss_perturbed_room` failed before any production change (exit 8, 0 passed/1 failed/0 skipped). The exact invalid Scene, red log, JUnit and numerical pairs are retained.
2. After the intersection repair the Scene validated, but cyclic rotation of each triangle's three indices changed derived cut start/end ordering, despite preserving the same oriented triangles. The same regression reached and failed its byte-equivalence assertion. The first mismatch was an edge's swapped endpoint IDs; both reconstructions retained 21 planes. This second red log/source is also retained, not conflated with the first failure.

These failures affect scan -> inspect/correct -> save/reopen -> export consistency, not only rendering. A valid perturbed scan must accept a supported unknown-region correction instead of failing its own Scene validation; a different starting corner of the same oriented source triangle must not arbitrarily reverse the derived cut.

## Minimal production repairs

- `SpatialCore/src/scene_editing.cpp`: proper crossing requires strictly opposite orientation signs on both segments. Existing tolerance-zero finite `on_segment` checks remain responsible for endpoint contact and collinear overlap. No epsilon, coordinate limit, validation threshold or true-contact rejection was removed.
- `SpatialCore/src/live_reconstruction.cpp`: order the two derived cut intersections lexicographically before node/edge emission. Raw triangle order, source positions and source references remain untouched. This adds a constant-size comparison/swap to the existing quantum, not a whole-anchor pass, smoothing, geometry snapping or proximity-based user-target rebinding.

The six additional Scene-validation controls verify forward-collinear and concave rings remain valid, while actual proper crossing, nonadjacent endpoint contact, collinear overlap and zero-length edges remain invalid. This is a narrowly corrected Boolean predicate, not an implementation of adaptive exact arithmetic or a proof of all geometric predicates over all scales.

## Frozen challenge and numerical domain

The generator and profile were saved before the first baseline execution in `frozen-perturbed-regression.cpp` and `frozen-profile.json`. The outline and ceiling dimensions differ from the prior mixed-room fixture. The development profile uses yaw 37 degrees, translation (1.234,0,-0.753)m, 4x5 cells per face, alternating triangle diagonals and a smooth per-coordinate perturbation bounded by 0.00015m. Three sine functions of original coordinates supply the explicitly authored perturbation; it is not a calibrated LiDAR noise model. References are independent unperturbed double-precision quads; observations are perturbed Float32 mesh coordinates. The 0.15m front, 0.50m returns, oblique corner, separate ceiling levels/slope, detached column and missing skylight remain.

A reserved sibling profile (yaw -23 degrees, 3x7 cells, amplitude 0.0001m) was declared before baseline and first executed after the production repair. It passed without tuning. It is now an exercised regression, not a permanently held-out benchmark or statistically diverse real-world corpus.

Checks include one-to-one plane support against the nominal reference, finite cut interval coverage, endpoint degree, genuine missing-return handling, exact raw arrays, and cyclic-index Scene equality. The new oracle bound is `4*sqrt(3)*amplitude + 2e-6` metres, specific to these authored smooth perturbations; existing noiseless tests retain their original strict bounds. It is neither a production threshold change nor sensor accuracy.

Observed maxima in the development profile: nominal plane distance 0.000389580724093m, normal cross-product magnitude 0.000136465010288, cut perpendicular deviation 0.000204193994314m, projected chain-length deviation 0.000243034827491m. Reserved sibling maxima: 0.000145807344262m, 0.0000911068270459, 0.000136583946267m and 0.000164116678594m respectively. These are host synthetic measurements only.

## Actual durable workflow

Both profiles execute nine ordered events through the real C ABI and project store: epoch, confirmed floor reference, three mesh anchors, applied zero-target `mark_unknown`, removal of the observed return, restoration of that source face, and a ceiling-only source refinement with half-amplitude perturbation. The complete case has 21 fitted planes and no open endpoints; the omitted return has 20 and exactly two open endpoints. No hidden gap closure is invented.

A real correction is accepted on the previously invalid kind of Scene. Source refinement retains its exact result and unknown provenance. Each event publishes one revision. Checkpoint, destruction, reopen and ordered replay reproduce identical complete Scene and real OBJ/MTL/sidecar bytes. Every raw position/index is checked before and after export/replay; unknown issue codes survive the test export adapter. Raw evidence is never smoothed. This does not wire the native iPhone export/UI adapter or establish generic targeted-edit lineage migration.

## Verification

Actual Linux x86_64/GCC13.3/CMake3.31.6 root build, Debug assertions, Werror, ASan+UBSan and existing user-local ZLIB1.3. Compiler commands for both production files and the changed test are inspected. No replacement targets/stubs or continue-on-error behavior.

- Exact base source: 462 matching Git blobs, zero mismatch.
- First new baseline: 0 passed/1 failed/0 skipped, exit 8, 0.57s.
- Intersection-only repair: existing scene editing passed, cyclic equivalence failed; preserved as second red evidence.
- Final focused tests: 5 passed/0 failed/0 skipped, 20.56s.
- Full local root: 58 passed/0 failed/0 skipped, 64.57s; prior 57 plus the new perturbed test.

Published-tree readback/rebuild belongs in the accompanying PR metadata and durable handoff, not inferred from local success. Single-host test times are not a measured speedup or an iPhone latency claim. Requested 1us/50us quanta remain cooperative, not hard-real-time guarantees.

Workspace: `/home/abdulazeez/hss-work/perturbed-94042e2.qi9mqd4u/`. Evidence: `commands.json`, `source-integrity.json`, `frozen-profile.json`, `baseline-invalid-scene.json`, `false-intersection-diagnosis.json`, `baseline-perturbed.log/xml`, `strict-crossing-tests.log/xml`, `cyclic-diagnostic.log/xml`, `post-crossing-cyclic-failing-source.cpp`, `expanded-focused.log/xml`, `full-tests.log/xml`, `compiler-evidence.json`, `research-decisions.json` and publication manifests. A REPL continuation syntax error initially prevented writing a patch; its unchanged-source build is not counted as a repair verification. The patch was then written and read back normally. An absent optional origin/HEAD symbolic ref in the single-branch clone did not change source; default main was independently verified through authenticated GitHub.

## Research, adoption and cost

- [Shewchuk's original-author robust-predicate overview](https://www.cs.cmu.edu/~quake/robust.html), including the 1997 paper's bibliographic record: orientation signs near zero require care. The overview, not the full paper, was read. Adopted the distinction between strict crossing and finite collinear contact; rejected adding a broad exact-arithmetic framework for this proven Boolean-condition defect. The page identifies predicate C code as public domain and Triangle separately copyrighted. Neither was copied.
- [CGAL6.2 finite intersection documentation](https://doc.cgal.org/latest/Kernel_23/group__intersection__linear__grp.html): an intersection requires a point belonging to both finite objects. Adopted regression checks distinguishing segment contact from supporting-line coincidence. Documentation only; no CGAL code/dependency imported.

No ABI/schema change, new runtime dependency, cloud inference, capture upload, heavy mobile integration or altered ownership policy. Additional runtime work is constant-size. Physical noisy captures, broader discontinuity models, analytic curved/subpatch fitting, iOS/Metal/device endurance, native correction/issue wiring and fair competitor comparisons remain unverified.

## Next executable acceptance

Retain this fixed regression. Select one bounded, independently frozen higher-frequency/depth-dependent perturbation and partial ceiling observation through the same pipeline, measuring actual fragmentation/support limits. Do not claim these two smooth synthetic profiles solve realistic LiDAR noise, alter raw captures or widen merge thresholds without new evidence. Native integration follows a credible engine milestone under the owner's engine-first priority.

# ENG-FULL-COMPONENT-PLANE-SUPPORT-01

## Scope and source

Date: 2026-10-02. Parent: `6ff5b44c7fbe85d28a4f11016f74db8348171e35`.
Parent tree: `618b9bea5800ce437f7db26044880aad000482b0`.
An isolated authenticated fetch matched all 460 Git blobs. The original dirty
server and Mac checkouts were not changed. This is engine-first work requested
by the owner, not an iOS integration, deployment, or physical-accuracy result.

Product effect: curved or partly nonplanar captured geometry must not be
advertised as a single fitted plane merely because a sampled subset fits one.
The original mesh and its slice remain available for inspection/persistence;
unsupported flat geometry is not fabricated for downstream measurements.

## Reproduced defect

The actual C-ABI regression submitted a connected 202-vertex, 200-triangle strip
with 100 facets, 0.1m facet length, 3m height, and 0.2-degree adjacent turns.
These are declared synthetic coordinates, not sensor measurements. Pairwise
connectivity passed the existing normal/distance gates, but the complete arc
was not planar. The unchanged parent engine emitted **one plane**. CTest exited
8 with `a subset plane cannot stand for a whole curved component` (1 failed,
0 passed). The failing test and log are retained outside the source checkout.

The robust plane fitter correctly returns only its selected inlier support.
The defect was in the live adapter: it applied that subset's plane to the whole
connected component without validating the remaining captured vertices.

## Repair and bounds

- Keep deterministic connectivity, canonical identities and the 96-sample cap.
  Fit one bounded component sample set per quantum while its original anchor
  faces still exist. Fitter options and the 0.002m caller-declared threshold
  are unchanged; that threshold is **not** a physical accuracy claim.
- During the existing canonical face walk, check all three original vertices
  against that candidate plane using the same residual equation. Record the
  number of checked faces and whether any support fails. A complete face-count
  match is required before a plane is emitted.
- If the full component does not support the candidate, retain its observed
  mesh-patch representation, with null plane parameters/residual, and a
  `partial_plane_support` issue targeting the specific component. Source
  sequence and current scene revision are explicit. A future refinement can
  restore a fitted plane if its new complete support genuinely fits.
- Raw vertices/indices and triangle-based slices are unchanged. There is no
  whole-scene replacement, new source revision, unbounded journal replay,
  whole-anchor validation quantum, full-source membership copy, or new runtime
  dependency. Stored fit support is bounded by the existing 96-sample limit
  inside the existing per-patch workspace reservation. One extra bounded fit
  stage precedes the existing per-face traversal and final serialization.
- This is conservative full-component classification, not new analytic curved
  surface fitting or adaptive splitting into multiple fitted patches. Robust
  subset fitting remains available and unchanged in its own API.

## Regression coverage

New root CTest: `hss_full_component_plane_support`, using the existing actual
live-engine test binary, not a replacement stub. It covers:

1. The reproduced curved wall and a rotated overhead/ceiling counterpart.
2. A 1202-vertex/1200-triangle mostly planar strip whose final bend is outside
   all 96 selected samples. The test independently checks the selected sample
   coordinates are z=0 while the omitted tip exceeds 7mm. Checking the sampled
   inliers alone cannot pass this acceptance. Raw arrays are verified exactly.
3. Sixteen triangle-order permutations, multiple nonplanar anchors with valid
   unique issue/entity IDs, and requested 1us/50us cooperative execution.
4. The actual project store: epoch, initial flat source, floor reference,
   curved replacement, flat replacement, curved replacement; one revision per
   event. Classification/warning appear and clear with current measurements.
   Checkpoint, close, reopen and six-event replay reproduce the complete Scene
   byte-for-byte and preserve every raw vertex/index without flattening.
5. The unchanged existing regressions continue to exercise a 0.15m front with
   0.50m returns, shallow creases, disconnected coplanar/parallel supports,
   non-manifold/degenerate cases, exact raw preservation and tiny-step behavior.

## Executed host verification

Linux x86_64, GCC 13.3.0, CMake 3.31.6, C++20, Debug assertions active, root
Werror, ASan+UBSan, existing user-local ZLIB 1.3. Actual compiler commands for
both changed C++ sources were inspected; no NDEBUG or sanitizer suppression.

- Parent/new initial regression: **1 failed**, retained as red evidence.
- Initial repair plus permutation regression: **2 passed / 0 failed / 0 skipped**.
- Expanded focused suite: **3 passed / 0 failed / 0 skipped**, 24.35s.
- Full root configure/build PASS; **56 passed / 0 failed / 0 skipped**, 80.01s
  (JUnit aggregate time is rounded to 80s). This includes all prior 55 tests.
- Exact post-publication source integrity and rerun are recorded separately in
  the PR comment/server checkpoint; this document does not predict those results.

One observed sanitizer-host run of the existing 8192-triangle fixture reported
1us requests: 70435 steps, p95 124.895us, max 8071.94us; 50us requests: 25823
steps, p95 90.909us, max 8297.19us. These include facade work/host scheduling;
they are neither hard-real-time guarantees, device latency nor speedup claims.
The 2048-duplicate fixture yielded 12327 actual internal advance quanta.

Exact commands are retained in `commands.json`. Root invocation:

```sh
/home/abdulazeez/hss-work/tools/bin/cmake -S source -B build \
  -DHSS_ENABLE_SANITIZERS=ON -DHSS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZLIB_ROOT=/home/abdulazeez/hss-work/deps/zlib/usr
/home/abdulazeez/hss-work/tools/bin/cmake --build build --parallel 2
/home/abdulazeez/hss-work/tools/bin/ctest --test-dir build \
  --output-on-failure --output-junit full-tests.xml
```

Evidence root: `/home/abdulazeez/hss-work/engine-6ff5b44.w06u7qko/`.
Files: `source-integrity.json`, `baseline-regression-source.cpp`,
`baseline-regression.log/xml`, `commands.json`, `expanded-targeted-tests.xml`,
`full-tests.log/xml`, `compiler-evidence.json`, `verification-summary.json`,
`research-decisions.json` and the publication manifests when completed.
A setup manifest parser was corrected from quoted line parsing to NUL-delimited
Git paths before asserting all 460 matches. A REPL input syntax error while
recording research was corrected/read back; neither was counted as a test pass.

## Primary research decisions (read 2026-10-02)

- Open3D 0.19.0 documentation, plane segmentation and planar-patch detection:
  https://www.open3d.org/docs/release/tutorial/geometry/pointcloud.html#plane-segmentation
  Adopt the distinction between returned inlier support and a whole surface.
  Reject importing a heavyweight dependency or using a convex hull as a room
  boundary. Documentation only; no source/assets copied or Open3D executed.
- PCL 1.15.1-dev region-growing tutorial:
  https://pointclouds.org/documentation/tutorials/region_growing_segmentation.html
  Its smooth clusters motivate separating connectivity from plane acceptance;
  smoothness is not global planarity. No PCL dependency was introduced.
- PCL rendered region_growing.hpp, BSD three-clause header and
  `growRegion`/`validatePoint` implementation (source lines 415-517):
  https://pointclouds.org/documentation/region__growing_8hpp_source.html
  Residual/curvature can stop seed propagation without rejecting membership.
  This is not a sufficient whole-plane certificate. No code was copied. License
  inspection does not certify all transitive dependencies or mobile runtime.

Cost decision: use the existing face walk and bounded sample fitter rather than
adding a neighbour-search/octree framework. Formal observed plane support is
more valuable here than a broader unverified segmentation architecture.

## Limitations / next acceptance

No physical metrology, calibrated camera/LiDAR sequence, native Apple build,
thermal/endurance test, game-engine import or competitor benchmark was run.
The diagnostics are core/project data; this does not wire the native issue UI.
Metric planes can conservatively fall back to observed mesh patches for noisy
or genuinely curved components; useful subset-plane extraction would need
explicit membership/splitting support, not removal of this acceptance check.
Continue engine-first with a small declared-reference mixed-level ceiling /
narrow-topology challenge on the same capture/replay path, investigating only
measured deficiencies. Three purpose-driven modes remain the product scope;
this slice does not complete those modes or the application.

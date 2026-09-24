# ENG-PROTRUSION-FACE-IDENTITY-01 — Distinct protrusion face evidence

Date: 2026-09-23

## Scope and acceptance

Base branch: `hss/plane-fit-exact-scoring`, head before this change `8547ecd3e5351b56a6b341e6fe2c4eb75511952a`. Default `main` remained `598b43d4901f2c5f1e5886d80fea6373ddf737a0` when publication started.

The live protrusion assistance contract requires separate evidence targets for first return, front, and second return. The production `target_tools` evaluator previously validated each face target independently but did not reject reuse of one target identity for more than one required face. A caller could therefore make one confirmed target satisfy two geometrically distinct face slots. That weakens provenance and can authorize a protrusion intent without three distinct face selections.

Acceptance for this slice:

- first return, front, and second return retain their required order;
- each populated face target is current, confirmed, finite, supported, and different from the parent, as before;
- one face target identity cannot satisfy another required face;
- underlying support IDs are not required to be distinct, because one captured mesh/source can legitimately support multiple separately targeted faces;
- no coordinate, dimension, perpendicularity, symmetry, or missing surface is inferred;
- no public API, ABI, schema, status enum, or persistence format changes.

## Implementation

`SpatialCore/src/target_tools.cpp` keeps a bounded three-entry set of accepted face target IDs during `kMarkProtrusion` evaluation. Duplicate identity returns the existing `kInvalidEvidence` status and never makes a command eligible.

`tests/hss_target_tools_test.cpp` adds two regression assertions: duplicate first-return/front identity and duplicate front/second-return identity. Existing pending-target, revision/provenance, ordered-face, and stale-evidence checks remain intact.

## Executed host evidence

Exact production source and header were copied from GitHub PR #1 head into an isolated Linux host directory. A test-only regression was first compiled against the unchanged source to prove the fault before the fix.

GCC 14 baseline reproduction:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic \
  -I SpatialCore/include SpatialCore/src/target_tools.cpp tests/hss_target_tools_test.cpp \
  -o baseline_test
./baseline_test
```

Observed result: exit 1 with exactly the two new duplicate-identity assertions failing:

```text
FAIL: one target cannot prove two distinct protrusion faces
FAIL: duplicate evidence identity is rejected regardless of face position
```

The same command against the candidate source exited 0.

Clang 17 ASan/UBSan candidate:

```sh
clang++ -std=c++20 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I SpatialCore/include SpatialCore/src/target_tools.cpp tests/hss_target_tools_test.cpp \
  -o candidate_san
ASAN_OPTIONS=detect_leaks=1 ./candidate_san
```

Observed result: exit 0 with no sanitizer diagnostic.

A first generic `-Wextra -Werror` compilation exposed the repository's existing designated-initializer missing-field warning style in this test. The scoped verification therefore makes that single compiler warning policy explicit instead of changing unrelated initializer style in this correctness patch.

## Research decision

The same round reviewed the current live mesh grouping path and primary references before deciding whether to tune its near-coplanar thresholds. Apple documents AR mesh geometry as per-anchor vertices/faces/normals; Open3D planar-patch detection and PCL region growing both reinforce that normal/distance agreement is only part of grouping and that adjacency/region evidence matters. No external code was imported.

Sources reviewed:

- https://developer.apple.com/documentation/arkit/armeshgeometry
- https://www.open3d.org/docs/latest/tutorial/geometry/pointcloud.html
- https://pointclouds.org/documentation/classpcl_1_1_region_growing.html

The live engine currently protects arbitrary angles and narrow returns. Relaxing the existing grouping normal/offset gates without a noisy connected-wall plus shallow-angle/narrow-return replay fixture could merge distinct faces. This round therefore fixes the directly reproduced provenance defect and leaves grouping thresholds unchanged pending that evidence.

## Not run and next dependency

This targeted host slice did not run full CTest, Xcode/iOS simulator, physical iPhone LiDAR/Metal validation, real-room metrology, thermal profiling, or end-to-end export validation. It makes no sensor-accuracy or product-wide performance claim.

Next engine work should build deterministic live-reconstruction replay fixtures that combine slightly noisy connected coplanar wall triangles with a nearby shallow-angle or narrow return. Only after the fixture separates desired grouping from protected topology should normal/offset aggregation policy be changed.

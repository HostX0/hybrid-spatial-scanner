# ENG-MESH-PATCH-COMPONENTS-01 — Order-invariant anchor-local mesh components

Status: dependency-ready implementation; **not yet wired into `live_reconstruction.cpp`**.

## Scope and reason for this slice

The live reconstruction characterization in `ENG-LIVE-PATCH-GROUPING-01` showed that the existing streaming policy can change its patch partition when only triangle ingestion order changes. This slice implements and tests a bounded, anchor-local alternative without changing the production path prematurely.

The connected execution container cannot clone the private repository (`Could not resolve host: github.com`). The authenticated GitHub connector can read and publish repository content, but it exposes whole-file writes rather than a safe local checkout/patch/build loop for the existing ~32 KiB `live_reconstruction.cpp`. Shipping an uncompiled rewrite of that production file would violate the repository's evidence contract. Therefore this round publishes a standalone C++20 component grouper and regressions as the integration dependency, while leaving runtime behavior unchanged until the next slice can wire it with compiled SpatialCore coverage.

## Implemented behavior

`SpatialCore/include/hss/mesh_patch_components.hpp` groups indexed triangles using only anchor-local evidence:

- contiguous shared-edge adjacency is mandatory for merging, so disconnected coplanar or parallel faces cannot merge;
- the live `abs(dot(normal_a, normal_b)) >= 0.99999` gate is preserved rather than widened;
- the existing 2 mm point-to-plane concept is made symmetric and non-vacuous by testing each adjacent triangle's opposite vertex against the other triangle's local plane;
- non-manifold indexed edges (>2 incident triangles) are reported and never used as merge bridges;
- connected components are computed with a deterministic geometry-keyed DSU, then ordered by the lexicographically smallest sorted vertex-index triple in each component;
- robust-fit input is bounded to 96 unique anchor vertices per component using a deterministic hash rank, independent of triangle arrival order;
- invalid indices/non-finite coordinates and explicit triangle/component resource limits return typed status instead of inventing geometry.

No semantic labels, Manhattan/right-angle priors, cloud calls, generated geometry, ABI changes, schema changes, or external dependencies were added.

## Acceptance fixtures

The regression suite covers 32 shuffled triangle orders for each applicable case:

1. 400-triangle exactly coplanar grid -> one component for every permutation.
2. The same grid with deterministic 0.10 mm vertex perturbation over 50 mm cells -> one component for every permutation under the unchanged gates.
3. A 1 m synthetic two-face crease at 0.20 degrees -> two components for every permutation because the symmetric 2 mm cross-plane evidence rejects the merge; a 0.50 degree crease is likewise preserved. This is an algorithm fixture, not a claim that a phone can resolve 0.20 degrees in practice.
4. Continuation / 0.50 m return / 0.15 m front / 0.50 m return / continuation -> five components for every permutation.
5. Disconnected coplanar faces and a 20 mm offset parallel face -> three components.
6. Three faces sharing one non-manifold edge -> three components and one reported non-manifold edge.
7. A >96-vertex planar component -> exactly 96 stable sample vertex IDs across 32 triangle permutations.
8. Invalid indexed geometry and configured triangle/component limits -> explicit failure status.

## Local verification

Environment: Linux x86_64 connected execution container.

Commands and observed results:

```text
g++ 14.2.0, C++20, -O2 -Wall -Wextra -Werror -Wpedantic
PASS: mesh patch component tests: PASS

clang++ 17.0.0, C++20, -O2 -Wall -Wextra -Werror -Wpedantic
PASS: mesh patch component tests: PASS

clang++ 17.0.0, C++20, -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
PASS: mesh patch component tests: PASS
PASS: no ASan/UBSan diagnostic emitted
```

A separate synthetic host-only stress measurement used a 180 x 180 quad grid (64,800 triangles, 32,761 vertices) with deterministic 0.05 mm height perturbation and one resulting component. Five optimized GCC 14 runs measured 20.3867, 21.9204, 19.2365, 19.8931, and 20.6290 ms; median 20.3867 ms. This is only a Linux host microbenchmark of the standalone module. It is **not** the app's p95, an iPhone result, a thermal result, or a real-room accuracy result.

## Research basis and adoption decision

- Apple `ARMeshGeometry` documents each mesh anchor as indexed triangle faces with normals. That makes indexed shared-edge adjacency first-party source evidence suitable for an anchor-local grouping primitive: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D exposes connected-triangle clustering for mesh connected components and its planar-patch detector separates normal distribution from point-to-plane coplanarity before grow/merge. We adopted the architectural ideas of explicit connectivity plus separate angular/metric evidence, not Open3D code or a dependency: https://www.open3d.org/docs/latest/tutorial/geometry/mesh.html and https://www.open3d.org/docs/latest/tutorial/geometry/pointcloud.html
- PCL RegionGrowing independently exposes smoothness (normal-angle) and residual tests. We used that as corroborating design evidence, not code: https://pointclouds.org/documentation/classpcl_1_1_region_growing.html
- Open3D source is MIT-licensed and PCL is BSD-licensed, but this implementation is original and imports neither dependency. Keeping the mobile core dependency-free avoids adding substantial binary/build cost solely for this bounded primitive.

## Production integration contract for the next slice

Before replacing the current `Build::adjacency`/first-compatible-patch streaming behavior:

- wire this module through a bounded anchor-local lifecycle without changing the public C ABI/schema;
- derive patch IDs from `canonical_triangle` rather than input triangle position;
- map the selected stable vertex IDs back to the already transformed project-coordinate vertex buffer for the existing robust plane fit;
- preserve all raw output triangles even when grouping returns a resource/non-manifold condition;
- preserve cut-edge provenance and map each provisional cut edge to its finalized component identity;
- add compiled `hss_live_reconstruction_test` permutations that compare final JSON topology, not only helper output;
- run full applicable CTest plus sanitizer build before considering runtime merge;
- keep Xcode/simulator and physical iPhone/LiDAR/Metal validation explicitly separate.

## Not verified in this slice

No production runtime file was changed. Full SpatialCore CTest, Xcode/iOS build, simulator, physical LiDAR/ARKit capture, real-room metrology, Metal GPU behavior, device memory, battery, and thermal acceptance were not run. The existing hosted Actions failure-before-steps on PR #1 remains a separate unresolved infrastructure blocker and was not bypassed.

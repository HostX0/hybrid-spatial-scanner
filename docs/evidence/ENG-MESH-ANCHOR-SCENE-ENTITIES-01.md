# ENG-MESH-ANCHOR-SCENE-ENTITIES-01

## Task

Materialize the deterministic mesh-anchor surface/node/edge layers into schema-shaped scene entity arrays without changing the public C ABI or production runtime. This slice is stacked on PR #14 and is intentionally downstream of the deterministic surface and cut scene-graph layers.

## Revisions and source identities

- Default branch re-read during this round: `main` = `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`).
- Stack base: PR #14 head `754ef35c52343fd6793d989126434307adfda52c` (`hss/stable-cut-scene-graph`).
- Branch before this evidence commit: `hss/stable-scene-entities` = `9b467fb3048e270d4172b8f01eb67ceb2fa1f00b`.
- Current live runtime source re-read: `SpatialCore/src/live_reconstruction.cpp` blob `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`.
- Current `schemas/scene.schema.json` blob: `b4395373b9383d887da027ae0340c6ef83006cf9`.
- Published helper blob at the branch checkpoint: `SpatialCore/include/hss/mesh_anchor_scene_entities.hpp` = `3f5f95a03737dbad112a489de7ca9743dab408be`.
- Published regression source blob: `tests/hss_mesh_anchor_scene_entities_test.cpp` = `6e54baf5243e461487fdfb466fc4532601d29a0c`.
- Published portable verifier blob: `tools/verify_mesh_anchor_scene_entities.py` = `6bcaf8bf21640bdf1eb9fbcc5dc275268ef6d53f`.

## Delivered behavior

`mesh_anchor_scene_entities.hpp` consumes a successful deterministic scene graph plus stable surface assembly and:

- sorts surfaces, nodes and edges by stable internal keys before materialization so vector/arrival order does not determine scene order;
- maps every stable surface/node/edge key through the caller-supplied identity function exactly once;
- rejects malformed/non-canonical UUID results and identity collisions fail-closed instead of silently aliasing two entities;
- validates support-scoped surface ownership, node/edge incidence, edge-to-surface attribution, per-surface cut-fragment attribution and fitted-plane finiteness/unit-normal constraints;
- retains multi-surface edge membership rather than collapsing attribution to the first surface;
- emits only the `nodes`, `edges` and `surfaces` arrays owned by this mesh-topology path, matching the current Scene 1.1 field contract while leaving epochs, features, regions, objects, constraints and issues to the surrounding runtime snapshot builder;
- emits fitted plane records when the upstream stable surface has a valid fitted plane and an observed `mesh_patch` fallback when fitting did not produce a plane;
- keeps internal stable keys/provenance indices in typed records but does not leak `:patch:` keys into public JSON;
- enforces existing bounded counts/project-coordinate extent and a 16 MiB fragment-JSON ceiling, clearing partial derived output on resource failure;
- changes no public ABI/schema, coordinate relationship, raw mesh evidence, reconstruction thresholds, persistence, semantics, cloud/AI behavior or external dependency.

The published checked-in regression covers the protected approach / 0.50 m return / 0.15 m front / 0.50 m return / continuation fixture, 32 graph/surface permutations, one-call identity mapping, multi-surface edges, identity collisions/invalid IDs/exceptions, plane/status inconsistencies, provenance/incidence corruption, resource limits and source immutability. The checked-in verifier also parses the emitted arrays with Python and checks their exact node/edge/surface field sets against the current Scene shape.

## Current-round host verification

The connected execution environment still could not resolve `github.com` through the container and could not materialize the complete private checkout. To avoid claiming an unexecuted repository build, this round used authenticated GitHub reads to reconstruct the **exact published helper bytes** and verified them with `git hash-object`; the local helper hash matched GitHub exactly:

```text
3f5f95a03737dbad112a489de7ca9743dab408be
```

An independent local C++20 contract harness then exercised that exact helper against minimal contract-compatible PR #14 graph and PR #13 surface type shapes. The harness covers the protected five-segment geometry, 32 node/edge/surface permutations, exact current live `id(...)` behavior, one-call-per-stable-key mapping, byte-stable emitted arrays, public-key non-leakage, source immutability, invalid plane/incidence/support cases, identity collision/invalid ID, `bad_alloc` resource mapping and JSON-byte exhaustion.

Exact commands:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I include tests/exact_helper_harness.cpp -o bin/g++_scene_entities
bin/g++_scene_entities

clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I include tests/exact_helper_harness.cpp -o bin/clang++_scene_entities
bin/clang++_scene_entities

clang++ -std=c++20 -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror \
  -I include tests/exact_helper_harness.cpp -o bin/clang_san_scene_entities
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  bin/clang_san_scene_entities

python3 <local JSON contract checker over each emitted NODES_JSON/EDGES_JSON/SURFACES_JSON>
git hash-object include/hss/mesh_anchor_scene_entities.hpp
```

Observed results after the final helper bytes:

- GCC 14.2.0: PASS, `166/166` harness assertions.
- Clang 17.0.0: PASS, `166/166` harness assertions.
- Clang 17.0.0 + ASan/UBSan: PASS, `166/166` harness assertions, no sanitizer diagnostic observed.
- Total compiled harness assertions: `498` passed, `0` failed.
- Python 3.13.5 JSON parsing / exact field-set checks: PASS for all three executions; each emitted 6 nodes, 5 edges and 5 surfaces.
- Exact helper blob equality: PASS (`3f5f95a03737dbad112a489de7ca9743dab408be`).

The checked-in `tests/hss_mesh_anchor_scene_entities_test.cpp` and `tools/verify_mesh_anchor_scene_entities.py` were authenticated and reviewed in this round, but were **not** directly materialized/executed by the container. Their presence is not counted as an additional passing run.

## Evidence boundary

This is targeted host evidence for the exact published scene-entity helper plus an independent deterministic contract harness. It is **not**:

- a full repository CMake/CTest run;
- exact hosted GitHub Actions execution;
- a revalidation of PR #11 capture preparation, PR #12 cut generation, PR #13 robust fitting or PR #14 graph assembly algorithms;
- a link/run of the production `robust_plane_fit.cpp` in this round;
- production `live_reconstruction.cpp` integration;
- Swift portable tests, Xcode/simulator evidence, physical iPhone LiDAR/Metal evidence, thermal/memory characterization or real-room metrology.

Known exact-source GitHub Actions attempts on earlier stack revisions failed before any step with no assigned runner. That observable hosting condition was not retriggered here and its cause remains unverified.

## Research decision

Primary references re-read for this bottleneck:

- Apple `ARMeshGeometry`: one scene-mesh anchor owns array-based vertices and indexed triangular faces with per-face normals/classification: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D 0.20 `TriangleMesh`: triangles are indexed into vertices and `cluster_connected_triangles()` returns a cluster assignment for every triangle; `merge_close_vertices(eps)` averages merged vertex positions/normals/colors: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html

These are architecture references only. No Apple/Open3D code or runtime dependency is imported. Stable-key identity and explicit per-triangle/per-surface attribution remain separate from presentation serialization; geometry is not averaged or invented by this bridge.

## Remaining production gate

The live runtime still builds arrival-order patches, provisional cut edges and first-arrival node representatives in `live_reconstruction.cpp`; PRs #4-#14 and this branch are not wired into that path yet. Before a runtime merge claim, the actual repository must compile and test a production integration that:

1. prepares each accepted anchor once through the validated payload/project-coordinate/order-invariant component path;
2. creates deterministic cuts, stable surfaces and the deterministic graph;
3. maps stable surface/node/edge keys through the existing internal `id(...)` exactly once and splices these schema-shaped arrays into the complete Scene 1.1 snapshot;
4. preserves the exact raw triangle stream and source provenance on every derived failure path;
5. emits explicit issues for unavailable derived metric topology rather than inventing closure;
6. proves final full-scene JSON/topology invariance across triangle permutations, global vertex bases, the protected 0.15 m front / 0.50 m returns, noisy coplanar walls, supported shallow creases, disconnected coplanar surfaces and offset parallel faces;
7. passes applicable full CTest and sanitizer coverage on the actual repository before merge consideration.

Xcode/simulator and physical iPhone LiDAR/Metal/metrology remain separate acceptance layers.
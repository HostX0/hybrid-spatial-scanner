# ENG-MESH-ANCHOR-SCENE-GRAPH-01 — deterministic cut scene graph

## Task

Remove the last known triangle-arrival dependency from the **derived architectural-cut node/edge assembly boundary** without rewriting the production runtime before an exact repository compile is available. The slice consumes the deterministic cut fragments from PR #12 and stable surface records from PR #13, preserves the current live join tolerance, keeps observed endpoint coordinates rather than averaging them, and retains all coincident-edge surface/source attribution.

This is dependency-ready C++20 code. It does **not** modify `SpatialCore/src/live_reconstruction.cpp`, the public C ABI/schema, raw capture bytes, grouping/fitting thresholds, coordinate-frame relationships, persistence, semantics, AI/cloud behavior, or any source observation.

## Source checkpoint

- Repository: `HostX0/hybrid-spatial-scanner`
- Default branch re-read before implementation: `main` = `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`).
- Production live source re-read: `SpatialCore/src/live_reconstruction.cpp`, blob `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`.
- Parent stack head: PR #13 `hss/stable-surface-assembly` = `4c4b37269c5f7069846eca473f093a47233861d7`.
- Normative inputs re-read: `AGENTS.md`, `README.md`, `DRIVE_INDEX.md`, and repository copies of D14-D17.

The current production cut graph still has two order-sensitive details after `slice(...)` computes a valid segment:

1. node welding quantizes each 3D endpoint with `llround(coord / 1e-7)` but stores the first endpoint that reaches that cell as the node position/identity;
2. duplicate undirected node pairs are suppressed by first insertion, so a later coincident fragment from another finalized surface cannot add its surface/source attribution.

The earlier PR #12 removes triangle-order dependence from fragment generation and PR #13 validates finalized surface ownership, but a future runtime integration still needs a deterministic node/edge boundary before scene JSON generation.

## Delivered implementation

`SpatialCore/include/hss/mesh_anchor_scene_graph.hpp` adds an original dependency-free scene-graph assembler:

- accepts only successful PR #12 cut results and successful PR #13 surface results;
- requires one non-empty support identity and validates every stable surface key as `support + ":patch:" + suffix`;
- proves that every cut fragment is owned exactly once by the surface record that names the same stable key;
- preserves the live `1e-7 m` 3D join-cell policy exactly;
- sorts all cut endpoints by join cell, exact observed 3D/plan coordinates, stable surface key and source index before welding;
- chooses the lexicographically smallest **existing observed endpoint** as the node representative for each populated join cell; it never averages or synthesizes a new measured coordinate;
- emits a stable string `node_key` from the quantized join cell instead of from arrival order;
- canonicalizes each fragment to an undirected node pair and sorts edge candidates deterministically;
- folds coincident fragments into one edge while retaining the union of stable surface keys, source fragment indices and source triangle indices;
- emits stable string edge keys from stable endpoint keys; final scene UUID generation remains the runtime's existing `id(...)` responsibility;
- preserves node incident-edge attribution and surface attribution in sorted unique vectors;
- rejects nonfinite/out-of-range endpoints or plan coordinates, same-cell collapsed edges, malformed ownership, support/key mismatches and configured resource exhaustion fail-closed;
- clears partial derived graph state on resource/geometry failure and mutates no input cut/surface records.

The bounded defaults are 2,048 edges and 4,096 nodes. Those are integration bounds derived from the current live 2,048 metric-fragment ceiling, not a supported room-size or device-performance claim.

## Acceptance regressions

`tests/hss_mesh_anchor_scene_graph_test.cpp` executes 1,868 assertions per run and covers:

- the protected five-segment `approach -> 0.50 m return -> 0.15 m front -> 0.50 m return -> continuation` cut chain;
- 32 deterministic cut-fragment permutations, including reversed surface record order, with identical stable geometry/surface graph signatures;
- exact node degree/edge incidence and sorted source/surface provenance;
- two endpoints at `1.0 +/- 4e-8 m`, which occupy the same existing `1e-7 m` join cell: forward/reverse input order selects the same representative, specifically the lexicographically smaller observed endpoint rather than an average;
- two geometrically coincident fragments owned by two different stable surfaces: one edge is emitted and both surface keys plus both source fragment/triangle identities are retained;
- upstream cut/surface failure propagation;
- empty/mismatched support, malformed/duplicate surface keys, unowned/duplicate/out-of-range/mismatched fragment ownership;
- nonfinite and out-of-range 3D endpoints, nonfinite plan coordinates;
- node/edge capacity exhaustion and invalid configured caps;
- corrupt over-cap upstream result sizes;
- an edge whose endpoints collapse into one join cell, which is rejected instead of inventing topology;
- source cut geometry and surface ownership arrays remain unchanged.

The fixture dimensions and sub-micrometre join perturbation are synthetic algorithm tests, not claims about LiDAR resolution or room accuracy.

## Verification actually executed

Connected Linux x86_64 environment after the final implementation bytes:

```text
python3 -m py_compile tools/verify_mesh_anchor_scene_graph.py
PASS

python3 tools/verify_mesh_anchor_scene_graph.py --cxx g++
assertions=1868 failures=0

python3 tools/verify_mesh_anchor_scene_graph.py --cxx clang++
assertions=1868 failures=0

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  python3 tools/verify_mesh_anchor_scene_graph.py --cxx clang++ --sanitize
assertions=1868 failures=0
```

Observed toolchains:

- GCC 14.2.0
- Clang 17.0.0
- Python 3.13.5

No ASan/UBSan diagnostic was observed. Total final compiled assertions across the three executions: **5,604 passed, 0 failed**.

## Evidence boundary

The connected container still cannot resolve `github.com`, so it could not materialize the complete private checkout. The targeted compile used the exact new helper/test/verifier bytes with minimal local type-shape stubs for the authenticated PR #12 `mesh_anchor_cut` and PR #13 `mesh_anchor_surface` result structures that this layer consumes. Those stubs do not implement cut generation, grouping, plane fitting or surface assembly.

Therefore this evidence validates the new scene-graph assembly logic and its contract mapping only. It is **not**:

- full repository CMake/CTest;
- exact hosted-branch execution;
- a revalidation of PR #12 cut generation or PR #13 production plane fitting;
- Swift/Xcode/iOS simulator evidence;
- physical iPhone LiDAR/Metal, thermal, memory or real-room metrology evidence.

The known hosted GitHub Actions route remains separately blocked before job steps run; this slice did not repeatedly retrigger that unchanged infrastructure condition.

## Research decision

Apple's current `ARMeshGeometry` documentation continues to model a scene-mesh anchor as indexed vertices/faces with per-face normals. Open3D 0.20 similarly exposes per-triangle connected-component assignment. Open3D also exposes `merge_close_vertices(eps)`, which averages the merged vertex positions/normals/colors. That averaging behavior is useful for some triangle-soup cleanup, but it is deliberately **not adopted** here: averaging two near cut endpoints would create a new derived coordinate not present in either retained measurement path. The current live join-cell policy is instead preserved and made deterministic by selecting one existing endpoint. PCL region-growing remains a reference for keeping neighbourhood/smoothness decisions explicit rather than treating a single global threshold as topology authority.

No Apple/Open3D/PCL source code was copied and no external runtime dependency was added.

## Production integration still required

When a credible exact repository compile route becomes available, the next high-value change is to wire the already prepared stack into current `live_reconstruction.cpp`:

1. prepare each accepted anchor once through PR #11;
2. generate finalized deterministic cut fragments through PR #12;
3. assemble stable fitted/fallback surfaces through PR #13;
4. assemble deterministic nodes/edges through this scene-graph layer;
5. map each stable surface/node/edge key through the existing internal `id(...)` exactly once;
6. preserve the exact raw captured triangle stream and source provenance;
7. emit explicit issues on metric-plan/resource failure rather than closing unknown topology;
8. add final scene JSON/topology permutation regressions for noisy coplanar walls, supported shallow creases, the protected 0.15 m front / 0.50 m returns, disconnected/offset parallel faces, multiple anchors/global vertex bases, malformed payload/transform cases and raw-triangle preservation;
9. run applicable full CTest and sanitizer coverage on the actual repository before any runtime merge claim.

Xcode/simulator and physical iPhone LiDAR/Metal/metrology remain separate evidence gates.

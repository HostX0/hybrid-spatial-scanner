# ENG-MESH-ANCHOR-CUT-FRAGMENTS-01

Date: 2026-09-24
Repository: `HostX0/hybrid-spatial-scanner`
Parent branch/head: `hss/anchor-reconstruction-plan` / `57cdd3246d6d3869e045c9706d87cff65bf3f9c3`
Current main observed before implementation: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25`
Current live reconstruction source blob observed: `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`

## Task

Reduce the remaining runtime-integration risk between the finalized per-triangle anchor surface binding and the existing architectural-cut `slice(...)` path without rewriting `live_reconstruction.cpp` before an exact repository build is available.

The production source still creates patch identities from triangle arrival order and immediately slices each triangle using that provisional patch identity. PRs #4-#11 now provide deterministic complete-anchor capture preparation and stable component surface keys; this slice makes the cut-fragment stage consume finalized per-triangle surface identity without changing the public scene/ABI contracts.

## Delivered behavior

`SpatialCore/include/hss/mesh_anchor_cut_fragments.hpp` is an original dependency-free C++20 helper that:

1. consumes complete project-coordinate vertices, anchor-local indexed triangles and finalized per-triangle surface keys;
2. requires every nonempty surface key to belong to the supplied support identity (`support + ":patch:" + nonempty suffix`), preventing cross-anchor/cross-support attribution;
3. validates the current 65,536 vertex/triangle ceilings, the existing 2,048 metric-fragment ceiling, finite project coordinates within the existing +/-10,000 m extent, and a finite orthonormal bounded floor frame;
4. reproduces the current live architectural-cut half-open crossing rule exactly and preserves the existing `1e-12 m^2` minimum segment-length guard;
5. keeps coplanar, degenerate and deliberately metric-unbound triangles from manufacturing cut fragments;
6. retains source triangle index and local indexed triangle attribution for every derived fragment;
7. canonicalizes each segment direction in plan coordinates and deterministically sorts fragments by canonical plan segment, stable surface key and canonical triangle, so downstream iteration does not inherit triangle arrival order;
8. fails closed with no partial fragment vector on invalid geometry or resource exhaustion; and
9. never mutates or retains source geometry/binding data and performs no topology repair, plane fitting, semantic inference, scene-ID hashing or raw-capture rewriting.

No public C ABI/schema, reconstruction threshold, persistence format, cloud/AI behavior or captured-source ownership rule changes in this slice.

## Regression fixtures and acceptance

`tests/hss_mesh_anchor_cut_fragments_test.cpp` executes 469 assertions per compiler run.

The protected vertical chain uses the same plan profile as the preceding component/anchor regressions:

`approach -> 0.50 m return -> 0.15 m front -> 0.50 m return -> continuation`

with two indexed triangles per face and a cut height of 1.20 m. The tests verify:

- all ten triangle-level cut fragments remain attributed to five distinct stable surface keys;
- all fragments lie on the requested cut plane;
- 32 deterministic triangle-list permutations produce the same sorted geometry-to-surface signature;
- each sorted fragment still points back to the correct source triangle in that permutation;
- translating the project geometry and floor frame together preserves plan-space cut output;
- coplanar triangles produce no cut;
- the existing half-open rule keeps a true crossing when one vertex is exactly on the cut plane;
- deliberately unbound triangles produce no metric cut;
- degenerate triangles remain excluded and may not carry a finalized metric surface key;
- foreign-support and empty-suffix surface keys are rejected;
- invalid binding count, invalid cut height, attempts to raise the 2,048-fragment ceiling, nonorthonormal/out-of-range floor frame, invalid triangle index, nonfinite/out-of-range project vertex, vertex/triangle count overflow and fragment resource exhaustion fail closed;
- the input vertex/triangle/key collections remain unchanged.

The 0.50 m / 0.15 m dimensions are synthetic adversarial topology fixtures, not iPhone LiDAR accuracy or resolution claims.

## Encountered failures and repairs

Two test-development failures were retained rather than hidden:

1. The first GCC compile did not execute the test binary because the test attempted `std::vector<HSSVec3d> == ...`; the C ABI math struct intentionally has no C++ equality operator. The test was repaired with an explicit component-wise immutability comparison. Production helper behavior was not weakened.
2. After tightening the helper to require finalized surface keys to belong to the supplied support identity, the next GCC run compiled but reported seven fixture failures because several test cases still used placeholder key `"surface"` or passed support `"support"` against `"anchor-support:patch:..."` keys. The fixtures were corrected to exercise the actual stable-key contract. The validation was retained; no threshold or product rule was relaxed.

## Commands and observed results

Connected Linux x86_64 workspace, final implementation bytes:

```text
python3 -m py_compile tools/verify_mesh_anchor_cut_fragments.py
PASS

python3 tools/verify_mesh_anchor_cut_fragments.py --cxx g++
assertions=469 failures=0

python3 tools/verify_mesh_anchor_cut_fragments.py --cxx clang++
assertions=469 failures=0

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  python3 tools/verify_mesh_anchor_cut_fragments.py --cxx clang++ --sanitize
assertions=469 failures=0
```

Observed toolchains:

```text
g++ (Debian 14.2.0-19) 14.2.0
clang version 17.0.0 (swiftlang LLVM build 10999b6d...)
Python 3.13.5
```

Final total across the three compiled executions: 1,407 passed assertions, zero failures. No ASan/UBSan diagnostic was observed.

## Research decision

Primary/current references checked in this round:

- Apple `ARMeshGeometry`: geometry belongs to one scene-mesh anchor, faces use vertex indices and carry face normals/classification. This supports retaining anchor-local per-triangle attribution instead of allowing processing order to become identity.
- Open3D `TriangleMesh.cluster_connected_triangles`: connected-component output includes a cluster index for each triangle. This is architecture evidence for carrying explicit per-triangle derived ownership through later stages.
- PCL `RegionGrowing`: smoothness, residual and curvature tests are separate controls. This reinforces the existing decision not to compensate for topology/identity issues by widening a single scalar geometry threshold.

No Apple, Open3D or PCL source code is copied by this change and no external runtime dependency is introduced.

## Evidence boundary

The connected container still cannot resolve `github.com`, so the complete private repository checkout could not be materialized. Final targeted compilation uses the exact new helper/test/verifier bytes against a local mirror of the authenticated current coordinate-basis contract/implementation. The minimal local C ABI header contains only the exact math type shapes directly required by that coordinate helper; it is not asserted byte-identical to the full repository header.

Therefore these results are targeted host evidence only. They are **not** full repository CMake/CTest, exact hosted-branch execution, Swift/iOS simulator compilation, physical iPhone LiDAR/Metal validation, thermal profiling or real-room metrology.

No performance benchmark was run for this slice because it is a bounded linear triangle-cut attribution helper and the current gate is correctness/order invariance, not a claimed hot-path speedup.

## Remaining production gate

Production `live_reconstruction.cpp` remains unchanged by this slice. A future exact-repository compiled integration should:

1. consume one PR #11 complete anchor preparation per accepted anchor revision;
2. derive the finalized surface key for every metric-bound triangle from the PR #7/PR #11 component mapping;
3. produce deterministic cut fragments through this helper;
4. map each stable key through the existing internal `id(...)` exactly once when creating scene surface IDs;
5. preserve the exact raw triangle stream and provenance;
6. fit each finalized component using its bounded prepared samples; and
7. compare final scene JSON/topology under triangle-order permutations, including the protected protrusion, noisy coplanar wall, shallow supported crease, disconnected coplanar and offset parallel faces.

Applicable full CTest and sanitizer coverage on the actual repository are required before calling that runtime integration verified. Xcode/simulator and physical iPhone LiDAR/Metal/metrology remain separate evidence layers.

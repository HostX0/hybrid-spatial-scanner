# ENG-MESH-ANCHOR-RECONSTRUCTION-PLAN-01

Date: 2026-09-24
Repository: `HostX0/hybrid-spatial-scanner`
Parent branch/head: `hss/anchor-project-batch` / `d9ce348f6af7153683cb48cee561c21ff36eca8c`
Current main observed before publication: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25`
Current live reconstruction source blob observed: `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`

## Task

Reduce the remaining production-integration risk between the validated owned mesh payload/project-coordinate batch and the order-invariant anchor-local patch plan without rewriting `live_reconstruction.cpp` before a credible exact-repository build is available.

## Delivered behavior

`SpatialCore/include/hss/mesh_anchor_reconstruction_plan.hpp` composes the existing stack into one bounded preparation boundary:

1. decode and transform the complete owned mesh-event payload through `mesh_anchor_project_batch`;
2. retain project-coordinate vertices and anchor-local indexed triangles;
3. produce an atomically rebased raw-triangle copy for the existing global snapshot vertex array;
4. prepare the order-invariant metric patch plan from anchor-local topology;
5. keep capture truth separate from metric derivation so a grouping/binding/resource failure never discards valid raw/project evidence.

The helper deliberately does not hash scene IDs, slice the architectural cut, fit planes, modify raw source bytes, repair topology, infer semantics, alter persistence, or change the public ABI/schema. Surface keys remain anchor-local (`support identity + canonical indexed triangle`); global vertex placement only affects the separate raw snapshot indices.

A raw-index overflow is treated as an atomic capture-preparation failure because the existing snapshot stores UInt32 indices. Metric allocation failure is different: valid raw/project capture remains available and only the derived metric plan becomes `kResourceLimit`.

The bridge copies `HSSVec3d` project vertices into the internal `mesh_patch::Vec3` representation instead of aliasing distinct C/C++ types. The maximum count is already bounded by the existing 65,536-vertex contract. A future production caller must include this temporary bounded allocation in its existing working-memory budget before integration is considered complete.

## Regression fixture

`tests/hss_mesh_anchor_reconstruction_plan_test.cpp` uses an indexed five-face vertical chain whose plan profile is:

`approach -> 0.50 m return -> 0.15 m front -> 0.50 m return -> continuation`

The test keeps shared indexed vertical edges between adjacent faces and exercises 32 deterministic triangle-list permutations. It verifies:

- five distinct protected components remain five;
- geometry-to-surface-key mapping is invariant to triangle order;
- raw triangle order/indices are preserved exactly apart from the requested global vertex base;
- a nonzero global vertex base and a valid rigid frame change cannot rename anchor-local surfaces;
- fit samples retain the supplied mesh-event diversity group;
- a metric `max_components` resource failure retains complete raw/project capture evidence;
- empty support identity fails only the metric plan and does not erase raw capture;
- truncated payload and nonrigid transforms expose no partial geometry;
- UInt32 raw-index overflow fails atomically.

Final test count: 533 assertions per compiler execution.

## Commands and observed results

Connected Linux x86_64 test workspace:

```text
python3 -m py_compile tools/verify_mesh_anchor_reconstruction_plan.py
PASS

python3 tools/verify_mesh_anchor_reconstruction_plan.py --cxx g++
assertions=533 failures=0

python3 tools/verify_mesh_anchor_reconstruction_plan.py --cxx clang++
assertions=533 failures=0

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  python3 tools/verify_mesh_anchor_reconstruction_plan.py --cxx clang++ --sanitize
assertions=533 failures=0
```

Toolchains observed in the same environment during the preceding stack validation: GCC 14.2.0, Clang 17.0.0, Python 3.13.5. No ASan/UBSan diagnostic was observed. Total assertions executed across the three compiled runs: 1,599 passed, zero failed.

## Source identities and evidence boundary

Authenticated GitHub reads in this round re-read the live contracts and stack. Important source blobs include:

- `mesh_anchor_project_batch.hpp`: `22797e9635d15ee327d62a044ca52326ddbac90d`
- `mesh_anchor_patch_plan.hpp`: `e85b725f3ddae89b8a45c0f8ac6850d341c4d038`
- `mesh_event_payload.hpp`: `ebb66d8137afe7e87cadd661930fa129e6be8a1c`
- `mesh_patch_components.hpp`: `953864104afbd6f850ef9dfd104b5faf63334860`
- `mesh_patch_runtime_plan.hpp`: `1624c20cf896598a7ae34f5f7dcbbe6aef248e37`
- `mesh_patch_surface_binding.hpp`: `fd90893f883800ff28e17b2c22fcf47ed431bfe6`
- `coordinate_basis.hpp`: `1f4d36c4fd9026d43b4d26a235259c4ed30c6fc5`
- `coordinate_basis.cpp`: `2f883af9eb039879025582c2ee38b5a73ef667ca`
- `robust_plane_fit.hpp`: `35735ffc667c45357bef2023c28875ff46219dd1`

The connected container still cannot resolve `github.com`, so a complete private checkout could not be materialized. The targeted compile used the exact new helper/test/verifier bytes and a local source mirror built from the authenticated dependency contents consumed by this helper. Dependency mirror blob equality was not asserted. This is therefore targeted host integration evidence, not a full repository CMake/CTest result and not exact hosted branch execution.

Not run in this slice: full CMake/CTest, current-main/stack reconciliation build, Xcode, iOS simulator, physical iPhone LiDAR/ARKit, Metal/device profiling, thermal endurance, real-room metrology or final scene JSON/topology integration.

## Research decision

Apple `ARMeshGeometry` documents mesh data for one scene-mesh anchor, with faces represented by vertex indices. Open3D documents per-triangle connected-component assignment and explicit geometry transforms. These support keeping indexed anchor-local topology and per-triangle attribution independent from coordinate transforms and global snapshot storage. They are architectural references only; no Apple/Open3D code or runtime dependency is imported.

## Next production gate

When a credible exact repository compile route is available, wire this preparation boundary into the current `live_reconstruction.cpp` rather than reproducing its pieces again. The runtime must:

- preserve the exact raw triangle stream/provenance;
- derive scene surface IDs from finalized stable surface keys;
- attribute every `slice(...)` fragment through finalized per-triangle component identity;
- feed only bounded prepared samples to the existing robust plane fit;
- retain raw evidence and issue an explicit metric warning if the patch plan fails;
- compare final scene JSON/topology across triangle-order permutations and multiple global vertex bases;
- run applicable full CTest and sanitizer coverage before any runtime merge claim.

This slice does not complete that production gate.

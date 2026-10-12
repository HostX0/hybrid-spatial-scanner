# ENG-MESH-CURRENT-MAIN-RECONCILIATION-01

## Task

Reconcile the deterministic mesh-reconstruction stack with the actual current `main` line without rewriting the existing stacked PR history, preserve current-main build/test work, and remove concrete CTest-discovery/integration blockers before any production merge claim.

## Source identities

- Repository: `HostX0/hybrid-spatial-scanner`
- Current main observed during the latest audit: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`)
- Prior stack checkpoint: PR #17 head `afe2c994ebae7af73e736439ac1976e1e2e013d3`
- Prior stack/main merge base: `598b43d4901f2c5f1e5886d80fea6373ddf737a0`
- GitHub synthetic merge commit for PR #18: `ae1e14ec31ff0473b8d119280b8b5a1ae645479d`
- Synthetic merge tree: `4f70d43a61ea8b4d79467ca6d03829c9e9727d4c`
- Synthetic merge parents: current main `4cae5a0...` and stack `afe2c994...`
- Reconciliation PR: #18, `hss/current-main-stack-candidate` -> `main`, draft
- Mesh-test registration hardening commit: `82726799ca208db8d4b094c3596e2b5896f1d0c7`

GitHub recomputed PR #18 as mergeable, but mergeability is not interpreted as test success. No merge to `main` was performed.

## Current-main CMake preservation

The current-main root `CMakeLists.txt` blob before mesh registration was:

`3631977c845ff9155776e0ebba495bea4b0e250b`

The stack had never changed that file relative to the old merge base. The reconciled branch was therefore created from GitHub's current-main + stack synthetic merge tree, not by replacing current-main CMake with the stale stack copy.

Commit `b21f12073a96af253b662b966bcb49a022a4ca16` changes root `CMakeLists.txt` by exactly two added lines and zero deletions:

```cmake

include(cmake/HSSMeshPipelineTests.cmake)
```

The resulting root CMake blob remains `068385bd79424dc0b70f0df13ed5b00baa254f09`. It preserves the current-main ZLIB/native archive import, `HSS_ENABLE_PLAN_ANNOTATION_RUNTIME=1`, dynamic-evidence verifier, B-044 golden-suite registration, native project import, plan-annotation runtime, and existing live/scene-editing/dynamic/resource runtime registrations.

## Test registration implementation

Module:

`cmake/HSSMeshPipelineTests.cmake`

Current published blob after the latest hardening:

`21137534e70252ab096db7edc03cfda1cba3ae5e`

The module registers the twelve deterministic mesh-pipeline C++ sources that were previously invisible to CTest:

1. `hss_mesh_patch_components`
2. `hss_mesh_patch_runtime_plan`
3. `hss_mesh_patch_surface_binding`
4. `hss_mesh_anchor_patch_plan`
5. `hss_mesh_event_payload`
6. `hss_mesh_anchor_project_batch`
7. `hss_mesh_anchor_reconstruction_plan`
8. `hss_mesh_anchor_cut_fragments`
9. `hss_mesh_anchor_surface_assembly`
10. `hss_mesh_anchor_scene_graph`
11. `hss_mesh_anchor_scene_entities`
12. `hss_mesh_anchor_scene_snapshot`

Registration uses the repository's C++20 warnings-as-errors profile and honors `HSS_ENABLE_SANITIZERS` for every target.

### Exact linkage audit

The twelve exact test translation units and their authenticated dependency headers were re-read from the reconciled branch before changing registration behavior.

- `hss_mesh_patch_components`, `hss_mesh_patch_runtime_plan`, `hss_mesh_patch_surface_binding`, `hss_mesh_anchor_patch_plan`, and `hss_mesh_event_payload` exercise header-defined logic and do not call a required non-inline repository implementation.
- `hss_mesh_anchor_project_batch` calls `coordinate::is_rigid_transform` and `coordinate::transform_point`, which are declared non-inline in `coordinate_basis.hpp`; it must link the real `SpatialCore` implementation.
- `hss_mesh_anchor_reconstruction_plan` transitively uses the project-coordinate batch and therefore must link `SpatialCore`.
- `hss_mesh_anchor_cut_fragments` calls non-inline `coordinate::is_valid_floor_frame` and `coordinate::project_to_floor`; it must link `SpatialCore`.
- `hss_mesh_anchor_scene_snapshot` calls non-inline coordinate-basis rigidity/floor validation; it must link `SpatialCore`.
- `hss_mesh_anchor_surface_assembly` deliberately defines its own deterministic `hss::plane_fit::fit` stub. It must **not** link `SpatialCore`/`hss_robust_plane_fit`, because that would duplicate the fit symbol and invalidate the regression boundary.
- `hss_mesh_anchor_scene_graph` and `hss_mesh_anchor_scene_entities` consume already-assembled value records and header-defined assembly logic; their tests do not require a non-inline coordinate or plane-fit implementation.

No linkage mismatch was found in the existing target list.

## Fail-closed CTest discovery hardening

The original registration helper silently skipped an expected source when the file was absent:

```cmake
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  message(STATUS "HSS mesh pipeline test source not present: ${source}")
  return()
endif()
```

That behavior was unsafe for this reconciliation candidate because a future incomplete merge could remove one of the required twelve tests while configure and CTest still appeared green with reduced coverage.

Commit `82726799ca208db8d4b094c3596e2b5896f1d0c7` changes this to a configure-time hard failure:

```cmake
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  message(FATAL_ERROR "Required HSS mesh pipeline test source is missing: ${source}")
endif()
```

This does not change production runtime behavior, ABI/schema, geometry thresholds, or source data. It only prevents silent loss of required regression coverage on the PR #18 validation candidate.

CMake's current primary documentation states that `message(FATAL_ERROR ...)` stops processing/generation and returns a non-zero exit code, while `add_test()` creates tests for CTest after testing is enabled. These semantics are the intended basis for the fail-closed gate:

- https://cmake.org/cmake/help/latest/command/message.html
- https://cmake.org/cmake/help/latest/command/add_test.html
- https://cmake.org/cmake/help/latest/command/enable_testing.html

## Verification actually executed

### Published-byte check

The hardened module was read back through GitHub after publication. Local `git hash-object` over the exact bytes used by the harness produced:

`21137534e70252ab096db7edc03cfda1cba3ae5e`

which equals the published Git blob.

### Positive registration harness

A throwaway CMake project used the exact hardened module bytes, twelve minimal placeholder translation units, an `INTERFACE` `SpatialCore` target, testing enabled, and sanitizer mode enabled.

Observed toolchain:

- CMake 3.31.6
- GCC 14.2.0
- Clang 17.0.0

Commands equivalent to:

```sh
cmake -S /tmp/hss-cmake-harness -B /tmp/hss-cmake-harness/build \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
cmake --build /tmp/hss-cmake-harness/build --parallel 4
ctest --test-dir /tmp/hss-cmake-harness/build -N
ctest --test-dir /tmp/hss-cmake-harness/build --output-on-failure

CC=clang CXX=clang++ cmake -S /tmp/hss-cmake-harness \
  -B /tmp/hss-cmake-harness/build-clang \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
cmake --build /tmp/hss-cmake-harness/build-clang --parallel 4
ctest --test-dir /tmp/hss-cmake-harness/build-clang -N
ctest --test-dir /tmp/hss-cmake-harness/build-clang --output-on-failure
```

Observed:

- GCC configure: PASS
- GCC placeholder build: PASS
- GCC `ctest -N`: `Total Tests: 12`
- GCC placeholder execution: 12/12 PASS
- Clang configure: PASS
- Clang placeholder build: PASS
- Clang `ctest -N`: `Total Tests: 12`
- Clang placeholder execution: 12/12 PASS

This validates CMake registration/link-option mechanics only. It is **not** evidence that the twelve real repository test sources compile or pass together.

### Negative missing-source regression

After removing only the placeholder `tests/hss_mesh_anchor_scene_snapshot_test.cpp`, the same module was configured again.

Observed:

- Configure exit: non-zero (`1`)
- CMake stopped generation.
- Diagnostic identified the exact missing required source:
  `Required HSS mesh pipeline test source is missing: tests/hss_mesh_anchor_scene_snapshot_test.cpp`

This proves the registration layer no longer silently reduces required CTest coverage.

### Hosted Actions observed on the current candidate

PR #18 automatically triggered the two stack-provided pull-request workflows on head `ab3ba05a2e788cd194a283f08d8420af6af8be22` before this hardening commit:

- Anchor-patch exact branch verification: run `35978493265`, job `107564515757`
- Plane-fit exact scoring: run `35978493092`, job `107564513937`

Both completed `failure`, but both jobs report `steps=[]`, `runner_id=0`, empty runner name, and requested label `ubuntu-24.04`. Therefore checkout, compiler, verifier, CMake, CTest and sanitizer commands did **not** execute. The hosting cause remains unverified and is not attributed to billing, quota or account state. No manual rerun was requested.

## Evidence boundary / remaining gate

The connected execution container still cannot materialize the complete private repository checkout. Therefore this round did **not** run:

- full reconciled-tree CMake configure/build against all real sources;
- real-source `ctest -N` on the complete checkout;
- full CTest or sanitizer execution on the reconciled tree;
- production `live_reconstruction.cpp` integration;
- Swift/Xcode/simulator tests;
- physical iPhone LiDAR/Metal, thermal/memory, or real-room metrology.

PR #18 remains a draft and must not be merged merely because GitHub reports a clean merge tree or because the registration harness is green.

## Next acceptance

On the actual reconciled checkout, run:

```sh
cmake -S . -B build -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

Acceptance requires all twelve mesh test names above to appear in `ctest -N` in addition to every retained current-main test registration. Any missing expected source must now fail configure rather than silently skip coverage. Only after the exact-tree build/test gate passes should production integration replace arrival-order mesh grouping/slicing/node-edge identity in `live_reconstruction.cpp` with the finalized #11 -> #15 pipeline and final Scene 1.1 permutation regressions.

# ENG-MESH-CURRENT-MAIN-RECONCILIATION-01

## Task

Reconcile the deterministic mesh-reconstruction stack with the actual current `main` line without rewriting the existing stacked PR history, preserve current-main build/test work, and remove the concrete CTest-discovery blocker identified by `ENG-MESH-STACK-INTEGRATION-READINESS-01`.

## Source identities

- Repository: `HostX0/hybrid-spatial-scanner`
- Current main observed before reconciliation: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`)
- Prior stack checkpoint: PR #17 head `afe2c994ebae7af73e736439ac1976e1e2e013d3`
- Prior stack/main merge base: `598b43d4901f2c5f1e5886d80fea6373ddf737a0`
- GitHub synthetic merge commit for PR #18: `ae1e14ec31ff0473b8d119280b8b5a1ae645479d`
- Synthetic merge tree: `4f70d43a61ea8b4d79467ca6d03829c9e9727d4c`
- Synthetic merge parents: current main `4cae5a0...` and stack `afe2c994...`
- Reconciliation PR: #18, `hss/current-main-stack-candidate` -> `main`, draft

GitHub recomputed PR #18 as `mergeable=true`, `rebaseable=true`, `mergeable_state=unstable`. `unstable` is not interpreted as test success; it only establishes that GitHub can produce a three-way merge tree. No merge to `main` was performed.

## Current-main CMake preservation

The current-main root `CMakeLists.txt` blob before this change was:

`3631977c845ff9155776e0ebba495bea4b0e250b`

The stack had never changed that file relative to the old merge base. The reconciled branch was therefore created from GitHub's current-main + stack synthetic merge tree, not by replacing current-main CMake with the stale stack copy.

After adding mesh-test discovery, commit `b21f12073a96af253b662b966bcb49a022a4ca16` changes root `CMakeLists.txt` by exactly two added lines and zero deletions:

```cmake

include(cmake/HSSMeshPipelineTests.cmake)
```

The resulting CMake blob is `068385bd79424dc0b70f0df13ed5b00baa254f09`. This preserves the current-main ZLIB/native archive import, plan-annotation runtime definition/test, dynamic-evidence verifier, B-044 golden-suite registration, and existing live/dynamic/resource runtime integration tests.

## Test registration implementation

New module:

`cmake/HSSMeshPipelineTests.cmake`

Published blob:

`bb7c283235378031467d61bc70a10a650d032f9b`

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

Registration uses the repository's C++20 warnings-as-errors profile and honors `HSS_ENABLE_SANITIZERS` for every target. Tests that call the real non-inline coordinate-basis contract link `SpatialCore`: project batch, reconstruction plan, cut fragments, and scene snapshot. The surface-assembly test deliberately does **not** link `SpatialCore` or `hss_robust_plane_fit`, because that regression defines its own deterministic `hss::plane_fit::fit` stub to verify assembly glue and exact option forwarding; blanket linkage would create a duplicate symbol and misstate the test boundary.

## Verification actually executed

### Published-byte check

The published module was read back from GitHub. A local `git hash-object` over the exact bytes used by the registration harness produced:

`bb7c283235378031467d61bc70a10a650d032f9b`

which equals the published Git blob.

### CMake/CTest registration harness

A local throwaway CMake project used the exact published module bytes, twelve minimal placeholder translation units, and an `INTERFACE` `SpatialCore` target. This checks the module's CMake syntax, target/link-option construction, sanitizer branch, and CTest discovery mechanics only.

Commands:

```sh
cmake -S /tmp/hss-cmake-reg -B /tmp/hss-cmake-reg/build \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
cmake --build /tmp/hss-cmake-reg/build --parallel
ctest --test-dir /tmp/hss-cmake-reg/build -N
ctest --test-dir /tmp/hss-cmake-reg/build --output-on-failure
git hash-object /tmp/hss-cmake-reg/cmake/HSSMeshPipelineTests.cmake
```

Observed:

- Configure: PASS
- Build of registration harness: PASS
- `ctest -N`: exactly 12 mesh-pipeline tests discovered
- Harness execution: 12/12 placeholder tests PASS
- Published-byte identity: PASS (`bb7c283...`)

This harness result is **not** evidence that the twelve real repository test sources compile or pass together. It validates only the new registration layer.

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

Acceptance requires the twelve names above to appear in `ctest -N` in addition to all current-main tests, with no current-main test registration lost. Only after the exact-tree build/test gate passes should production integration replace arrival-order mesh grouping/slicing/node-edge identity in `live_reconstruction.cpp` with the finalized #11 -> #15 pipeline and final Scene 1.1 permutation regressions.

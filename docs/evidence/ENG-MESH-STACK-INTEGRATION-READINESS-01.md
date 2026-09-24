# ENG-MESH-STACK-INTEGRATION-READINESS-01

## Scope

This round is a consolidation and integration-readiness audit for the open deterministic mesh-reconstruction stack. It deliberately does not add another geometry/serialization helper and does not rewrite `SpatialCore/src/live_reconstruction.cpp` without a credible exact-repository compile route.

The audit answers one concrete question: what must be reconciled before PRs #1-#16 can be treated as a tested current-product integration rather than individually targeted dependency work?

## Exact repository identities observed

- Current default branch: `main`
- Current main commit: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`)
- Current stack top: PR #16 / `hss/deterministic-scene-snapshot`
- Stack head: `45453570f42c1d6669191d149835c127bb41e654`
- Stack merge base with current main: `598b43d4901f2c5f1e5886d80fea6373ddf737a0`
- GitHub compare status: `diverged`
- Stack versus current main: 56 commits ahead, 13 commits behind
- Current-main `CMakeLists.txt` blob: `3631977c845ff9155776e0ebba495bea4b0e250b`
- Stack-head `CMakeLists.txt` blob: `1be556891f6af99fc92dfa07aaddf5dd788ed947`
- Merge-base `CMakeLists.txt` blob: `1be556891f6af99fc92dfa07aaddf5dd788ed947`

The last two identities are significant: the stacked work did not modify the root CMake file after the original merge base, while current main did. Any integration procedure that replaces current main's CMake file with the stack copy would discard current-main build/test work.

## Current-main build/test work that must survive reconciliation

The current-main CMake file contains build/test changes absent from the stack copy, including:

- `find_package(ZLIB REQUIRED)` and native archive-import test linkage;
- the `HSS_ENABLE_PLAN_ANNOTATION_RUNTIME=1` SpatialCore definition;
- `hss_dynamic_evidence_verifier` library/test integration;
- the B-044 golden-suite test registration;
- `hss_native_project_import_test`;
- `hss_plan_annotation_runtime_test`;
- current `hss_live_reconstruction_test`, scene-editing adapter and dynamic-runtime integration registrations.

These are independent main-line changes and are not optional casualties of integrating the mesh stack.

## Concrete CTest registration gap

Twelve deterministic mesh pipeline C++ tests exist on the stack but are not registered by the stack `CMakeLists.txt`:

1. `tests/hss_mesh_patch_components_test.cpp`
2. `tests/hss_mesh_patch_runtime_plan_test.cpp`
3. `tests/hss_mesh_patch_surface_binding_test.cpp`
4. `tests/hss_mesh_anchor_patch_plan_test.cpp`
5. `tests/hss_mesh_event_payload_test.cpp`
6. `tests/hss_mesh_anchor_project_batch_test.cpp`
7. `tests/hss_mesh_anchor_reconstruction_plan_test.cpp`
8. `tests/hss_mesh_anchor_cut_fragments_test.cpp`
9. `tests/hss_mesh_anchor_surface_assembly_test.cpp`
10. `tests/hss_mesh_anchor_scene_graph_test.cpp`
11. `tests/hss_mesh_anchor_scene_entities_test.cpp`
12. `tests/hss_mesh_anchor_scene_snapshot_test.cpp`

This means a plain successful `ctest` run on the current stack would not exercise these twelve tests. CMake's own documentation is explicit that `add_test()` registers tests for `ctest`, and that testing metadata is generated after testing is enabled. This audit therefore treats registration on the reconciled actual tree as a required acceptance step, not an optional cleanup.

Do not blindly link every test to all production libraries. Some later targeted tests intentionally use narrow local stubs to characterize glue semantics and avoid duplicate production symbols. Registration/linkage must follow each test's actual contract and then be compiled on the reconciled repository.

## Production integration gap remains unchanged

The current production `SpatialCore/src/live_reconstruction.cpp` is still the authority used by the product. The open helper stack has not replaced its arrival-order mesh behavior. The runtime still retains raw rebased triangles while the derived path uses provisional triangle-order patch identity, immediate architectural slicing, first-observed endpoint welding within the existing join cell and first-wins duplicate edge handling.

Therefore merging the helper files alone is not equivalent to delivering the deterministic runtime behavior. A production claim requires an exact-tree change to the runtime plus final-scene regressions.

## Hosted CI status

The repository pull-request Actions history observed in this round contained three runs total, all associated with the earlier PR #1 / PR #8 verification attempts. The exact-source route did not provide useful compile/test evidence; one previously inspected job had no executed steps and no allocated runner identity. The underlying runner/allocation/account cause remains unverified and is not diagnosed here.

No hosted success is inferred for PR #16 or the later stack merely because no status context exists.

## Minimal safe integration sequence

1. Reconcile the stacked commits onto current `main` without replacing the current-main root CMake file with the merge-base copy.
2. Once the stack's test sources exist on that reconciled tree, register the applicable mesh tests in the *current-main* CMake structure and preserve all current-main targets listed above.
3. Configure and build the reconciled exact repository with warnings-as-errors and sanitizers enabled.
4. Run `ctest -N` first and verify the intended new mesh tests are actually discoverable; then run the complete CTest suite with failure output enabled.
5. Only after exact-tree host evidence passes, wire the complete-anchor preparation -> deterministic cuts -> stable surface assembly -> deterministic scene graph -> Scene 1.1 entity materialization into `live_reconstruction.cpp`.
6. Map stable surface/node/edge keys through the existing internal `id(...)` exactly once, preserve the raw triangle stream/provenance, and retain runtime-owned epochs/features/regions/objects/constraints/issues.
7. Add actual final-scene permutation/topology regressions covering the noisy coplanar wall, supported shallow crease, protected 0.15 m front / 0.50 m returns, disconnected coplanar faces, offset parallel faces, multiple anchor/global vertex bases, malformed payload/transform cases and raw-triangle preservation.
8. Keep Xcode/simulator and physical iPhone LiDAR/Metal/metrology as separate evidence layers.

## Required exact-tree host commands

```sh
cmake -S . -B build -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

`ctest -N` is intentionally included: a zero-failure test run is not useful evidence for tests that were never registered.

## Research basis for this audit

Primary-source references used for the concrete integration bottleneck:

- CMake `add_test`: https://cmake.org/cmake/help/latest/command/add_test.html
- CMake `enable_testing`: https://cmake.org/cmake/help/latest/command/enable_testing.html
- CTest tutorial: https://cmake.org/cmake/help/latest/guide/tutorial/Testing%20and%20CTest.html
- GitHub commit comparison: https://docs.github.com/en/pull-requests/how-tos/commit-changes/comparing-commits

The geometry architecture remains governed by repository D14-D17: raw evidence and calibrated frames remain separate from derived scene products, structural fitting remains separate from display simplification, no Manhattan prior is introduced, nearby parallel faces cannot merge on normals alone, the protected 0.15 m front / 0.50 m returns fixture remains mandatory, and synthetic host evidence is not sensor evidence.

## Evidence boundary

Executed this round:

- authenticated read of current `main`, all open PR metadata through #16, mandatory AGENTS/README/DRIVE_INDEX/D14-D17, current main and stack CMake files, current pull-request Actions history, and the stack test directory;
- authenticated compare of current main against the stack head and merge base;
- exact blob identity check proving stack CMake == merge-base CMake and current-main CMake differs;
- inventory of the twelve stack test sources missing from CTest registration;
- current-primary-source review of CMake/CTest registration semantics and GitHub compare semantics.

Not executed / not claimed:

- full repository CMake build or CTest;
- hosted exact-branch compile success;
- production `live_reconstruction.cpp` integration;
- Xcode/simulator validation;
- physical iPhone LiDAR/Metal validation;
- thermal, memory or real-room metrology validation.

This is blocker-removal evidence for the next exact-tree integration round, not a claim that the open stack is ready to merge into production.
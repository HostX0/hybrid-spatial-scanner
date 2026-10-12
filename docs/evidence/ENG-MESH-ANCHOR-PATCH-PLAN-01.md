# ENG-MESH-ANCHOR-PATCH-PLAN-01 — anchor-local patch integration plan

Date: 2026-09-23
Status: dependency-ready; not production-integrated

## Scope and live repository state

This round closes another production-adjacent gap without rewriting `live_reconstruction.cpp` while the private repository cannot be materialized in the connected container. The new adapter composes the order-invariant component grouper, project-coordinate fit preparation, and validated triangle-to-surface binding into one anchor-local plan. It also makes the local-index/global-snapshot-index boundary explicit, so adding earlier anchors cannot silently rename a later anchor's component identity.

The live default branch changed during the scheduled sprint. At the start of this round `main` was `ab747c36ea0cd46260895268984276f9853fb847` (`Register golden suite in host tests`), two commits ahead of the prior merge base `598b43d4901f2c5f1e5886d80fea6373ddf737a0`. PR #6 remained open at `f5c1571c8e19cb7bf14662e29345c6d5edcc1aac`. GitHub comparison reported the PR #6 branch diverged from current main: 12 commits ahead, 2 commits behind, merge base `598b43d4901f2c5f1e5886d80fea6373ddf737a0`. This round deliberately stacks on PR #6 instead of rebasing or rewriting the existing stack.

Authenticated source inspected:

- `SpatialCore/src/live_reconstruction.cpp` on PR #6: streaming arrival-order patch construction and immediate cut slicing remain in production.
- `SpatialCore/include/hss/mesh_patch_components.hpp`: blob `953864104afbd6f850ef9dfd104b5faf63334860`.
- `SpatialCore/include/hss/mesh_patch_runtime_plan.hpp`: blob `1624c20cf896598a7ae34f5f7dcbbe6aef248e37`.
- `SpatialCore/include/hss/mesh_patch_surface_binding.hpp`: blob `fd90893f883800ff28e17b2c22fcf47ed431bfe6`.
- `SpatialCore/include/hss/robust_plane_fit.hpp`: blob `35735ffc667c45357bef2023c28875ff46219dd1`.

PR #6 had no GitHub Actions workflow runs and no commit status contexts when checked. PR #1's earlier hosted failure-before-steps remains a separate unresolved infrastructure problem.

## Delivered implementation

`SpatialCore/include/hss/mesh_anchor_patch_plan.hpp` adds a small dependency-free C++20 adapter that:

1. Accepts one accepted anchor revision as project-coordinate vertices plus anchor-local indexed triangles, a support identity, and one retained mesh-event diversity group.
2. Calls the existing deterministic runtime plan and validated surface-binding layers exactly once for the complete anchor-local batch.
3. Returns fail-closed typed status, finalized `component_of_triangle`, degenerate/non-manifold counts, bounded fit samples, and one stable surface key per component.
4. Keeps component identity anchor-local: `support_identity + ":patch:" + canonical local indexed triangle`. Global snapshot vertex base is not part of the identity.
5. Adds `rebase_raw_triangle(...)` for the separate raw-output concern. It changes only the retained snapshot indices by a supplied vertex base and returns no value on uint32 overflow, with no partial output.
6. Leaves degenerate but index-valid raw triangles retainable while keeping them explicitly unbound from metric components.

No public C ABI, JSON schema, geometry threshold, robust-fit threshold, semantic prior, raw evidence policy, cloud requirement, or external runtime dependency changes in this slice.

## Regression contract

`tests/hss_mesh_anchor_patch_plan_test.cpp` executes 786 assertions per run. It verifies:

- the mandatory approach / 0.50 m return / 0.15 m front / 0.50 m return / continuation chain remains five surfaces;
- geometry-to-surface-key mapping is identical across 32 triangle-list permutations;
- a 12x12-cell slightly perturbed coplanar wall remains one component, uses exactly the stable 96-sample cap, and retains identical geometry-to-surface binding across 32 permutations;
- synthetic 0.20 degree and 0.50 degree shared-edge creases remain two surfaces across 32 permutations;
- disconnected 20 mm-offset parallel faces remain two surfaces across 32 permutations;
- prepared weighted samples exactly match the supplied project-coordinate vertices, unit weight, and retained mesh-event diversity group;
- rebasing raw triangle indices by a nonzero global vertex base does not change any anchor-local surface key;
- uint32 raw-index overflow fails atomically;
- empty support identity, invalid local indices, and configured component limits fail closed;
- a degenerate triangle remains unbound while an index-valid raw copy can still be retained.

All geometry above is authored synthetic algorithm evidence. The 0.10 mm-scale perturbation and shallow angles are not claims about iPhone LiDAR resolution or physical accuracy.

## Local verification actually executed

The connected container still cannot resolve `github.com`; a private checkout therefore could not be cloned. The new header, test, and verifier were executed locally against contract-compatible transcriptions of the authenticated PR #6 dependency headers. The new files themselves are the exact local bytes intended for publication; upstream dependency blob equality was not asserted for the local transcriptions.

Final commands after the last test edit:

```sh
python3 -m py_compile tools/verify_mesh_anchor_patch_plan.py
python3 tools/verify_mesh_anchor_patch_plan.py --cxx g++
python3 tools/verify_mesh_anchor_patch_plan.py --cxx clang++
python3 tools/verify_mesh_anchor_patch_plan.py --cxx clang++ --sanitize
```

Observed environment and results:

- Python 3.13.5 bytecode compilation: PASS.
- GCC 14.2.0, C++20, `-O2 -Wall -Wextra -Werror -Wpedantic`: PASS, 786/786 checks.
- Clang 17.0.0, same warnings-as-errors profile: PASS, 786/786 checks.
- Clang 17.0.0, ASan + UBSan, warnings-as-errors: PASS, 786/786 checks, no sanitizer diagnostic observed.
- Total targeted assertions observed across the three final executions: 2,358 passed, 0 failed.

Local SHA-256 before publication:

- `mesh_anchor_patch_plan.hpp`: `4643ce1ee3aa94355c96b095dca8213d552fdc9fc72554499459dfd8623bb7d8`
- `hss_mesh_anchor_patch_plan_test.cpp`: `c96752403cacde345df5e68e3586a026cf4e9a976c66b0ed45e600c760e29b62`
- `verify_mesh_anchor_patch_plan.py`: `797916c33dba588e8ffd69438178b529ae66e0141133745c2675d0f7ed97eaf7`

This is targeted Linux host evidence. It is not a full repository CMake/CTest run, Apple build, simulator run, device benchmark, or metrology result.

## Research decision

Apple describes `ARMeshGeometry` as indexed triangle geometry belonging to an `ARMeshAnchor`, which supports keeping component identity local to that anchor's indexed topology. Open3D's connected-triangle clustering explicitly returns a cluster index for each triangle. PCL RegionGrowing keeps normal smoothness and residual/curvature checks as separate criteria. Together with D14-D17, these references support explicit anchor-local connectivity and per-triangle attribution rather than using global snapshot offsets or triangle arrival order as structural identity. No external source code was copied and no external runtime dependency was added.

Primary references reviewed:

- Apple, `ARMeshGeometry`: https://developer.apple.com/documentation/arkit/armeshgeometry
- Apple, `ARGeometrySource`: https://developer.apple.com/documentation/arkit/argeometrysource
- Open3D, `TriangleMesh.cluster_connected_triangles`: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html
- PCL, Region Growing segmentation: https://pointclouds.org/documentation/tutorials/region_growing_segmentation.html

## Production integration gate still open

This adapter is not a runtime fix until `live_reconstruction.cpp` is compiled and tested against the actual repository. The next production integration must batch the complete anchor revision before cut-surface attribution, preserve every raw triangle and source relationship, use finalized per-triangle component binding for `slice(...)`, feed the existing robust plane fit from prepared bounded samples, and surface explicit metric-work issues on grouping/binding failure rather than inventing closure.

Required native regressions remain final-scene JSON/topology comparisons across triangle-order permutations for the noisy coplanar wall, supported shallow crease, mandatory 0.15 m front/0.50 m returns, disconnected coplanar surfaces, and offset parallel surfaces. Full CTest/sanitizers must run against the real checkout before runtime integration is merged. Xcode/simulator and physical iPhone LiDAR/Metal/metrology remain separate higher verification layers.

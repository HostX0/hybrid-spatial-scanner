# ENG-MESH-PATCH-RUNTIME-PLAN-01 — deterministic runtime integration plan

Date: 2026-09-23
Status: dependency-ready; not production-integrated

## Scope

This slice closes a concrete integration gap between the order-invariant anchor-local mesh grouping introduced by `ENG-MESH-PATCH-COMPONENTS-01` and the existing live reconstruction pipeline. It adds a small C++20 adapter that converts a complete anchor-local grouping result into deterministic bounded `plane_fit::WeightedSample` inputs and preserves a per-triangle component map for later cut-edge/surface attribution.

It does **not** modify `SpatialCore/src/live_reconstruction.cpp`, the public C ABI, scene schema, thresholds, captured triangle output, topology semantics, or any cloud/AI behavior. It is intentionally staged before the runtime rewrite because the connected execution environment cannot clone the private repository for a full SpatialCore/CTest build.

## Source identities inspected

- Stack base / PR #4 head: `0435f54e1d8642ccf86c50ff41a4d0df9f9b5b8a`.
- Production runtime inspected: `SpatialCore/src/live_reconstruction.cpp`, blob `883465c0b83f6a37f7402daf625c981c11c685ca`.
- Grouping dependency inspected: `SpatialCore/include/hss/mesh_patch_components.hpp`, blob `953864104afbd6f850ef9dfd104b5faf63334860`.
- Plane-fit interface inspected: `SpatialCore/include/hss/robust_plane_fit.hpp`, blob `35735ffc667c45357bef2023c28875ff46219dd1`.
- `main` was re-read immediately before publication and remained `598b43d4901f2c5f1e5886d80fea6373ddf737a0`.

## Delivered adapter

`SpatialCore/include/hss/mesh_patch_runtime_plan.hpp` provides:

1. A typed fail-closed status map from the component grouper.
2. A deterministic `ComponentPlan` containing the canonical indexed triangle, component triangle count, bounded selected vertex IDs, and corresponding project-coordinate `WeightedSample` values.
3. Preservation of `component_of_triangle`, including explicit `npos` for degenerate triangles, so the later production integration can attach each architectural cut fragment to the finalized component identity instead of the first triangle encountered.
4. `canonical_component_suffix(...)`, based only on the canonical indexed triangle, so the runtime can replace arrival-order-derived patch keys with anchor-local geometry-derived keys.
5. One caller-supplied diversity group copied to every prepared sample. The intended live caller is the retained mesh event sequence; this does not claim independent sensor evidence inside one mesh event.

The underlying component helper still owns the 96-unique-vertex cap and the unchanged 0.99999 normal / 2 mm symmetric cross-plane grouping gates. This adapter does not widen them.

## Regression contract

`tests/hss_mesh_patch_runtime_plan_test.cpp` verifies:

- a 20×10, 50 mm-cell synthetic wall with deterministic 0.10 mm height perturbation remains one component and produces exactly 96 stable selected project-coordinate samples;
- 0.20° and 0.50° synthetic two-face creases remain two components under 32 triangle-list permutations;
- the mandatory continuation / 0.50 m return / 0.15 m front / 0.50 m return / continuation fixture remains five components under 32 permutations;
- disconnected coplanar and 20 mm-offset parallel faces remain separate;
- selected `WeightedSample` coordinates exactly equal the indexed project-coordinate vertices, with weight `1.0` and the requested diversity group;
- the per-triangle component partition and canonical component keys remain stable across permutations;
- degenerate triangles remain explicit `npos` evidence rather than generating geometry;
- invalid indexed geometry and invalid resource options fail closed.

These are deterministic algorithm fixtures. The 0.10 mm perturbation and 0.20° crease are not iPhone LiDAR accuracy/resolution claims.

## Local verification actually executed

The connected Linux x86_64 container does not have a materialized private-repository checkout. For this targeted header-only slice, the new adapter/test were compiled against a local transcription of the interfaces and behavior read from the authenticated PR #4 `mesh_patch_components.hpp` and `robust_plane_fit.hpp`. This is useful host evidence but is **not** a full-repository build and the local dependency-file blob identities were not asserted equal to the GitHub blobs.

Executed successfully after the final test edit:

```sh
python3 -m py_compile tools/verify_mesh_patch_runtime_plan.py
python3 tools/verify_mesh_patch_runtime_plan.py --cxx g++
python3 tools/verify_mesh_patch_runtime_plan.py --cxx clang++
python3 tools/verify_mesh_patch_runtime_plan.py --cxx clang++ --sanitize
```

Observed toolchains:

- GCC 14.2.0: C++20, `-O2 -Wall -Wextra -Werror -Wpedantic` — PASS.
- Clang 17.0.0: C++20, `-O2 -Wall -Wextra -Werror -Wpedantic` — PASS.
- Clang 17.0.0: C++20, ASan + UBSan, warnings as errors — PASS, no sanitizer diagnostic observed.
- Python verifier bytecode compilation — PASS.

Each compiled run printed `mesh patch runtime plan tests: PASS` and the verifier printed `mesh patch runtime plan verification: PASS`.

## Research decision

Apple documents `ARMeshGeometry` as geometry for one scene-mesh anchor, with indexed face vertex data and a normal per face. Open3D exposes connected-triangle clustering that returns a cluster index for every triangle. PCL RegionGrowing keeps smoothness and residual/curvature tests as distinct controls. These sources support preserving explicit indexed connectivity and a per-triangle component assignment instead of using triangle arrival order as architectural identity. No Open3D/PCL source is copied and neither library becomes a runtime dependency.

Primary references reviewed this round:

- Apple, `ARMeshGeometry`: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D 0.20, `TriangleMesh.cluster_connected_triangles`: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html
- PCL, `pcl::RegionGrowing`: https://pointclouds.org/documentation/classpcl_1_1_region_growing.html

## Production integration acceptance left open

The next runtime slice should only be claimed after all of the following are implemented and compiled against the actual repository:

1. Build the complete anchor-local triangle array and transformed project-coordinate vertex view before grouping.
2. Call the runtime plan once per accepted anchor revision; derive patch IDs from `support_id + canonical_component_suffix` rather than triangle arrival index.
3. Preserve every raw captured triangle unchanged in `Snapshot::triangles`.
4. Associate each triangle's architectural cut fragment with `component_of_triangle` and therefore the finalized component surface ID.
5. Feed only the prepared bounded `fit_samples` into the existing robust plane fit; preserve source support/event provenance.
6. On invalid/resource grouping status, preserve raw mesh/source evidence, surface an explicit issue, and do not invent/close metric topology.
7. Add compiled `hss_live_reconstruction_test` permutations that compare final scene JSON/topology for the noisy wall, shallow crease, 0.15 m front/0.50 m returns, disconnected coplanar and offset parallel fixtures.
8. Run applicable full CTest plus sanitizer coverage before merging a production-runtime change.

## Not run / not established

- Production `live_reconstruction.cpp` integration.
- Full repository CMake/CTest.
- Xcode/iOS compilation or simulator tests for this change.
- Physical iPhone LiDAR/ARKit capture.
- Metal/device memory, thermal or frame-budget profiling.
- Real-room metrology or competitor comparison.

No product accuracy, feature-resolution, app-FPS, thermal, or superiority claim follows from this host slice.

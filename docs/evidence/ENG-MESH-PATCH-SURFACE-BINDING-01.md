# ENG-MESH-PATCH-SURFACE-BINDING-01 — stable triangle-to-surface attribution

Date: 2026-09-23
Status: dependency-ready; not production-integrated

## Scope

This slice closes the identity gap between the deterministic anchor-local component plan in `mesh_patch_runtime_plan.hpp` and the existing live architectural cut path. The current production runtime associates each cut fragment with a patch selected while triangles are streamed; the patch identity is derived from the triangle arrival index. The new binding layer validates a completed runtime plan and exposes one stable surface key per non-degenerate input triangle, derived from the anchor support identity plus the component's canonical indexed triangle.

It does not modify `live_reconstruction.cpp`, public C ABI, scene schema, grouping thresholds, raw triangle storage, robust-plane thresholds, semantics, or cloud/AI behavior.

## Live source inspected

- `main`: `598b43d4901f2c5f1e5886d80fea6373ddf737a0`.
- Stack base / PR #5 head: `8fbff87dda7bd6fdfd89b5499f2a78a6f0e8f481`.
- `SpatialCore/src/live_reconstruction.cpp`: blob `883465c0b83f6a37f7402daf625c981c11c685ca`.
- `SpatialCore/include/hss/mesh_patch_components.hpp`: blob `953864104afbd6f850ef9dfd104b5faf63334860`.
- `SpatialCore/include/hss/mesh_patch_runtime_plan.hpp`: blob `1624c20cf896598a7ae34f5f7dcbbe6aef248e37`.
- `SpatialCore/include/hss/robust_plane_fit.hpp`: blob `35735ffc667c45357bef2023c28875ff46219dd1`.

The runtime still streams one triangle at a time, picks the first compatible adjacent patch, derives a new patch identity from `b.triangle`, and immediately passes that identity into `slice(...)`. The completed runtime plan from PR #5 already preserves `component_of_triangle`; this slice provides the integrity-checked bridge needed to use that finalized mapping for cut-surface attribution.

## Delivered implementation

`SpatialCore/include/hss/mesh_patch_surface_binding.hpp` adds:

1. A fail-closed validation pass over a completed `mesh_patch_runtime::Plan`.
2. Exact preservation of the per-triangle component assignment and explicit `npos` for degenerate triangles.
3. Strict checks that mapped triangle counts agree with each component's declared count, degenerate counts agree, fit-sample and selected-vertex counts agree, every mapped component exists, and canonical component identities are strictly ordered and unique.
4. A stable component suffix based on the canonical indexed triangle.
5. `surface_key_for_triangle(...)`, which returns no key for upstream failure, invalid plans, out-of-range/degenerate triangles, or an empty support identity. Valid triangles receive `support_identity + ":patch:" + canonical_component_suffix`.

The layer intentionally does not hash or emit scene UUIDs. `live_reconstruction.cpp` owns its existing derived-ID function and can convert the returned stable key without changing the public contract.

## Regression contract

`tests/hss_mesh_patch_surface_binding_test.cpp` verifies:

- the five-face approach / return / front / return / continuation topology remains five distinct surface keys;
- both triangles belonging to each face bind to the same surface key;
- 32 deterministic triangle-list permutations preserve the geometry-to-surface-key mapping exactly;
- degenerate triangles remain unbound instead of inheriting a neighboring surface;
- empty support identity cannot manufacture a surface key;
- out-of-range component references fail closed;
- triangle-count mismatches fail closed;
- degenerate-count mismatches fail closed;
- duplicate/non-strict canonical component identities fail closed;
- selected-vertex / fit-sample count mismatches fail closed;
- upstream resource failure remains an upstream failure and exposes no usable binding.

These are deterministic algorithm fixtures, not sensor accuracy or physical metrology evidence.

## Local verification actually executed

The connected Linux container still cannot resolve `github.com`, so the private repository could not be cloned/materialized. The binding test was therefore compiled against a minimal contract-compatible local stub reproducing the authenticated PR #5 `Plan`, `ComponentPlan`, status, canonical-suffix, and weighted-sample type shapes that the new header directly consumes. The grouping implementation itself was not reimplemented or revalidated in this round.

Executed after the final test edit:

```sh
python3 -m py_compile tools/verify_mesh_patch_surface_binding.py
python3 tools/verify_mesh_patch_surface_binding.py --cxx g++
python3 tools/verify_mesh_patch_surface_binding.py --cxx clang++
python3 tools/verify_mesh_patch_surface_binding.py --cxx clang++ --sanitize
```

Observed toolchains and results:

- GCC 14.2.0, C++20 `-O2 -Wall -Wextra -Werror -Wpedantic`: PASS.
- Clang 17.0.0, C++20 `-O2 -Wall -Wextra -Werror -Wpedantic`: PASS.
- Clang 17.0.0 with ASan + UBSan: PASS; no sanitizer diagnostic observed.
- Python 3.13.5 verifier bytecode compilation: PASS.

This is targeted host evidence for the new binding logic. It is not a full repository CMake/CTest build and does not establish Apple-platform or device behavior.

## Research decision

Apple documents `ARMeshGeometry` as indexed triangle geometry for one mesh anchor, with each face represented by vertex indices and a face normal. Open3D's connected-triangle clustering returns a cluster index for every triangle, and PCL RegionGrowing keeps smoothness and residual/curvature tests as separate geometric controls. These references support retaining explicit per-triangle component attribution through later topology stages instead of allowing triangle arrival position to become surface identity. No external source code or runtime dependency is introduced.

Primary references reviewed:

- Apple, `ARMeshGeometry`: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D, `TriangleMesh.cluster_connected_triangles`: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html
- PCL, `pcl::RegionGrowing`: https://pointclouds.org/documentation/classpcl_1_1_region_growing.html

## Production integration acceptance left open

The next production slice should only be claimed after the actual repository can be compiled with the change. It should build the complete anchor-local runtime plan before assigning cut-surface IDs, use `surface_key_for_triangle(...)` for every non-degenerate cut fragment, preserve raw triangles and provenance unchanged, feed the existing robust fit from the prepared component samples, and fail closed with an explicit issue on invalid/resource grouping status. Final `hss_live_reconstruction_test` regressions must compare scene topology across triangle-order permutations for the noisy coplanar wall, supported shallow crease, mandatory 0.15 m front / 0.50 m returns, disconnected coplanar faces, and offset parallel faces.

## Not run / not established

- Production `live_reconstruction.cpp` integration.
- Full repository CMake/CTest.
- Hosted workflow at the new head before publication.
- Xcode/iOS compilation or simulator tests.
- Physical iPhone LiDAR/ARKit capture.
- Metal/device thermal or memory profiling.
- Real-room metrology or competitor comparison.

No app-level speed, accuracy, feature-resolution, thermal, or superiority claim follows from this slice.

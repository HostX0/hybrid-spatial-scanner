# ENG-MESH-ANCHOR-SURFACE-ASSEMBLY-01

## Task

Reduce the remaining production-integration risk between the complete anchor preparation / deterministic architectural cut stack and `live_reconstruction.cpp` scene-surface publication without rewriting the production runtime in an environment that still cannot materialize the full private repository.

Base: PR #12 head `40b8e85db8e0f28b890dff2353dcb776e068c097` (`hss/stable-cut-fragments`).

Delivered slice: `SpatialCore/include/hss/mesh_anchor_surface_assembly.hpp` plus a deterministic regression and portable repository verifier. This slice is dependency-ready; it does not modify `live_reconstruction.cpp`.

## Live source and contract facts re-read before implementation

Current `main` was `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` when the slice was prepared. The live `SpatialCore/src/live_reconstruction.cpp` blob was `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`.

The live runtime currently:

- retains raw rebased mesh triangles;
- creates arrival-order patch identities while streaming triangles;
- fits each patch later with `inlier_threshold_m=0.002`, `max_input_samples=96`, `candidate_iterations=24`, `max_refinement_iterations=4`, `max_residual_evaluations=4096`, `min_support_samples=3`, and `min_distinct_support_points=3`;
- publishes a fitted plane when `plane_fit::fit` returns one and otherwise preserves that patch as a mesh-patch fallback;
- uses the half-open architectural-cut rule and `1e-12 m^2` minimum segment guard.

PR #11 already separates valid capture truth from derived metric-plan failure. PR #12 already produces deterministic cut fragments with stable per-triangle surface keys and retained source-triangle attribution. The missing assembly boundary was a checked, deterministic mapping from finalized components and fragments into plane-fit surface records.

D14-D17 remain authoritative: raw evidence and calibrated frames are retained, structural fitting is independent of display simplification, no Manhattan/right-angle prior is introduced, similar normals alone cannot merge nearby parallel surfaces, the protected 0.15 m front / 0.50 m returns fixture remains mandatory, synthetic checks are not sensor evidence, and no C++ exception may cross the C facade.

## Implementation

`mesh_anchor_surface_assembly.hpp`:

- consumes a completed `mesh_anchor_reconstruction::Prepared` and a successful `mesh_anchor_cut::Result`;
- requires the same support identity across the anchor metric plan and cut result;
- validates strict canonical component ordering, unique support-scoped stable surface keys, component triangle counts, component-map bounds, the 96-sample live cap, and exact selected-index / fit-sample cardinality;
- cross-checks every cut fragment's source triangle and local triangle against the finalized `component_of_triangle` mapping before exposing its surface attribution;
- rejects duplicate cut attribution for one source triangle and fragments whose stable key disagrees with the finalized component;
- preserves deterministic component order and cut-fragment indexes from the already deterministic PR #12 result;
- calls the existing robust plane fitter exactly once per finalized component using a centralized copy of the current live bounded fitting options;
- keeps a component as a stable mesh-patch fallback when plane fitting returns no plane, preserving the fit status instead of deleting the surface or closing topology;
- returns typed capture/metric/cut/argument/plan/resource failure and exposes no partial surface vector on validation/allocation failure;
- changes no public ABI/schema, capture bytes, raw triangle output, coordinate relationship, grouping threshold, fitting threshold, persistence, semantics, cloud/AI behavior, or external dependency set.

No scene UUID/hash is generated here. The future runtime integration can map each stable `surface_key` through the existing internal `id(...)` exactly once.

## Deterministic regression

`tests/hss_mesh_anchor_surface_assembly_test.cpp` executes 1,207 assertions per compiled run. It checks:

- five protected face components remain five stable surface records;
- 32 deterministic triangle-list permutations preserve the same ordered surface signature;
- one deterministic cut-fragment index remains bound to each protected component;
- the current live plane-fit option values are passed exactly on every component fit;
- one intentional plane-fit failure remains a stable mesh-patch fallback with its failure status rather than becoming an assembly failure;
- capture, metric and cut upstream failures remain distinct;
- support mismatch, zero/oversized surface budgets, duplicate/cross-support keys, noncanonical component ordering, triangle-count mismatch, out-of-range component mapping, >96 fit samples, selected-index/sample mismatch, bad cut source triangle, local-triangle mismatch, surface/component mismatch, and duplicate cut attribution all fail closed.

The permutation fixture is synthetic algorithm evidence, not iPhone LiDAR angular/feature-resolution evidence.

## Commands and observed results

Connected Linux x86_64 environment:

```text
g++ --version
# g++ (Debian 14.2.0-19) 14.2.0

clang++ --version
# clang version 17.0.0

python3 --version
# Python 3.13.5
```

Targeted compilation against the exact new helper/test bytes and contract-compatible local stubs of the authenticated PR #11 / PR #12 data shapes plus the exact `robust_plane_fit.hpp` public shape:

```text
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I new/SpatialCore/include -I stubs \
  new/tests/hss_mesh_anchor_surface_assembly_test.cpp -o gcc_test
./gcc_test
# assertions=1207 failures=0

clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I new/SpatialCore/include -I stubs \
  new/tests/hss_mesh_anchor_surface_assembly_test.cpp -o clang_test
./clang_test
# assertions=1207 failures=0

clang++ -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I new/SpatialCore/include -I stubs \
  new/tests/hss_mesh_anchor_surface_assembly_test.cpp -o asan_test
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./asan_test
# assertions=1207 failures=0
# no sanitizer diagnostic observed

python3 -m py_compile tools/verify_mesh_anchor_surface_assembly.py
# PASS
```

Total final compiled assertions across the three C++ runs: 3,621 passed, 0 failed.

## Evidence boundary

The connected container still cannot clone/materialize the complete private repository. Therefore this round did **not** execute full repository CMake/CTest or link the exact `robust_plane_fit.cpp` implementation. The plane fitter was represented by a deterministic test stub that records and validates the options passed by the new assembly layer and exercises fit-success / fit-failure propagation. Robust fitting itself remains covered by its separate production tests/evidence, including PR #1; this round does not upgrade that evidence.

The checked-in verifier is written for the real repository and compiles the new test with the actual `SpatialCore/src/robust_plane_fit.cpp` once an exact checkout/runner is available. It was syntax-checked here, but not executed against the inaccessible full checkout.

Not run in this slice: full CMake/CTest, exact hosted-branch execution, Swift portable tests, Xcode/iOS simulator, physical iPhone LiDAR/ARKit/Metal, thermal/memory profiling, real-room metrology, export validation, or competitive benchmarks.

## Research basis

Primary/current references reviewed this round:

- Apple `ARMeshGeometry`: one scene-mesh anchor owns indexed faces and normals. This supports keeping stable per-triangle/component attribution explicit rather than deriving semantic identity from iteration order.
- Open3D 0.20 `TriangleMesh::ClusterConnectedTriangles`: returns a cluster assignment for every triangle plus per-cluster counts/areas. This is an architectural reference for explicit triangle-to-component state, not a shipping dependency.
- PCL `sample_consensus`: treats plane fitting as a model-estimation operation over supplied points. This supports keeping grouping/attribution separate from the plane-fit stage instead of allowing the fitter to decide topology.

No Apple/Open3D/PCL source code was copied. No runtime dependency was added.

## Remaining production gate

When an exact repository compile route is available, the highest-value next step is to wire the composed stack into `live_reconstruction.cpp`:

1. prepare each accepted anchor once through PR #11;
2. create deterministic architectural cuts through PR #12;
3. assemble stable surfaces through this helper;
4. map each stable surface key through the existing internal `id(...)` once;
5. preserve the exact raw triangle stream and source provenance;
6. build nodes/edges from deterministic fragments and fit surfaces from finalized component samples;
7. emit explicit metric issues on derived-plan/resource failure without inventing topology;
8. compare final scene JSON/topology across triangle permutations, global vertex bases, noisy coplanar wall, supported shallow crease, protected 0.15 m front / 0.50 m returns, disconnected coplanar surfaces and offset parallel faces.

A runtime merge claim still requires applicable full CTest/sanitizer evidence on the actual repository. Apple simulator and physical device/metrology gates remain separate.

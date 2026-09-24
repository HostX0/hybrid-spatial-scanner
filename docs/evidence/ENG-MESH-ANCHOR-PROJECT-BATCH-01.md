# ENG-MESH-ANCHOR-PROJECT-BATCH-01 — Anchor-local mesh to project-coordinate batch

Date: 2026-09-23

## Scope

This round adds one dependency-ready C++20 batch boundary between the existing owned `HSSMeshEvent` payload decoder and the future order-invariant anchor-local patch-plan integration. It does **not** modify `live_reconstruction.cpp`, the public C ABI/schema, reconstruction thresholds, raw-triangle output, provenance rules, persistence, semantics, or cloud behavior.

The new helper decodes one complete accepted mesh-anchor payload and transforms all anchor-local vertices into canonical project metres using the same ordered transform chain already used by the live runtime: `T_world_anchor` followed by the accepted epoch `T_project_world`. Triangle indices stay anchor-local and byte/topology-preserving; global snapshot rebasing remains a separate operation.

## Live source identities reviewed before implementation

- Repository: `HostX0/hybrid-spatial-scanner`
- Current `main` observed before publication: `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`).
- Stacked parent PR #9 head: `c781fc2e2c026ceedd4512cd8c4fd39e5eb62d36` on branch `hss/mesh-event-payload-normalization`.
- New branch: `hss/anchor-project-batch`, created exactly from PR #9 head without rebasing or rewriting the existing stack.
- `SpatialCore/src/live_reconstruction.cpp` on current `main` was re-read. It still transforms each mesh vertex with `T_world_anchor`, then the accepted epoch `T_project_world`, rejects project points outside the existing bounded coordinate domain, retains raw triangle topology, and still uses arrival-order patch grouping later in the pipeline.
- `SpatialCore/include/hss/coordinate_basis.hpp` and `SpatialCore/src/coordinate_basis.cpp` were re-read. `HSSMat4d` is column-major, rigid transforms are finite/orthonormal with determinant near +1, and `transform_point` applies `R*p+t`.
- `include/hss/hss_api.h` was re-read. `HSSMeshEvent` exposes anchor-local tightly packed XYZ Float32 metres plus UInt32 triangle indices and `T_world_anchor`.
- PR #9 `SpatialCore/include/hss/mesh_event_payload.hpp` was re-read before composing it here.

The open stack remains intentionally non-destructive. Comparing current main with PR #9 showed divergence from the original merge base, so this slice does not force-rebase or rewrite prior PR history.

## Normative constraints preserved

D14-D17 require source/derived separation, calibrated coordinate frames, arbitrary supported angles, no Manhattan/right-angle prior, retained raw evidence and explicit evidence-layer reporting. D15 defines canonical project coordinates in metres and column-major rigid transforms. D16 requires rigid-transform validation and forbids pretending a numerically shaped matrix establishes a valid unrelated epoch relationship. This helper assumes the caller supplies the already accepted epoch transform and does not create or infer one.

## Implementation

`SpatialCore/include/hss/mesh_anchor_project_batch.hpp`:

- consumes a caller-owned byte span plus validated mesh counts;
- delegates owned-payload decoding to PR #9's exact decoder rather than reimplementing byte parsing;
- propagates payload size, geometry and resource failures fail-closed;
- validates both `T_world_anchor` and `T_project_world` through the existing `coordinate::is_rigid_transform` contract;
- converts decoded Float32 anchor-local coordinates to `HSSVec3d` and applies `anchor-local -> world -> project` exactly in that order;
- rejects nonfinite or project-coordinate results outside the existing absolute 10,000 m runtime bound;
- retains anchor-local triangle indices exactly and rechecks them defensively against the transformed vertex batch;
- clears partial derived output on transform/geometry/resource failure;
- never mutates or retains the source payload;
- performs no global snapshot-index rebasing, topology repair, grouping, plane fitting, semantic classification or surface generation.

No third-party runtime dependency was added.

## Regression coverage

`tests/hss_mesh_anchor_project_batch_test.cpp` contains 60 assertions covering:

- identity-transform round trip, exact local topology and input-byte immutability;
- an independently calculated two-stage rigid transform (90-degree Z anchor-to-world rotation plus translation, then 90-degree Y world-to-project rotation plus translation);
- empty and vertex-only batches consistent with the existing payload decoder;
- rejection of scale, reflection and nonfinite transforms;
- exact acceptance of the 10,000 m project bound and rejection just beyond it;
- rejection when a locally valid coordinate is transformed outside the project bound;
- propagation of truncated-payload, invalid-index and count-ceiling failures;
- exact triangle-index invariance under different valid transforms.

The transform fixture checks explicit expected project coordinates rather than calling the production transform helper to construct its expected values.

## Targeted local verification

Connected Linux x86_64 environment:

- GCC: `g++ (Debian 14.2.0-19) 14.2.0`
- Clang: `clang version 17.0.0 (https://github.com/swiftlang/llvm-project.git 10999b6d034fe318f3d56c83bddb6572593a8bb0)`
- Python: `3.13.5`

Observed commands/results after the final implementation bytes:

```text
python3 -m py_compile tools/verify_mesh_anchor_project_batch.py
PASS (exit 0)

python3 tools/verify_mesh_anchor_project_batch.py --cxx g++
PASS: assertions=60 failures=0

python3 tools/verify_mesh_anchor_project_batch.py --cxx clang++
PASS: assertions=60 failures=0

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  python3 tools/verify_mesh_anchor_project_batch.py --cxx clang++ --sanitize
PASS: assertions=60 failures=0; no sanitizer diagnostic observed
```

This was targeted host compilation of the new helper/test against the exact PR #9 decoder content and the current coordinate-basis contract/implementation mirrored into the connected test workspace. The connected environment still could not materialize the private repository as a complete checkout, so this is **not** a full repository CMake/CTest run or exact hosted branch execution.

## Research decision

Primary references reviewed for this slice:

- Apple ARMeshGeometry: https://developer.apple.com/documentation/arkit/armeshgeometry
- Apple ARMeshAnchor.geometry: https://developer.apple.com/documentation/arkit/armeshanchor/geometry
- Apple MeshAnchor.originFromAnchorTransform: https://developer.apple.com/documentation/arkit/meshanchor/originfromanchortransform
- Apple ARGeometrySource: https://developer.apple.com/documentation/arkit/argeometrysource
- Open3D geometry transform documentation: https://www.open3d.org/docs/latest/tutorial/geometry/transformation.html

The architecture follows the already-selected product contracts: keep indexed geometry local to its mesh anchor, apply explicit rigid frame relationships to derive project coordinates, and keep topology separate from global storage placement. No Apple, Open3D or other third-party source code was copied, and no new dependency was introduced.

## Evidence boundaries

This slice is a dependency-ready transformation boundary. It is **not** evidence of:

- production `live_reconstruction.cpp` integration;
- full repository CMake/CTest success;
- successful hosted GitHub Actions execution;
- Xcode/iOS simulator success for this branch;
- physical iPhone LiDAR/Metal behavior;
- real-room accuracy, repeatability, thermal performance or competitor comparison.

Synthetic transform fixtures validate arithmetic and failure behavior only; they do not establish sensor accuracy.

## Next dependency

When a credible exact-repository compile path is available, production integration remains the next gate: consume the owned mesh payload through PR #9, prepare one complete project-coordinate batch through this helper, invoke the PR #7 anchor-local patch plan once per accepted anchor revision, derive stable scene surface identities from finalized component keys, preserve the original raw triangle output and provenance, and bind every architectural `slice(...)` fragment to its finalized per-triangle component surface. Add final-scene JSON/topology permutation regressions for noisy coplanar walls, supported shallow creases, the mandatory 0.15 m front / 0.50 m returns, disconnected/offset parallel faces, multiple anchors/global vertex bases, malformed payloads and raw topology preservation. Applicable full CTest and sanitizer coverage must pass before calling runtime integration complete.

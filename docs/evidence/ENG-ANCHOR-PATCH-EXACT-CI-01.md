# ENG-ANCHOR-PATCH-EXACT-CI-01 — Exact-source hosted verification attempt

Date: 2026-09-23

## Scope

This evidence record covers one narrow verification slice stacked on PR #7. It does not change reconstruction behavior, thresholds, public ABI/schema, captured geometry, semantics, or provenance rules.

The goal was to close one known evidence gap: PR #7's anchor-local patch-plan regression was compiled in the connected Linux container against contract-compatible transcriptions because the private repository could not be materialized there. This round therefore added a read-only GitHub Actions workflow that compiles the exact repository branch sources on a clean hosted runner.

## Source identities

- Repository: `HostX0/hybrid-spatial-scanner`
- Main observed before publication: `77380bcdc7e90ba23d1ee9e8b549221012221b47`
- PR #7 base/head for this slice: `937dc3ec7e1c75e3dbdc80157cf264421c7183a2`
- Verification branch: `hss/anchor-patch-exact-ci`
- First workflow commit: `6850ed9d1c76bb2ee1b0464964abf8691ea27db5`
- Workflow: `.github/workflows/anchor-patch-plan.yml`

Current main and the PR #7 stack are diverged. This slice intentionally remains stacked on PR #7 and does not rewrite or force-rebase the existing open stack.

## Workflow contract

The workflow:

- uses `ubuntu-24.04`;
- grants only `contents: read`;
- pins `actions/checkout` to `11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2);
- persists no checkout credentials;
- records the exact checkout SHA and GCC/Clang/Python versions;
- runs `python3 tools/verify_mesh_anchor_patch_plan.py --cxx g++`;
- runs the same verifier with `clang++`;
- runs the same verifier with `clang++ --sanitize` for ASan+UBSan;
- uses no secrets, deployment, external service, paid inference, or private capture data.

## Hosted result actually observed

GitHub Actions run `35929903068`, job `107413717308`, head SHA `6850ed9d1c76bb2ee1b0464964abf8691ea27db5`:

- run status: `completed`;
- conclusion: `failure`;
- job conclusion: `failure`;
- job steps: empty (`[]`);
- runner id: `0`;
- runner name: empty;
- requested label: `ubuntu-24.04`.

No checkout step, compiler invocation, Python verifier, unit assertion, sanitizer, CMake, CTest, Xcode, simulator, or device test ran in this hosted attempt. Therefore this result is **not** a failed anchor-patch regression; it is a pre-step hosted-execution failure.

The underlying runner/allocation/account cause is not verified by the available evidence and is deliberately not guessed. The earlier independent plane-fit workflow on PR #1 showed the same observable pre-step shape. Re-running the unchanged hosted route is not useful engineering progress until the hosting condition changes or more diagnostic evidence becomes available.

## Research decision

The exact-source verification remains valuable because the production mesh path consumes indexed AR mesh triangles and the new helper stack depends on preserving per-triangle attribution. Apple documents AR mesh geometry as indexed faces within an anchor. Open3D's connected-triangle clustering likewise maintains a cluster assignment per triangle, and PCL Region Growing separates connectivity/smoothness decisions from residual/curvature tests. These references support explicit indexed topology and per-triangle binding; they do not establish this product's physical accuracy or justify importing those libraries.

Primary references reviewed this round:

- Apple ARMeshGeometry: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D TriangleMesh connected-triangle clustering: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html
- PCL RegionGrowing: https://pointclouds.org/documentation/classpcl_1_1_region_growing.html

No third-party source code or runtime dependency was added.

## Production gap and next dependency

`SpatialCore/src/live_reconstruction.cpp` still groups one triangle at a time, selects the first compatible arrival-order patch, names a new patch from the current triangle position, and slices immediately with that provisional identity. PRs #4–#7 are not wired into this path.

If an exact repository compile becomes available, the next production change should integrate PR #7 into the anchor lifecycle and compare final scene JSON/topology across triangle-order permutations while preserving raw triangle output byte-for-byte/topology-for-topology.

If credible compiled production integration remains unavailable, the next useful dependency-ready slice is a bounded anchor-payload extraction/normalization helper based on the existing v1 mesh-event contract: tightly packed host-representation Float32 XYZ vertices followed by UInt32 triangle indices in the owned event payload, with explicit count/size/overflow validation. That helper must not change the public ABI and must be tested against malformed/truncated payloads before production wiring.

## Evidence boundaries

This round establishes only:

1. the exact-source workflow was published on the stated branch; and
2. GitHub attempted the job but failed before any step executed.

It does not establish a passing hosted build, full repository CTest, iOS/Xcode compatibility, physical LiDAR behavior, Metal performance, thermal behavior, real-room metrology, or competitor superiority.

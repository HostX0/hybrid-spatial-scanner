# ENG-LIVE-PATCH-GROUPING-01 — deterministic live grouping characterization

Date: 2026-09-23

Stack base: `23c812f7acbc28e3913dfc86c91a192f8084a798` (open PR #2 head)

Production source inspected: `SpatialCore/src/live_reconstruction.cpp`, blob `883465c0b83f6a37f7402daf625c981c11c685ca`.

## Scope

This round characterizes the existing streaming near-coplanar triangle grouping before changing its tuning. The goal is to reproduce the suspected interaction between shared-edge traversal, the current normal/offset gates, small geometric noise, and protected shallow/narrow faces.

No production threshold, public API, ABI, schema, persistence contract, source observation, metric coordinate, or geometry was changed in this slice. That is deliberate: the characterization shows that simply widening the normal-angle gate can reduce fragmentation while also merging a supported shallow crease. A threshold-only change would therefore trade one failure mode for another without establishing a topology-safe policy.

`tools/analyze_live_patch_grouping.py` extracts the three current policy values from `live_reconstruction.cpp`, mirrors the exact first-compatible-adjacent-patch decision, and runs deterministic synthetic fixtures. The dimensions in these fixtures are regression geometry only, never product defaults or physical accuracy claims.

## Current policy under test

The inspected production source accepts a connected triangle into the first adjacent patch that satisfies all of the following:

- prior patch support has at most 93 samples before adding the next three triangle vertices;
- `abs(dot(prior.normal, triangle.normal)) > 0.99999`, corresponding to about 0.256234729 degrees;
- the candidate point is within 0.002 m of the prior patch's retained seed plane.

After acceptance the triangle's three vertices are added to that patch and its edges overwrite their adjacency entry. The patch plane used by this local gate remains the patch's initial triangle plane; the later robust plane fit happens after mesh traversal.

## Reproduced behavior

The deterministic 20 x 10 planar grid contains 400 triangles. Even with exactly coplanar vertices, the current streaming policy produces 20 patches of 20 triangles each. The 96-sample downstream fit ceiling would permit up to 32 triangles in one patch, so this fragmentation is not explained by the fit input cap alone; it comes from the first-compatible local traversal/edge ownership behavior.

With the same grid and deterministic 0.05 mm vertex perturbation the result remains 20 patches. At 0.10 mm perturbation it becomes 39 patches. These numbers characterize this synthetic tessellation and do not estimate ARKit sensor noise.

The stronger finding is triangle-order sensitivity at a supported shallow crease. For the same two-quad geometry at 0.20 degrees and 0.25 degrees, processing the second face's interior triangle before its shared-edge triangle yields two patches, while processing the shared-edge triangle first yields one merged patch. At 0.30 degrees and 1.00 degree the current normal gate keeps two patches in both orders. Geometry and coordinates are identical; only triangle order changes.

The synthetic protected-protrusion chain — approach, 0.50 m return, 0.15 m front, 0.50 m return, continuation — remains five distinct patch groups under the current policy. This is a regression target, not evidence that a physical iPhone scan resolves those dimensions.

A threshold sweep demonstrates why this round does not tune the normal gate. At the current approximately 0.256 degree gate, the 0.10 mm jitter grid produces 39 patches. A 0.5 degree gate produces 20 patches while still separating the synthetic 0.5 degree bridge-first crease. A 1.0 degree or 2.0 degree gate also produces 20 grid patches but merges that 0.5 degree crease. The result is evidence that widening one scalar threshold is not a topology-safe fix.

Machine-readable values are in `docs/evidence/ENG-LIVE-PATCH-GROUPING-01.json`.

## Research decision

Apple documents `ARMeshGeometry` as indexed triangle faces with per-face normals inside an anchor. Triangle ordering is therefore a mesh representation property and should not become an accidental architectural-topology prior: https://developer.apple.com/documentation/arkit/armeshgeometry

Open3D's planar-patch documentation describes robust planarity checks using both the distribution of point-normal angles and point-to-plane distances, followed by iterative growing/merging of accepted planes rather than a single seed-triangle decision: https://www.open3d.org/docs/latest/tutorial/geometry/pointcloud.html#planar-patch-detection

PCL's RegionGrowing implementation likewise separates neighborhood smoothness/normal criteria from residual and curvature tests: https://pointclouds.org/documentation/classpcl_1_1_region_growing.html

These references are design evidence only. No Open3D or PCL code was copied or introduced as a dependency.

The next implementation should make grouping independent of incidental triangle order before changing geometric tolerances. A suitable bounded design candidate is an anchor-local triangle adjacency graph with deterministic connected-component growth/merge using explicit local normal and point-to-plane evidence, followed by the existing bounded robust fit. It must preserve source support and derived identities, remain offline, respect the existing metric work/memory bounds, and never join separated parallel faces merely because their normals are similar.

## Acceptance criteria for the next production change

A future production change should not be accepted merely because it creates fewer planes. At minimum it needs deterministic compiled-path regressions demonstrating:

1. the same connected mesh produces equivalent patch topology under permutations of triangle order;
2. a slightly noisy coplanar wall does not fragment solely because a different adjacent edge is visited first;
3. the 0.15 m front / 0.50 m return synthetic protrusion chain remains distinct;
4. a supported shallow-angle face remains distinct without a Manhattan/right-angle prior;
5. disconnected or offset parallel faces are never merged by normal similarity alone;
6. accepted raw triangles and source provenance are unchanged;
7. fit/sample/resource caps remain explicit and bounded.

The precise angle/noise acceptance envelope still needs calibration against physical captures. Synthetic fixtures can test topology invariants but cannot establish sensor accuracy.

## Commands and observed results

The connected execution container had no direct network route to clone the private repository. The production source and its exact grouping condition were therefore read through the authenticated GitHub connector. The local characterization used an exact policy excerpt from production blob `883465c0b83f6a37f7402daf625c981c11c685ca`.

Environment:

```text
Python 3.13.5
Linux 6.18.44 x86_64
```

Commands:

```sh
python3 tools/analyze_live_patch_grouping.py \
  --source <exact-policy-excerpt> \
  --output patch_grouping_report.json
python3 -m py_compile tools/analyze_live_patch_grouping.py
python3 tools/analyze_live_patch_grouping.py --source <exact-policy-excerpt> > run1.json
python3 tools/analyze_live_patch_grouping.py --source <exact-policy-excerpt> > run2.json
cmp -s run1.json run2.json
```

Observed results:

- characterization command: exit 0;
- Python compilation: pass;
- repeat execution: byte-identical JSON output;
- protected protrusion-chain assertion: pass;
- expected current order-sensitivity assertion: reproduced;
- threshold-relaxation tradeoff assertion: reproduced.

The harness normally defaults to the repository's real `SpatialCore/src/live_reconstruction.cpp`; the excerpt route above was only the execution-environment workaround and is explicitly recorded rather than presented as a compiled production test.

## Tests not run / limits

This slice did not run full CMake/CTest, a compiled `SpatialCore` reconstruction binary, Xcode/iOS simulator, physical iPhone LiDAR/ARKit capture, real-room metrology, thermal profiling, or sustained mobile memory testing. It does not prove that 0.10 mm mesh jitter or a 0.20 degree crease is physically resolvable. It does prove a deterministic property of the inspected grouping rule: for some supported synthetic meshes, patch membership changes when triangle order changes.

PR #1's hosted Actions run remains a separate unresolved infrastructure blocker; this slice does not bypass, disable, or reinterpret that failure.

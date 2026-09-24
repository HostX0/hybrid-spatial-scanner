# ENG-MESH-EVENT-PAYLOAD-01 — Owned mesh-event payload normalization

Date: 2026-09-23

## Scope

This round adds one bounded, dependency-ready decoder for the existing owned `HSSMeshEvent` ingress payload. It does **not** modify `live_reconstruction.cpp`, the public C ABI/schema, grouping thresholds, source provenance, captured raw geometry, semantic interpretation, persistence, or cloud behavior.

The helper exists to remove one integration hazard before the order-invariant anchor-local patch plan is wired into production: the reconstruction path should consume a fully validated anchor-local vertex/index batch instead of repeating unchecked pointer arithmetic while triangles are processed one at a time.

## Live source identities reviewed before implementation

- Repository: `HostX0/hybrid-spatial-scanner`
- Current `main` observed this round: `8a36c25eec137173c372df42040a6d7722cb5dd9` (`Move archive import work off workspace actor`)
- Stacked parent PR #8 head: `b3febb4ef0fc5054b8699c64273a0b93f112649d`
- PR #8 base/head relationship was re-read before this slice; PR #8 remained open, mergeable and unmerged.
- Current-main `include/hss/hss_api.h` declares `HSSMeshEvent.vertices_xyz` as tightly packed anchor-local XYZ Float32 metres and `triangle_indices` as UInt32 triangle indices.
- Current-main `SpatialCore/src/hss_engine.cpp` validates counts at `<= 65536`, rejects nonfinite or absolute coordinate values above `10000`, rejects triangle indices outside `vertex_count`, computes checked vertex/triangle byte lengths, and copies vertices followed by triangle indices into owned ingress storage.
- `SpatialCore/include/hss/bounded_ingress.hpp` states that retained pointer fields are nulled and the payload vector is sole retained ownership.
- The same payload count/range contract was checked on the PR #8 stack before publication.

The main branch is actively advancing independently of the open PR stack. This slice therefore remains additive and stacked; it does not force-rebase or rewrite the existing PR history.

## Implementation

`SpatialCore/include/hss/mesh_event_payload.hpp` provides a header-only C++20 decoder with these properties:

- consumes a caller-owned `std::span<const uint8_t>` and never retains or mutates it;
- enforces the existing 65,536 vertex/triangle ceilings before allocation;
- computes expected Float32 XYZ and UInt32 triangle byte lengths using checked `size_t` arithmetic;
- requires the owned payload length to match the exact existing ingress layout (no truncation and no trailing bytes);
- decodes through `std::memcpy`, avoiding alignment-dependent casts;
- rechecks finite coordinates and the existing `|coordinate| <= 10000 m` ingress bound as defense in depth;
- rechecks every triangle index against `vertex_count`;
- returns all-or-nothing decoded vectors; malformed geometry clears partial output;
- maps allocation failure to a typed resource-limit result;
- accepts an empty payload when both counts are zero and accepts vertex-only payloads when triangle count is zero, matching the current non-remove input contract;
- performs no coordinate-frame transformation and no topology repair. Those remain explicit later stages.

No third-party runtime dependency was added.

## Regression coverage

`tests/hss_mesh_event_payload_test.cpp` contains 50 assertions covering:

- exact vertex and triangle round-trip, including input-byte immutability;
- empty and vertex-only payloads;
- truncated vertex bytes;
- truncated triangle bytes;
- unexpected trailing bytes;
- count/payload mismatch;
- out-of-range triangle indices;
- nonfinite coordinates;
- coordinates beyond the existing 10,000 m bound;
- triangles with zero vertices;
- vertex/triangle resource ceilings;
- exact acceptance of the positive and negative coordinate bound.

`tools/verify_mesh_event_payload.py` compiles and executes the exact helper and regression file without requiring CMake changes.

## Local targeted verification

Environment:

- GCC: `g++ (Debian 14.2.0-19) 14.2.0`
- Clang: `clang version 17.0.0 (https://github.com/swiftlang/llvm-project.git 10999b6d034fe318f3d56c83bddb6572593a8bb0)`
- Python: `3.13.5`

Commands and observed results:

```text
python3 -m py_compile tools/verify_mesh_event_payload.py
PASS (exit 0)

python3 tools/verify_mesh_event_payload.py --compiler g++
PASS 50/50 assertions

python3 tools/verify_mesh_event_payload.py --compiler clang++
PASS 50/50 assertions

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  python3 tools/verify_mesh_event_payload.py --compiler clang++ --sanitize
PASS 50/50 assertions; no sanitizer diagnostic observed
```

Final locally tested Git blob identities before publication:

- `SpatialCore/include/hss/mesh_event_payload.hpp`: `ebb66d8137afe7e87cadd661930fa129e6be8a1c`
- `tests/hss_mesh_event_payload_test.cpp`: `41d2c952ea9b4396dbf9a2a817ee8a15eeef88ef`
- `tools/verify_mesh_event_payload.py`: `1b17ab61a35cc822d0f12c3bbba06ae1c1bd8004`

These blob identities should be re-read from GitHub after publication before claiming byte-for-byte publication of the tested files.

## Research decision

Apple documents `ARMeshGeometry` as geometry for one scene-mesh anchor, with vertices and faces where each face is represented by vertex indices. That supports retaining anchor-local indexed topology as the normalization boundary instead of deriving architecture from buffer order. Apple also documents low-level mesh layouts in terms of explicit buffer offsets/strides, reinforcing the decision to make the product ABI's already-normalized contiguous layout explicit rather than rely on implicit C++ object alignment.

Primary references reviewed this round:

- Apple ARMeshGeometry: https://developer.apple.com/documentation/arkit/armeshgeometry
- Apple LowLevelMesh.Layout: https://developer.apple.com/documentation/realitykit/lowlevelmesh/layout

No Apple sample or third-party source code was copied.

## Evidence boundaries

This targeted compile verifies only the new standalone decoder and its regression against the documented/live ingress contract. It is **not**:

- a full repository CMake/CTest run;
- an exact hosted branch run (the known hosted runner route remains blocked before steps);
- a compile of `live_reconstruction.cpp` with the helper wired in;
- an Xcode or iOS simulator result;
- a physical iPhone LiDAR/Metal result;
- real-room metrology, thermal evidence, or a competitor benchmark.

## Next dependency

When a credible exact-repository compile route is available, wire this decoder at the owned mesh-event consumption boundary, transform the complete local vertex batch into project coordinates, invoke the PR #7 anchor-local patch plan once per accepted anchor revision, and bind `slice(...)` fragments through finalized per-triangle surface identity while preserving the original raw triangle output. Final-scene JSON/topology permutation regressions and applicable full CTest/sanitizers remain required before calling that production integration complete.

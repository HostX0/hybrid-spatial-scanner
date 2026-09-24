# ENG-MESH-ANCHOR-SCENE-SNAPSHOT-01

## Task

Add a bounded deterministic full-Scene 1.1 oracle around the already deterministic mesh-derived `nodes`, `edges`, and `surfaces` arrays from PR #15. This slice is intentionally not a `live_reconstruction.cpp` rewrite. It reduces the final production-integration risk by proving the top-level scene envelope, epoch ordering, uint64 revision serialization, source-synthetic flag, project basis, and mesh-derived arrays can be assembled byte-stably without inventing regions/issues or changing public contracts.

## Revisions and source identities

- Default branch re-read at round start: `main` = `4cae5a0aaf5b28827f6c96b54d2ec9dbea42ef25` (`Fix HSSCAN archive export names`).
- Current stack parent discovered at round start: PR #15 head `1c48cbbda71e088496ccba05347a86cf030070d7` (`hss/stable-scene-entities`).
- Current production runtime source: `SpatialCore/src/live_reconstruction.cpp` blob `1b6f8f57b6ae77ab75918766e085d4589b6e97e6`.
- Current Scene 1.1 schema: `schemas/scene.schema.json` blob `b4395373b9383d887da027ae0340c6ef83006cf9`.
- Current coordinate contract header on PR #15: `SpatialCore/include/hss/coordinate_basis.hpp` blob `1f4d36c4fd9026d43b4d26a235259c4ed30c6fc5`.
- Parent PR #15 helper blob: `SpatialCore/include/hss/mesh_anchor_scene_entities.hpp` = `3f5f95a03737dbad112a489de7ca9743dab408be`.
- New locally tested helper pre-publication blob: `ce38ffe7aa2bf252dcae9989fece1bd60d603e4a`.
- New locally tested regression pre-publication blob: `b73c9936e31031cd67201f7eedc042c1186f71ad`.
- New locally tested verifier pre-publication blob: `8c2484c4cf332293bbac47ff8e01cefca0ef35ef`.

## Delivered behavior

`SpatialCore/include/hss/mesh_anchor_scene_snapshot.hpp` consumes a successful PR #15 scene-entity result plus typed project/epoch metadata and:

- emits one complete minimal Scene 1.1 document with the exact top-level field set used by the current schema;
- preserves `scene_revision` as a canonical uint64 decimal **string**, including `UINT64_MAX`, avoiding JSON-number precision loss;
- validates canonical lowercase UUID syntax for project/capture IDs;
- validates the project floor basis through the existing coordinate-basis contract and keeps the current +/-10,000 m project-origin bound;
- validates accepted epoch transforms through the existing rigid-transform contract, requires accepted epochs to carry a transform, and unresolved epochs to carry `null`;
- rejects zero transform revisions and duplicate `(capture_id, session_epoch)` records;
- sorts epochs deterministically by capture ID, session epoch and transform revision so caller iteration order cannot perturb snapshot bytes;
- embeds PR #15 deterministic `nodes`, `edges`, and `surfaces` arrays unchanged;
- deliberately emits `features`, `regions`, `objects`, `constraints`, and `issues` as empty arrays and `quality_state="provisional"`, making this an explicit **mesh-only success oracle**, not an authority that fabricates independent runtime-owned layers;
- preserves the current 16 MiB whole-scene JSON ceiling and 32-epoch ceiling; resource failure publishes no partial scene string;
- changes no C ABI, Scene schema, reconstruction threshold, raw captured triangles, calibrated frame relationship, persistence, semantics, cloud/AI behavior, or external dependency.

This helper does **not** claim JCS/RFC 8785 compatibility. It uses an explicit stable field/array ordering only as a regression oracle for this repository's current Scene writer. RFC 8259 defines JSON objects as unordered; byte-stable output here is a testing/integration property, not a semantic requirement imposed on external JSON consumers.

## Regression evidence

`tests/hss_mesh_anchor_scene_snapshot_test.cpp` executes 186 assertions per run. It covers:

- full Scene 1.1 envelope creation around schema-shaped mesh node/edge/surface arrays;
- exact `UINT64_MAX` revision string preservation;
- 32 repeated epoch/input variants, including reverse epoch arrival order, with byte-identical JSON after accounting only for the intentional `source_is_synthetic` Boolean;
- stable canonical epoch order independent of input order;
- accepted versus unresolved epoch transform semantics;
- empty-epoch/empty-mesh arrays;
- malformed project/capture UUIDs, zero transform revision, duplicate epoch identity, nonrigid transform, accepted-without-transform and unresolved-with-transform failures;
- right-handed floor-frame rejection for scaled/reflected bases;
- exact +/-10,000 m project-origin boundary acceptance and rejection beyond it;
- nonfinite project origin rejection;
- entity-upstream failure propagation;
- epoch and JSON resource ceilings with no partial output.

`tools/verify_mesh_anchor_scene_snapshot.py` compiles/runs the test and additionally parses the emitted full JSON with Python's JSON parser, checks the exact top-level Scene field sequence used by this oracle, confirms the canonical uint64 revision string, epoch order, fixed provisional state, and empty independently-owned arrays.

Final local commands after the final implementation bytes:

```bash
python3 -m py_compile tools/verify_mesh_anchor_scene_snapshot.py
python3 tools/verify_mesh_anchor_scene_snapshot.py --cxx g++
python3 tools/verify_mesh_anchor_scene_snapshot.py --cxx clang++
ASAN_OPTIONS=detect_leaks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  python3 tools/verify_mesh_anchor_scene_snapshot.py --cxx clang++ --sanitize
```

Observed toolchains:

- GCC 14.2.0
- Clang 17.0.0
- Python 3.13.5

Observed final results:

- GCC 14.2 C++20 O2 warnings-as-errors: PASS `186/186` assertions; JSON parse/field checks PASS.
- Clang 17 C++20 O2 warnings-as-errors: PASS `186/186`; JSON parse/field checks PASS.
- Clang 17 ASan+UBSan: PASS `186/186`; JSON parse/field checks PASS; no sanitizer diagnostic observed.
- Python verifier bytecode compilation: PASS.
- Total compiled assertions: `558` passed, `0` failed.

## Evidence boundary

The connected execution container still cannot materialize the complete private repository. The targeted compile used the exact new helper/test/verifier bytes against minimal local contract-compatible stubs for the PR #15 scene-entity result and the authenticated coordinate-basis function shapes. The new repository regression source itself only uses members/functions present in the authenticated production contracts, but this round did **not** link or run the complete repository.

Therefore this evidence is **not**:

- full CMake/CTest on the actual stack;
- exact hosted GitHub Actions execution;
- revalidation of PR #11 capture preparation, PR #12 cut generation, PR #13 robust fitting, PR #14 graph assembly, or PR #15 identity/entity materialization;
- production `live_reconstruction.cpp` integration;
- Swift portable tests, Xcode/simulator evidence, physical iPhone LiDAR/Metal evidence, thermal/memory characterization, or real-room metrology.

PR #15 had no workflow runs and zero individual commit statuses when checked. Repository-wide pull-request workflow history still showed only the known PR #1 / PR #8 exact-source attempts, whose hosted jobs failed before useful execution; that unchanged route was not retriggered and the underlying hosting cause remains unverified.

## Research decision

Primary references re-read for this bottleneck:

- Apple `ARMeshGeometry`: indexed geometry belongs to one scene-mesh anchor; this continues to support keeping per-anchor topology/identity explicit upstream of serialization: https://developer.apple.com/documentation/arkit/armeshgeometry
- Open3D 0.20 `TriangleMesh`: indexed triangles and per-triangle connected-component assignment remain explicit; no Open3D runtime dependency is added: https://www.open3d.org/docs/latest/python_api/open3d.geometry.TriangleMesh.html
- RFC 8259: a JSON object is semantically unordered, so this oracle's byte ordering is deliberately a repository regression property rather than a claim about general JSON semantics: https://www.rfc-editor.org/rfc/rfc8259
- RFC 8785: canonicalization exists for cases requiring invariant JSON bytes, but this slice does not adopt JCS because doing so would introduce a broader serialization contract not required by Scene 1.1: https://www.rfc-editor.org/rfc/rfc8785.html
- JSON Schema Draft 2020-12: `properties`/`additionalProperties` semantics support continuing to match the repository's exact schema field set rather than adding private top-level properties: https://json-schema.org/draft/2020-12/json-schema-core

No external source code or runtime dependency was imported.

## Remaining production gate

The current runtime still builds arrival-order patches/cuts/nodes directly in `live_reconstruction.cpp`. Before any production runtime merge claim, an exact repository checkout must compile and test an integration that:

1. consumes PR #11 preparation once per accepted anchor revision;
2. generates PR #12 cuts, PR #13 surfaces, PR #14 graph and PR #15 scene entities;
3. uses this full-Scene oracle (or equivalent exact production writer logic) to compare complete Scene 1.1 output across triangle permutations/global vertex bases before publishing a snapshot;
4. preserves raw triangle stream and source provenance on all derived failures;
5. retains surrounding runtime-owned epochs, regions, issues, features, objects and constraints rather than replacing them with this oracle's empty success-only arrays;
6. emits explicit issues when metric derivation is unavailable instead of inventing topology;
7. passes applicable full CTest and sanitizer coverage on the actual repository.

Xcode/simulator and physical iPhone LiDAR/Metal/metrology remain separate acceptance layers.

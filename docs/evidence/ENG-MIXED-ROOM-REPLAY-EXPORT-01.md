# ENG-MIXED-ROOM-REPLAY-EXPORT-01

Date: 2026-10-02. Base: `ea3839e8436415b391cb812dc8058036814194ba`.
Scope: B-008/B-018/B-021/B-044 host integration evidence. Engine-first owner priority.
This change adds an executable challenge, not a new reconstruction algorithm or an iOS/physical-accuracy result. Production engine/exporter code, ABI, schema and numerical thresholds are unchanged.

## Product question and actual path

Can a mixed architectural scene retain supported narrow/oblique walls, separate overhead levels, an unobserved skylight and a detached column while scanning continues, a user records an unknown region, a source face disappears/returns, and the project is saved/reopened/exported?

The new root CTest `hss_mixed_room_replay` runs the actual C ABI, retained mesh reconstruction, actual scene-edit command processor, durable project store and real OBJ/MTL exporter. It is not a standalone ceiling classifier. Its small test adapter supplies exact snapshot vertices/indices and extracts retained source UUID/range mappings from validated `mesh_source` records. This does not wire or verify the native Objective-C++/Swift export/UI adapters.

## Authored reference and assertions

One compact synthetic challenge family, in metres, +Y up, cut Y=1.20m. Zero simulated sensor noise; input positions are Float32 encodings of independently authored double-precision reference quads. This is not a LiDAR replay or a diverse realistic/held-out corpus.

- Nine exterior wall faces, including a 0.15m front and two 0.50m returns, plus an oblique corner.
- Four detached-column faces with a 0.30m by 0.50m footprint; unknown attachment/area policy remains explicit.
- Eight overhead faces: 2.80m and 3.20m levels, a vertical step, a sloped 3.20–3.50m patch, and four panels around a missing 1m by 1m skylight. A later ceiling-only source replacement raises the slope endpoint to 3.65m.
- Complete input: 84 vertices / 42 triangles / 21 reference planes in three anchors. Removing one observed return leaves 40 triangles / 20 supported planes and two actual open endpoints; retained unused vertices do not close the gap. Restoring the source restores the loop.

Each actual fitted plane is matched one-to-one against the reference face equations, checking all four corners and normal direction. Each finite cut fragment must match exactly one reference segment; sorted projected intervals must cover each observed segment without gaps/overlap. Every closed node has degree two; an omitted return leaves exactly two degree-one nodes and an explicit open issue. Two complete loops remain unclassified with null net area. No sky geometry or clean hidden substrate is generated.

Observed host maxima across the executed family: plane-reference distance `9.53674317294e-08 m`, normal-vector cross-product norm `2.33172204323e-08`, cut perpendicular deviation `9.53674317294e-08 m`, cut-chain length deviation `9.53674317183e-08 m`. Assertion bounds are `1e-6 m` / `1e-6` normal sine, only for this noiseless Float32 numerical domain; they are **not** sensor accuracy. Three deterministic triangle-order permutations reproduce identical complete derived Scene bytes with exact source index order preserved per run. 1us/50us requests test cooperative scheduling, not hard-real-time deadlines.

The actual eleven-event store sequence is epoch, floor reference, three anchor observations, zero-target unknown command, ceiling replacement, missing return, restored return, high cut, normal cut. It checks exact command/result bytes, raw measurements and one revision per event. Checkpoint, destruction, reopen and ordered replay yield byte-equivalent Scene, OBJ, MTL and sidecar. Parsed OBJ positions and one-based face indices reproduce every retained raw coordinate/index exactly. Actual issue codes, including `HSS_UNKNOWN_REGION`, survive the sidecar; full user reason remains in Scene/journal. No appearance assets are included or uploaded.

## Failures and verification

The initial geometry-only challenge passed on the unchanged base. The first durable/export attempt failed at exporter acceptance because the newly authored test adapter put human-readable reasons in `warnings`, while the existing exporter/native producer require stable diagnostic codes. Only that test mapping was corrected. Its source, failing log and JUnit are retained as `durable-failing-source.cpp`, `durable-mixed.log`, `durable-mixed.xml`; this is **not** claimed as a reproduced production defect or an engine repair. No legitimate assertion or production gate was weakened.

Actual Linux x86_64/GCC13.3/CMake3.31.6 root configure/build: PASS. Debug assertions active, Werror and ASan+UBSan verified in the actual affected compiler commands; existing user-local ZLIB1.3. Focused final tests: **4 passed / 0 failed / 0 skipped**, 14.44s. Final full root: **57 passed / 0 failed / 0 skipped**, 58.64s; prior56 plus new mixed integration. JUnit and subprocess exits inspected. Single-host timing is not an iPhone or performance-improvement result.

Workspace: `/home/abdulazeez/hss-work/mixed-ea3839e.grc8ost0/`. Evidence includes commands.json, source-integrity.json (461 matching base blobs), compiler-manifest.json, focused-tests.xml/log, full-tests.xml/log, verification-summary.json, research-decisions.json and failed-attempt artifacts. Exact published-source readback/rebuild results belong in the accompanying PR comment and durable automation handoff; do not infer them from these local results.

## Primary-source decisions and integration cost

- CGAL6.2 [Polygon mesh slicer](https://doc.cgal.org/latest/PMP_Boolean_operations/classCGAL_1_1Polygon__mesh__slicer.html) and [intersection/polyline reference](https://doc.cgal.org/latest/Polygon_mesh_processing/group__PMP__intersection__grp.html): a cut is a collection of finite polylines; closure and multiple components require their own checks. Adopted independent finite interval/degree checks; rejected invented closure or treating every closed slice as a room. Documentation only; no CGAL code/dependency imported.
- Open3D `v0.19.0` [TriangleMesh.cpp](https://raw.githubusercontent.com/isl-org/Open3D/v0.19.0/cpp/open3d/geometry/TriangleMesh.cpp), inspected transform/merge index offsets and per-triangle normal/adjacency sections: keep raw face geometry/indices distinct from derived representation and semantics. No smoothing or close-vertex merge applied to metric source. [MIT notice](https://raw.githubusercontent.com/isl-org/Open3D/v0.19.0/LICENSE) inspected; no external source/assets copied and no blanket transitive-dependency approval claimed.

No new mobile runtime or inference dependency, no production resource increase. The only extra link is the existing OBJ/MTL library in the host test executable.

## Remaining work and scope limits

This challenge proves this bounded noiseless family, not arbitrary buildings, noisy/occluded physical data, analytic curves, capture coaching, textured game import, native warning projection or all three production modes. Native export currently only adds its own synthetic warning; the test adapter's richer issue/source mapping must not be claimed to ship on iPhone. Existing generic targeted-edit lineage/undo and native wiring gates remain.

Next engine acceptance: author and freeze an independently parameterized/tessellated/rotated sibling with declared depth/mesh perturbations and partial observations before tuning, run through the same real pipeline, then fix only a reproduced deficiency. Do not relabel the currently exercised fixtures as held-out or manufacture production edits to make a green challenge look like a repair.

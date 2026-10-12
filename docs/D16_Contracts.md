# D16 — Executable Contracts, Ownership and Integration Guide

Engineering baseline v1.1. The delivered include/hss/hss_api.h and schemas directory are normative contract artifacts. They are complete declarations and validation schemas, not an implementation of every scanner function. The host build compiles the header and selected reference kernels; no iPhone application binary has been produced.

## 1. Contract authority and compatibility

Use the exact contract files in the engineering pack when implementing the bridge. Do not retype abbreviated signatures from v1.0 prose. The ABI remains major version 1; the JSON schema version is 1.1. Source-compatible documentation revisions and incompatible data-format revisions are separate concepts. A breaking binary layout change requires an ABI major version or a new function/type with a migration plan.

The current header defines fixed-size IDs, math values and borrowed byte views, then extensible messages for configuration, frames, epochs, mesh events, counts and engine statistics. Fixed math values such as a three-double vector do not contain version prefixes. Extensible messages begin with uint32 abi_version and uint32 struct_size. This clarifies the overbroad v1.0 phrase that every struct contains a prefix.

Readers reject undersized message layouts and unsupported ABI versions. Version 1 permits a larger message only when its trailing bytes are zero; the caller guarantees that the described memory is accessible. Unsupported flags cannot be silently ignored. Pointer safety cannot be established from a length field alone: valid allocation/lifetime is a caller obligation.

C ABI scalars use the host representation; captured depth bytes have an explicit Float32 little-endian encoding. Persisted files never contain a raw memory dump of a C struct, because pointers, padding and host ABI are not a portable serialization format.

## 2. Engine lifetime and execution model

An HSSEngine is a single-owner mutable object. The Swift adapter serializes create, submit, command, step, statistics and destroy calls. Do not invoke destroy while another call is running. The ABI creates no background callback thread; the adapter owns scheduling. A successful submit validates and copies required input data before returning, but does not promise the entire reconstruction job has completed.

A snapshot is immutable and has exactly one owning handle returned by acquire_snapshot. Copies of that pointer are borrows, not new retained owners. Release exactly once; releasing NULL is allowed. A snapshot retains its own immutable data and may outlive the engine. A subsequent engine mutation cannot invalidate its vertices or alter its scene revision.

In Swift, wrap each acquired handle in a final reference type that releases in deinit and exposes no public owning raw pointer. Keep mutable engine use on one explicitly selected executor. Do not mark a pointer-owning wrapper @unchecked Sendable merely to suppress diagnostics. Pass immutable copied snapshots to UI tasks or copy the needed presentation buffers under a documented lifetime.

The host ContractSmoke.swift imports the C module and checks initialization of HSSConfig. It does not test the full wrapper, actor correctness, ARKit, ARC-to-C lifetime stress, iOS simulator or physical sensor input. Swift C++ documentation motivates a narrow boundary but does not replace these implementation tests. [R05]

## 3. Input data and numeric bounds

HSSFrame includes capture UUID, uint64 sequence, epoch, tracking state, timestamp, rigid pose, depth intrinsics, dimensions, strides, lengths, pointers, optional ordinal confidence and depth-source enum. Null depth data with positive dimensions is invalid. A confidence pointer may be null; in that case its length and stride must be zero. Non-null confidence dimensions match the normalized depth grid.

Before allocation, check products for overflow. For width w, height h and row stride s, required addressed depth bytes are s*(h-1)+4*w, not blindly 4*w*h when rows are padded. Verify s>=4*w, all sizes fit the declared caps, and depth_byte_length covers the addressed range. The maximum normalized buffer dimensions and byte caps are defensive bounds, not supported sensor specifications.

Pose matrices must be finite, have a homogeneous final row, an orthonormal rotation and determinant near +1 within a declared numerical threshold. Reject embedded scale, reflection or nonrigid transforms. A matrix has a semantic frame relationship as well as a numeric shape; a valid rotation cannot establish that two unrelated epochs describe the same room.

The selected metric depth source is sceneDepth. A smoothedSceneDepth message is not accepted as additional independent measurement evidence. Unsupported tracking prevents metric commitment. The UI may still display the camera and a warning. Intrinsics must correspond to the depth image, not the screen overlay.

## 4. Functions and status semantics

The header declares create/destroy, set_epoch, submit_frame, submit_mesh_event, submit_room_proposal_json, apply_command_json, copy_command_result_json, step, get_stats, acquire_snapshot, snapshot release, counts, vertex/index copy and scene JSON copy.

Every status-producing entry catches all C++ exceptions before returning across C linkage. Map bad allocation to RESOURCE_LIMIT where possible, validation to a specific input/status code, and unexpected failure to INTERNAL_ERROR with a nonprivate diagnostic ID. Do not include captured imagery, personal room labels or entire payloads in telemetry merely to debug a failure.

Copy-out APIs use a two-call capacity protocol. A null destination with capacity zero requests the required size. When capacity is insufficient, return BUFFER_TOO_SMALL, set required to the complete needed count, and write no partial output. JSON length excludes a trailing NUL. Triangle capacity and required count are index elements, not triangle count. Other output counts explicitly state whether they count bytes, vertices or entities.

On unrelated failure initialize output handle/count fields safely where the API contract requires it. BUFFER_TOO_SMALL is the exception where required must report the actual required length, not zero. Every consumer must check status before reading buffers. A fixed snapshot makes repeated size/data queries coherent without a race against a live engine mutation.

NOT_READY means the requested result or accepted snapshot does not yet exist. DUPLICATE_MISMATCH means a command ID was reused with a different UTF-8 payload bytes. Neither can be translated into success by the UI. Engine receipt, applied result and durable storage are separate states.

## 5. JSON schema set

### Additive review-history facade (2026-10-11)

`hss_engine_copy_command_history_json` is an owner-serialized, bounded derived
view, using the same two-call no-partial-output protocol as other JSON copy
functions. Schema version `1.1`, project UUID and current scene revision bind
the view. Each record exposes only command UUID, type, optional original-command
UUID, and before/after revisions. It does not expose command payloads, private
notes or event clocks, and is not a durable-journal replacement. A stored exact
command retry returns `HSS_OK` even for an originally conflicting/rejected
proposal; callers must still validate its original result state, unchanged
revision boundary and exact bytes before journaling it.

The native review supports current evidence-backed edge protection/split,
confirmed node-position lock, dimension with source/uncertainty and hard/soft
mode, and strict-lineage undo/redo. Source/control revision gaps disable
historical reversal. An unresolved correction after source refinement remains
an explicit nonmetric review marker, not a constraint rebound to a nearby node
or generated measured geometry. A conflicting proposal retains the last
accepted scene and its exact journal result; it must never be presented as an
applied correction.

Compatibility: this adds a function, not a binary-layout or persisted schema
change. Older clients ignore the new function; existing projects replay without
migration. UI history is recomputed after replay and after an ordered command
append. New-project relative time fixes remain separate from legacy export:
portable HSSCAN sharing creates a validated derivative with its first event
time subtracted from every source/control context. Derived chunk hashes and
manifests change consistently; original sources, metadata, geometry, sequence,
revision, and exact command/result bytes remain unchanged. Existing imported
archives remain readable and future export can normalize their clock origin.
Do not rewrite a local project in place or label an exported derivative as the
original byte-identical archive.

The pack includes seven Draft 2020-12 JSON Schemas: command, command-result, frame, scene, manifest, export and room-proposal. Every defined object rejects unrecognized additional properties. Command variants are discriminated with oneOf and a const type, rather than an unrestricted payload dictionary.

Schemas verify shape, declared enums, ordinary bounds and UUID syntax when the validator enables format checks. They do not verify geometry, memory accessibility, correspondence between entity IDs, numeric finiteness in a permissive parser, archive hashes, transform rigidity or all uint64 bounds. Run a semantic validation pass after schema validation. Standard JSON rejects NaN and Infinity; configure writers and readers accordingly.

Sequence and revision fields are canonical decimal strings in JSON. Reject leading zeros, signs, decimal points, non-ASCII digits and values above 18446744073709551615. The regex alone limits representation length but does not enforce that entire upper bound; the semantic validator does. Counter serialization never routes through floating-point numbers.

Version 1.1 schema documents are bundled locally. A validator must not retrieve arbitrary remote schema references while parsing a project. The schema IDs are stable identifiers, not a network dependency. External document text, image content and AI output are data; none can alter validation policy or execute tool instructions.

## 6. Scene entities and invariants

Scene contains project identity, revision, units, basis, epoch relationships, nodes, edges, surfaces, features, regions, objects, constraints, issues and overall quality state. It also includes source_is_synthetic so reference fixtures cannot be mistaken for a real capture. Synthetic values do not become physical evidence merely because they pass schema validation.

Nodes store project position, provenance and lock state. Edges link valid distinct nodes, supporting surfaces, parent lineage, topology protection and provenance. Surfaces distinguish fitted planes from mesh patches, preserving their applicable parameters, mesh reference and fit diagnostic. A plane requires a unit normal and finite offset; a mesh patch may have null plane parameters. Cross-field semantic rules enforce this distinction after schema parsing.

Features identify protrusions, recesses, fixed obstructions, openings, overhead geometry, ceiling patches or unknown regions. Their attachment state is explicit. Regions contain outer node loops and hole node loops, area policy, view and cut height. A region's area is null when it is not a valid accepted closed boundary. Object labels and optional schematic footprints remain separate from metric topology.

Provenance carries geometry origin, semantic origin, support IDs, optional measurement uncertainty and quality. Null uncertainty means unknown/not calibrated, not perfect accuracy. Inferred/unknown geometry cannot be promoted to observed without linked observations. A user-confirmed category is not equivalent to a tape/laser measurement.

Constraints identify targets, source command, value, units, hard/soft state, weight and active/conflict/superseded status. Issues identify current targets, source sequence, reason, action and active/suppressed/resolved state. Suppression affects presentation; it does not prove that an issue has been resolved geometrically.

## 7. All live command variants

mark_corner supplies a supported point or pending ray and source IDs. Semantic validation requires a usable point/support combination or an explicit pending intent, not both missing. A ray direction must be nonzero and normalized. A confirmed point protects the corner's existence without automatically freezing its metric coordinate.

split_edge targets one current edge with parameter strictly inside (0,1). Generate child-edge lineage and retain the original observation references. merge_edges targets adjacent compatible edges and requires preservation of source lineage; reject removal of an intervening protected feature or a new self-intersection.

mark_protrusion and mark_recess target a parent boundary region with ordered point/support references. The input is intent and evidence for an ordered feature; it must not force symmetry or 90-degree returns. Multiple incomplete observations may remain a pending feature until the missing face is captured or a reference measurement is supplied.

mark_obstruction supplies a footprint, attachment state and fixed classification. An unresolved or attached region cannot be blindly subtracted as a detached hole. A fixed flag is a planning classification, not proof that the object is load-bearing. classify_object changes semantic label/origin only and does not create perfect box geometry.

set_dimension requires two distinct endpoint references, a positive finite value, declared units, source, optional uncertainty, mode and soft weight. Convert accepted units once to metres. exact_model uses the explicit equality path; soft_measurement uses a documented residual. A guess remains inferred. Reject unsupported cross-epoch endpoints or contradictory constraints without modifying the prior valid scene.

protect_feature preserves topology and has lock_position=false. lock_position explicitly confirms a selected adopted coordinate. unlock_position identifies the existing constraint. mark_unknown gives a reason for an unsupported region. exclude_dynamic_region supplies a finite ordered bounding region and a valid inclusive sequence interval; min must not exceed max, and start must not exceed end.

undo and redo reference an accepted original command and do not invent fresh targets. Reversal applies a recorded compensating operation and validates current lineage. An inapplicable redo is rejected rather than redirected to a different wall. All fifteen variants have valid and invalid example JSON files in the reference pack.

## 8. Idempotency, hashing and journal durability

Before applying a command, validate its project, expected revision and target identity. Persist the exact validated UTF-8 command bytes and their SHA-256 digest. Retries reuse those bytes, including whitespace and number spelling. Byte-identical command-ID retries return the stored result; different bytes under the same ID return DUPLICATE_MISMATCH. This deliberately avoids cross-language JSON canonicalization ambiguity.

A command ID plus identical bytes returns the prior result. A command ID plus different bytes fails. A stale expected revision can be reconciled only when the target lineage is proven unchanged and the command contract explicitly allows it; otherwise request confirmation. Do not silently weaken revision checking to avoid UI errors.

Persist intent, accepted result and associated scene revision in a transaction. The storage layer can acknowledge durable completion only after its required filesystem operations finish. A result marked not_committed cannot be displayed as Saved. If the process dies between engine acceptance and journal commit, recovery replays the last committed journal and may discard the unsaved preview. This is an explicit recoverable state, not invisible data corruption.

The production journal uses monotonically ordered records, length framing and checksums. At startup, validate the last complete record and reject or truncate only an explicitly identified incomplete tail. Preserve older valid commits and report recovery state to the user. Do not parse half-written JSON lines as an accepted command.

## 9. Project package and archive boundary

The canonical project is a directory with a manifest, immutable source chunks, ordered events/commands, immutable scene revisions, optional images, meshes, quality reports and export records. The shareable HSSCAN file is a ZIP wrapper, not a special executable format. The manifest commits to actual asset paths, lengths and SHA-256 values.

Every asset path is a canonical relative slash-separated path. Reject absolute paths, parent/current dot segments, empty components, backslashes, drive prefixes, NUL, duplicates after normalization and links that escape the import directory. Validate compression expansion and file count before extraction. Treat symlinks and hard-link-style archive entries as unsupported in the baseline. A valid JSON path string is not by itself a safe extraction path.

Initial expanded-project, file-count, binary-chunk and JSON-record limits remain 8 GiB, 20,000 files, 256 MiB and 16 MiB respectively, subject to an actual device resource policy. Streaming validation avoids allocating the full archive in memory. File size, element count, stride and payload length must agree before any point/mesh allocation.

The binary container must have explicit magic, format version, record type, element count, stride, payload size, endianness and integrity metadata. Define a fixed on-disk header with byte offsets in implementation, not sizeof of an in-memory struct. Unsupported record types fail when critical; safely ignorable metadata records require an explicit flag and contract.

## 10. Atomic commits and recovery

Write immutable source/scene files first, flush and close them, verify integrity, then write a temporary manifest and replace the active manifest atomically. Keep the previous validated manifest until the new commit and directory state are durable. The active manifest references only completed assets; a newest timestamp is not proof of a complete commit.

Recovery validates the current manifest and referenced assets. If invalid, it evaluates the previous commit and reports the fallback. Orphaned incomplete temporary files are not accepted scenes. Garbage collection follows reachability from retained valid commits, not filename age alone. A cache eviction must never remove the only copy of a source observation required for refinement.

The provided storage.py executes a small POSIX manifest-last oracle with injected interruptions and hash validation. It verifies the logical ordering on the host filesystem, not iOS protected-data behavior or every APFS edge case. The iOS implementation must test backgrounding, phone lock, process termination, low disk, permission change and migration independently.

## 11. Export contracts and consistency

ExportRecord specifies source scene revision, format, units, axes, selected view, cut height, layers, warnings and artifact integrity. PDF, SVG and DXF are metric plan products; OBJ is a captured-mesh bundle with an explicit metadata sidecar. An optional platform structured USDZ must not be confused with captured object detail.

An exporter freezes its source snapshot, writes a temporary bundle, validates internal references and commits as one export job. Cancellation removes only that job's temporary files. A user changing live scan geometry during export cannot alter the frozen revision halfway through its files.

PDF scale statements depend on the actual page/printing policy and include a scale bar. SVG has one declared coordinate inversion. DXF uses model-space metres with an explicit unit declaration and tested consumer behavior. OBJ assets use relative texture paths and a sidecar for units/axes, since an OBJ file alone is not a reliable complete metric metadata contract.

The sample exports in this pack are authored synthetic examples. They are not camera reconstructions, and they do not establish that every native exporter has been implemented or that a commercial CAD application has accepted the files. Corresponding tests and evidence must be added to the production export gate.

## 12. Repository integration and first milestone

Keep the production application, contracts, portable core, platform adapters, tools and fixtures in separate directories. The delivered pack is a reference repository that can be imported into version control. It is not an existing remote GitHub repository and contains no signing credentials or developer-account configuration.

The first implementation milestone adopts the header and schemas, compiles a real iOS target, records effective session configuration and saves one actual capture with calibration/pose data. Then replay normalized events through a minimal core and produce an irregular plan. Do not implement a cloud enhancement service or account system before this offline path works.

Update the contract files and both sides of the bridge in one reviewed change. Schema or coordinate changes require fixture migration and regression evidence. Keep a real toolchain/dependency lock after successful build; never put invented device UUIDs, commit hashes or unexecuted test results into the handoff.

## 13. Required contract test families

Compile the C header as C11 and C++20 with warnings as errors. Type-check the C import with Swift. Test sizeof/offsetof invariants on every supported architecture, not just the current x86_64 host. Stress creation/destruction and immutable-snapshot lifetimes under sanitizers in the actual engine implementation. No exception may cross the ABI.

Validate every valid and invalid command example, finite numeric handling, uint64 round trips, transform rigidity, row strides, missing buffers, unsupported flags, stale revisions, duplicate mismatches and copy-out capacities. Schema checks cannot exercise dereferencing a native pointer; native implementation tests are separate.

Round-trip actual project records and inject write failures at every commit stage. Verify unknown/inferred provenance survives saving, migration and export. Add hostile archive cases before enabling project import to users. All unexecuted tests must remain visible as not run, not inferred from a successful host schema test.

## Additive live-capture facade controls (I-CORE)

The existing ABI-v1 structs and existing function signatures are unchanged. Two additive entry points are declared in the canonical `include/hss/hss_api.h`:

- `hss_engine_set_source_is_synthetic(engine, uint32_t)`: accepts only 0/1 before the first successfully queued source event or plan-reference control. Default 0 preserves captured-source compatibility. Synthetic producers must explicitly set 1. Source provenance is unrelated to whether physical accuracy has been validated.
- `hss_engine_set_plan_reference(engine, const HSSMat4d* T_project_floor, double cut_height_m)`: caller-confirmed rigid frame, local +X=U, +Y=N, -Z=V, positive cut height up to 100 metres (normal architectural policy 1.20 m). Call when the engine is idle; otherwise `HSS_NOT_READY` requests a retry. The accepted request starts a transactional bounded rebuild. Publication changes the scene revision and plan interpretation, never captured project geometry. Source identity must be configured first.

Without a confirmed floor reference the facade emits a provisional project-Y=1.20 m section and an explicit floor-unknown issue, with area unavailable. A closed validated section after confirmation is a provisional architectural-cut candidate. Multiple loops retain unknown attachment/hole classification and unavailable area; the facade does not automatically call a shelf a fixed obstruction. Capture semantics and fit uncertainty remain unknown unless supplied by another accepted authority.

Submit success means copied/queued, not reconstructed. Poll `hss_engine_step` until stats `lifecycle_state` is 1 (idle); 2 means pending/rebuilding. Every successfully processed source event or plan-reference control publishes exactly one revision. A failed event publishes none and does not advance accepted capture sequence. This is independent of wall-clock slicing, so ordered source/control replay yields identical revisions and entity IDs. Store must persist controls in their order and drain prior work before applying a plan-reference control. Captured mesh arrays and scene JSON belong to the same immutable publication.

### Scene-edit command adapter

`hss_engine_apply_command_json` accepts exact UTF-8 only while the source queue and reconstruction are idle. The engine serializes one scene-edit processor, passes it the immutable current scene JSON, and retains its exact result JSON for `hss_engine_copy_command_result_json`. An applied candidate atomically creates a new immutable snapshot and increments the revision once; its captured vertex/index buffers and captured-source provenance are copied unchanged. A conflict, rejection, duplicate mismatch, or pending dynamic exclusion never replaces the snapshot or increments its revision, but its exact result remains available for audit.

The durable project journal records the outcome disposition with exact command/result bytes and before/after revisions. Replay first rebuilds the committed source/control baseline in order, then submits each command once. Applied controls must advance one revision. Nonmutating controls verify their exact stored result and unchanged scene after the processor returns its ordinary status; they are not replay failures. A typed `exclude_dynamic_region` remains a `pending_exclusion` audit record until a separately verified B035 source-evidence consumer is installed. It must not mask captured mesh, mutate scene geometry, or claim successful source filtering in this adapter.

Capture sequence increases strictly per capture. Epochs must be registered before dependent observations; only accepted epoch transforms contribute project geometry. Epoch transform revisions increase strictly. Mesh lifecycle is keyed by capture/epoch/anchor: add revision 1, replace exact prior revision + 1, remove the current revision. Re-add after removal starts revision 1 and increments an internal generation. Tombstones reject stale replacement. Old immutable snapshots survive engine destruction. `mesh_source` issues map support UUIDs to actual capture/epoch/anchor/generation/revision/event sequence and vertex ranges; epoch entries carry accepted transform revisions.

The live backend retains calibrated source depth and preview depth separately; it does not fuse either into a new mesh. ARKit/platform triangle evidence drives this path. Select calibrated keyframes before submission: engine retention is bounded at 256 frames, 128 anchor identities including tombstones, 32 epoch relationships and 32 captures. Full-history durability belongs to the source store. Resource exhaustion returns `HSS_RESOURCE_LIMIT` without truncating an accepted mesh or fabricating geometry. Maximum active project geometry is 65,536 vertices and 65,536 triangles. Metric processing is capped at 2,048 patch candidates/section edges; raw triangles remain intact when metric limits are reached. A contour validation unit has at most 128 edges; larger unresolved contours produce no area. Geographic/global magnitudes beyond 10,000 m are rejected.

`working_bytes` reports conservative reservations for retained evidence, candidate scratch, JSON/geometry and immutable versions pinned by acquired snapshots. Fixed engine/handle bookkeeping is outside this payload/workspace reservation; it is not an RSS measurement. Old snapshots keep their reservations until released. `step` checks a steady-clock deadline between finite work quanta: one vertex, triangle, at most 96-point/24-candidate robust fit, at most 128-edge validation, bounded metadata setup/cleanup or one JSON record. A quantum can overrun the requested microseconds; this is cooperative scheduling, not a hard real-time guarantee. No background task runs after return.

Command and RoomPlan-proposal execution remain explicitly `HSS_UNSUPPORTED_CAPABILITY` until their verified processors are integrated. They are never acknowledged and discarded. No schema migration is needed: emitted scenes use schema 1.1 with the existing provenance and issue fields. Consumers that do not invoke the new controls still link against all existing symbols; native producers must adopt source declaration and floor confirmation to use the corresponding features.

### Revision-bound plan and annotation snapshot view

The ABI adds `HSSPlanAnnotationSummary`, `hss_snapshot_get_plan_annotation_summary`, `hss_snapshot_copy_plan_json`, and `hss_snapshot_copy_annotation_json` without changing an existing layout or function. The view belongs to the acquired immutable snapshot and uses the same scene revision. Copy calls follow the ordinary complete-size/two-call rule and return `HSS_UNSUPPORTED_CAPABILITY` when the accepted scene cannot be represented by B-031/B-032 without invented lineage.

The captured-mesh runtime submits its exact cut segments and retained mesh-support IDs as captured-section evidence. It does not forge a B-017 plane-intersection record. Unknown floor reference makes the semantic plan unavailable and suppresses annotation values. A confirmed, validated single loop remains provisional because captured mesh alone does not establish finished-room semantics or calibrated uncertainty; its numeric area remains null even when the lower-level ring calculator succeeded. Open topology also has null area. Multiple loops, resource-limited cuts, and scenes whose edit result has no typed plan mapping stay unknown, unsupported, or absent rather than being classified as holes, fixed obstructions, overhead geometry, or repaired boundaries.

Automatic runtime annotations are fitted lengths of the explicit retained cut fragments only. Their anchors, support IDs, revision, availability, and metre values are copied through B-032. No label collision operation moves a wall or an anchor. These JSON documents are derived snapshot views; source events plus ordered plan-reference controls remain the durable authority and deterministically regenerate them. A command-edited snapshot does not reuse a stale pre-command typed plan when the edit adapter cannot prove the mapping.

The current CMake core enables this adapter and links the B-031/B-032 modules. A direct-source native target that has not added those module sources keeps the same additive ABI but reports the view unsupported; native project-file adoption and renderer consumption are separate integration work.

### Resource admission and checkpoint handshake

The ABI adds `HSSResourceSample`, `HSSResourceDecision`,
`hss_engine_observe_resource_sample`, `hss_engine_get_resource_decision`, and
`hss_engine_acknowledge_resource_checkpoint`. Existing layouts and functions
are unchanged. The serialized native owner supplies an ordered sample with an
independent measured/unmeasured status for thermal state, memory pressure, and
free storage. The core does not query platform sensors. The checkpoint
acknowledgement reports the result of durable work performed elsewhere; it
does not write a journal, source chunk, or project package.

The engine uses the provisional B-041 policy defaults. Constrained pressure
drops smoothed preview depth while calibrated scene depth and mesh add/replace
remain metric observations subject to all existing validity checks. Unsafe
pressure rejects new metric observations with `HSS_NOT_READY`, requests a
checkpoint, retains accepted source, and continues draining work that was
already accepted. Epoch transitions, mesh removals, scene commands, plan
reference controls, dynamic-evidence commands, and checkpoint completion stay
on the protected control path. Preview degradation never turns off metric
validation or converts preview depth into measurement evidence.

Invalid and out-of-order samples leave policy pressure state unchanged and
latch `input_fault`, so new metric intake fails closed. An unmeasured sample
cannot clear that latch. A subsequent complete, valid, ordered sample can
clear the input fault; recovery from a policy pause still requires explicit
checkpoint acknowledgement and the policy's complete nominal-sample
hysteresis. `last_accepted_sample_sequence` and time identify the last sample
the policy accepted, not the last call attempted.

### Additive measured-guidance owner and appearance sidecar (2026-10-11)

`include/hss/hss_guidance.h` adds a serialized, presentation-only opaque owner.
ABI version remains 1; no existing layout, core source schema or archive format
is changed. New guidance structs require their exact known `struct_size`, valid
wide enum values and zero reserved fields. No exception crosses C. Invalid input
leaves owner state and presentation output unchanged. Destroy/reset are safe on
a null owner; the caller owns serialization and lifetime.

`measured_facts` is the public `HSS_GUIDANCE_*_MEASURED` availability mask:
depth/support/views/fit/stability/coverage/occlusion are bits 0 through 6.
Unknown bits are invalid. Zero-initialized C input means all scalar facts are
unavailable; finite [0,1] placeholders do not themselves certify measurement.
The older complete C++ `QualityVector` default retains all-facts availability
for compatibility; partial adapters must explicitly clear unavailable bits.
Native capture supplies only actually measured current-frame depth support,
not guessed fit, independent views, stability or target coverage. Every issued
recommendation is bound to its evidence revision and expires before the next
quality revision. Presentation debounce cannot extend stale evidence. UI clocks
and owner revisions are ephemeral and never persisted in a project or export.

The optional local native appearance sidecar is independently versioned at 1.
It stores at most 24 owned JPEG/depth records with project/capture/epoch/source
event identity, accepted scene/transform revision, project-relative time,
calibration, pose and digests. No borrowed image buffer or original device clock
origin is saved. Pending images are bound to an ephemeral runtime token and
become durable only after the canonical source checkpoint's last event sequence;
event count is not a cursor when bounded intake creates sequence gaps. Recovery
prunes bindings above the recovered cursor before new source events are accepted.
An unverifiable sidecar disables optional color, never the valid metric project.

Local asset publication is immutable-assets-first/manifest-last with protected
files, flush and readback. Projection uses only an accepted frozen project,
revision, topology and one verified epoch cohort. Supported coincident RGB/depth
camera poses, checksums and full depth visibility are required; cross-camera
occlusion is unsupported rather than guessed. Work/binding/image budgets may
return neutral appearance without changing source geometry or any metric result.

Legacy projects with no sidecar remain valid and colorless. HSSCAN import/export
does not carry this new sidecar; no automatic archive migration is implied.
Default 3D export remains colorless; explicit image inclusion adds only verified
relative JPEG references and provenance. OBJ's new material-library option is a
validated simple `.mtl` basename; the legacy `capture.mtl` default is retained,
while native bundles choose their actual `scene.mtl` companion. No metric
positions/indices are changed by UV binding or disposable renderer duplication.
Native imports, layout/type checks, raster orientation, crash/rollback, protected
file enforcement and end-to-end export tests remain required on the exact final
native tree; host portable tests are not a substitute for them.

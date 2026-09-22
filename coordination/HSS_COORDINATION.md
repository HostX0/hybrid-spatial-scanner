# HSS development coordination

Coordinator task: 01a0c60a-fef4-7de3-93bc-06e59c9a23f6

Repository: `/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/outputs/hybrid-spatial-scanner`
Source: supplied Drive folder `1a4-Zzm3lQZssDaFH7tj9lSO0fC_lHRbk`, reference archive v1.1, live S01 backlog read 2026-09-22. The original Drive documents remain unchanged.

## Owner decisions

- Latest owner direction: finish software before the phone trial; build dependable measured room/hall capture plus a separate detailed-object workflow, polished Arabic/English UX, and back up the project to a private GitHub repository. Private backup is explicitly authorized: https://github.com/HostX0/hybrid-spatial-scanner. Public release remains outside scope.

- Start from the reference package; there is no existing app/code project.
- Owner's physical test device is iPhone 15 Pro Max, with testing planned after software construction. No physical pass may be inferred from simulator, host or unsigned device builds.
- Conserve tokens without sacrificing correctness. Owner update 2026-09-22 removes the two-worker cap and permits 20 or more concurrent tasks when useful. Dispatch all dependency-ready, independently owned work within actual tool/host capacity and review throughput; do not create speculative or duplicate assignments just to increase the count. Optional external AI B-050 remains deferred.
- Software dependencies can advance after predecessor implementation, relevant host/native checks and coordinator review. Full acceptance retains every original prerequisite, including physical tests. Any assumption about shared capture configuration remains provisional until device evidence exists.

## Execution protocol

Owner update 2026-09-22: reviews must have direct value and follow real delivery milestones. Workers run the necessary focused tests once; coordinator uses their exact commit/evidence for a brief integration check. Do not spawn a separate review agent or rerun passing suites for every small task. Batch a single integration smoke check at a runnable milestone, or investigate a concrete failure, changed contract, conflict, or critical uncovered risk. Record the reason before any extra review/test pass; stop when that reason is resolved. Existing confirmed storage defects remain in scope for repair.

The owner authorizes routine local HSS implementation, testing, commits, integration and internal task coordination without repeated conversational confirmation, and requests quiet overnight work. Respect actual platform permission boundaries; do not bypass mandatory approvals. If a tool permission blocks a callback, finish the local report and let the coordinator collect it. The current coordinator environment already has unrestricted local filesystem/network execution with approval policy never; this does not establish the permissions of every worker or connector. Do not claim all OS/plugin prompts have been disabled.

Use `task-routing.json` for detailed scope/model/dependency selection and `HSS_STATUS.json` for live dispatch state. Each engineering task works in its own git worktree and branch. Shared contracts are owned by only one task at a time. Do not edit main from a worker. Commit focused changes and return a compact evidence report; the coordinator checks integration relevance and batches smoke checks at real delivery milestones.

Task states: planned, running, review, implementation_verified, accepted, blocked, deferred. `implementation_verified` is permission for scoped downstream coding, not full product acceptance. Missing device criteria stay explicit. There is no automatic Done from a worker's claim.

Every assignment states B IDs, exact worktree, selected spec paths, prerequisites, permitted file scope, acceptance checks, excluded work, and next dependency. Final reports stay under 250 words and include commit, behavior, tests actually run, untested hardware criteria and blockers. Raw logs live in evidence files. The coordinator collects results with `wait_threads`. Each worker also sends exactly one completion/blocker callback with the real commit and evidence path to the coordinator via `send_message_to_thread` (model `gpt-5.6-terra`, thinking `medium` for routine coordination), then ends its turn. The owner explicitly authorized each chat reporting to this coordinator. This starts the next review promptly. Never poll or duplicate full documentation into chat. Escalate a complex review to Sol with a bounded reproduction when needed.

Assess both model and reasoning effort per assignment. Use Luna for bounded documentation/basic verification, Terra for standard implementation, Sol for capture ownership, geometry and numerical complexity. Astra or the highest effort is justified by a specific difficult core problem, critical coupled reasoning, or a demonstrated unresolved defect; a wasteful failed lower-tier attempt is not mandatory when that need is already clear. Record the reason for each unusually expensive assignment. No API billing or paid service setup is required for development.

On task completion: inspect changed contracts and existing evidence, integrate when verified, update state, and start only eligible work. Do not duplicate successful unchanged worker tests. At major phases, verify the complete available workflow once and preserve failures. At software readiness, prepare build/install instructions and one consolidated physical test checklist for the owner. Release and accuracy claims remain blocked until required device/metrology results exist.

## Continued coordination

Completion callbacks drive normal work; an hourly thread heartbeat is only a recovery mechanism. Inspect every active result: `wait_threads` returns on the first actionable task and can omit others; collect omitted tasks with compact targeted reads. Do not process only one already-completed task per hourly wake. Filter outputs to final/status/evidence, not full raw histories. Stay quiet while unchanged. No external messages or public release; push verified commits to the authorized private backup.

## Native integration correction — 2026-09-22

At d6373fc, the portable modules are verified within their scoped tests, but `hss_engine` still publishes empty geometry and B-010 is a synthetic-only provisional plan. This is partial implementation, not a completed capture-to-plan product. Five completed modules B-021/023/026/039/041 were collected together, integrated, and passed one focused sanitizer CTest 5/5. Do not use module count as a product-completion percentage.

Five actual product workers now have isolated worktrees: I-CORE (Astra/high), I-STORE (Sol/high), I-NATIVE (Sol/high), I-UI (Terra/high), OBJECT-001 (Sol/high). State contains exact IDs, branches, paths and cursors. Astra is justified for the coupled live geometry/epoch/snapshot problem only. I-CORE owns hss_api.h additive source/floor declarations; I-STORE owns hss_project_store.h; I-NATIVE owns shared Swift model and Xcode registration; I-UI owns views/localization; OBJECT-001 owns its separate capture directory. Shared contracts are sent early, before full implementation. Only I-NATIVE runs the combined Xcode milestone build.

Source research decisions are in HSS_ENGINE_SOURCE_DECISIONS.md. D15 section 2 explicitly permits ARKit mesh as the first captured geometry path with custom fusion gated. No library listing is an implementation or an accuracy result. Captured-versus-synthetic provenance must remain separate from physical test status. Floor-relative cut height must not silently use AR camera origin as the floor. Pending after this milestone: connected commands/undo/constraints, annotations and frozen export formats, remaining privacy/import/recovery/resource wiring, final build/install route and owner test checklist. Preserve every original acceptance dependency.

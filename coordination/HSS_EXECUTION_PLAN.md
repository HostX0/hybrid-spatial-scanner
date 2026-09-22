# Hybrid Spatial Scanner execution plan

## Baseline and operating rule

Current priority (2026-09-22): close actual native integration gaps before more detached modules. See HSS_STATUS.json integration tasks I-CORE/I-STORE/I-NATIVE/I-UI and owner extension OBJECT-001. B-010 is partial: synthetic replay-to-plan is verified, native capture-to-saved-plan is not. Five workers now implement those connected paths with early shared contracts. D15 permits ARKit mesh first, custom fusion gated. Back up verified project commits to the owner-authorized private GitHub repository. After this runnable milestone, complete connected command editing/undo/constraints, annotations and frozen exporters, then one consolidated build/install checklist. Phone testing remains last; no physical pass or accuracy claim is inferred.

Build from `work/reference/HSS_Engineering_v1_1`; it is a contract/oracle pack, not product code. The owner will test with iPhone 15 Pro Max. The recorded environment is Xcode 27.0 / Swift 6.4; CMake 3.31.6 and Python 3.12.14 are now available in the isolated `work/host-venv` toolchain. Use Xcode/`xcodebuild` for native work and record actual evidence. The app remains offline-first; B-050 is deliberately not scheduled.

Every B item has two states. **Implemented/reviewed** means its scoped code, relevant host/native checks, compact evidence note, and coordinator review exist. That state may unblock hardware-independent consumers. **Done/accepted** means its source acceptance criterion is met at its required evidence layer. The original B dependency edges remain the acceptance graph in `task-routing.json`; no task may be called Done until every predecessor is Done. A physical criterion is never replaced by a host, simulator, fixture, or screenshot result. This distinction permits coding while the phone is offline, while preserving the release graph.

The coordinator (01a0c60a-fef4-7de3-93bc-06e59c9a23f6) dispatches all useful independent work within real host/tool capacity, without an arbitrary worker cap (owner update 2026-09-22). Start a task only after its implementation prerequisites are reviewed, assign exclusive file/contract ownership, and retain hardware assumptions in its evidence note. Select model and effort for each task. Use Astra/highest effort when a specific hard core problem or unresolved defect warrants it, recording why; routine work stays on economical models.

## Ownership, evidence, and review

Owner's cost policy: perform focused worker tests once and a brief coordinator integration check; batch broader reviews/smoke checks after a real runnable milestone. Repeat tests or commission an additional reviewer only for a concrete uncovered risk, failure, changed contract or merge conflict, with the reason recorded. No automatic reviewer per task and no duplicate unchanged passing suites.

Create the product tree in B-002: `HSSApp/` owns SwiftUI, ARKit/RoomPlan adapters, storage orchestration and export UI; `SpatialCore/` owns portable C++ scene, topology, solver and C facade; `Metal/` owns kernels only; the existing `include/` and `schemas/` remain the single normative ABI/schema locations; `Tests/` owns host/native/device tests and fixtures; `docs/evidence/` owns append-only factual reports. Reference files remain unchanged. Interface, coordinate, schema, export, and provenance changes require one review across affected owners.

For each handoff, provide: task ID and revision, changed paths, contract impact, commands and actual output, fixture/device evidence, omitted tests and why, known limitations, and next implementation prerequisite. Reviewer checks D14--D17/`AGENTS.md` invariants: metres/right-handed basis, retained calibrated source, snapshot authority, no generated metric geometry for unknowns, protected topology, narrow C ownership boundary, offline/privacy, and frozen revision exports.

Host evidence can establish contracts, deterministic replay, math, archive behavior and export structure. Native evidence needs an actual iOS target; hardware evidence needs the named phone. Maintain a device checklist before B-004: permission/support/readiness; single ARSession + RoomPlan effective configuration; sceneDepth/intrinsics/off-axis/orientation; mesh replacement; epoch/relocalization; save/reopen/interruption/low storage; Metal parity; thermal; real narrow rib; offline exports. Later B-042/B-045--B-048 run it with model/OS/app/tuning/operator, references/endpoints/uncertainty, duration, conditions, results and failures. A failed or blocked check remains visible.

## Waves (dependency order; independent ready tasks may run concurrently)

These are small reviewable slices, not permission to skip the final acceptance graph.

| Wave | Queue after implementation review | Purpose |
|---|---|---|
| 0 | B-001 | fixture scope and baseline |
| 1 | B-002 | app/core/contracts scaffold |
| 2 | B-003, B-009 | readiness contract and ABI bridge |
| 3 | B-004, B-011 | shared-session adapter and coordinate oracle |
| 4 | B-005, B-006, B-014 | calibrated ingest, mesh lifecycle, ABI negatives |
| 5 | B-007, B-012, B-013 | durable source, epochs, bounded ingress |
| 6 | B-008, B-038 | replay and recovery foundation |
| 7 | B-010, B-015 | minimal vertical slice and fitting |
| 8 | B-016, B-020 | graph validation and overhead separation |
| 9 | B-017, B-018, B-019, B-021 | oblique, rib, hole, ceiling branches |
| 10 | B-022, B-023 | protected topology and revisioned commands |
| 11 | B-024, B-025, B-026 | undo/constraints/coaching |
| 12 | B-027, B-029 | target tools and provenance UI |
| 13 | B-028, B-030 | calibrated overlays and localized scan UX |
| 14 | B-031, B-035 | plan semantics and captured/dynamic 3D |
| 15 | B-032, B-036 | annotations and appearance isolation |
| 16 | B-033, B-034, B-037 | frozen local exporters |
| 17 | B-039, B-041 | secure import and resource policy |
| 18 | B-040, B-043 | sharing privacy and export regression |
| 19 | B-042, B-044, B-045, B-047, B-048 | device/held-out preparation and execution; B-045 may start after B-001 |
| 20 | B-046, B-049 | benchmark then release/handoff |

B-050 remains optional/disabled and has no completion impact. ENG-01--ENG-14 are secondary workpack traceability only, never substitute for B completion counts or edges.

## Acceptance and release gates

Before final acceptance, each task owner supplies its mapped TC evidence, an implementation review, and any layer-specific evidence. Gate P2: actual offline capture-to-irregular-plan B-010 plus B-004/B-005/B-006 physical checks. Gate P3: retained arbitrary angles, holes, ribs, epochs and reversible constraints. Gate P4: frozen coherent plan/mesh/export, recovery, secure import, no automatic upload. Gate P5: independent held-out physical metrology, 20-minute thermal/performance, accessibility/Arabic usability and accurate release claims. B-049 passes only after all its source predecessors and their full gates pass. Phone-offline work may be labeled implemented/unverified, never Done.

The routing file is the machine-readable authority for all 50 B IDs, source acceptance edges, implementation readiness, model selection, ENG traceability, and optional status.

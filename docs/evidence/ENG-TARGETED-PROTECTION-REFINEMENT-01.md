# ENG-TARGETED-PROTECTION-REFINEMENT-01

Date: 2026-10-02. Owner-authorized server engineering round.
Parent: `3b7229b4f587427dfbafffb9522153303a7db3b8`.
Main observed: `78c41ac88b3be530a69974bfe487f432f3ff282d`.
Integration path: PR #18, `hss/current-main-stack-candidate`.

## Product effect and limits

The captured-source workflow previously accepted `protect_feature` on a measured
edge and then silently discarded its constraint on a later mesh replacement.
The new regression failed on the exact published parent with:
`source refinement must surface accepted protection or remove an undone intent`.

Accepted protection now stages a bounded, command-owned nonmetric unknown
annotation for the next source rebuild. If identity/lineage is not proved, that
rebuild emits `HSS_TARGET_LINEAGE_UNRESOLVED`, identifying the original command
and historical target in its explanation. Its current target is its own unknown
annotation, NOT a missing/nearby measured edge. It contains no metric geometry,
no measured support, and no active constraint rebound to a replacement.
The original accepted command result is not rewritten. The current source is
still the authority for raw vertices and derived geometry.

The existing immutable, budget-accounted retention overlay carries these
records. The combined retained annotation cap remains 256. There is no new C ABI,
wire schema, dependency, extra accepted revision, or journal replay in an advance
quantum. Publication/rollback stays on the existing transactional path. A strict
undo before refinement removes the pending protection; after source divergence,
undo explicitly rejects rather than restoring the old whole Scene.

This completes the **engine-level** protected-target conflict slice, not stable
lineage rebasing. Generic dimension/lock/split/merge retention is not established.
Conflict resolution/retargeting is not implemented here. Unknown annotations do
not measure occluded surfaces. No accuracy, FPS, or competitor-parity claim follows.

## Executed verification

Workspace: `/home/abdulazeez/hss-work/targeted-3b7229b.pcxkvxnd/`.
Fresh isolated clone: 459 source blobs matched the exact parent before edits;
see `source-integrity.json`. The original dirty checkout was untouched.
Linux x86_64, GCC 13.3.0, user-local CMake 3.31.6 and ZLIB 1.3.
Root compilation used `-Wall -Wextra -Werror -Wpedantic`, ASan+UBSan and assertions
(no `-DNDEBUG` in any of the 95 compile-command entries).

Commands, with `W` equal to the workspace above:

```sh
/home/abdulazeez/hss-work/tools/bin/cmake -S "$W/source" -B "$W/build" \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZLIB_INCLUDE_DIR=/home/abdulazeez/hss-work/deps/zlib/usr/include \
  -DZLIB_LIBRARY=/home/abdulazeez/hss-work/deps/zlib/usr/lib/x86_64-linux-gnu/libz.a
/home/abdulazeez/hss-work/tools/bin/cmake --build "$W/build" --parallel 2
/home/abdulazeez/hss-work/tools/bin/ctest --test-dir "$W/build" \
  --output-on-failure --no-tests=error --timeout 180 \
  --output-junit "$W/full-tests.xml"
```

- Baseline regression on published parent: 1 failed, 0 passed, 0 skipped,
  0.11 s; retained `baseline-regression.log`, XML, and exact regression source.
- One premature targeted test launch raced the link step: 2 tests **not run**
  (executable temporarily unavailable). This is retained in `targeted-tests.log`
  and XML, not counted as a product pass. After collecting build exit 0, the
  targeted adapter+regression run passed 2/2, 0 failed, 0 skipped, 43.04 s.
- After adding invalid-source atomicity, strict post-refinement undo rejection,
  and cooperative 1us/50us advance coverage, the final full root build succeeded
  and **55/55 CTests passed, 0 failed, 0 skipped, 89.82 s**. This includes the prior
  54 tests plus newly discovered `hss_targeted_protection_refinement`.
- New regression executes accepted protection, fresh raw geometry, explicit
  user/unknown provenance, valid references, one conflict per command, repeated
  source rebuilds, a later independent edit, unchanged exact retry bytes,
  checkpoint/reopen and byte-equivalent complete durable replay, pre-source undo,
  invalid replacement rollback, and post-source undo refusal. Requested 1us/50us
  budgets are cooperative requests, not hard-real-time guarantees.

`commands.json` retains actual commands/PIDs/logs. `full-tests.xml` and
`full-tests.log` are the final local result. Published-tree verification belongs
to the follow-up PR comment and durable handoff; the above is not a claim that
unfetched published bytes or native/device code have passed.

## Native delivery audit: next dependency, not more speculative engine work

The repo already contains a SwiftUI/Xcode application and historical simulator
and unsigned iPhoneOS evidence. Those results do not validate this exact head.
The server was online and execution succeeded; the authorized Mac was offline.
No native test, signed device package, physical scan, or upload ran this round.

Current concrete application gaps found in the exact parent:

1. `HSSApp/ScannerWorkspaceProductHost.swift:140-141` binds `assist` and `undo`
   to empty closures. The native workspace protocol currently exposes targeting
   but no applied scene-command operation. A core command test is NOT proof that
   the application's correction button works.
2. That same host's `issues` projection (lines 283-319) handles workspace/save
   errors and synthetic-data disclosure, not the core Scene's issue records.
   This round preserves conflict data in the engine/durable project; it does
   NOT claim the new conflict is already presented in the iPhone UI.
3. Exact-current Apple-toolchain compilation, native command/journal wiring and
   core-issue presentation need focused simulator tests before an owner-device
   trial. Then verify capture -> inspect 2D/3D -> supported correction -> durable
   save/reopen -> export on the physical iPhone. Signing/device trust and physical
   reference measurements require the owner only where actually unavoidable.

Do not start a replacement app. Do not treat this audit as every backlog item
being absent: the original tracker snapshot is stale. Existing modes and native
source remain intact; no unrelated server service or capture data was touched.

## Primary research and decisions

- Apple, ARPlaneExtent:
  https://developer.apple.com/documentation/arkit/arplaneextent
  and Tracking and visualizing planes:
  https://developer.apple.com/documentation/arkit/tracking-and-visualizing-planes
  ARKit refines estimated shape/extent as evidence arrives. Adopted: retain new
  source estimates as authority. An anchor or derived ID alone is not our proof
  of unchanged edited topology. Rejected: copying old metric geometry over new
  observations or automatically choosing the nearest target.
- Apple, Running your app on simulated or physical devices:
  https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices
  Adopted: exact native build and physical feature tests are separate gates;
  simulator/Linux results cannot stand in for LiDAR/device performance.
- GitHub, createCommitOnBranch:
  https://docs.github.com/en/graphql/reference/commits
  Use `expectedHeadOid` for atomic expected-head publication of exact tested bytes.

Documentation reading only; no Apple/GitHub sample code was copied, no third-party
library/dependency was added, and no license incompatibility was introduced.
The mobile integration cost is bounded retained annotation memory using the
existing budget leases; native UI integration remains explicit unfinished work.

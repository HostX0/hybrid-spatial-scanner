# Native privacy and supported correction delivery

Date: 2026-10-11. Integration baseline: `b09c3d21ccbdeda5e58c9438a9f7d5d33e3cd254`.
Engineering evidence, not a customer brochure or App Store readiness claim.

## Product implementation

HSSCAN sharing now uses an export-only validated portable copy. Legacy device
boot origin is removed from all decoded source/control event contexts and
manifest record times; chunks are re-encoded and hashed before manifest-last
publication. The original project is read-only. Scene JSON, captured vertices,
triangles and exact command/result bytes are preserved. Size, symlink,
source/staging/destination boundaries and failure cleanup remain enforced.

Current supported boundary selection uses actual immutable graph UUIDs with
explicit measured/fitted/user-measured support. Raw mesh vertices and camera
rays are not implicitly converted into durable edit targets. Native review
offers protection, split, confirmed point-position lock, sourced dimension with
optional uncertainty and hard/soft mode, and strict-lineage undo/redo. Localized
decimal entry requires complete finite bounded input, including Arabic digits.
Selection/confirmation resets after an accepted revision changes. A rejected
proposal keeps the last accepted shape and user input rather than silently
retargeting it. A dimension conflict is journaled without advancing the scene.

The position-lock/refinement regression proved that an accepted lock's visible
effect could disappear on a source rebuild. The repair retains active split,
position-lock and dimension intent as an explicit nonmetric review marker when
new target correspondence cannot be proved. The original exact journal remains;
no guessed metric constraint is rebound. Undo stops at source/control revision
gaps. Generic automatic target rebasing and CAD-complete editing are not claimed.

A bounded independent review found two further defects: exact stored retries
return `HSS_OK` even when the original result was conflict/rejected, and the
model overwrote a conflict message with missing-evidence success text. Recovery
now validates original disposition/revisions/exact result bytes; feedback keeps
rejection distinct from applied/saved. Debug-only one-shot result-copy and
journal-append failures test both real-runtime recovery paths. These failure
hooks and the native UI setup are guarded by `#if DEBUG`. Their absence from
the final exact-tree Release binary remains an explicit archive check.

## Privacy and file-protection inventory

The app has no account, analytics/tracking SDK or app-owned network client. Local
camera/mesh/depth/photo/project/object data stays on device unless the user
explicitly exports/shares. Archives and reconstructed models may reveal private
spaces. Source photos persist until explicit session deletion. Deleting locally
cannot erase already shared copies or system backups; the privacy screen says
so in English and Arabic. The empty collected-data declaration describes the
current offline app, not every destination chosen in the system share sheet.

`PrivacyInfo.xcprivacy` is an app resource. Required-reason inventory:

- System boot time, `35F9.1`: transient elapsed timeout/interaction/resource
  timers and in-app event intervals. Export removes the absolute clock origin.
- Disk space, `E174.1`: storage readiness and observable low-storage checkpoint/
  pause behavior. Capacity samples are not exported.
- File timestamps, `C617.1`: Foundation/C++ filesystem metadata for app-container
  records. `3B52.1`: metadata for explicit user-granted document-picker imports.

Default app data protection is configured as
`NSFileProtectionCompleteUntilFirstUserAuthentication`, matching the existing
explicit object-session/file policy. Data Protection capability and Debug/
Release entitlement inputs are set. Entitlement configuration in an unsigned
build is not evidence of signed provisioning or real locked-device enforcement.

Official Apple references inspected; no third-party source code or dependencies
were copied:

- [Required-reason APIs](https://developer.apple.com/documentation/bundleresources/describing-use-of-required-reason-api)
- [Approved reason categories](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitype)
- [Approved reasons](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitypereasons)
- [Data Protection entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.default-data-protection)

## Native UI fixture and reproducible journeys

`hss_native_boundary_fixture` is a host-only executable enabled by
`HSS_BUILD_TESTS`. It uses the actual engine/store to checkpoint four walls in
a synthetic reference room. It refuses a nonempty or symlink destination and
never links into the iPhone app. The catalog and engine both label the source
synthetic. There is no private or licensed real-world capture in this fixture.

For manual native inspection, install the Debug app in a dedicated simulator,
resolve its data container using `simctl get_app_container`, and run the helper
against the empty `Documents/HSSProjects` directory. The scene has eight
triangle-supported cut segments, not four analytically merged wall edges. No
tolerance or topology rule is altered to change that count.

XCTest reinstalls/reset app containers in this environment, so boundary journeys
explicitly request a Debug-only native setup (`HSS_UI_REFERENCE_ROOM=1`). It
refuses a nonempty library, checkpoints the same synthetic source through the
real runtime, and reloads the ordinary catalog. Both app startup and setup code
are excluded from Release. No reference data becomes a production capture.
Run `HybridSpatialScannerJourneys` serially in a dedicated simulator. Ordinary
library/privacy/object journeys do not request seeding. English and Arabic
journeys select a real retained edge, split it, undo and redo through native
commands, verify edge counts, and run unsuppressed current-screen contrast,
hit-region, text-clipping and Dynamic Type audits. Reference data is not physical
sensor or dimensional-accuracy evidence.

Use `-collect-test-diagnostics never` for bounded local runs, as in the previous
accessibility delivery. This prevents long post-failure system diagnostics; it
does not suppress any test assertion or accessibility audit. Wait for initial
Simulator migration/boot completion and for the previous xcodebuild operation
to exit before launching another test operation on the same simulator.

## Verification record

Before the independent-review fixes: 107/107 actual iPhone Simulator XCTest;
59/59 AppleClang Debug ASan+UBSan CTest (37.13 s). These are local uncommitted-tree
results, not published-head or hosted-CI results. The lock/refinement red result
is retained in `Test-HybridSpatialScanner-2026.10.11_15-07-52-+0300.xcresult`:
“A source rebuild must not silently discard an accepted position lock.”
Portable-export initial failures and repaired nine-test native import evidence
are retained in their original logs. The first review-recovery build lacked a
Debug compilation condition; the next Simulator install stalled and was
explicitly cancelled/restarted, not counted as a test failure or pass. The two
review-recovery assertions then passed on the real iPhone 17 Simulator.

A targeted export-validation reproduction also proved that a valid encoded
floor-control payload mislabeled as a command could pass portable export.
The red executable exited 1 with “portable export must reject a floor payload
mislabeled as a command” (`test-portable-control-kind-reproduction-2.log`).
The repair checks the decoded control variant against the manifest kind before
writing any active derivative manifest. The regression checks both rejection
and a correctly labeled floor control, plus original manifest and payload-byte
immutability. The first attempt's missing constructor-limit arguments were a
test-build error, not the reproduced product failure.

Final verification is recorded below only after commands complete. Original
dirty checkouts, private captures and previous evidence are preserved. No seam
tolerance is widened and no engine is rewritten. Signing, phone installation,
real sensor/privacy-lock/metrology/thermal/competitor acceptance remain deferred
and unverified; Team ID is not needed for the remaining software work.

### Completed local results and resource-coordination pause

- Reviewed native runtime/model tree: 110/110 iPhone 17 Simulator XCTest,
  zero failed or skipped, iOS 26.5 arm64. Result:
  `Test-HybridSpatialScanner-2026.10.11_17-12-19-+0300.xcresult` under
  `build-xcode-sim-test/Logs/Test`. This precedes the Debug UI setup, camera
  localization and final portable control-kind check; it is not exact-final-tree
  native evidence.
- Final portable core, including the decoded control-kind repair: 59/59
  AppleClang Debug ASan+UBSan CTest, 157.21 seconds, command exit 0.
  Logs: `build-privacy-correction-core-final.log` and
  `ctest-privacy-correction-core-final.log`; JUnit:
  `ctest-privacy-correction-core-final.xml`. The increased duration occurred
  during concurrent Mac resource pressure; this is not a performance benchmark.
- Earlier unsigned iPhoneOS Release archive completed for the reviewed privacy/
  correction tree. It is arm64 and includes the privacy manifest, version 0.1/
  build 1, with no Team or signing identity. It precedes the last source changes.
  The subsequent final archive was interrupted (exit 75) and is not deliverable.
- New privacy/correction UI tests have not completed. The bounded English run
  (`build-ui-boundary-bounded.log`) ended with `TEST INTERRUPTED`, exit 75,
  following cross-project shutdown/build interruption. No boundary journey,
  accessibility pass, UI screenshot or product defect is inferred from it.

The owner explicitly requested on 2026-10-11 that Mac builds and simulators stay
off while they test ZERO X. Lightweight source/document work and server checks
continue. No HSS Mac build or simulator will be restarted until the owner
resumes heavy Mac verification. Do not stop or operate the ZERO X iPad simulator.
No hourly task is restored. Signing Team ID remains deferred, not a blocker.

Resume from this same isolated source and retained build products, serially:

1. Finish the single English boundary split/undo/redo journey with bounded
   diagnostics; inspect any completed failing result and repair only demonstrated
   failures. Then run all seven unsuppressed English/Arabic/landscape journeys.
2. Run the full native XCTest suite on the exact source tree.
3. Produce an exact-tree unsigned arm64 Release archive. Inspect packaged
   `PrivacyInfo.xcprivacy`, English/Arabic `InfoPlist.strings`, archive metadata,
   and absence of Debug-only setup/fault hooks in the Release binary.
4. Reconcile fresh server lock/head/checkpoint and exact-source Linux sanitizer
   results before integration publication. Preserve original dirty checkouts.

Current work is not published, merged, signed, installed on a physical phone or
App Store ready. A source snapshot and server validation can be completed during
the resource pause without representing this as a finished native milestone.

### Completed server verification during the Mac pause

Code checkpoint: `37311ff682802d093f6a58883461e60356fd262c`;
source tree: `b53701ab45abf67e63dde06c970cfd06a3e98b7a`.
The transferred Git bundle SHA-256 was
`660a1ae34197b3e582cd3134258a93e80d315cf89c6c685bbc4ae182c81a6006`.
The fresh isolated server checkout verified both identities, clean tracked
source and bundle integrity before compiling. The original `run.lock` inode
`12106462` was held by the bounded worker throughout shared writes and checks.

- Fresh GCC Debug ASan+UBSan: 59/59 CTest passed, zero failed/skipped,
  79.99 seconds; worker exit 0. This is a new exact-code run, not reused evidence.
- Host reference-room creation: eight triangle-supported boundary segments,
  eight raw vertices, three retained events and checkpoint revision 3. A second
  invocation refused the nonempty destination. This validates host fixture
  setup, not UIKit interaction or physical sensor accuracy.
- Server plist parsing verified offline privacy declarations and the configured
  Data Protection value. It does not prove packaged native resources or signed
  entitlement enforcement.

Evidence directory: `/home/abdulazeez/hss-work/codex-privacy-server-9fuibaf3`.
It contains the isolated source, bundle, configure/build/CTest logs, JUnit,
fixture success/refusal logs and retained Mac reproduction/verification logs.
Paired HANDOFF/state were written in that order, fsynced and read back. The
lease/current run were cleared and the kernel lock released on completion.
No GitHub push, PR merge, Mac build or simulator startup was performed in this
server milestone. Published integration remains the baseline stated above.

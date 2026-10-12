# ENG-12 — separate Object Capture lifecycle

## Scope and dependencies

Continues the verified native product baseline `b70787f039b827c00cbcdb529d829cf73fc1e14a`.
Uses the existing RealityKit capture/reconstruction workflow, app-local storage and
SwiftUI shell. No engine rewrite, tolerance change, C ABI or canonical room schema change.
Object photogrammetry remains a visual artifact with `metricAuthority=false`.
The owner's hourly Codex heartbeat `hss` was deleted through the app on 2026-10-10;
it was not replaced with another schedule. Direct engineering continues.

## Delivered behavior

- An independent Object library is reachable from the room library and an idle,
  saved room review. It is blocked during capture, project opening/saving and
  outstanding source/journal work; one camera workflow remains the owner.
- Discover original `session.json` sessions, including cancelled sessions with
  retained images. Show completed visual models, incomplete photos, unreadable
  sessions and interrupted deletions distinctly. Corrupt records never authorize
  reconstruction or silently disappear from the library.
- Rebuild incomplete sessions from retained photos using existing RealityKit
  reconstruction checkpoints. This restarts reconstruction, not camera capture;
  checkpoint reuse and successful reconstruction remain hardware-dependent.
- Preview USDZ through Apple's Quick Look and share only the visual model using
  an explicit system ShareLink. Source photos are not included by this action.
- Explicit irreversible deletion removes only the selected UUID session's photos,
  checkpoints, model and provenance. A separately flushed, root-level deletion
  intent blocks reconstruction throughout partial cleanup, including when the
  session directory is already gone. Retry is idempotent. Other object sessions,
  room projects, external/shared copies and OS backups are unaffected.
- Rebase decoded absolute URLs onto the trusted container's UUID-owned paths.
  Reject redirected roots/session folders, image/checkpoint/output links and
  dangling top-level links. Recursive deletion unlinks nested links without
  following them. Reconstruction enumeration fails closed on traversal errors.
  Bounded record reads, enumeration and counts prevent unbounded catalog work.
- Object directories and atomic JSON writes explicitly request iOS
  `completeUntilFirstUserAuthentication` file protection. This is a software
  configuration result, not a physical protected-data/lock test or a full-app
  privacy-manifest approval. Backups/providers retain their own policies.
- English/Arabic library and object guidance strings, native scalable controls,
  system sheets and adaptive horizontal/vertical model actions. SwiftUI skill
  guidance informed native patterns, semantic surfaces and accessibility labels.

## Executed verification

- iPhone Simulator native XCTest: **88 passed, 0 failures** on the final source.
  Seven added object lifecycle tests cover legacy discovery, safe URL rebasing,
  selected deletion/nested-link survival, durable partial cleanup, redirected
  roots/checkpoints, corrupt records and dangling session links.
- Dedicated `HybridSpatialScannerJourneys` XCUITest scheme: **3 passed, 0 failures**. English object-library
  round trip and capture introduction/unsupported gate, Arabic RTL round trip,
  landscape controls. Screenshot attachments are actual app renderings, not mocks.
- Generic iPhoneOS **Release unsigned build passed**. No team was guessed and no
  provisioning/signing credentials were changed.
- `git diff --check`, project and Arabic strings `plutil -lint` passed.
- Initial test failure retained in `build-object-lifecycle-test.log`: the legacy
  ISO8601 record intentionally stores whole seconds, while the fixture compared
  fractional seconds. The fixture now uses an exact whole-second date; no storage
  format or numeric tolerance was widened.
- Final logs: `build-object-native-final.log`,
  `build-object-journeys-contrast.log`, `build-object-release-contrast.log`.
- Portable engine source is unchanged from the baseline's independently executed
  59/59 GCC ASan+UBSan CTest run. That result is **reused baseline evidence**, not
  falsely reported as a new engine run for this Swift-only packet.

Toolchain: Xcode 27.0 (27A266a), Swift 6.4, iOS 27 SDK, deployment target iOS 17.
Simulator: `D80BA976-A08C-427F-846D-A35C380A0C3A`.

## Research and reuse

Uses platform implementation, no new third-party dependency or copied source:

- [Apple Object Capture session](https://developer.apple.com/documentation/realitykit/objectcapturesession)
- [Meet Object Capture for iOS](https://developer.apple.com/videos/play/wwdc2023/10191/): retained photographs/checkpoints support local reconstruction.
- [Apple Quick Look](https://developer.apple.com/documentation/quicklook): system USDZ preview.

## Explicit remaining gates

Simulator tests do not execute supported-device photogrammetry, camera, physical
accuracy, checkpoint acceleration, phone-lock behavior, thermal/storage endurance,
real USDZ interoperability or competitor comparisons. Full VoiceOver and large-text
journeys remain to be expanded. Canonical room floor confirmation/corrections,
portable capture-time/privacy-manifest audit, selected-team signing/archive and
store submission remain separate unfinished packets. The owner defers connecting
the physical phone until the software milestone. This packet is not an App Store
readiness claim, a merge or a physical install.

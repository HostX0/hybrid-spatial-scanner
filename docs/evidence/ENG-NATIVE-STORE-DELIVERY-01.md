# ENG-15 — Native verification and first Apple upload

Engineering evidence, not customer-facing material. Date: 2026-10-12.
This closes a real signing/upload milestone, not App Review or physical acceptance.

## Source and ownership

Started from published integration head `4dbbf46f81f857a769b98d7486b564d9154d9bb6`
in a fresh isolated Mac clone, branch `hss/native-release-20261012`. The original
dirty integration checkout, private captures and ZERO X submission were untouched.
PR 18 remains open/draft/unmerged; main remains `78c41ac88b3be530a69974bfe487f432f3ff282d`.
The Linux coordinator owns the original project flock inode 12106462 with a
bounded, identity-checked lease and paired HANDOFF/state checkpoints.

## Demonstrated repairs and verification scope

- Extracted the existing owned CoreImage JPEG encoder without changing its policy.
  Independent asymmetric BGRA pixel-buffer tests verify half/quarter raster
  scaling, image decoding, off-axis calibration and UV alignment without GPS data.
- Fixed a real first-open empty boundary sheet by binding presentation to one
  immutable snapshot item. Current revision and supported-command guards remain.
- Added adaptive project identity layout and multiline boundary instructions;
  unsuppressed accessibility audits remain independent release gates.
- Disabled signing for the static core library: a global manual-signing override
  had incorrectly demanded a bundle identifier for that static archive.
- Added the actual generated Info.plist encryption declaration; the application
  uses standard OS protection and SHA hashing, not proprietary encryption.

The native unit/bridge/appearance/guidance suite passed 121 tests before the final
presentation/build changes. Final exact-source native and UI results are recorded
below when complete; this earlier pass is not silently relabeled as a final pass.
Portable core source is unchanged: reuse the retained 60/60 Linux ASan+UBSan
assessment and licensed real-data retention/replay results; no seam tolerance was
widened and no new physical accuracy claim is made.

Earlier UI failures are retained: initial empty sheet, lazy Form row-count oracle,
an unsigned runner executing removed test code, and app-center swipes panning
the SceneKit preview instead of scrolling the Form. Current signed-simulator
hierarchy proved boundary selection succeeds. Correction tests now scroll the
Form gutter and scope history queries to the presented Form, avoiding identical
semantic identifiers in the underlying workspace. Real command acknowledgements
and independent runtime topology assertions remain required. Accessibility
failures are not ignored, expected-failed or declared successes.

## Signed artifact and actual Apple upload

- Xcode 27.0 (27A266a), arm64 iPhoneOS SDK 27.0, minimum iOS 17.
- App: `org.hybridspatialscanner.app`, version 0.1, build 1.
- Apple team: THE THIRD ACT LLC, `J3VB9XTF33`.
- Existing distribution certificate SHA-1:
  `DD64C4032C9688B30A205E9A7410378E04E24612`; no private key was read/exported.
- App Store profile: `HSS App Store 20261012`, UUID
  `432fcbff-f23c-4163-84c5-2ecdfae4eab4`, exact app/team, `get-task-allow=false`,
  no registered-device list, protection `NSFileProtectionCompleteUntilFirstUserAuthentication`.
- Company Release archive succeeded; deep/strict code-sign verification passed.
  Signed entitlements, packaged privacy manifest, localized camera strings and
  actual `ITSAppUsesNonExemptEncryption=false` were checked. Debug reference-room
  and forced-failure strings are absent from the Release executable.
- Signed executable SHA-256:
  `2a96f74bb02073d51313b423fa512ba2c63dcfe70db16abbbc8611e27b1881bb`.
- On 2026-10-12 at approximately 00:47 UTC, authenticated Xcode Organizer showed
  **App upload complete — HybridSpatialScanner 0.1 (1) uploaded**. Organizer then
  showed **Uploaded to Apple**, build 1, THE THIRD ACT LLC and the exact app ID.
- App Store Connect record: `6821764872`. English metadata, version 0.1, support,
  marketing/privacy URLs and approved company review contact were saved. Manual
  release is selected. No public release or App Review submission was performed.
- App Privacy No-data-collection response is saved as a draft. Its final Publish
  dialog is a legal attestation and awaits the owner's action-time confirmation.
  Upload success does not imply privacy publication, processing or TestFlight access.

CLI automatic export failed with No Accounts despite an authenticated GUI Admin
account. The actual delivery used the existing distribution identity, a scoped
profile issued through the authorized Apple portal, and GUI Organizer upload.
The unsigned and failed signing attempts remain preserved, not overwritten.

## Private retained evidence and remaining acceptance

Native logs, xcresults, hierarchy and real UI screenshots, unsigned/company
archives and coordinator receipts remain in the private `work/` delivery area.
The initial iOS 27 run failed before tests with an invalid connection UUID; this
is retained as infrastructure failure, not app acceptance. iOS 26.5 was used for
actual native execution. Verbose failure sysdiagnostics hung after a later run;
subsequent focused runs preserve screenshots/logs without that unnecessary dump.

Remaining gates: four unresolved accessibility audits, iPad acceptance,
clean actual App Store screenshots, required store
questionnaires/pricing, owner-confirmed privacy publication, Apple processing and
review, and supported-phone installation/capture/measurement/recovery/thermal/
locked-device verification. No physical installation or capture occurred.

## Final exact-source execution

`native-published-unit-20261012.xcresult`: **121 passed, 0 failed, 0 skipped**.
`native-published-journeys-20261012.xcresult`: **5 passed, 4 failed, 0 skipped**.
All five functional journeys passed: English/Arabic boundary split/undo/redo,
English object-capture introduction/return, Arabic object-library round trip,
and landscape controls. Four independent accessibility audits still fail:
Arabic boundary text clipping, English boundary unsupported Dynamic Type,
and English/Arabic library partially unsupported Dynamic Type. These prevent
claiming complete accessibility or public-release readiness. Native help/privacy
audits later in the library journey are not reached after its first failure.

The final app source matches the signed uploaded build; subsequent changes are
test harness and engineering documentation only. Source publication and hosted
checks remain distinct from native-built, signed and Apple-uploaded evidence.

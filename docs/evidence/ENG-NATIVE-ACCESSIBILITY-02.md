# ENG-16 — Adaptive native layout and validated second Apple build

Engineering evidence, not public product copy. Date: 2026-10-12.
Parent integration head: `b8e6b8be8fa231e2cbe180d43aacaf15f860e751`.
PR 18 remains open/draft/unmerged. No portable engine or capture ABI changed.

## Demonstrated issues and product changes

The previous actual largest-text Arabic screenshots showed a truncated boundary
navigation title, a fragmented Trash label and search symbols consuming their
controls. These are demonstrated presentation issues, not seam-tolerance or
metric defects. Current-source changes:

- Library headings become vertical at accessibility sizes; project actions stay
  reachable. Long library/Trash labels have unrestricted multiline height.
- Search has a visible multiline label at accessibility sizes, an explicit
  accessibility label and bounded decorative symbols rather than oversize icons.
- The boundary sheet uses a short inline Review navigation title and a full
  multiline Boundary corrections heading in its scrolling body. The explanatory
  icon and text use an explicit top-aligned layout.
- Native SceneKit represents the boundary preview as one accessible image. The
  existing edge list retains selectable boundary identities and measurements;
  no geometry is removed and no accessible command is hidden.
- Store metadata now carries an explicit build number. The source verifier
  accepts a valid matching release increment and rejects malformed or stale
  build metadata instead of hardcoding build 1 forever.

Largest-text tests request Accessibility XXXL and assert actual rendered project
name height greater than 35 points (the normal row is approximately 20 points).
They retain screenshots/hierarchy, scroll lazy rows into view and check that
boundary selection and correction controls remain reachable. This is direct
rendering/interaction evidence, not merely a semantic font declaration.
Current Arabic screenshots show the full multiline heading/instructions and
unfragmented library/Trash labels. Off-viewport content remains scrollable.

## Necessary native execution, with failures retained

Xcode 27.0 (27A266a), Apple arm64, iOS 26.5 Simulator (23F77):

| Evidence | Actual result | Scope |
| --- | --- | --- |
| `native-adaptive-layout-20261012.xcresult` | 7 passed, 4 failed, 0 skipped | iPhone 17 Pro, adaptive source; seven functional journeys and four independent accessibility audits |
| `native-build2-unit-20261012.xcresult` | 121 passed, 0 failed, 0 skipped | Build 2 native bridge/runtime/appearance/guidance tests |
| `native-ipad-functional-20261012.xcresult` | 7 passed, 0 failed, 0 skipped | iPad Pro 13-inch (M5), build-2 functional device-family gate |
| `native-support-functional-verified-20261012.xcresult` | 2 passed, 0 failed, 0 skipped | Build-2 iPhone English/Arabic privacy and offline support round trips, independently reachable after library audit failures |
| `native-store-build2-verified-20261012.log` | 10 passed | Source/store consistency and strict malformed/stale build regressions; not native compilation |

The adaptive iPhone run preceded only the app build-counter increment to 2.
iPad executed all seven functional methods on build-2 source, including Arabic,
landscape, split/undo/redo and largest-text English/Arabic controls. Its selection
explicitly excluded the four known independent audits; it is not an iPad
accessibility pass. No audit issue handler, expected failure or ignored issue was
introduced. The four unsuppressed iPhone failures all report **Dynamic Type font
sizes are partially unsupported**. The earlier Arabic clipping warning is absent
in this run, but that does not make the remaining warnings successes.

The independent support journeys open and dismiss the privacy disclosure and
offline help, scroll approved email/phone destinations into view and return to
the library. They do not send messages, call a telephone, open external pages or
upload captures. These two later tests are separate results, not a rerun of all
13 current UI methods or completion of the blocked later-screen audits.

Retained source-check failures: the original verifier rejected the valid build
increment, then the system Python lacked Pillow. The corrected guard and negative
regressions passed using the existing bundled trusted Python runtime; no package
or unrecognized software was installed. A later support-only UI invocation first
selected a target outside the specified scheme; this infrastructure invocation
failure is retained separately, not counted as executed app tests.

Apple staff have described similar audit reports as a possible Inspector bug
when fonts actually grow and do not clip. That is conditional guidance, **not
proof that these four application reports are false positives**. Retained visible
growth and repaired layout justify the finite diagnosis; full accessibility
acceptance, VoiceOver and minimum-supported-OS checks remain unverified.

## Signed build 2 and actual Apple readback

- App `org.hybridspatialscanner.app`, version 0.1/build 2; THE THIRD ACT LLC,
  team `J3VB9XTF33`, existing distribution identity/profile from the first receipt.
- `HybridSpatialScanner-build2-20261012.xcarchive`: arm64 Release archive succeeded.
  Deep/strict code-sign verification passed. Bundle/version/team, packaged privacy
  manifest, file protection and non-exempt-encryption false were checked.
- Signed executable SHA-256:
  `138401558b01c84189281e92ddcdb5df5dc2e896d30b4ab09282ecc1337f1bd5`.
- Authenticated Xcode Organizer visibly confirmed **0.1 (2) uploaded**, then
  **Uploaded to Apple**; Organizer displayed upload time 04:24 on October 12.
- Actual App Store Connect record `6821764872`, Apple build UUID
  `5b99342d-de52-422c-a367-c226c54ebea3`, shows **Ready to Submit**, expiring in
  90 days. Build Metadata shows **Validated**, 0.1/build 2, SDK 24A430, arm64,
  iPhone/iPad, Arabic/English, expected bundle/team/protection and no non-exempt
  encryption. Apple displayed upload time October 12 at 04:28; retain the two
  actual display observations rather than inventing a normalized upload instant.
- Product-only What to Test is saved, including the remaining font-scaling
  limitation. Groups and individual testers are both zero. No invitation,
  installation, App Review submission or public release occurred.
- App Privacy No-data-collection draft and public policy URL are saved. Final
  privacy Publish awaits the owner's requested action-time legal confirmation.

Subsequent live follow-through, after those initial observations:

- The owner explicitly confirmed the privacy Publish attestation in this chat.
  The exact HSS App Privacy page then showed **Published a few seconds ago**
  and **Data Not Collected**. English/Arabic app-specific public policy URLs are
  saved; publishing the privacy response is not submitting the application.
- Version 0.1 has saved build-2 association, English/Arabic product descriptions,
  keywords/subtitles and support/marketing URLs. Productivity and Utilities
  categories are saved. The factual age questionnaire calculates global 4+ with
  displayed regional exceptions. No Made for Kids or age override was selected.
  Content-rights answer matches the shipped inventory: no third-party media or
  content service. No licensed research images are bundled in the application.
- The age-capability answers follow Apple's definition of broad user-content
  distribution, not the existence of a private local scan or system share sheet.
  There is no in-app browser, social feed, messaging, advertising, medical advice,
  supplied mature/violent content, gambling, contest or randomized purchase.
  Reference: https://developer.apple.com/help/app-store-connect/reference/app-information/age-ratings-values-and-definitions
- Build-2 App Store IPA export succeeded, approximately 2.3 MiB. SHA-256:
  `89c72c5c686ee5c96cb337bcc494c8e31659365467dc89245e2352005b5d0c53`.
  This distribution profile is not direct sideload or installation evidence.
- The owner requested designed actual-app screenshots and continued submission.
  A Release Simulator build of the unchanged build-2 app source succeeded
  for authentic UI images (`native-store-release-build-20261012.log`). Reference geometry must remain explicitly disclosed
  as illustrative, never a captured-room or physical-precision result.

The native application files in the uploaded archive are the current build-2
source. Later changes add test harness coverage and engineering records only;
they do not require reusing a changed app under the same uploaded build number.
No private interior capture, credential, private key or engineering screenshot
was uploaded as store marketing material.

## Coordination and finite remaining gates

The isolated Mac branch preserves original dirty sources. The successor worker
owns the same Linux flock inode 12106462 under a bounded lease, with paired
HANDOFF/state updates. Publication, hosted narrow checks, actual source merge,
native build, Apple upload and physical verification are separate states.
Do not relabel a hosted PR merge-preview as a main merge.

Do not reopen the completed portable core/real-depth retention assessment or
widen seam tolerances. Strict real RGB/depth timing still admitted zero texture
faces; synthetic asymmetric raster/UV checks are not physical texture accuracy.
Remaining release gates are the four font audit warnings and broader device/OS
accessibility, consented final-app store screenshots, required account/store
questionnaires/pricing, owner-confirmed privacy publication, App Review, and
actual supported-phone capture/measurement/recovery/thermal/locked-device checks.

## Primary references inspected

- Apple Dynamic Type layout guidance:
  https://developer.apple.com/videos/play/wwdc2024/10074/
- Apple accessibility auditing scope:
  https://developer.apple.com/documentation/accessibility/performing-accessibility-audits-for-your-app
- Apple staff's conditional Inspector diagnosis (April 2026 discussion):
  https://developer.apple.com/forums/thread/823968

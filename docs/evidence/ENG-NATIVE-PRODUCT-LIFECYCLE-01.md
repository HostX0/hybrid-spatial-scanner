# Native product lifecycle and truthful review — 2026-10-10

## Scope and authority

Owner authorized software-first implementation toward an App Store product, explicitly deferring phone connection and physical testing. Selected work: ENG-12 local-data lifecycle, with ENG-10 presentation dependencies and ENG-14 original icon packaging. The original dirty Mac/server checkouts and previous experiments remain untouched. Work uses the isolated integration clone on `hss/current-main-stack-candidate`, parent `1f59a088c36636e1fcbb94dcbf779fa22da300a8`. Publication/automation checkpoint must acquire the existing server `automation/run.lock`; the hourly worker is not disabled.

## Implemented behavior

- Responsive, scrollable project library: search, recent/name sorting, explicit project menus, metadata rename, recoverable Trash, restore and separately confirmed permanent deletion. English and Arabic copy; semantic text sizing and 44-point-or-larger controls; a consistent dark/mint provisional identity.
- Additive optional catalog fields `deletedAt` and `purgeStarted`. Missing fields in old catalogs preserve active/recoverable behavior. No C ABI, engine schema, project geometry or reconstructed revision changes.
- Permanent purge requires a library-only state with no open runtime or pending journal. Durable destructive intent is written **before** cleanup. Partial cleanup or a catalog-write failure stays visible/retryable and cannot be restored as an intact project. Large cleanup runs off the UI executor.
- Purge targets only a UUID-owned room directory and generated local archives whose filenames carry the same UUID, valid revision and export UUID. Validate all top-level targets before removal; reject project/archive directory symlinks. Nested symlinks are removed, not followed. Other rooms, separately captured objects, and user-shared copies are not removed. Trash has no hidden automatic expiry. Device backups/file providers retain their own policies.
- Corrected a real presentation dependency: captured 3D no longer requires successful semantic-plan decoding. Missing plan interpretation must not hide retained mesh evidence.
- Revision-bound native quality issues now reach live/review UI, with stable IDs and coordinate/critical problems first. Internal `mesh_source` lineage records are not presented as quality failures. Resolved/suppressed and stale-revision issues are not surfaced. Malformed/mismatched reports produce an explicit blocking assessment instead of an apparently clean scan.
- Scan insight separates retained mesh, floor-reference availability and section-boundary state. Closed sections remain candidates, not certified room envelopes. No fabricated confidence percentage, area, automatic repair or physical-accuracy claim. Native synthetic flags also suppress captured-room claims when a Swift marker is absent.
- Original opaque 1024×1024 app icon with a reproducible CoreGraphics generator. No third-party artwork or added runtime dependencies. This is provisional identity, not a final owner-approved brand.

## Verification actually executed

Xcode 27.0 / Swift 6.4 / iOS 27 SDK, minimum deployment iOS 17.

```sh
xcodebuild -project HSSApp/HybridSpatialScanner.xcodeproj -scheme HybridSpatialScanner -configuration Debug -destination 'platform=iOS Simulator,id=D80BA976-A08C-427F-846D-A35C380A0C3A' -derivedDataPath build-xcode-sim-test CODE_SIGNING_ALLOWED=NO test
xcodebuild -project HSSApp/HybridSpatialScanner.xcodeproj -scheme HybridSpatialScanner -configuration Release -destination 'generic/platform=iOS' -derivedDataPath build-xcode-release-device-audit CODE_SIGNING_ALLOWED=NO build
swift tools/generate_app_icon.swift HSSApp/Assets.xcassets/AppIcon.appiconset/AppIcon.png
sips -g pixelWidth -g pixelHeight -g hasAlpha HSSApp/Assets.xcassets/AppIcon.appiconset/AppIcon.png
plutil -lint HSSApp/HybridSpatialScanner.xcodeproj/project.pbxproj
plutil -lint HSSApp/ar.lproj/Localizable.strings
git diff --check
```

Final simulator suite: **81 tests, 0 failures** (`build-product-final-test.log` and its `.xcresult`). Includes 5 new library/purge regressions and 4 new assessment regressions. Final unsigned iPhoneOS Release build: **BUILD SUCCEEDED** (`build-product-release.log`). Icon: 1024×1024, `hasAlpha: no`; both plist checks and whitespace check passed.

Earlier 77-test library suite also passed. Initial assessment build failed on optional UInt32 versus imported Int constants; explicit UInt32 conversion/branch comparison repaired it. Failure log `build-insight-test.log` is preserved, subsequent assessment/final suites both passed 81 tests. Two normal build warnings remain: scheme manual build ordering and unused AppIntents extraction. No warning was suppressed to obtain a pass.

Native screenshots are actual simulator rendering, not sensor captures. Empty-library rendering was inspected in English and Arabic (RTL); Arabic title, controls and copy are visible without clipped text in the inspected portrait screenshot. Other sizes/orientations and full UI journeys are not claimed tested. Simulator initially was shut down; install/launch failed with CoreSimulator 405, then normal boot/install/launch succeeded. A later redundant boot reported the already-booted state; install/launch succeeded. This is simulator-only install evidence. No phone was connected or requested.

## Research/adoption decisions

Master Drive index was read through the owner's existing authenticated Drive connection; D14–D17 checked-in contracts were read. SwiftUI skill informed native controls, scalable text and accessible menus; no Figma file or design export was required. Google Drive skill grounded the exact source document and preserved sharing.

- [Open3D reconstruction system](https://www.open3d.org/docs/release/tutorial/reconstruction_system/index.html) and [Open3D paper](https://arxiv.org/abs/1801.09847): keep as an independent desktop reconstruction/research reference. Do not import a desktop RGB-D stack merely to polish an existing native application. No code or dependency copied; engine tolerances unchanged.
- [Apple privacy manifest guidance](https://developer.apple.com/documentation/technotes/tn3183-adding-required-reason-api-entries-to-your-privacy-manifest) and [required-reason API reference](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitype): disk-space and boot-time permissions include behavioral and off-device restrictions. Do not insert guessed reason codes and declare release compliance. Existing monotonic capture timestamps can enter user-exported archives; complete that data-flow audit before adopting the boot-time declaration. Lifecycle UI does not itself constitute an App Store privacy review.

## Explicit limits and finite next dependencies

This software slice is implemented/tested locally; publication evidence is a separate server checkpoint. It is not merged, signed, uploaded to App Store Connect, installed on an iPhone, physically measured, usability-accepted or superior to competitors.

Next software work is finite: (1) typed engine issue localization/action mapping and a calibrated explicit floor-reference interaction; (2) separate Object Capture session management; (3) privacy-manifest/data-flow and file-protection audit, including portable capture-time policy; (4) UI journey tests and portrait/landscape, Arabic RTL and accessibility inspection; (5) distribution signing/archive and store metadata once the owner chooses the correct Apple team/brand. Assist/Undo, generic feature-edit lineage and physical performance remain gated, not silently enabled.

The previous bounded seam study already reached a conservative software assessment. Do not restart completed smooth-perturbation fixes, widen tolerances or accumulate endless new synthetic fixtures. Hardware metrology/thermal/relocalization and competitor acceptance remain deferred until the owner requests that stage. Software-first progress does not prove them.

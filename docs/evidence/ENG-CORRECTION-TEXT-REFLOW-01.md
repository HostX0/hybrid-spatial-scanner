# ENG-20 — Correction text reflow and corrected review candidate

Private engineering evidence. Date: 2026-10-12. This is a bounded checkpoint,
not customer-facing copy or a declaration of complete accessibility/readiness.

## Ownership and source

The user explicitly requested a corrected binary replacing version 0.1/build 3
in App Review. That replacement has not occurred at this checkpoint.
The isolated source is `work/hss-native-release-20261012`, branch
`hss/native-correction-build5-20261012`, parent
`a6760881878509b771a0f87a5e5ed1017770287e`.
Root owns native execution, release/status updates and the original project
lock: inode `12106462`, worker `990103`, run
`native-correction-store-build5-20261012`. This document-authoring subtask did not acquire
a lock, build, drive UI, sign, upload or modify production/tests.

ENG-19 retains all earlier failed results, including the recurrence of Dynamic
Type reports after earlier warning absence. This new result does not rewrite
those checkpoints as successes or prove their exact font root cause.

## Demonstrated finding and minimal repair

One container experiment replaced the correction Form with scrollable cards.
Selected correction controls are eager; the existing bounded boundary list
(maximum 2,048 decoded edges) remains a `LazyVStack`. Semantic fonts, original
commands/values, modal focus, revision guards and all four strict audit types
were preserved. Container replacement alone did not pass the selected audits.

The retained red attachments recovered real associated targets rather than
guessing from nil audit elements: English Boundary 2–8 names/lengths were
SwiftUI StaticText; the Arabic Split action had a 22.33-point-high button frame;
the Arabic Boundary 1 contrast target was beneath the navigation bar.
Evidence is `work/native-build5-reflow-font-targets-20261012/manifest.json` and
its associated metadata/hierarchy files. These are rendered observations, not
instrumented font sizes or proof that every previous warning had one cause.

The scoped follow-on uses the existing semantic multiline `ScannerDynamicText`
UIKit label for visible boundary names/lengths and the visible Split label.
Boundary/action buttons have minimum 44-point target height, expanding with
their content. There is no font-size cap, hidden replacement text, audit-type
exclusion, expected failure or issue filtering. The original topology commands,
capture/engine schema, numerical tolerances and privacy/export policy are unchanged.

## Actual native evidence

Root used the owned simulator, the existing journey scheme and only the stated
method selections. Logs contain commands; result bundles retain outcomes.

| Evidence | Result | Qualification |
| --- | --- | --- |
| `work/native-build5-correction-reflow-20261012.log` and `.xcresult` | 2 methods, 0 passed, 2 failed; 23 failed assertions, 0 unexpected; 108.426s | Container-only EN/AR correction audits failed; real associated targets retained |
| `work/native-build5-correction-labels-20261012.log` and `.xcresult` | 2 passed, 0 failed, 0 skipped; 105.788s | Scoped visible-label repair; original contrast/hit-region/text-clipped/Dynamic-Type audits unfiltered in both languages |
| `work/native-build5-largest-input-20261012.log` and `.xcresult` | 2 passed, 0 failed, 0 skipped, 0 unexpected; 174.771s | Paired L/Accessibility XXXL name/length/Split bounds, full visibility and minimum tap bounds; largest-text numeric 2.0 entry, keyboard Done and editor dismissal in both languages |

The green EN/AR correction methods actually include boundary selection, modal
covered-workspace absence/restoration, native Done/reopen, and accepted
split/undo/redo acknowledgements. They are not audit-only stubs or engine
accuracy measurements. Method counts and failed-assertion counts are distinct.
Both affected pairs completed successfully. They are scoped evidence for this
candidate, and no whole-suite/iPad/engine rerun
is claimed or required by this document.

Actual rendered heights (points, normal L → Accessibility XXXL): English name
20.33 → 125.33, length 14.33 → 102.33, Split 20.33 → 125.33; Arabic name
22.33 → 69.00, length 16.33 → 58.00, Split 22.33 → 136.67. These are element
bounds, not exposed UIFont point sizes. The inspected normal English/largest
Arabic captures show readable wrapping rather than ellipsized labels. All four
original audit types remain active; no finding was filtered. This closes this
editor's demonstrated font/clip repair, not complete VoiceOver/physical acceptance.

The ordinary Review tap in the new probe uses XCTest's real scroll-to-visible
gesture, not an injected editor/scroll state. Outside the editor, the measured
reveal gesture now starts inside scroll content instead of empty trailing padding;
the whole-frame viewport assertion and finite budget remain unchanged.

Final store-source checks passed 10/10 in 0.058 seconds; whitespace/project plist
checks passed. Unchanged native engine/runtime unit evidence remains its earlier
scope, not a fabricated build-5 full rerun.

No private capture or screenshot was published. The UI reference project remains
explicitly synthetic; neither rendered text nor accepted history proves physical
sensor accuracy, thermal behavior or installation on a real iPhone.

## Delivery still to record

Build-5 source publication, completed Apple upload/processing/GUID, version
association and actual replacement/resubmission remain pending at this checkpoint.
The actual signed arm64 Release archive and local IPA export succeeded using the
existing THE THIRD ACT identity/profile. Read-only deep/strict signature, bundle,
0.1/build5, minimum OS17, device families1/2, protection, profile and privacy
checks passed. Release contains no DEBUG fixture/UUID test-namespace markers.
The owned simulator was shut down after the affected gates; no heavy native job
remains. Xcode's actual Upload was initiated only after that verification; a
starting-upload screen is not a completed receipt. Build-4 receipts are not build5.

| Signed local artifact | SHA-256 |
| --- | --- |
| Archive executable | `77b21f8f655dabd7ab7cff066b720688f871733e572a5403d8a0386384b8409e` |
| Independently exported local IPA | `6feff01ecb305f86448d2ce360749a182c2716701f2385810daf99752e1916c7` |
| Local IPA executable | `95bc00a19e22dad3bdcd9fce4c7f5ad9298ce72960271b07fc7f2b631e3c50de` |

Archive: `work/HybridSpatialScanner-build5-corrected-20261012.xcarchive`;
IPA: `work/HSS-build5-corrected-export-20261012/HybridSpatialScanner.ipa`;
receipt: `work/native-build5-corrected-signature-receipt-20261012.json`.
The local export can be re-signed/stripped independently from Xcode's upload;
the distribution transport IPA SHA is unknown and is not equated with these bytes.
The existing build-3 review request is
`806f5402-6ace-4df7-922f-e479cae79f28`; no change to it is asserted here.
Root will append subsequent Apple delivery receipts with actual
source/artifact identities. Review approval, public release and physical
installation remain separate acceptance states.

## Official replacement path and references

Apple allows removing a version in Waiting for Review, In Review or Pending
Developer Release. Version page: remove this version from review → Remove;
alternatively App Review → exact submission → Cancel Submission → Confirm.
Removal affects all items in that submission, changes the version to Developer
Rejected and restarts review on resubmission. Then remove the old Build
association, select the processed corrected build, Save, Add for Review and
finally Submit for Review. Adding for review alone is not submission. Preserve
manual release; do not release the old approved build to replace it.

- [Apple: Remove a submission from review](https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/remove-a-submission-from-review)
- [Apple: Choose a build to submit](https://developer.apple.com/help/app-store-connect/manage-builds/choose-a-build-to-submit)
- [Apple: Submit an app](https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/submit-an-app)
- [Apple: Select a release option](https://developer.apple.com/help/app-store-connect/manage-your-apps-availability/select-an-app-store-version-release-option)
- [Apple: Semantic preferred fonts](https://developer.apple.com/documentation/uikit/uifont/preferredfont%28fortextstyle%3Acompatiblewith%3A%29)

# ENG-19 — Native Dynamic Type, correction feedback and usable offline export

Engineering evidence, not customer-facing product copy. Date: 2026-10-12.
This records the increment's pre-publication checkpoint with completed native unit/UI results, a verified
signed local archive/IPA and observed Apple build-4 upload. It preserves failed
results; it does not claim final UI acceptance, source publication/merge, Apple
version/review association or public release. Apple processing is verified below.

## Source and delivery boundaries

The isolated working source is `work/hss-native-release-20261012`, branch
`hss/native-dynamic-type-20261012`, starting at documentation/integration head
`048292a1e2f055e4e289c4b496977305cddbe8a4`. The independent baseline is
`work/hss-dynamic-baseline-20261012`. Original dirty checkouts and ZERO X were
preserved. Native execution used the owned simulator destination
`2F6A107C-24BF-43F4-B091-6DE5271B48EE`, not a connected physical phone.

Build counter 4 now has an actual signed Release archive and exported IPA, with
the artifact identities below. Its source changes remain uncommitted,
unpublished and unmerged at this checkpoint. Root initiated an Organizer upload
at 15:54 UTC and observed completion, independently read back as **Uploaded to
Apple** for 0.1/build 4 at 15:55 UTC (minute precision). At 16:02 UTC, App Store
Connect metadata showed binary **Validated**, TestFlight **Ready to Submit** and
build GUID `d3895ec4-5ea9-433c-9883-0a4805ae7d97`. Version association, build-4
review submission and public release are not established.
Root's authenticated Apple readback at 2026-10-12 15:46–15:47 UTC observed the
existing version 0.1/build 3 request still **Waiting for Review**, with
the exact prior submission `806f5402-6ace-4df7-922f-e479cae79f28` preserved. Its
shipping application source is `1e7508fe899f6d4c49fd9cddb08e9840158f8beb`; ENG-18
records its actual signed/uploaded/submitted evidence. The older retained
`work/HSS-apple-review-final-receipt-20261012.json` records the 07:30 UTC checkpoint,
not that later direct UI readback. Neither observation relabels build 3 as
containing these repairs, or makes build 4 Apple-accepted/live.

No portable engine, numerical tolerance, project schema, C ABI, capture policy
or external dependency is changed. No private capture is uploaded or used as a
public fixture. Physical sensor accuracy and installed-device acceptance remain
separate from native bridge, packaging and rendered-UI evidence.

## Finite closeout, isolated fixtures and owner-authorized cleanup

The completed targeted evidence read found all four current Arabic font reports
have audit type 65536 and `issue.element == nil`. Their screenshots are identical
full-screen selected-refinement views, without highlights. They do not identify
a UILabel, Menu or button as the culprit. Exported metadata/observations are
in `work/dynamic-type-recurrence-review-20261012`; the canonical completed result
bundle is retained. No speculative production patch or repeated audit is made.

The prior English largest-text failure had no realized target and showed older
readiness-test drafts. Source inspection confirms that reference seeding refuses
a nonempty library. Each explicit DEBUG simulator UI launch now supplies a fresh
canonical UUID namespace under `Documents/HSSUITestProjects`, isolated from the
existing `HSSProjects` library. Malformed/path identifiers are rejected; explicit
trusted roots take precedence. The namespace override/helper and launch opt-in
are excluded from Release and physical-device Debug. No installed library is
cleared to reset a test. The shipping artifact above predates this test-only
isolation change and is not relabelled as a newly signed binary.

Two new native namespace unit tests passed, zero failures, in 0.004 seconds
(`work/native-ui-namespace-unit-20261012.xcresult`). A single fresh-root English
largest-text regression then found and opened the actual reference project at
both text categories, but failed later on `boundary-review.open` after 14
gestures. The exact final frame is `(20.17, 918.93, 362.33, 187.40)`, below the
`(0, 64, 402, 810)` viewport; it exists but is not hittable. This is not an
oversized card or missing seeded project, and does not prove a font root cause.
The one-method result remains **failed**, 80.890 seconds, one assertion, zero
unexpected failures (`work/native-ui-isolated-largest-20261012.xcresult`). Its
exact text attachment is retained in `work/native-ui-isolated-largest-evidence-20261012`.
No further native loop was commissioned. Final source/store checks passed
10/10 in 0.046 seconds; whitespace and localized strings/project plist checks pass.

On the owner's explicit cleanup request, both owned HSS simulator devices above
and `314885E1-7513-4020-8FBF-A58CB75F9028` were shut down/deleted. Their generated
test data is disposable, not physical capture evidence. An explicit ten-build-root
whitelist removed 2,071,807,569 logical bytes of reproducible Build/module/SDK
caches, while preserving all source/Git checkouts, Logs, TestResults, canonical
xcresults, signed archives/final IPAs, public screenshots and private captures.
No global Xcode/runtime/home cache or unrelated project was cleaned. Local
receipts are `work/native-cache-cleanup-receipt-20261012.json` and
`work/native-owned-simulator-cleanup-receipt-20261012.json`. Available Mac space
read back as 74 GiB, and no simulator is booted. Deleted generated caches/test
devices cannot be undeleted through this cleanup but can be regenerated.
Future native verification must create a new explicitly owned device; these
deleted destination IDs must not be reused blindly. No heavy native job remains.

The repository's agent contract now requires a named defect/repair/regression
cycle, one affected Mac job with one compiler job by default, fresh fixture
identity, a stop rule for unidentifiable repeated audit findings, exact delivery
state distinctions and narrow owner-authorized cleanup. Source publication and
its exact immutable identity are recorded separately in the paired server/PR
receipt. This is not full accessibility acceptance, review approval or launch.

## Demonstrated problems and scoped implementation

### Dynamic Type and correction feedback

Fresh strict English baseline audits reproduced the existing Dynamic Type
reports on the captured-boundaries header and saved-date/project-identity area.
The old normal-height threshold alone was insufficient: a multiline label could
exceed it without proving font growth. Earlier EN/AR audit failures remain in
ENG-15/ENG-16 and are not retrospectively converted to successes.

`ScannerDynamicText` uses a multiline UIKit label with semantic preferred fonts,
automatic content-size adjustment, natural text alignment, static-text/header
traits and explicit content-size-category mapping. SwiftUI owns available width
and vertical expansion. It is retained for affected project identity, boundary
section/header and explanatory labels. Current action/Menu labels use semantic
SwiftUI body text with unconstrained vertical expansion. There is no font-size cap, hidden
replacement label, expected failure or ignored accessibility issue.

At accessibility sizes, readiness scrolls so Start and Open projects remain
reachable. The readiness journey creates and retains a real local empty draft,
then returns to the library without tapping Start or requesting camera access.
It does not assert unsupported-sensor timing or accuracy.

Incomplete correction actions now explain the missing input in a native alert
before invoking the engine: positive measured length, valid optional uncertainty,
point-position confirmation and exact-model confirmation remain required.
Revision/selection guards and the native finite/range/endpoint validation are
retained. This does not make an incomplete action a valid geometry command.

The first build-4 whole UI run still found selected-control contrast issues and
Arabic clipping. Current numeric input uses a UIKit field with a refreshed binding,
main-actor Coordinator, semantic preferred font and the same explicit Dynamic Type
environment/category mapping as its visible label. Its height tracks that font;
the full visible/accessibility labels retain metre units without a placeholder.
The decimal keyboard has a field-specific Done action. Source/mode choices retain
the same enums through Menu controls with separate multiline headings, values
and semantic accessibility values. The final point-confirmation toggle uses
semantic SwiftUI body text with vertical fixed-size expansion and an explicit
accessibility label; there is no font-size cap.

The custom solid-header/separate-Form-viewport experiment failed both strict
EN/AR methods, with eight recorded contrast/clipping failures, and was reverted.
It is not the current product design or a successful repair. The bad under-bar
rendering remains in
`work/native-build4-boundary-final-attachments-20261012/`, and the experiment's
failure remains in `work/native-build4-boundary-viewport-20261012.xcresult`.

Current source restores the original NavigationStack/Form and native toolbar,
with an explicitly visible opaque system-grouped toolbar background. The UIKit
numeric fields, main-actor Coordinator, shared font/category mapping and nil
placeholder remain. The covered underlying workspace is excluded from
accessibility only while the correction editor is presented; the editor's own
header, edges and Done control remain available. Native tests assert the known
covered Review action is absent, then present and hittable after dismissal and
again after audit-driven reopen. This is modal focus ownership, not hiding
reported controls or changing the audit types.

On iOS 26 and later, `ScannerBoundaryScrollContrast` removes only the Form's
visual scroll-edge effect using `scrollEdgeEffectHidden(true, for: .all)`. Earlier
OS versions keep their existing Form behavior. It does not hide accessibility
elements; the opaque native toolbar and all four original strict audit types
remain. The modal-focus and scroll-contrast checkpoints each passed Arabic,
while English retained three and then one recorded findings respectively.

The later action-layout checkpoint uses semantic SwiftUI `.body` text with
`.fixedSize(horizontal: false, vertical: true)` only for the two Lock/Apply
actions and Menu values, retaining the native project/header labels. English
passed; Arabic retained one text-clipping finding, with no contrast, Dynamic Type
or hit-region finding reported. A bounded follow-on added plain style and an
unconstrained-width, minimum-44-point multiline frame to only Lock/Apply actions.
The completed action-wrap run passed Arabic, while English retained two clipping
findings. These are checkpoints, not full accessibility clearance or proof of
the exact earlier culprit.

Both later whole-19 runs completed and failed overall. With the old reveal helper,
Arabic strict correction passed, English retained three clipping findings, and
both functional journeys failed waiting for the unconfirmed-position alert after
a Lock tap. With bounded unobscured-viewport geometry, both functional journeys
passed actual native validation alerts, numeric/source editing and split/undo/redo,
but both strict selected-refinement audits still reported unfiltered contrast/
clipping. English largest-text navigation also failed to fully reveal its first
project target after 14 gestures; Arabic largest-text navigation passed. Those
remaining observations are preserved, not assigned a speculative font or engine
root cause. Those earlier runs did not repeat the original Dynamic Type reports,
but the subsequent focused adaptive run did: Arabic selected refinement reported
four unsupported-Dynamic-Type findings and English two, alongside contrast/
clipping. The earlier disappearance is **not stable font closure**. Both strict
library audits passed in that five-method selection; English largest-text reveal
again exhausted its 14-gesture budget. The frozen source is not accessibility-
accepted, and no font root cause is inferred from method counts alone.

### Asynchronous export ownership

The old model guarded export success with project/revision only and published
late errors without ownership checks. Controlled native regressions demonstrated
late failure/cancellation contaminating the returned library and old success
overwriting a newer request after same-project/same-revision reopen (ABA).

Each offline/HSSCAN operation now has an ephemeral unique request identity,
project and revision. Both success and failure require ownership of the current
exporting request and frozen project context. Navigation, snapshot/context reset
and a newer request invalidate obsolete publication. Duplicate invocation does
not replace active progress with an ineligibility error. Current-request failure
remains visible and retryable; completed immutable outputs are not deleted by
the model's ownership invalidation.

The internal defaulted operation seam holds deterministic completion in native
tests without disk-size/timing guesses. Production defaults remain the existing
off-main local exporter/archive implementation. Two test fixture helpers inherit
their caller's actor isolation using `isolated (any Actor)? = #isolation`; no
whole-test-class isolation or unchecked-Sendable XCTest workaround was added.

### Usable derivative export and privacy boundary

Previously PDF/SVG/DXF/OBJ/MTL/provenance output was saved under the app-private
project tree but did not reach a system sharing/Files action. The existing
HSSCAN archive contains retained project source, not those ordinary derivative
files; it is not a substitute for this product capability.

After explicit Export, the host prepares a local ZIP off the UI actor and presents
the native activity sheet. Cancelled/superseded completion checks state, project
and revision before presentation; obsolete returned ZIPs are discarded. Sheet
dismissal removes only the exact transient ZIP returned by this packager, never
the original derivative directory, private source tree or a user/provider URL.
No destination, recipient or outbound activity is selected automatically.

The packager validates the exact project/revision transaction directory and
manifest, then copies only the fixed required derivative names plus explicitly
included bounded texture companions. It rejects extra raw files, traversal,
duplicate entries, wrong project/revision/location, symbolic/hard-link aliases
and missing MTL texture companions. Per-file, file-count and aggregate limits
are enforced. Staging and the final ZIP remain under `<project>/exports/shares`,
inside the existing project purge tree. Originals remain unchanged. Optional
source-color inclusion retains the owner's existing explicit opt-in policy.

Foundation's directory snapshot API produces the ZIP locally; the implementation
copies its result inside the accessor before the coordinator removes its temporary
file. The API name `forUploading` is not evidence of a network upload. This code
contains no transmission path; any later sharing is the owner's system choice.

### Signed candidate and Apple upload, not review/public delivery

`work/native-build4-final-archive-20261012.log` records `ARCHIVE SUCCEEDED` for
`work/HybridSpatialScanner-build4-final-20261012.xcarchive`.
`work/native-build4-final-export-20261012.log` records `EXPORT SUCCEEDED` for
`work/HSS-build4-final-export-20261012/HybridSpatialScanner.ipa`. The receipt is
`work/native-build4-final-signature-receipt-20261012.json`.

| Artifact identity | Verified SHA-256 |
| --- | --- |
| Signed archive executable | `7171c33338dd5fe2938308e0dc9c8b31f521da658c4753f0ecd1bfe0bf8bf180` |
| Exported IPA executable | `60b8261af275bd1812fd97d912a60dbff8a475d041424292abd743de65e74c09` |
| Complete IPA file | `04d8cf842640924ef0533f37e18c0f20bf388e55a15095e6b41091fde20a1072` |

The archive and IPA receipt verifies bundle `org.hybridspatialscanner.app`, version
0.1/build 4, minimum OS 17.0, iPhone/iPad families, a distribution profile and
strict signatures. It records no debug fixture markers, file protection until
first user authentication, no privacy tracking and no collected-data types.
The final Toggle label change is included in this production candidate; the
later reveal-helper changes are test-only and do not change its app binary.
The two executable hashes are recorded separately, not conflated. The signature
receipt records uploaded/merged/public-live/physical-verified as false at its
earlier checkpoint; the later independent Apple upload receipt records actual
upload completion. Successful signing/export/upload does not clear the failed
strict UI gates below.

`work/native-build4-apple-upload-receipt-20261012.json` retains authenticated
distribution completion, “HybridSpatialScanner 0.1 (4) uploaded”, and independent
Organizer “Uploaded to Apple” readback at 15:55 UTC / 18:55 Baghdad. It identifies
the final 18:29 Baghdad archive, THE THIRD ACT LLC, the app bundle, build 4 and
arm64; version/build management was unchecked and symbols included. Its
processing/GUID/association/build-4-review/public-live/physical verification are
not confirmed, and the exact prior build-3 review request was not changed.
This is an archive upload receipt, not evidence that the independently exported
local IPA's bytes equal Xcode's transport package: the transport SHA-256 is
unknown and Xcode may re-sign/repackage during distribution.

`work/native-build4-apple-processing-receipt-20261012.json` records the independent
16:02 UTC App Store Connect metadata readback: build 4 binary **Validated**,
TestFlight **Ready to Submit**, GUID `d3895ec4-5ea9-433c-9883-0a4805ae7d97`, Arabic/
English languages, iPhone/iPad families, minimum OS 17.0, arm64 and symbols present.
It confirms processing, not association/submission/approval/release: there are
zero testers/groups, build-4 version association and review submission are false,
and the prior build-3 request is unchanged. Source publication, installation,
physical verification and transport-IPA byte equality are not claimed.

## Executed evidence, including failed runs

Private retained paths below are relative to the delivery workspace, outside the
repository. Native simulator-test logs record the actual `xcodebuild` invocation;
their matching `.xcresult` retains test outcomes and attachments. Simulator commands
used the existing schemes, owned simulator, `-jobs 2`, nonparallel testing,
`-collect-test-diagnostics never`, `CODE_SIGN_IDENTITY=-` and
`ONLY_ACTIVE_ARCH=YES`. Simulator signing is not distribution signing.

| Evidence | Observed result | Exact scope/qualification |
| --- | --- | --- |
| `work/dynamic-type-baseline-20261012.log` and `.xcresult` | 2 methods executed, 0 passed, 2 failed | Fresh baseline strict English library and boundary audits; both Dynamic Type reports reproduced |
| `work/export-request-ownership-red-20261012.log` and `.xcresult` | Compilation stopped; no regression execution | Swift 6 rejected sending XCTest `self` from the fixture helper; retained separately |
| `work/export-request-ownership-red-v2-20261012.log` and `.xcresult` | 8 methods executed: 2 passed, 6 failed; 8 failed assertions | Corrected test isolation, old production ownership guards; deterministic late failure/cancellation and ABA reproduction |
| `work/export-request-ownership-green-20261012.log` and `.xcresult` | 8 passed, 0 failed | Same regression selection with current request-ownership guards; current failure/retry preserved |
| `work/native-dynamic-export-unit-20261012.log` and `.xcresult` | 139 passed, 0 failed | Native bridge/runtime/appearance/guidance/unit suite, including 10 packager and 8 ownership cases; precedes latest final UI/source changes |
| `work/derived-export-zip-consumer-20261012/verdict.json` | 2 archives passed, 0 failed | Independent system unzip and Python standard-library readers; synthetic colorless/textured packaging fixtures only |
| `work/dynamic-type-repair-first-20261012.log` and `.xcresult` | 17 methods: 12 passed, 5 failed; 16 recorded failures, 2 unexpected | First expanded UI run; selected-control findings, camera-lifecycle test assumption and audit infrastructure failures retained |
| `work/native-build4-ui-20261012.log` and `.xcresult` | 19 methods: 14 passed, 5 failed, 0 skipped; 10 recorded failures, 0 unexpected | First whole build-4 UI run; strict EN/AR library audits and paired largest-text tests passed, selected residual issues/test locators still failed |
| `work/native-build4-ui-corrected-20261012.log` and `.xcresult` | 6 methods: 4 passed, 2 failed; 9 recorded failures, 0 unexpected | EN/AR functional invalid-input/split/undo/redo and native ZIP caption/Close/retry journeys passed; both strict selected-boundary audits failed, with four contrast findings each plus Arabic clipping; no Dynamic Type diagnostic |
| `work/native-build4-input-menu-20261012.log` and `.xcresult` | 4 methods: 1 passed, 3 failed; 10 recorded failures, 0 unexpected | English functional journey passed; both strict audits failed. Arabic input/source/Done observations passed before a test-only deletion loop assumed the caret was at the end; that later assertion failed |
| `work/native-build4-boundary-final-20261012.log` and `.xcresult` | 2 methods: 0 passed, 2 failed; 5 recorded failures, 0 unexpected | Arabic clipping plus lazy undo/snapshot test assertions and one English contrast finding; no Dynamic Type diagnostic |
| `work/native-build4-boundary-viewport-20261012.log` and `.xcresult` | 2 methods: 0 passed, 2 failed; 8 recorded failures, 0 unexpected | Custom-header/Form-viewport experiment: two Arabic contrast/three clipping reports and three English clipping reports; no Dynamic Type diagnostic. Experiment reverted |
| `work/native-build4-modal-focus-20261012.log` and `.xcresult` | 2 methods: 1 passed, 1 failed; 3 recorded failures, 0 unexpected | Arabic strict correction journey passed; English retained one contrast and two text-clipping findings. Original four audit types remain unfiltered |
| `work/native-build4-scroll-contrast-20261012.log` and `.xcresult` | 2 methods: 1 passed, 1 failed; 1 recorded failure, 0 unexpected | Arabic passed; English retained one text-clipping finding. Original Form/opaque toolbar, visual-only iOS-26 fade removal and real modal absent/restored assertions; all four audit types unfiltered |
| `work/native-build4-action-layout-20261012.log` and `.xcresult` | 2 methods: 1 passed, 1 failed; 1 recorded failure, 0 unexpected | English passed; Arabic retained one text-clipping finding after action/Menu labels returned to semantic multiline SwiftUI body text. No contrast, Dynamic Type or hit-region finding reported |
| `work/native-build4-action-wrap-20261012.log` and `.xcresult` | 2 methods: 1 passed, 1 failed; 2 recorded failures, 0 unexpected; 88.253s | Arabic strict correction passed; English retained two text-clipping findings. Plain multiline Lock/Apply action frames; no audit filtering |
| `work/native-build4-final-ui-20261012.log` and `.xcresult` | 19 methods: 16 passed, 3 failed; 5 recorded failures, 0 unexpected; 535.209s | Final Toggle production source with old reveal helper: Arabic strict correction passed; English strict correction had three clipping findings; EN/AR functional journeys failed waiting for the Lock validation alert |
| `work/native-build4-viewport-verified-ui-20261012.log` and `.xcresult` | 19 methods: 16 passed, 3 failed; 7 recorded failures, 0 unexpected; 736.084s | Same frozen production with geometry-aware helper: EN/AR functional alerts/numeric/source/history and ZIP Close/retry passed; Arabic strict refinement retained two contrast/two clipping findings, English one contrast/one clipping; English largest-text first-project reveal exhausted 14 gestures |
| `work/native-build4-adaptive-viewport-20261012.log` and `.xcresult` | 5 methods: 2 passed, 3 failed; 14 recorded failures, 0 unexpected; 306.056s | Focused adaptive helper selection: both library strict audits passed; Arabic selected refinement had one contrast/four Dynamic Type/three clipping findings; English two contrast/two Dynamic Type/one clipping; English largest-text reveal still exhausted 14 bounded gestures. All four audit types remain unfiltered |
| `work/native-build4-final-unit-current-20261012.log` and `.xcresult` | 139 passed, 0 failed, 0 skipped; 1.644s test time / 1.681s suite wall time | Current final Toggle production native unit/bridge/runtime/appearance/guidance suite, including packager and ownership regressions; completed successfully at 15:53 UTC. Does not clear strict UI failures |
| `work/native-derived-export-consumer-20261012/verdict.json` | Actual native-derived ZIP consumer passed | Frozen synthetic reference-room export, nine derivative files plus publisher manifest / 18,822 total uncompressed bytes, format/structure/provenance checks; not a physical scan |
| `work/native-store-build4-verified-20261012.log` | 10 discovered, 1 error | System Python lacked PIL; not a store consistency pass |
| `work/native-store-build4-runtime-verified-20261012.log` | 10 passed, 0 failed | Existing trusted bundled Python runtime, store/native-source checks; not native compilation or Apple persistence |

Method counts and recorded failure counts are intentionally distinguished: one
failed audit method may contain several failed assertions. The earlier 139-test
pass remains a pre-final-source checkpoint; the later current-production 139-test
pass is actual final-source native unit evidence, not accessibility/physical
acceptance or a passing whole UI suite.
The viewport experiment's failures remain preserved despite its reversion.
Subsequent scoped EN/AR improvements do not relabel that run or the earlier whole
UI failures. The action-wrap and both whole-19 runs are completed failures, not
running work. Final source unit tests and local distribution signature checks
passed separately; the later five-method adaptive selection also completed with
failures and disproved stable absence of Dynamic Type warnings. Strict selected-
refinement/English largest-text acceptance, iPad final acceptance and Apple
build-4 association/review remain outstanding. Completed upload and processing
are independently recorded above.

An initial store-check discovery pattern selected zero tests. That invocation is
retained in execution history as an invalid verification selection, not counted
as a pass. The subsequent system-Python PIL error and trusted-runtime ten-test
recovery have the separate log paths above; no package was installed to conceal
the missing dependency.

The eight ownership selectors are the offline/archive versions of:

- `test*ExportLateFailureCannotReplaceLibraryState`
- `test*ExportLateCancellationCannotReplaceLibraryState`
- `test*ExportOldSuccessCannotReplaceNewSameRevisionRequest`
- `test*ExportCurrentFailureRemainsVisibleAndRetryable`

The exact selector lists and baseline/fixed workspaces are retained in the RED-v2
and GREEN logs. The pre-fix seam/regression patch is
`work/hss-export-ownership-prefixed-regression-20261012.patch`; its historical
filename is retained without implying a fixed production baseline.

## Rendered growth and new test-oracle repairs

The first build-4 UI run contains no Dynamic Type diagnostic. Both library audits
passed and both paired largest-text journeys passed. This is evidence of a scoped
improvement, not a blanket VoiceOver or full accessibility clearance: selected
boundary audits still reported contrast and Arabic clipping.

The final-production old-helper whole-19 run also passed both largest-text
library/boundary journeys. In the later geometry-aware-helper whole-19 run,
Arabic passed while English stopped before first-project navigation when the
14-gesture reveal budget was exhausted. Its earlier rendered metrics are not
silently promoted into a passing latest English journey. In the subsequent
focused adaptive run, both library audits passed but EN/AR selected refinement
again reported unsupported Dynamic Type, contrast and clipping. Earlier warning
absence and growing element bounds do not establish stable font acceptance.

Actual XCUI bounds, normal category L versus Accessibility XXXL, for the same
persisted reference project `Example room`:

| Language | Project-name height | Saved-date height | Captured-boundaries heading height |
| --- | --- | --- | --- |
| English | 20.33 → 125.33 pt | 30.33 → 102.33 pt | 20.33 → 125.33 pt |
| Arabic | 22.33 → 136.67 pt | 34.33 → 115.33 pt | 22.33 → 136.67 pt |

These are rendered element bounds, not instrumented font point sizes. Screenshots,
hierarchy and metrics are retained in
`work/native-build4-ui-all-attachments-20261012/`, including metric files
`9C3ADB23-B040-492F-AB28-985E32F0CEA8.txt` (English) and
`D02C8734-3E3C-48AD-A963-7ABB693345A1.txt` (Arabic). The tests also scroll actual
controls into view. They do not create measured source or validate phone capture.

Failures were investigated without converting them into passes:

- The first readiness test tapped Start then required readiness to stay present
  and disabled. Production changes to scanning while permission resolves and may
  choose reduced ARKit fallback; missing LiDAR does not prove that test oracle.
  Current navigation acceptance never starts capture and verifies the retained
  draft after returning to the library.
- First-run library audits reported accessibility-service `Invalid target app`
  and timeout errors. Audit callbacks now retain issues without querying live
  hierarchy inside the callback; post-audit evidence remains unsuppressed.
- First whole build-4 EN/AR sharing tests timed out on an identifier attached to
  the controller's wrapper. Retained real native hierarchy shows
  `ActivityListView`, `UIActivityContentView`, caption elements and native close
  control. The revised EN/AR tests passed in the corrected six-method run:
  actual ZIP caption/identity, native Close and enabled retry after cancellation.
  They dismiss only; no file provider, recipient or outbound service is selected.
- An English edge query ran before its lazy row was realized. The current oracle
  reveals/waits for the selectable edge before asserting its presence. This is
  not a change to accepted topology or a removed correction assertion.
- The new Arabic typing journey successfully entered measured length and optional
  uncertainty, changed/read back the source and used keyboard Done. A subsequent
  delete-count loop assumed end-of-string caret placement after refocusing; its
  assertion failed. The bad oracle is retained in the input-menu run, not presented
  as failed binding/decimal parsing or a successful whole journey.
- After strict audits changed the viewport, a later Arabic undo query ran while
  its lazy row was absent. Current tests preserve every audit finding, then use
  real Done/reopen/reselect of a current edge before the full split/undo/redo
  sequence. History controls are revealed/waited for before presence assertions;
  no audit failure is filtered to let that functional sequence appear successful.
- The scroll-contrast and action-layout journeys also assert real modal focus:
  the covered workspace Review action is absent while the editor's header/edges/
  Done remain accessible, then returns hittably after dismissal. Those assertions
  passed within the journeys; outstanding audit failures remain failures.
- The final old-helper whole run failed EN/AR Lock-alert expectations after the
  empty-dimension alert had been dismissed. The new geometry-aware helper requires
  a target to be fully inside the viewport below the native navigation bar,
  rather than trusting `isHittable` for a partially covered row. EN/AR native
  invalid-input alerts, numeric/source editing and split/undo/redo passed with
  that helper in the completed viewport-verified run; the old failures remain.
- The same geometry-aware run exhausted 14 fixed gestures while revealing the
  English largest-text first-project button. Follow-on target-frame analysis
  identified a 306-point gesture overshooting a 414-point target within an
  810-point viewport by 39.67 points. An adaptive test-helper correction is not
  a successful follow-on result: the completed adaptive selection also exhausted
  the 14-gesture budget. It does not establish a demonstrated font or engine
  root cause, and neither bounded diagnosis erases the completed failures.

Native share observations remain in
`work/native-build4-share-evidence-20261012/english-share-failure-ui-hierarchy`
and the associated snapshot/manifest. Original UI failures remain in
`work/dynamic-type-repair-first-20261012.xcresult`, its attachment directory and
`work/native-build4-ui-20261012.xcresult`. No audit type is excluded and the issue
handler does not return a filtering success.

## Independent packaging and actual derivative-consumer evidence

The two retained native-generated ZIPs were read independently with system
`unzip -t`, `unzip -l` and Python's standard-library `zipfile`. The verdict checks
exact relative paths and bytes against retained expected-file maps, CRC/readability,
OBJ/MTL/texture relative companions and absence of private-source markers,
duplicates, links, encryption and traversal. The colorless fixture contains nine
derivative files plus publisher manifest (ten files); the explicit textured
fixture contains twelve files, under `HSS-export-r7/`.

The optional JPEG is an opaque four-byte packaging fixture, not a valid-image or
appearance-quality test. PDF/SVG/DXF fixture payloads likewise do not establish
CAD/PDF application interoperability. Existing real RGB-D/appearance evidence is
separate and is not silently attributed to these ZIP checks. Native hostile-input
and source-preservation tests are distinct from this external output consumer.

A separate later check consumed a frozen ZIP produced by the actual native
exporter and packager for the owned synthetic reference-room fixture, not the
opaque packaging-only payloads above. Evidence is
`work/native-derived-export-consumer-20261012/verdict.json` and its retained
ZIP/consumer output. The exact ZIP SHA-256 is
`b87892d197ffa8ad63e0874366d073543d178d3afbf2d82480a1f02267f861ac`; compressed
size is 7,101 bytes, with nine derivative files plus publisher manifest (ten
files total) / 18,822 uncompressed bytes at revision 3.

- `qpdf --check` found no PDF syntax/stream-encoding error; configured `pdfinfo`
  read one unencrypted A4-landscape page, no JavaScript or metadata stream.
  qpdf's own limitation that it cannot detect every PDF error remains applicable.
- SVG XML parsed with metre units, one Y inversion and eight paths/provenance
  fragments at the retained scene revision.
- DXF retained eight LWPOLYLINE entities, metre INSUNITS and exact checked
  provenance coordinates/section structure.
- OBJ retained eight vertices/eight triangular faces with valid indices and
  provenance face count; MTL companion/material definitions resolved. This is a
  texture-free bundle, not new registered-color evidence.

The consumer verified CRC, exact allowlist/manifest lengths, path/link/encryption
and private-extra rejection and preserved the frozen inspected ZIP. It does not
claim byte equality with a live application container that could change during
concurrent UI launches. The publisher manifest records identity/revision/paths/
lengths, not original-source SHA-256 integrity. Provenance still says synthetic,
plan quality remains draft and semantic area remains unknown (`null`); successful
format checks do not promote those warnings into measured physical readiness.
Commercial CAD/printing interoperability, full PDF semantic correctness, physical
accuracy and successful system file-provider transfer remain separate gates.

## Finite remaining acceptance at this checkpoint

1. Resolve or explicitly retain the latest unfiltered EN/AR selected-refinement
   Dynamic Type/contrast/clipping findings and English largest-text reveal
   failure. Earlier font-warning absence is not stable closure. The exact
   font-evidence diagnosis completed with nil targets; do not repeat it. Shipping
   production remains frozen; no further whole-19, iPad-14 or engine rerun is commissioned at this
   checkpoint. Do not infer a font/engine culprit, reuse an earlier pass as latest
   acceptance or promote the reverted viewport experiment into a fix. EN/AR
   functional alerts/numeric/source/history and share Close/retry passed in the
   latest whole run; its three failed methods remain failed. Current-production
   native units passed 139/139; no unbounded repeat of those gates is required.
2. Retain the deferred iPad final acceptance gap and Apple build-4 version/review
   association gap. Signed local archive/IPA, strict signatures, privacy-package
   checks, Apple upload and Validated/Ready-to-Submit processing are confirmed,
   but publication/
   merge, review association/public delivery and a passing strict whole UI gate
   are not established.
3. Preserve build 3's existing Apple request. Approval, public availability,
   physical installation, actual Files transfer, sensor accuracy, thermal/resource
   behavior and minimum-supported-device/OS acceptance are not established here.

No repeated engine rewrite, tolerance widening or unbounded synthetic experiment
is required by these presentation/export repairs. Final gate outcomes and any
later gate/upload receipts must be appended with their actual source/artifact
identity before changing this checkpoint's readiness verdict.

## Primary implementation references

Apple documents semantic fonts chosen for text style and trait collection, and
automatic category adjustment for preferred-font/scaled-font labels. Those APIs
inform the native multiline labels; actual rendering/audit results above remain
the application-specific evidence. See
[UIFont preferredFont(forTextStyle:compatibleWith:)](https://developer.apple.com/documentation/uikit/uifont/preferredfont%28fortextstyle%3Acompatiblewith%3A%29) and
[adjustsFontForContentSizeCategory](https://developer.apple.com/documentation/uikit/uicontentsizecategoryadjusting/adjustsfontforcontentsizecategory).

Apple documents that a coordinated directory read with `forUploading` yields a
temporary zipped snapshot, whose URL becomes unavailable after the accessor
returns unless preserved there. This supports copying the locally generated ZIP
inside the accessor; it does not itself authorize or perform application sharing.
See [NSFileCoordinator.ReadingOptions.forUploading](https://developer.apple.com/documentation/foundation/nsfilecoordinator/readingoptions/foruploading).

Apple describes scroll-edge styles as visual blur/fade effects and documents
`scrollEdgeEffectHidden` for hiding that effect. This supports the
availability-guarded visual-only Form modifier, not suppressing accessibility
findings. See
[ScrollEdgeEffectStyle](https://developer.apple.com/documentation/swiftui/scrolledgeeffectstyle) and
[scrollEdgeEffectHidden(_:for:)](https://developer.apple.com/documentation/swiftui/view/scrolledgeeffecthidden%28_%3Afor%3A%29).

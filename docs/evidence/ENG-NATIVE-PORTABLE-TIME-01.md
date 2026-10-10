# Native portable event-time repair and finite privacy readiness

Base: `849323a0aaff453e6884629f2e9cd4672e06a180`.
No source-store ABI/schema/engine/tolerance change. No private capture uploaded.

## Executed diagnosis

Two targeted native tests failed before repair:

- A fresh project's first source time of 1,000,000 was stored as 1,000,000
  instead of zero; later-session source time of 2,000,000 leaked the new device
  boot origin rather than continuing the project's relative event intervals.
- A real native retained-source fixture reopened through the production model
  acquired a review-note cursor of 32009.652221458335 instead of preserving its
  accepted cursor of 3.0. The note's source sequence increased correctly.

`build-portable-clock-reproduction.log` preserves both failed tests and six
assertion failures. Xcode's post-failure simctl diagnostic collector remained
running after the tests finished. Only the exact owned reproduction xcodebuild
and its diagnostic child were interrupted, producing exit 75; the failure is not
claimed as a normally completed test command. No server worker was interrupted.

## Repair and compatibility

ProjectCaptureClock anchors its first source event to the existing durable
cursor. Only source-event intervals within that session advance the log.
Delivery inversions retain the existing bounded-skew rejection and nondecreasing
clamp. Source frame identity/age checks still use the original in-memory ARKit
clock; calibration, event sequence, accepted epochs and mesh/depth bytes are not
rewritten. A review-only missing-evidence control uses the existing durable time
with a new event sequence, not system boot time or invented physical duration.

New projects therefore start their durable event-time axis at zero. Reopened
relative-time projects preserve that axis across reboot or device changes.
Legacy projects retain their accepted cursor and source bytes; existing captures
are not silently migrated, rehashed or scrubbed. A legacy boot-origin cursor
remains a legacy privacy/export concern even after this repair.

The existing schema already accepts nonnegative ordered event times and resolves
control order by strictly increasing sequence. No reader migration is required
for newly written relative values; no claim of elapsed time between sessions is
introduced.

## Finite privacy data-flow assessment

| API / path | Actual use and disposition | Readiness |
| --- | --- | --- |
| systemUptime / AR source clock | Live selection freshness, bounded drain timer, resource policy and transient input identity. New project source events are converted to project-relative intervals before native persistence. | Repaired new-project origin and review-note leak; legacy origin remains. |
| HSSBridge frame/mesh/control metadata | Native frame timestamp, replay metadata and source-store context use supplied project event time. | Source inspected; replay/control tests executed. Not a full byte-level export privacy certification. |
| HSSCAN archive | Export copies selected committed manifests/source chunks, validates sizes and hashes; it does not redact old timestamps. | Legacy migration/export policy must be resolved before final privacy-manifest compliance. Preserve immutable originals. |
| Disk-space APIs | Readiness and low-storage capture policy behave differently based on available capacity. Resource samples are passed to the engine policy, not appended as source records by these native paths. | Candidate E174.1; confirm all archive/SDK paths before final declaration. |
| stat/lstat and file metadata | Container ownership, size/type/link and durable-store checks; document-picker inputs are separately user-granted. | Candidate C617.1 / 3B52.1 by path, not a blanket permission. Complete compiled C++/platform inventory. |
| UserDefaults / tracking / third-party analytics | No app-source UserDefaults, tracking or analytics integration found in the targeted native inventory. | Not proof of every linked framework's privacy behavior or complete App Store labels. |
| File protection | Object session metadata/photo directories have explicit protection configuration. Room source storage is also created through C++ filesystem paths. | Room source and phone-lock behavior need complete verification; no blanket protected-data claim. |

Apple's current [required-reason API reference](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitype)
was read from its official documentation JSON. Reason 35F9.1 permits exported
elapsed time between in-app events, not arbitrary boot-derived information.
8FFB.1 permits event absolute timestamps, not exporting boot-time information.
The [privacy-manifest technote](https://developer.apple.com/documentation/technotes/tn3183-adding-required-reason-api-entries-to-your-privacy-manifest)
is the release checklist, not permission to guess declarations. No misleading
complete privacy manifest is added while the legacy export concern remains.

## Verification and distribution scope

- 100/100 Simulator XCTest passed: `build-portable-clock-native.log`.
  Added regressions cover fresh/later-session relative mapping and the production
  reopen/note/save/replay path. Existing reboot, inversion, epoch/depth/mesh and
  floor-reference persistence tests also pass.
- Unsigned generic iPhoneOS Release build passed:
  `build-portable-clock-release.log`.
- 3/3 native English/Arabic RTL/landscape object-library UI regressions passed:
  `build-portable-clock-ui.log`. Not a physical capture journey.
- Unsigned iPhoneOS Release archive passed: `build-portable-clock-archive.log`.
  Actual archive metadata was inspected: arm64, app ID
  `org.hybridspatialscanner.app`, version 0.1/build 1, packaged AppIcon, empty
  Team and SigningIdentity. Code-sign verification confirms the app is unsigned.
  This archive cannot be claimed as an installable or Store-uploadable package.
- Portable engine tree remains unchanged from the 59/59 GCC ASan+UBSan baseline;
  that result is reused evidence, not a new execution.

Still unresolved: legacy-source privacy export policy, full manifest and labels,
room-source protected-data validation, generic feature correction lineage,
complete native/accessibility journeys, owner-selected Apple team/brand and
signed distribution. Physical sensor/metrology/thermal and competitor evaluation
remain deferred. Engine synthetic characterization is finite and completed; do
not restart it or widen tolerances to create a readiness claim.

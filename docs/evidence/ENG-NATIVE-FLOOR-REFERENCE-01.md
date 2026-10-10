# ENG-10 — retained-source floor reference and reopened review repair

Base: `91339c67feb0c774f750b92d3d770a2e7e7cff6d` (published object lifecycle).
No C ABI, canonical schema, engine geometry or tuning-threshold changes.

## Demonstrated defects and repairs

1. Reopening a valid saved project clears the current AR epoch intentionally.
   `canConfirmPlanReference` nevertheless required that live epoch, preventing an
   explicit review control against already retained accepted coordinates. The
   targeted native test failed this assertion before the repair.
2. Merely fixing eligibility exposed a second failure: reopened projects have no
   live `captureClock`, so the queued reference control could not reach the
   native engine. A second executed test retained the unchanged revision and
   failed. Review controls now initialize an ordered clock at the verified
   durable cursor. This is an ordering key, not a physical elapsed-time claim.
3. A review control's intermediate snapshot was mutable. The repair explicitly
   requests a freeze after the source/control queue drains.

The review now resolves an accepted epoch from the exact snapshot, matching the
project UUID, current retained capture UUID, canonical revision, metre units,
valid transform revision and finite accepted matrix. It never invents a new
epoch/world relationship. Eligibility requires an idle frozen review, no camera,
no pending source/journal work and a valid retained context. Cut height remains
within the existing engine's positive finite, at-most-100-metre contract.

## Native interaction

The real captured-3D reviewer exposes an observed-floor-reference sheet. The
operator selects three distinct retained vertices on the visible physical floor
and explicitly confirms their physical category before applying the reference.
SceneKit hit faces map through the actual filtered display index buffer to raw
Float64 retained vertices; excluded faces are not silently retargeted. Highlight
spheres are selection markers only and do not enter canonical geometry.

The proposal checks conditioning and nonvertical orientation, constructs a
right-handed rigid frame from the selected support and retains the first point
as its origin. It supports an arbitrary nonvertical slope; no Manhattan axis,
world-origin floor, unseen surface completion or rectangular closure is used.
The numeric conditioning cutoff is not a sensor-accuracy claim. The operator
chooses architectural cut height independently. The frozen selection cannot be
applied to a changed revision; both the sheet and host check its binding.

The sheet distinguishes applied from saved and states uncertainty honestly.
Floor category confirmation is not a calibrated physical measurement. Captured
vertices/indices are unchanged. Semantic room boundary and area remain unknown
unless the existing engine has separate valid supporting authority. Synthetic
reference scenes do not expose this as a captured physical-floor workflow.
English/Arabic copy is included.

## Executed evidence

- **93/93 Simulator XCTest passed** in `build-floor-reference-verified-test.log`.
  Five added tests cover the reopened apply/freeze/save/reopen path, rejection of
  stale/unrelated/unresolved contexts, right-handed arbitrary-slope fitting,
  degenerate/vertical/unretained rejection and display-to-raw vertex mapping.
- **Unsigned generic iPhoneOS Release build passed** in
  `build-floor-reference-verified-release.log`.
- **3/3** object-library English, Arabic RTL and landscape native XCUITest regression
  uses `build-floor-reference-ui-regression.log`. It does not exercise the
  physical floor-selection journey; the simulator cannot supply an actual scan.
- `git diff --check` and Arabic strings `plutil -lint` passed.
- Preserved failures: `build-floor-reopen-reproduction.log` (test helper isolation
  compile error, repaired without suppressing Swift concurrency checks),
  `build-floor-reopen-native-reproduction.log` (missing live-epoch eligibility),
  `build-floor-reopen-repaired.log` (missing review-control clock). Failures are
  not relabeled as sensor results and no tolerance was widened to pass them.
- Portable core source unchanged from the 59/59 GCC ASan+UBSan baseline. Those
  tests are reused baseline evidence, not a newly executed engine run.

Toolchain: Xcode 27.0 (27A266a), Swift 6.4, iOS 27 SDK, deployment target iOS 17;
Simulator `D80BA976-A08C-427F-846D-A35C380A0C3A`.

Apple's [gravity alignment documentation](https://developer.apple.com/documentation/arkit/arconfiguration/worldalignment-swift.enum/gravity)
confirms that world origin is the initial device position, not a physical floor.
This interaction uses selected retained floor support rather than assigning Y=0
the meaning of floor. Existing platform capture and SceneKit are reused; no new
third-party engine, library or model dependency was introduced.

## Remaining gates

No physical floor picking, sensor/metrology, protected-data phone-lock, thermal,
complete VoiceOver/large-text interaction or competitor comparison was executed.
The owner defers connecting the phone until the software milestone. This is not
a signed archive, installation, merge or App Store readiness claim. Typed issue
actions and remaining correction lineage, portable-time/privacy-manifest audit,
full native journeys and owner-selected-team distribution remain unfinished.

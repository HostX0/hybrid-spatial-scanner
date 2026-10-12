# ENG-10 — revision-bound native quality actions and localized coaching

Base: `0efda573a4535454411c9cb7d2fbc4ed3d136685`.
No engine, ABI, canonical schema, tolerance or external dependency change.

## Demonstrated integration gaps

The native floor-reference issue had no executable recovery even after the
observed-floor sheet became available. The host also discarded the selected
issue's identity and dispatched the current driver's recovery for every card.
Driver failures were presented as notice-level messages, while fallback save
and synthetic-reference cards received new UUIDs on every presentation.

The decoder now retains typed native code and exact revision beside the stable
native UUID. A closed allowlist offers the already implemented floor-reference
sheet only for a matching active issue in a valid current captured assessment
and an eligible frozen workspace. The host checks this binding again on tap.
Synthetic, malformed, stale, resolved, unrelated and unsupported issues cannot
acquire this route. Native action text remains diagnostic data, never commands.

Driver recovery checks the clicked identity separately. The fallback failed-save
card invokes the real save method only while the failure remains current.
Built-in cards have stable identities and deterministic tie ordering. Failed
workspace state is blocking; actionable driver issues are important.

Seven known native codes now have English/Arabic evidence-based coaching.
Original bounded reason/action text remains accessible under Engine details.
Unsupported codes preserve their diagnostic text without invented repair tools.
VoiceOver keeps the disclosure and action as contained, individually accessible
controls; stable issue and action identifiers aid native automation. Presentation
reuses the already decoded quality assessment instead of reparsing the report.

## Executed verification

- 98/98 Simulator XCTest passed: `build-quality-actions-native.log`.
  Five added tests cover supported current routing, stale/ineligible rejection,
  synthetic/resolved/unrelated rejection, untrusted action text, original reason
  and identity preservation, failure priorities and stable built-in identities.
- Unsigned generic iPhoneOS Release build passed:
  `build-quality-actions-release.log`.
- 3/3 English, Arabic RTL and landscape object-library XCUITest regressions
  passed: `build-quality-actions-ui.log`. These journeys are not a physical
  scan or a floor-action interaction test.
- Arabic strings plist validation and `git diff --check` passed.
- Portable core source is unchanged. The 59/59 GCC ASan+UBSan result remains
  reused baseline evidence, not a new execution.

Xcode 27.0, Swift 6.4, iOS 27 SDK, deployment target iOS 17.

## Authority and remaining work

Opening a supported correction is not resolution, durable save, calibrated
accuracy or verified room area. Generic selected-feature Assist/Undo/lineage is
still unavailable and not falsely enabled. Native live-capture/physical floor
interaction, comprehensive accessibility, privacy data flow, portable event-time,
owner-selected distribution identity, signing and App Store gates remain separate.
No merge, physical installation or store-readiness claim is made.

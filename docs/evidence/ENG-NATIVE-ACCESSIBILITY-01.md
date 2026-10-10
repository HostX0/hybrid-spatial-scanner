# Native library accessibility audit and toolbar repair

Base: `e3fd840de16abffc23ce40a7a02cb3f458193c25`.
No engine/ABI/schema/tolerance or third-party dependency change.

## Demonstrated UI failure

Two new actual XCUITest journeys run Apple's built-in accessibility audit on
the current project and object libraries in English and Arabic. They check
contrast, hit regions, text clipping and Dynamic Type without an issue-ignore
callback. These use actual app rendering, not generated mockups or capture
fixtures.

The baseline ran five tests: three existing journeys passed and both audits
failed with Dynamic Type font sizes partially unsupported on object-library
Done. Adding explicit body font/minimum touch height still failed the same two
audits; that unsuccessful attempt is preserved, not called a repair.

The final toolbar uses a standard SF Symbol close affordance, semantic font,
44-point minimum touch dimensions and localized accessible Done/Close labels.
The existing action and stable automation identity are unchanged. Arabic
right-to-left placement is native. No audit issue is suppressed.

## Executed final verification

- 5/5 XCUITest passed: `build-native-accessibility-final.log`. Includes both
  project/object library audits, Arabic round trip, English introduction/return
  and landscape control reachability.
- 100/100 native Simulator XCTest passed:
  `build-native-accessibility-final-native.log`.
- Unsigned iPhoneOS Release build passed:
  `build-native-accessibility-final-release.log`.
- Actual unsigned arm64 archive passed:
  `build-native-accessibility-final-archive.log`.
  Metadata inspected: version 0.1/build 1, expected app ID, empty Team and
  SigningIdentity. No physical install or upload-ready signature.
- English/Arabic screenshot attachments were exported from final
  `Test-HybridSpatialScannerJourneys-2026.10.10_06-04-14-+0300.xcresult`.
  Arabic portrait project/object libraries were visually inspected.
- `git diff --check` passed. Portable engine tree is unchanged; 59/59
  GCC ASan+UBSan is reused baseline evidence, not a new run.

Preserved failures: `build-native-accessibility-audit.log` (five executed tests,
two failures, exit 65; only its owned post-test diagnostic collector stopped
after all tests finished) and `build-native-accessibility-repaired.log` (same
two failures, exit 65). Final commands use the documented
`-collect-test-diagnostics never` option to avoid lengthy system diagnostics;
this does not disable any accessibility check or test assertion.

Apple's [audit type reference](https://developer.apple.com/documentation/xcuiautomation/xcuiaccessibilityaudittype)
documents the four checks. Existing platform UI is reused instead of a custom
navigation/toolbar framework.

## Boundaries

These are current visible library-screen audits on iOS 27 Simulator, not a full
VoiceOver usability study, every populated state, all operating-system/device
versions, floor selection, live sensor/thermal/accuracy or competitor evidence.
The owner defers phone connection. Legacy-source privacy export, complete
manifest/protected-data audit, generic correction lineage and owner-selected
team signing remain separate release gates. No merge or App Store-ready claim.

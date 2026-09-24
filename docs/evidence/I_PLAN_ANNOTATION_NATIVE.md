# I-PLAN-ANNOTATION-NATIVE evidence

Date: 2026-09-24. Worktree branch: `hss/i-plan-native`.

The direct-source Xcode `SpatialCore` target now compiles `plan_semantics.cpp` and
`annotation_geometry.cpp` with `HSS_ENABLE_PLAN_ANNOTATION_RUNTIME=1` in Debug
and Release. The app copies the fixed B-031/B-032 summary and owned JSON from
one acquired core snapshot, rejects a revision mismatch, and carries these as
additive fields in `ScannerSceneSnapshot`. This does not change the C ABI, the
project store schema, or source replay. Reopened committed source reconstructs
the same derived semantic bytes. Export writes `semantic.provenance.json` beside
the existing metric exporter artifacts; the measured scene JSON and captured
mesh remain the PDF/SVG/DXF/OBJ inputs. Export rejects a stale semantic view.

Review draws supported B-031 fragments and displays only B-032 lengths with
available numeric values, matching revision and fragment anchors, and measured
origin. The open-boundary synthetic fixture has a retained captured-section
fragment but unavailable plan and annotation length; the UI suppresses its
numeric dimension. Floor-unknown and area-unavailable explanations are shown
in English and Arabic. The Files picker accepts the dynamic `.hsscan` type via
`UTType.data`; the existing archive parser and project validator still validate
selected bytes.

Focused simulator verification on Xcode / AppleClang 21.0.0.21000334:

```sh
xcodebuild -project HSSApp/HybridSpatialScanner.xcodeproj \
  -scheme HybridSpatialScanner -configuration Debug \
  -destination 'platform=iOS Simulator,id=2F6A107C-24BF-43F4-B091-6DE5271B48EE' \
  -derivedDataPath /tmp/hss-i-plan-native-derived CODE_SIGNING_ALLOWED=NO \
  -only-testing:HybridSpatialScannerTests/ScannerWorkspacePipelineTests \
  -only-testing:HybridSpatialScannerTests/HSSProjectImportTests test
xcodebuild -project HSSApp/HybridSpatialScanner.xcodeproj \
  -scheme HybridSpatialScanner -configuration Debug \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath /tmp/hss-i-plan-native-derived CODE_SIGNING_ALLOWED=NO build
git diff --check
```

Observed: 15 selected simulator tests passed with zero failures; the simulator
build succeeded; diff whitespace check passed. Tests cover revision-bound
summary, unknown floor and null area, unavailable length suppression, frozen
semantic export provenance, stale revision rejection, committed save/reopen
replay, archive export/import, and `.hsscan` picker type conformance. The initial
focused run failed because the optional runtime define was attached to the app
instead of the static core target; moving it to the target that compiles
`live_reconstruction.cpp` resolved the failure. A later assertion expecting a
length for an unavailable open plan was corrected to test suppression, matching
the core contract. No accuracy threshold was changed.

This is simulator and synthetic-fixture evidence. Device capture, real floor
confirmation, localized label collision/layout, actual PDF/DXF consumer checks,
physical metrology, and thermal/resource endurance remain untested. The core
currently leaves finished-room area unavailable, including for a confirmed
single captured-mesh loop, until classification and calibrated quality have
independent accepted authority.

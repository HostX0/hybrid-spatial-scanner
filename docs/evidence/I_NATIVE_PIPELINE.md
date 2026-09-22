# I-NATIVE capture, snapshot, save, and reopen evidence

Date: 2026-09-22

Branch: `hss/native-pipeline`

## Integrated path

The application owns one `ARSession` and forwards copied scene-depth keyframes,
mesh add/replace/remove events, and explicit epoch controls through a bounded
mailbox to one actor-isolated native runtime. Scene depth remains calibrated
measurement input; ARKit captured mesh remains the geometry source. The bridge
packs mesh vertices as tightly packed Float32 XYZ values, preserves anchor
identity/revision/removal information, and waits for each accepted engine event
to publish its deterministic scene revision before appending the matching
portable source record.

Stopping capture flushes queued native input before freezing an immutable
snapshot. Save reports success only after the project store checkpoint returns
durable state. Reopen constructs a fresh candidate engine/store, validates and
replays committed source, acquires a rebuilt snapshot, and swaps that candidate
into the workspace only after replay succeeds. Synthetic provenance is explicit
and immutable. Epoch transform revisions start at one and increase when the
relationship changes from unresolved to accepted.

The project catalog and workspace model expose create, capture, stop/freeze,
save, reopen, library navigation, plan-reference confirmation, interruption,
bounded-ingress accounting, and recoverable failure states to the product UI.
Simulator capture is reported as unsupported rather than presenting fabricated
sensor capability.

## Focused simulator evidence

Executed on the iOS 26.5 **iPhone 17 Pro Max simulator** with Xcode's iOS 27.0
SDK and an iOS 17.0 deployment target:

```sh
xcodebuild test \
  -project HSSApp/HybridSpatialScanner.xcodeproj \
  -scheme HybridSpatialScanner \
  -destination 'platform=iOS Simulator,id=BF8044E8-13DD-430B-87D7-3DB0B02DF972' \
  -derivedDataPath work/DerivedData-native-pipeline \
  -jobs 1 -parallel-testing-enabled NO \
  -only-testing:HybridSpatialScannerTests/ScannerWorkspacePipelineTests \
  CODE_SIGNING_ALLOWED=NO
```

Observed result: `** TEST SUCCEEDED **`; 4 tests executed with 0 failures.
The focused suite verifies stationary-frame rejection, translation/rotation
keyframe acceptance, newly observed coarse depth support, the explicit retained
frame limit, and an end-to-end synthetic flow that creates a project, applies
two epoch relationship revisions and a captured mesh, checkpoints, destroys the
runtime, reopens into a fresh runtime, replays three records, and compares the
rebuilt frozen vertices, triangle indices, and scene revision.

The integration run also exposed two boundary issues that were fixed before the
passing result: floating-point `from_chars`/`to_chars` are unavailable at the
iOS 17 deployment target, so manifest numbers use bounded classic-locale streams;
and synthetic project metadata was rejected on reopen because the parser's
minimum length assumed the five-character `false` literal rather than allowing
the four-character `true` literal.

## Evidence limits

This is simulator and synthetic replay evidence. It does not validate ARKit
capture, LiDAR accuracy, camera/depth synchronization, physical save/reopen,
protected-data behavior, memory pressure, sustained thermal performance, or
metrology on the owner-authorized iPhone 15 Pro Max. Plan area remains gated on
an explicit caller-confirmed observed floor reference. The integrated backend
uses captured mesh and retained depth evidence; it does not claim TSDF fusion or
device-validated accuracy.

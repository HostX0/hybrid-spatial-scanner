# OBJECT-001 — offline RealityKit detailed-object workflow

Date: 2026-09-22  
Branch: `hss/object-capture`  
Starting revision: `ae9b61e`

## Result

Implemented a separate iOS 17+ small-object capture and on-device reconstruction workflow in new files under `HSSApp/ObjectCaptureWorkflow/`. The public integration surface is:

```swift
ObjectCaptureWorkflowView { artifact in
  // Persist or present ObjectCaptureArtifact.
}
```

`ObjectCaptureArtifact` returns the stable app-container session directory, retained image directory, completed USDZ URL, capture timestamps, image count, processing warnings, explicit photo-retention policy, and provenance. The provenance sets `metricAuthority` to `false` and records that the USDZ is an appearance artifact rather than canonical measured room geometry.

The view does not create or accept an `ARSession`. Integration must present it only after room capture has been paused or ended. Unsupported hardware and Simulator receive an honest unavailable state scoped only to detailed-object capture; the room workflow remains usable.

## Implemented behavior

- Checks both `ObjectCaptureSession.isSupported` and `PhotogrammetrySession.isSupported` before constructing a capture session.
- Requires the user to explicitly select **Start Object Capture** before camera capture begins.
- Uses Apple's `ObjectCaptureView` for native guided capture and observes native state, feedback, image-count, and completed-pass async sequences.
- Keeps failed object detection recoverable when a horizontal supporting surface is not yet visible.
- Supports additional-height passes and a pause/confirm flow before calling `beginNewScanPassAfterFlip()`.
- Finishes capture before creating a separate `PhotogrammetrySession` and requests the only iOS-supported `.reduced` model detail.
- Reports RealityKit reconstruction stage and actual request progress. It does not present a fabricated capture-quality or metric-accuracy percentage.
- Preserves invalid/skipped-image, automatic-downsampling, and incomplete-stitching warnings in the UI and final artifact manifest.
- Supports explicit cancellation during capture and reconstruction. A completion callback is delivered at most once and only after the USDZ exists and the manifest is durably written.
- Gives specific setup guidance for diffuse light, textured background, multiple heights, hidden backs/undersides, and the known difficulty of reflective, transparent, and very thin objects.
- Requests no cloud account, upload, network service, or Photo Library permission.

## App-container layout

The workflow creates durable identity and metadata before prolonged capture:

```text
Application Support/ObjectCaptures/<UUID>/
├── session.json
├── Images/
├── CaptureCheckpoints/
├── ReconstructionCheckpoints/
├── Output/Object.usdz
└── manifest.json
```

Captured photos use the declared `retainedUntilExplicitSessionDeletion` policy. Successful reconstruction does not remove source images. This task adds no deletion UI or hidden cleanup policy.

## API verification

The implementation was checked against the installed Xcode SDK, not inferred from memory:

- Xcode `27.0` build `27A266a`.
- iPhoneOS SDK: `/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk`.
- Installed interfaces confirm iOS 17 availability and signatures for `ObjectCaptureView(session:)`, `ObjectCaptureSession.isSupported`, `start(imagesDirectory:configuration:)`, `startDetecting()`, `startCapturing()`, pass/flip, finish/cancel, checkpoint configuration, and async update sequences.
- Installed interfaces confirm iOS 17 availability and signatures for `PhotogrammetrySession.isSupported`, input/checkpoint configuration, `.modelFile(url:detail:)`, `.reduced`, `process(requests:)`, cancellation, outputs, progress, stage, warning, error, and completion cases.

Primary Apple references:

- [Object Capture overview](https://developer.apple.com/documentation/realitykit/realitykit-object-capture)
- [ObjectCaptureSession](https://developer.apple.com/documentation/realitykit/objectcapturesession)
- [ObjectCaptureView](https://developer.apple.com/documentation/realitykit/objectcaptureview)
- [Creating 3D objects from photographs](https://developer.apple.com/documentation/realitykit/creating-3d-objects-from-photographs)
- [Scanning objects using Object Capture](https://developer.apple.com/documentation/realitykit/scanning-objects-using-object-capture)
- [Meet Object Capture for iOS, WWDC23](https://developer.apple.com/videos/play/wwdc2023/10191/)

Apple's material guidance supports the UI wording: provide overlapping, well-lit views from multiple angles; reflective, transparent, translucent, very thin, deformable, and hidden surfaces are difficult or unavailable. Apple's documentation says iOS currently supports only `.reduced` reconstruction detail.

## Verification performed

1. Device-SDK compile-time verification:

   ```sh
   sdk=$(xcrun --sdk iphoneos --show-sdk-path)
   xcrun swiftc -typecheck -parse-as-library -swift-version 6 \
     -target arm64-apple-ios17.0 -sdk "$sdk" \
     HSSApp/ObjectCaptureWorkflow/*.swift
   ```

   Result: exit 0.

2. Simulator-SDK compile-time verification of the guarded unavailable path:

   ```sh
   sdk_sim=$(xcrun --sdk iphonesimulator --show-sdk-path)
   xcrun swiftc -typecheck -parse-as-library -swift-version 6 \
     -target arm64-apple-ios17.0-simulator -sdk "$sdk_sim" \
     HSSApp/ObjectCaptureWorkflow/*.swift
   ```

   Result: exit 0. This is compile-time evidence only; no Simulator runtime capability is claimed.

3. Formatting and patch hygiene:

   ```sh
   xcrun swift-format lint --strict HSSApp/ObjectCaptureWorkflow/*.swift
   git diff --check
   ```

   Result: both exit 0.

4. A temporary macOS Swift smoke executable compiled the production artifact/storage files, created a fixed-UUID session under a unique temporary root, verified every directory and the initial `session.json`, wrote a stand-in model, atomically persisted `manifest.json`, decoded it, and checked artifact equality, `metricAuthority == false`, and the retained-photo policy.

   Result: `ObjectCaptureStorage smoke test passed`. The temporary source, binary, and session directory were removed from the worktree after the run.

## Integration and remaining evidence

The native owner must register all four new Swift files in the app target and run the serialized target build. This worktree deliberately does not edit `project.pbxproj` and did not run `xcodebuild`, per task ownership.

No physical supported iPhone or iPad was attached. Camera permission presentation, guided capture, reconstruction duration/memory/thermal behavior, cancellation timing, checkpoint recovery, USDZ visual quality, and the callback after a real reconstruction remain hardware-unverified. No accuracy or completeness claim is made. A physical-device test must retain device/OS, object/material/lighting, image count, duration, warnings/failures, and any independent reference dimensions before a product claim is considered.

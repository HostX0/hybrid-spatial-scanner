# B-002 test-host repair evidence

## Cause and repair

The XCTest target had no `SDKROOT` or `SUPPORTED_PLATFORMS`. During the shared
scheme's simulator test action, its `TEST_HOST` was expanded against a generic
`Debug` products directory instead of `Debug-iphonesimulator`, so Xcode could
not locate the app executable.

The test target now declares the iOS device/simulator platforms and resolves
the host through the canonical platform-aware products directory:

```text
TEST_HOST = $(BUILT_PRODUCTS_DIR)/$(TEST_HOST_TARGET_NAME).app/$(TEST_HOST_TARGET_NAME)
TEST_HOST_TARGET_NAME = HybridSpatialScanner
BUNDLE_LOADER = $(TEST_HOST)
```

`xcodebuild -target HybridSpatialScannerTests -sdk iphonesimulator
-configuration Debug -showBuildSettings` expanded both `TEST_HOST` and
`BUNDLE_LOADER` to `.../build/Debug-iphonesimulator/HybridSpatialScanner.app/
HybridSpatialScanner`.

## Executed test

```sh
xcodebuild -project HSSApp/HybridSpatialScanner.xcodeproj \
  -scheme HybridSpatialScanner \
  -destination 'platform=iOS Simulator,id=7606D6E6-462F-427C-88C1-171D8824D707' \
  CODE_SIGNING_ALLOWED=NO test
```

Observed result on the available iPhone 18 Pro (iOS 27.0) simulator:
**TEST SUCCEEDED** — 5 tests executed, 0 failures. These are readiness-shell
tests only; they do not exercise physical ARKit, LiDAR, or device deployment.

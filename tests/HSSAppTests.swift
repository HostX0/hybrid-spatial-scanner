import XCTest
@testable import HybridSpatialScanner

final class HSSAppTests: XCTestCase {
    func testReadyWhenEveryRequirementIsSatisfied() {
        let readiness = makeProvider().currentReadiness()
        XCTAssertTrue(readiness.isReadyForCapture)
        XCTAssertEqual(readiness.blockedConditions, [])
    }

    func testDeniedPermissionBlocksCapture() {
        let readiness = makeProvider(permission: .denied).currentReadiness()
        XCTAssertFalse(readiness.isReadyForCapture)
        XCTAssertEqual(readiness.blockedConditions, ["Camera access is denied. Capture is unavailable until it is allowed in Settings."])
    }

    func testMissingARKitCapabilityBlocksCapture() {
        let readiness = makeProvider(features: false).currentReadiness()
        XCTAssertFalse(readiness.isReadyForCapture)
        XCTAssertEqual(readiness.blockedConditions, ["This device does not support ARKit scene depth and mesh reconstruction required for capture."])
    }

    func testUnavailableStorageBlocksCapture() {
        let readiness = makeProvider(storage: false).currentReadiness()
        XCTAssertFalse(readiness.isReadyForCapture)
        XCTAssertEqual(readiness.blockedConditions, ["Usable local storage is unavailable. Capture cannot begin because source observations cannot be retained."])
    }

    func testInitialTrackingMustBeNormal() {
        let readiness = makeProvider(tracking: .notEstablished).currentReadiness()
        XCTAssertFalse(readiness.isReadyForCapture)
        XCTAssertEqual(readiness.blockedConditions, ["Initial AR tracking has not been established. Capture remains unavailable until a later session reports normal tracking."])
    }

    private func makeProvider(permission: CameraPermissionState = .authorized, features: Bool = true, storage: Bool = true, tracking: InitialTrackingState = .normal) -> CaptureReadinessProvider {
        CaptureReadinessProvider(platform: StubPlatform(permission: permission, features: features, storage: storage, tracking: tracking))
    }
}

private struct StubPlatform: CapturePlatformInspecting {
    let permission: CameraPermissionState
    let features: Bool
    let storage: Bool
    let tracking: InitialTrackingState
    func cameraPermission() -> CameraPermissionState { permission }
    func supportsRequiredARFeatures() -> Bool { features }
    func hasUsableStorage() -> Bool { storage }
    func initialTrackingState() -> InitialTrackingState { tracking }
}

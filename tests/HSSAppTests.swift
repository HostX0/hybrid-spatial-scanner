import XCTest
@testable import HybridSpatialScanner

@MainActor
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

    func testCoordinatorDoesNotRequestPermissionOrStartSessionOnInitialization() {
        let permission = StubPermissionRequester(status: .notDetermined, requestResult: .authorized)
        let driver = StubSessionDriver()

        _ = makeCoordinator(permission: permission, driver: driver)

        XCTAssertEqual(permission.requestCount, 0)
        XCTAssertEqual(driver.runRequests, [])
    }

    func testPermissionIsRequestedOnlyByExplicitAction() async {
        let permission = StubPermissionRequester(status: .notDetermined, requestResult: .authorized)
        let coordinator = makeCoordinator(permission: permission)

        await coordinator.requestCameraPermission()

        XCTAssertEqual(permission.requestCount, 1)
        XCTAssertEqual(coordinator.readiness.cameraPermission, .authorized)
        XCTAssertEqual(coordinator.phase, .idle)
    }

    func testSupportedGateRunsRequestedConfigurationAndRecordsEffectiveChoice() {
        let driver = StubSessionDriver()
        let coordinator = makeCoordinator(driver: driver)

        coordinator.startSession()

        XCTAssertEqual(driver.runRequests, [.requested])
        XCTAssertEqual(coordinator.phase, .awaitingNormalTracking)
        XCTAssertEqual(
            coordinator.effectiveConfiguration,
            EffectiveCaptureConfigurationReport(
                requestedDepthSemantic: .sceneDepth,
                sceneDepthSupported: true,
                chosenDepthSemantic: .sceneDepth,
                requestedMeshOption: .mesh,
                meshReconstructionSupported: true,
                chosenMeshOption: .mesh,
                worldTrackingSupported: true,
                roomPlanIntegration: .notEnabledPendingPhysicalValidation,
                proposalsAreReduced: false,
                fallbackReason: nil
            )
        )
    }

    func testUnsupportedRequiredFeatureUsesARKitFallbackAndRecordsReason() {
        let driver = StubSessionDriver()
        let coordinator = makeCoordinator(
            readinessFeatures: false,
            support: CaptureFeatureSupport(worldTracking: true, sceneDepth: false, meshReconstruction: true),
            driver: driver
        )

        coordinator.startSession()

        XCTAssertEqual(driver.runRequests, [.arkitFallback])
        XCTAssertEqual(coordinator.effectiveConfiguration?.chosenDepthSemantic, CaptureDepthSemantic.none)
        XCTAssertEqual(coordinator.effectiveConfiguration?.chosenMeshOption, CaptureMeshOption.none)
        XCTAssertEqual(coordinator.effectiveConfiguration?.proposalsAreReduced, true)
        XCTAssertNotNil(coordinator.effectiveConfiguration?.fallbackReason)

        driver.send(.tracking(.normal))
        XCTAssertFalse(coordinator.readiness.isReadyForCapture)
        guard case .fallback = coordinator.phase else {
            return XCTFail("Normal tracking must not promote reduced fallback to metric capture readiness.")
        }
    }

    func testRequestedConfigurationFailureRetriesOnSameDriverWithFallback() {
        let driver = StubSessionDriver(failingConfigurations: [.requested])
        let coordinator = makeCoordinator(driver: driver)

        coordinator.startSession()

        XCTAssertEqual(driver.runRequests, [.requested, .arkitFallback])
        XCTAssertEqual(coordinator.effectiveConfiguration?.chosenDepthSemantic, CaptureDepthSemantic.none)
        XCTAssertEqual(coordinator.effectiveConfiguration?.chosenMeshOption, CaptureMeshOption.none)
        XCTAssertEqual(coordinator.effectiveConfiguration?.proposalsAreReduced, true)
    }

    func testTrackingMustBecomeNormalBeforeCaptureIsReady() {
        let driver = StubSessionDriver()
        let coordinator = makeCoordinator(driver: driver)
        coordinator.startSession()

        driver.send(.tracking(.limited(.relocalizing)))
        XCTAssertFalse(coordinator.readiness.isReadyForCapture)
        XCTAssertEqual(coordinator.trackingState, .limited(.relocalizing))

        driver.send(.tracking(.normal))
        XCTAssertTrue(coordinator.readiness.isReadyForCapture)
        XCTAssertEqual(coordinator.phase, .captureReady)
    }

    func testStopReleasesDelegateAndStopsOwnedSession() {
        let driver = StubSessionDriver()
        let coordinator = makeCoordinator(driver: driver)
        coordinator.startSession()

        coordinator.stopSession()

        XCTAssertEqual(driver.stopCount, 1)
        XCTAssertNil(driver.eventHandler)
        XCTAssertNil(driver.sceneDepthIngestionHandler)
        XCTAssertFalse(coordinator.isSessionRunning)
        XCTAssertEqual(coordinator.phase, .stopped)
    }

    func testRepeatedStartDoesNotRunAnotherSessionConfiguration() {
        let driver = StubSessionDriver()
        let coordinator = makeCoordinator(driver: driver)

        coordinator.startSession()
        coordinator.startSession()

        XCTAssertEqual(driver.runRequests, [.requested])
    }

    private func makeProvider(permission: CameraPermissionState = .authorized, features: Bool = true, storage: Bool = true, tracking: InitialTrackingState = .normal) -> CaptureReadinessProvider {
        CaptureReadinessProvider(platform: StubPlatform(permission: permission, features: features, storage: storage, tracking: tracking))
    }

    private func makeCoordinator(
        permission: StubPermissionRequester = StubPermissionRequester(status: .authorized, requestResult: .authorized),
        readinessFeatures: Bool = true,
        storage: Bool = true,
        support: CaptureFeatureSupport = CaptureFeatureSupport(worldTracking: true, sceneDepth: true, meshReconstruction: true),
        driver: StubSessionDriver = StubSessionDriver()
    ) -> ARSessionCoordinator {
        ARSessionCoordinator(
            readinessProvider: makeProvider(permission: permission.status, features: readinessFeatures, storage: storage, tracking: .notEstablished),
            permissionRequester: permission,
            supportProvider: StubSupportProvider(support: support),
            sessionDriver: driver
        )
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

@MainActor
private final class StubPermissionRequester: CameraPermissionRequesting {
    private(set) var status: CameraPermissionState
    let requestResult: CameraPermissionState
    private(set) var requestCount = 0

    init(status: CameraPermissionState, requestResult: CameraPermissionState) {
        self.status = status
        self.requestResult = requestResult
    }

    func authorizationStatus() -> CameraPermissionState { status }

    func requestAccess() async -> CameraPermissionState {
        requestCount += 1
        status = requestResult
        return status
    }
}

@MainActor
private struct StubSupportProvider: CaptureFeatureSupportProviding {
    let support: CaptureFeatureSupport
    func currentSupport() -> CaptureFeatureSupport { support }
}

private enum StubSessionError: Error {
    case configuredFailure
}

@MainActor
private final class StubSessionDriver: CaptureSessionRunning {
    var eventHandler: ((CaptureSessionEvent) -> Void)?
    var sceneDepthIngestionHandler: ((SceneDepthIngestionResult) -> Void)?
    private(set) var runRequests: [CaptureSessionRunConfiguration] = []
    private(set) var stopCount = 0
    private let failingConfigurations: Set<CaptureSessionRunConfiguration>

    init(failingConfigurations: Set<CaptureSessionRunConfiguration> = []) {
        self.failingConfigurations = failingConfigurations
    }

    func run(configuration: CaptureSessionRunConfiguration) throws -> CaptureSessionRunConfiguration {
        runRequests.append(configuration)
        if failingConfigurations.contains(configuration) { throw StubSessionError.configuredFailure }
        return configuration
    }

    func stop() {
        stopCount += 1
        eventHandler = nil
        sceneDepthIngestionHandler = nil
    }

    func send(_ event: CaptureSessionEvent) {
        eventHandler?(event)
    }

    func send(_ result: SceneDepthIngestionResult) {
        sceneDepthIngestionHandler?(result)
    }
}

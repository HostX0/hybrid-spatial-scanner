@preconcurrency import ARKit
import AVFoundation
import Combine
import Foundation

enum CaptureDepthSemantic: String, Equatable, Sendable {
    case sceneDepth
    case none
}

enum CaptureMeshOption: String, Equatable, Sendable {
    case mesh
    case none
}

enum RoomPlanIntegrationState: String, Equatable, Sendable {
    /// RoomPlan is deliberately not started until shared-session behavior is
    /// demonstrated on the target physical device.
    case notEnabledPendingPhysicalValidation
}

struct CaptureFeatureSupport: Equatable, Sendable {
    let worldTracking: Bool
    let sceneDepth: Bool
    let meshReconstruction: Bool
}

struct CaptureSessionRunConfiguration: Equatable, Hashable, Sendable {
    let depthSemantic: CaptureDepthSemantic
    let meshOption: CaptureMeshOption

    static let requested = CaptureSessionRunConfiguration(depthSemantic: .sceneDepth, meshOption: .mesh)
    static let arkitFallback = CaptureSessionRunConfiguration(depthSemantic: .none, meshOption: .none)
}

struct EffectiveCaptureConfigurationReport: Equatable, Sendable {
    let requestedDepthSemantic: CaptureDepthSemantic
    let sceneDepthSupported: Bool
    let chosenDepthSemantic: CaptureDepthSemantic
    let requestedMeshOption: CaptureMeshOption
    let meshReconstructionSupported: Bool
    let chosenMeshOption: CaptureMeshOption
    let worldTrackingSupported: Bool
    let roomPlanIntegration: RoomPlanIntegrationState
    let proposalsAreReduced: Bool
    let fallbackReason: String?
}

enum CaptureTrackingLimitation: String, Equatable, Sendable {
    case initializing
    case excessiveMotion
    case insufficientFeatures
    case relocalizing
    case unknown

    var message: String {
        switch self {
        case .initializing: "Tracking is initializing. Hold the phone steady and keep visible surfaces in view."
        case .excessiveMotion: "Tracking is limited by excessive motion. Move more slowly."
        case .insufficientFeatures: "Tracking needs more visible detail or light. Precise capture is paused."
        case .relocalizing: "Tracking is relocalizing. Existing geometry is not committed until coordinates recover."
        case .unknown: "Tracking is limited. Precise capture is paused."
        }
    }
}

enum CaptureTrackingState: Equatable, Sendable {
    case notEstablished
    case normal
    case limited(CaptureTrackingLimitation)
    case unavailable
}

enum CaptureSessionEvent: Equatable, Sendable {
    case tracking(CaptureTrackingState)
    case failed
    case interrupted
    case interruptionEnded
    case stoppedAfterFlush
}

private enum CaptureDriverDelivery: Sendable {
    case session(CaptureSessionEvent)
    case sceneDepth(SceneDepthIngestionResult)
    case mesh(EpochStampedMeshLifecycleResult)
    case ingressOverflow
    case stopBarrier

    var isExpendableFrame: Bool {
        if case .sceneDepth = self { true } else { false }
    }
}

/// One bounded ordered handoff from ARSession's delegate queue to MainActor.
/// Frame payloads are expendable under load; epoch/session and mesh lifecycle
/// events preserve order and capacity failure becomes an explicit scan failure.
private final class CaptureDriverDeliveryMailbox: @unchecked Sendable {
    private let lock = NSLock()
    private let maximumQueuedEvents: Int
    private let maximumQueuedFrames: Int
    private var pending: [CaptureDriverDelivery] = []
    private var deliveryScheduled = false
    private var overflowed = false

    init(maximumQueuedEvents: Int = 512, maximumQueuedFrames: Int = 2) {
        precondition(maximumQueuedEvents > maximumQueuedFrames && maximumQueuedFrames > 0)
        self.maximumQueuedEvents = maximumQueuedEvents
        self.maximumQueuedFrames = maximumQueuedFrames
    }

    func submit(
        _ event: CaptureDriverDelivery,
        deliver: @escaping @MainActor @Sendable (CaptureDriverDelivery) -> Void
    ) {
        lock.lock()
        if event.isExpendableFrame {
            let frameIndices = pending.indices.filter { pending[$0].isExpendableFrame }
            if frameIndices.count >= maximumQueuedFrames, let oldest = frameIndices.first {
                pending.remove(at: oldest)
            }
        }
        if pending.count < maximumQueuedEvents {
            pending.append(event)
        } else {
            overflowed = true
        }
        let mustSchedule = !deliveryScheduled
        deliveryScheduled = true
        lock.unlock()
        guard mustSchedule else { return }

        Task { @MainActor in
            while let next = self.takeNext() { deliver(next) }
        }
    }

    func reset() {
        lock.lock()
        pending.removeAll(keepingCapacity: true)
        overflowed = false
        lock.unlock()
    }

    private func takeNext() -> CaptureDriverDelivery? {
        lock.lock()
        defer { lock.unlock() }
        if !pending.isEmpty { return pending.removeFirst() }
        if overflowed {
            overflowed = false
            return .ingressOverflow
        }
        deliveryScheduled = false
        return nil
    }
}

enum CaptureSessionPhase: Equatable {
    case idle
    case requestingPermission
    case starting
    case awaitingNormalTracking
    case captureReady
    case limited(String)
    case fallback(String)
    case interrupted
    case stopped
    case blocked(String)
    case failed(String)

    var summary: String {
        switch self {
        case .idle: "Ready to start the ARKit session."
        case .requestingPermission: "Requesting camera access."
        case .starting: "Starting the owned ARKit session."
        case .awaitingNormalTracking: "ARKit is running; waiting for normal tracking before metric capture."
        case .captureReady: "ARKit tracking is normal. Metric capture may begin with the recorded configuration."
        case .limited(let reason): reason
        case .fallback(let reason): "ARKit fallback is active with reduced proposals. \(reason)"
        case .interrupted: "The ARKit session was interrupted. Metric capture is paused."
        case .stopped: "The ARKit session is stopped."
        case .blocked(let reason), .failed(let reason): reason
        }
    }
}

@MainActor
protocol CameraPermissionRequesting {
    func authorizationStatus() -> CameraPermissionState
    func requestAccess() async -> CameraPermissionState
}

@MainActor
protocol CaptureFeatureSupportProviding {
    func currentSupport() -> CaptureFeatureSupport
}

@MainActor
protocol CaptureSessionRunning: AnyObject {
    var captureSession: ARSession? { get }
    var deliversStopAfterFlush: Bool { get }
    var eventHandler: ((CaptureSessionEvent) -> Void)? { get set }
    var sceneDepthIngestionHandler: ((SceneDepthIngestionResult) -> Void)? { get set }
    var meshAnchorLifecycleHandler: ((MeshAnchorLifecycleResult) -> Void)? { get set }
    var epochStampedMeshLifecycleHandler: ((EpochStampedMeshLifecycleResult) -> Void)? { get set }
    func run(configuration: CaptureSessionRunConfiguration) throws -> CaptureSessionRunConfiguration
    func setCaptureEpoch(_ epoch: UInt64)
    func stop()
}

extension CaptureSessionRunning {
    var captureSession: ARSession? { nil }
    var deliversStopAfterFlush: Bool { false }

    var epochStampedMeshLifecycleHandler: ((EpochStampedMeshLifecycleResult) -> Void)? {
        get { nil }
        set { _ = newValue }
    }

    func setCaptureEpoch(_ epoch: UInt64) {
        _ = epoch
    }
}

@MainActor
struct SystemCameraPermissionRequester: CameraPermissionRequesting {
    func authorizationStatus() -> CameraPermissionState {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized: .authorized
        case .denied: .denied
        case .restricted: .restricted
        case .notDetermined: .notDetermined
        @unknown default: .restricted
        }
    }

    func requestAccess() async -> CameraPermissionState {
        _ = await AVCaptureDevice.requestAccess(for: .video)
        return authorizationStatus()
    }
}

@MainActor
struct SystemCaptureFeatureSupportProvider: CaptureFeatureSupportProviding {
    func currentSupport() -> CaptureFeatureSupport {
        guard ARWorldTrackingConfiguration.isSupported else {
            return CaptureFeatureSupport(worldTracking: false, sceneDepth: false, meshReconstruction: false)
        }
        return CaptureFeatureSupport(
            worldTracking: true,
            sceneDepth: ARWorldTrackingConfiguration.supportsFrameSemantics(.sceneDepth),
            meshReconstruction: ARWorldTrackingConfiguration.supportsSceneReconstruction(.mesh)
        )
    }
}

enum SystemARSessionDriverError: Error {
    case unsupportedWorldTracking
    case unsupportedSceneDepth
    case unsupportedMeshReconstruction
    case effectiveConfigurationUnavailable
}

/// The only production owner of an ARSession. No RoomPlan session or second
/// camera session is created by this spike.
@MainActor
final class SystemARSessionDriver: NSObject, CaptureSessionRunning, ARSessionDelegate {
    private let session: ARSession
    nonisolated private let sceneDepthNormalizer: SceneDepthFrameNormalizer
    nonisolated private let frameIdentity: CaptureEventIdentitySequence
    nonisolated private let deliveryMailbox = CaptureDriverDeliveryMailbox()
    nonisolated private let meshAnchorStore = MeshAnchorLifecycleStore()
    var eventHandler: ((CaptureSessionEvent) -> Void)?
    var sceneDepthIngestionHandler: ((SceneDepthIngestionResult) -> Void)?
    var meshAnchorLifecycleHandler: ((MeshAnchorLifecycleResult) -> Void)?
    var epochStampedMeshLifecycleHandler: ((EpochStampedMeshLifecycleResult) -> Void)?

    var captureSession: ARSession? { session }
    var deliversStopAfterFlush: Bool { true }

    init(
        session: ARSession = ARSession(),
        sessionEpoch: UInt64 = 0,
        sceneDepthNormalizer: SceneDepthFrameNormalizer = SceneDepthFrameNormalizer()
    ) {
        self.session = session
        self.sceneDepthNormalizer = sceneDepthNormalizer
        self.frameIdentity = CaptureEventIdentitySequence(epoch: sessionEpoch)
        super.init()
    }

    func setCaptureEpoch(_ epoch: UInt64) {
        frameIdentity.transition(to: epoch)
    }

    func run(configuration requested: CaptureSessionRunConfiguration) throws -> CaptureSessionRunConfiguration {
        guard ARWorldTrackingConfiguration.isSupported else {
            throw SystemARSessionDriverError.unsupportedWorldTracking
        }

        let configuration = ARWorldTrackingConfiguration()
        if requested.depthSemantic == .sceneDepth {
            guard ARWorldTrackingConfiguration.supportsFrameSemantics(.sceneDepth) else {
                throw SystemARSessionDriverError.unsupportedSceneDepth
            }
            configuration.frameSemantics.insert(.sceneDepth)
        }
        if requested.meshOption == .mesh {
            guard ARWorldTrackingConfiguration.supportsSceneReconstruction(.mesh) else {
                throw SystemARSessionDriverError.unsupportedMeshReconstruction
            }
            configuration.sceneReconstruction = .mesh
        }

        session.delegate = self
        session.run(configuration)

        guard let effective = session.configuration as? ARWorldTrackingConfiguration else {
            throw SystemARSessionDriverError.effectiveConfigurationUnavailable
        }
        return CaptureSessionRunConfiguration(
            depthSemantic: effective.frameSemantics.contains(.sceneDepth) ? .sceneDepth : .none,
            meshOption: effective.sceneReconstruction == .mesh ? .mesh : .none
        )
    }

    func stop() {
        session.pause()
        session.delegate = nil
        enqueue(.stopBarrier)
    }

    deinit {
        session.pause()
        session.delegate = nil
    }

    nonisolated func session(_ session: ARSession, cameraDidChangeTrackingState camera: ARCamera) {
        let state: CaptureTrackingState
        switch camera.trackingState {
        case .normal:
            state = .normal
        case .notAvailable:
            state = .unavailable
        case .limited(let reason):
            let limitation: CaptureTrackingLimitation
            switch reason {
            case .initializing: limitation = .initializing
            case .excessiveMotion: limitation = .excessiveMotion
            case .insufficientFeatures: limitation = .insufficientFeatures
            case .relocalizing: limitation = .relocalizing
            @unknown default: limitation = .unknown
            }
            state = .limited(limitation)
        }
        enqueue(.session(.tracking(state)))
    }

    nonisolated func session(_ session: ARSession, didUpdate frame: ARFrame) {
        let identity = frameIdentity.next()
        let input = ARFrameSceneDepthAdapter.makeInput(
            frame: frame,
            sessionEpoch: identity.epoch,
            sequence: identity.sequence
        )
        let result = sceneDepthNormalizer.normalize(input)
        enqueue(.sceneDepth(result))
    }

    nonisolated func session(_ session: ARSession, didAdd anchors: [ARAnchor]) {
        ingestMeshAnchors(anchors, kind: .add)
    }

    nonisolated func session(_ session: ARSession, didUpdate anchors: [ARAnchor]) {
        ingestMeshAnchors(anchors, kind: .update)
    }

    nonisolated func session(_ session: ARSession, didRemove anchors: [ARAnchor]) {
        ingestMeshAnchors(anchors, kind: .remove)
    }

    nonisolated private func ingestMeshAnchors(_ anchors: [ARAnchor], kind: MeshAnchorEventKind) {
        for meshAnchor in anchors.compactMap({ $0 as? ARMeshAnchor }) {
            let identity = frameIdentity.next()
            let input = ARMeshAnchorLifecycleAdapter.makeInput(
                anchor: meshAnchor, kind: kind, sessionEpoch: identity.epoch,
                sequence: identity.sequence, monotonicTimestamp: ProcessInfo.processInfo.systemUptime
            )
            let priorRevision = meshAnchorStore.liveRevision(for: meshAnchor.identifier)
            let result = meshAnchorStore.apply(input)
            let acceptedEvent: OwnedMeshAnchorEvent?
            switch result {
            case .accepted(let revision?, _):
                acceptedEvent = OwnedMeshAnchorEvent(
                    kind: kind, anchorID: revision.anchorID,
                    anchorRevision: revision.revision, generation: revision.generation,
                    sessionEpoch: revision.sessionEpoch, sequence: revision.sequence,
                    monotonicTimestamp: revision.monotonicTimestamp,
                    worldFromAnchor: revision.worldFromAnchor, payload: revision.payload
                )
            case .accepted(nil, _):
                acceptedEvent = priorRevision.map { prior in
                    OwnedMeshAnchorEvent(
                        kind: .remove, anchorID: prior.anchorID,
                        anchorRevision: prior.revision, generation: prior.generation,
                        sessionEpoch: input.sessionEpoch, sequence: input.sequence,
                        monotonicTimestamp: input.monotonicTimestamp,
                        worldFromAnchor: input.worldFromAnchor, payload: nil
                    )
                }
            case .rejected:
                acceptedEvent = nil
            }
            let stamped = EpochStampedMeshLifecycleResult(
                identity: EpochObservationIdentity(
                    epoch: identity.epoch,
                    sequence: identity.sequence,
                    source: .meshAnchor
                ),
                eventKind: kind,
                anchorID: meshAnchor.identifier,
                acceptedEvent: acceptedEvent,
                lifecycleResult: result
            )
            enqueue(.mesh(stamped))
        }
    }

    nonisolated func session(_ session: ARSession, didFailWithError error: any Error) {
        enqueue(.session(.failed))
    }

    nonisolated func sessionWasInterrupted(_ session: ARSession) {
        enqueue(.session(.interrupted))
    }

    nonisolated func sessionInterruptionEnded(_ session: ARSession) {
        enqueue(.session(.interruptionEnded))
    }

    nonisolated private func enqueue(_ delivery: CaptureDriverDelivery) {
        deliveryMailbox.submit(delivery) { [weak self] delivery in
            self?.deliver(delivery)
        }
    }

    private func deliver(_ delivery: CaptureDriverDelivery) {
        switch delivery {
        case .session(let event):
            eventHandler?(event)
        case .sceneDepth(let result):
            sceneDepthIngestionHandler?(result)
        case .mesh(let stamped):
            if let epochStampedMeshLifecycleHandler {
                epochStampedMeshLifecycleHandler(stamped)
            } else {
                meshAnchorLifecycleHandler?(stamped.lifecycleResult)
            }
        case .ingressOverflow:
            eventHandler?(.failed)
        case .stopBarrier:
            eventHandler?(.stoppedAfterFlush)
            eventHandler = nil
            sceneDepthIngestionHandler = nil
            meshAnchorLifecycleHandler = nil
            epochStampedMeshLifecycleHandler = nil
        }
    }
}

/// Owns the capture-session lifecycle and records the configuration selected by
/// the session driver. RoomPlan is intentionally kept out of the runtime path
/// until the shared-session combination is physically verified.
@MainActor
final class ARSessionCoordinator: ObservableObject {
    @Published private(set) var phase: CaptureSessionPhase
    @Published private(set) var readiness: CaptureReadiness
    @Published private(set) var trackingState: CaptureTrackingState
    @Published private(set) var effectiveConfiguration: EffectiveCaptureConfigurationReport?
    @Published private(set) var sceneDepthIngestionCounters: SceneDepthIngestionCounters
    @Published private(set) var lastSceneDepthRejection: SceneDepthFrameRejection?
    @Published private(set) var meshAnchorLifecycleCounters: MeshAnchorLifecycleCounters
    @Published private(set) var lastMeshAnchorRejection: MeshAnchorLifecycleRejection?
    @Published private(set) var epochRelocalization: EpochRelocalizationSnapshot
    @Published private(set) var lastEpochObservationDisposition: EpochObservationDisposition?

    private let readinessProvider: any CaptureReadinessProviding
    private let permissionRequester: any CameraPermissionRequesting
    private let supportProvider: any CaptureFeatureSupportProviding
    private let sessionDriver: any CaptureSessionRunning
    private let normalizedFrameSink: (NormalizedSceneDepthFrame) -> Void
    private let meshAnchorSink: (OwnedMeshAnchorEvent) -> Void
    private let epochControlSink: (CaptureEpochControl) -> Void
    private let epochPolicy: EpochRelocalizationPolicy
    private var activeConfiguration: CaptureSessionRunConfiguration?
    private var fallbackAttempted = false
    private var hasAcceptedSessionRun = false
    private var nextEpochPolicyEventSequence: UInt64 = 0
    private var firstCaptureEpoch: UInt64?
    private var firstEpochAccepted = false

    init(
        readinessProvider: any CaptureReadinessProviding = CaptureReadinessProvider(),
        permissionRequester: any CameraPermissionRequesting = SystemCameraPermissionRequester(),
        supportProvider: any CaptureFeatureSupportProviding = SystemCaptureFeatureSupportProvider(),
        sessionDriver: any CaptureSessionRunning = SystemARSessionDriver(),
        epochPolicy: EpochRelocalizationPolicy = EpochRelocalizationPolicy(),
        normalizedFrameSink: @escaping (NormalizedSceneDepthFrame) -> Void = { _ in },
        meshAnchorSink: @escaping (OwnedMeshAnchorEvent) -> Void = { _ in },
        epochControlSink: @escaping (CaptureEpochControl) -> Void = { _ in }
    ) {
        self.readinessProvider = readinessProvider
        self.permissionRequester = permissionRequester
        self.supportProvider = supportProvider
        self.sessionDriver = sessionDriver
        self.epochPolicy = epochPolicy
        self.normalizedFrameSink = normalizedFrameSink
        self.meshAnchorSink = meshAnchorSink
        self.epochControlSink = epochControlSink
        let baseline = readinessProvider.currentReadiness()
        let initialTracking = CaptureTrackingState.notEstablished
        self.readiness = CaptureReadiness(
            cameraPermission: permissionRequester.authorizationStatus(),
            supportsRequiredARFeatures: baseline.supportsRequiredARFeatures,
            hasUsableStorage: baseline.hasUsableStorage,
            initialTracking: .notEstablished
        )
        self.trackingState = initialTracking
        self.phase = .idle
        self.sceneDepthIngestionCounters = .zero
        self.lastSceneDepthRejection = nil
        self.meshAnchorLifecycleCounters = .zero
        self.lastMeshAnchorRejection = nil
        self.epochRelocalization = epochPolicy.snapshot()
        self.lastEpochObservationDisposition = nil
        installSessionHandlers()
    }

    var canRequestCameraPermission: Bool {
        readiness.cameraPermission == .notDetermined
    }

    var canStartSession: Bool {
        readiness.cameraPermission == .authorized && readiness.hasUsableStorage
    }

    var isSessionRunning: Bool {
        activeConfiguration != nil
    }

    /// The one session used for capture and camera preview. Consumers may
    /// render it but must not change its delegate or configuration.
    var captureSession: ARSession? { sessionDriver.captureSession }

    /// This is the sole camera permission request path and is called only from
    /// the explicit UI action in ReadinessView.
    func requestCameraPermission() async {
        guard canRequestCameraPermission else {
            refreshReadiness()
            return
        }
        phase = .requestingPermission
        let status = await permissionRequester.requestAccess()
        refreshReadiness(permissionOverride: status)
        if status == .authorized {
            phase = .idle
        } else {
            phase = .blocked(permissionBlockMessage(for: status))
        }
    }

    func startSession() {
        guard activeConfiguration == nil else { return }
        refreshReadiness()
        guard readiness.cameraPermission == .authorized else {
            phase = .blocked(permissionBlockMessage(for: readiness.cameraPermission))
            return
        }
        guard readiness.hasUsableStorage else {
            phase = .blocked("Usable local storage is required before starting capture.")
            return
        }

        let support = supportProvider.currentSupport()
        guard support.worldTracking else {
            recordReport(support: support, chosen: .arkitFallback, fallbackReason: "ARKit world tracking is unavailable on this device.")
            phase = .blocked("ARKit world tracking is unavailable on this device.")
            return
        }

        installSessionHandlers()
        fallbackAttempted = false
        phase = .starting

        let b003AllowsRequestedConfiguration = readiness.supportsRequiredARFeatures
            && support.sceneDepth
            && support.meshReconstruction
        if b003AllowsRequestedConfiguration {
            do {
                let effective = try sessionDriver.run(configuration: .requested)
                if effective == .requested {
                    activeConfiguration = effective
                    recordAcceptedSessionRun()
                    refreshReadiness()
                    recordReport(support: support, chosen: effective, fallbackReason: nil)
                    phase = .awaitingNormalTracking
                } else {
                    startFallback(support: support, reason: "ARKit did not apply the requested sceneDepth and mesh configuration.")
                }
            } catch {
                startFallback(support: support, reason: "The requested sceneDepth and mesh configuration could not start.")
            }
        } else {
            let reason = fallbackReason(readinessAllowsFeatures: readiness.supportsRequiredARFeatures, support: support)
            startFallback(support: support, reason: reason)
        }
    }

    func stopSession() {
        sessionDriver.stop()
        guard sessionDriver.deliversStopAfterFlush else {
            finishStoppedSession()
            return
        }
    }

    private func finishStoppedSession() {
        advanceEpochPolicy(.sessionStopped)
        activeConfiguration = nil
        trackingState = .notEstablished
        refreshReadiness()
        phase = .stopped
    }

    private func startFallback(support: CaptureFeatureSupport, reason: String) {
        fallbackAttempted = true
        do {
            let effective = try sessionDriver.run(configuration: .arkitFallback)
            guard effective == .arkitFallback else {
                throw SystemARSessionDriverError.effectiveConfigurationUnavailable
            }
            activeConfiguration = effective
            recordAcceptedSessionRun()
            refreshReadiness()
            recordReport(support: support, chosen: effective, fallbackReason: reason)
            phase = .fallback(reason)
        } catch {
            activeConfiguration = nil
            recordReport(support: support, chosen: .arkitFallback, fallbackReason: "\(reason) The ARKit fallback also failed to start.")
            phase = .failed("The ARKit fallback could not start. Capture is unavailable.")
        }
    }

    private func handle(_ event: CaptureSessionEvent) {
        switch event {
        case .tracking(let state):
            advanceEpochPolicy(epochEvent(for: state))
            trackingState = state
            refreshReadiness()
            switch state {
            case .normal:
                if let reason = effectiveConfiguration?.fallbackReason {
                    phase = .fallback(reason)
                } else {
                    phase = .captureReady
                }
            case .limited(let limitation):
                phase = .limited(limitation.message)
            case .unavailable:
                phase = .limited("ARKit tracking is unavailable. Metric capture is paused.")
            case .notEstablished:
                phase = .awaitingNormalTracking
            }
        case .failed:
            advanceEpochPolicy(.sessionFailure)
            let support = supportProvider.currentSupport()
            if activeConfiguration == .requested && !fallbackAttempted {
                startFallback(support: support, reason: "The requested ARKit configuration failed while running.")
            } else {
                sessionDriver.stop()
                activeConfiguration = nil
                phase = .failed("The ARKit session failed. Capture is unavailable until it is restarted.")
            }
        case .interrupted:
            advanceEpochPolicy(.interruptionBegan)
            trackingState = .unavailable
            refreshReadiness()
            phase = .interrupted
        case .interruptionEnded:
            advanceEpochPolicy(.interruptionEnded)
            trackingState = .notEstablished
            refreshReadiness()
            phase = .awaitingNormalTracking
        case .stoppedAfterFlush:
            finishStoppedSession()
        }
    }

    private func handle(_ result: SceneDepthIngestionResult) {
        switch result {
        case .accepted(let frame, let counters):
            sceneDepthIngestionCounters = counters
            lastSceneDepthRejection = nil
            let disposition = epochPolicy.authorizeObservation(EpochObservationIdentity(
                epoch: frame.identity.sessionEpoch,
                sequence: frame.identity.sequence,
                source: .sceneDepth
            ))
            lastEpochObservationDisposition = disposition
            epochRelocalization = epochPolicy.snapshot()
            if case .accepted = disposition {
                normalizedFrameSink(frame)
            }
        case .rejected(let reason, let counters):
            sceneDepthIngestionCounters = counters
            lastSceneDepthRejection = reason
        }
    }

    private func handle(_ result: MeshAnchorLifecycleResult) {
        switch result {
        case .accepted(_, let counters):
            meshAnchorLifecycleCounters = counters
            lastMeshAnchorRejection = nil
            lastEpochObservationDisposition = epochPolicy.noteMissingObservationIdentity()
            epochRelocalization = epochPolicy.snapshot()
        case .rejected(let reason, let counters):
            meshAnchorLifecycleCounters = counters
            lastMeshAnchorRejection = reason
        }
    }

    private func handle(_ stamped: EpochStampedMeshLifecycleResult) {
        switch stamped.lifecycleResult {
        case .accepted(_, let counters):
            meshAnchorLifecycleCounters = counters
            lastMeshAnchorRejection = nil
            let disposition = epochPolicy.authorizeObservation(stamped.identity)
            lastEpochObservationDisposition = disposition
            epochRelocalization = epochPolicy.snapshot()
            if case .accepted = disposition, let acceptedEvent = stamped.acceptedEvent {
                meshAnchorSink(acceptedEvent)
            }
        case .rejected(let reason, let counters):
            meshAnchorLifecycleCounters = counters
            lastMeshAnchorRejection = reason
        }
    }

    private func installSessionHandlers() {
        sessionDriver.eventHandler = { [weak self] event in self?.handle(event) }
        sessionDriver.sceneDepthIngestionHandler = { [weak self] result in self?.handle(result) }
        sessionDriver.meshAnchorLifecycleHandler = { [weak self] result in self?.handle(result) }
        sessionDriver.epochStampedMeshLifecycleHandler = { [weak self] result in self?.handle(result) }
    }

    /// Called only by the single session owner when a run option explicitly
    /// resets tracking. It creates an epoch boundary before later callbacks.
    func recordTrackingReset() {
        advanceEpochPolicy(.sessionReset)
    }

    func evaluateEpochRelationship(_ proposal: EpochRelationshipProposal) -> EpochTransformDecision {
        let decision = epochPolicy.evaluateRelationship(proposal)
        epochRelocalization = epochPolicy.snapshot()
        return decision
    }

    func authorizeEpochUse(
        from sourceEpoch: UInt64,
        to targetEpoch: UInt64,
        purpose: EpochUsePurpose
    ) -> EpochUseDecision {
        let decision = epochPolicy.authorizeUse(from: sourceEpoch, to: targetEpoch, purpose: purpose)
        epochRelocalization = epochPolicy.snapshot()
        return decision
    }

    private func recordAcceptedSessionRun() {
        advanceEpochPolicy(hasAcceptedSessionRun ? .sessionRestartAccepted : .sessionRunAccepted)
        hasAcceptedSessionRun = true
    }

    private func advanceEpochPolicy(_ event: EpochPolicyEvent) {
        let sequence = nextEpochPolicyEventSequence
        if nextEpochPolicyEventSequence != UInt64.max {
            nextEpochPolicyEventSequence += 1
        }
        let priorEpoch = epochRelocalization.currentEpoch
        switch epochPolicy.apply(event, sequence: sequence) {
        case .accepted(let snapshot), .rejected(_, let snapshot):
            epochRelocalization = snapshot
        }
        if let currentEpoch = epochRelocalization.currentEpoch, currentEpoch != priorEpoch {
            sessionDriver.setCaptureEpoch(currentEpoch)
            if firstCaptureEpoch == nil { firstCaptureEpoch = currentEpoch }
            epochControlSink(CaptureEpochControl(
                sessionEpoch: currentEpoch,
                relationshipState: .unresolved,
                projectFromWorld: Self.identityTransform,
                transformRevision: 1
            ))
        }
        if event == .trackingNormal,
           let currentEpoch = epochRelocalization.currentEpoch,
           currentEpoch == firstCaptureEpoch,
           !firstEpochAccepted {
            firstEpochAccepted = true
            epochControlSink(CaptureEpochControl(
                sessionEpoch: currentEpoch,
                relationshipState: .accepted,
                projectFromWorld: Self.identityTransform,
                transformRevision: 2
            ))
        }
    }

    private static let identityTransform: [Double] = [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    ]

    private func epochEvent(for state: CaptureTrackingState) -> EpochPolicyEvent {
        switch state {
        case .notEstablished:
            return .interruptionEnded
        case .normal:
            return .trackingNormal
        case .unavailable:
            return .trackingUnavailable
        case .limited(let limitation):
            let epochLimitation: EpochTrackingLimitation
            switch limitation {
            case .initializing: epochLimitation = .initializing
            case .excessiveMotion: epochLimitation = .excessiveMotion
            case .insufficientFeatures: epochLimitation = .insufficientFeatures
            case .relocalizing: epochLimitation = .relocalizing
            case .unknown: epochLimitation = .unknown
            }
            return .trackingLimited(epochLimitation)
        }
    }

    private func refreshReadiness(permissionOverride: CameraPermissionState? = nil) {
        let baseline = readinessProvider.currentReadiness()
        let initialTracking: InitialTrackingState
        switch trackingState {
        case .normal: initialTracking = .normal
        case .limited: initialTracking = .limited
        case .unavailable: initialTracking = .unavailable
        case .notEstablished: initialTracking = .notEstablished
        }
        readiness = CaptureReadiness(
            cameraPermission: permissionOverride ?? permissionRequester.authorizationStatus(),
            supportsRequiredARFeatures: baseline.supportsRequiredARFeatures
                && (activeConfiguration == nil || activeConfiguration == .requested),
            hasUsableStorage: baseline.hasUsableStorage,
            initialTracking: initialTracking
        )
    }

    private func recordReport(
        support: CaptureFeatureSupport,
        chosen: CaptureSessionRunConfiguration,
        fallbackReason: String?
    ) {
        effectiveConfiguration = EffectiveCaptureConfigurationReport(
            requestedDepthSemantic: .sceneDepth,
            sceneDepthSupported: support.sceneDepth,
            chosenDepthSemantic: chosen.depthSemantic,
            requestedMeshOption: .mesh,
            meshReconstructionSupported: support.meshReconstruction,
            chosenMeshOption: chosen.meshOption,
            worldTrackingSupported: support.worldTracking,
            roomPlanIntegration: .notEnabledPendingPhysicalValidation,
            proposalsAreReduced: fallbackReason != nil,
            fallbackReason: fallbackReason
        )
    }

    private func fallbackReason(readinessAllowsFeatures: Bool, support: CaptureFeatureSupport) -> String {
        if !readinessAllowsFeatures {
            return "The B-003 capability gate did not approve sceneDepth plus mesh reconstruction."
        }
        if !support.sceneDepth && !support.meshReconstruction {
            return "Scene depth and mesh reconstruction are unsupported."
        }
        if !support.sceneDepth {
            return "Scene depth is unsupported."
        }
        return "Mesh reconstruction is unsupported."
    }

    private func permissionBlockMessage(for status: CameraPermissionState) -> String {
        switch status {
        case .authorized: "Camera access is authorized."
        case .denied: "Camera access is denied. Allow it in Settings before starting capture."
        case .restricted: "Camera access is restricted on this device."
        case .notDetermined: "Use the Allow Camera button before starting capture."
        }
    }
}

import XCTest
@testable import HybridSpatialScanner

final class EpochRelocalizationTests: XCTestCase {
    func testRelocalizationCreatesOneBoundaryAndSuppressesStaleObservations() {
        let policy = EpochRelocalizationPolicy()

        XCTAssertEqual(accepted(policy.apply(.sessionRunAccepted, sequence: 0)).currentEpoch, 1)
        XCTAssertFalse(policy.snapshot().precisionAvailable)
        XCTAssertTrue(accepted(policy.apply(.trackingNormal, sequence: 1)).precisionAvailable)
        assertObservationAccepted(policy.authorizeObservation(identity(epoch: 1, sequence: 0, source: .sceneDepth)))

        let relocalizing = accepted(policy.apply(.trackingLimited(.relocalizing), sequence: 2))
        XCTAssertEqual(relocalizing.currentEpoch, 2)
        XCTAssertFalse(relocalizing.precisionAvailable)
        XCTAssertTrue(relocalizing.continuityIncidentOpen)

        let repeatedLoss = accepted(policy.apply(.trackingUnavailable, sequence: 3))
        XCTAssertEqual(repeatedLoss.currentEpoch, 2, "One continuity incident must not churn epochs on repeated degraded callbacks.")
        assertObservationRejected(.staleEpoch, policy.authorizeObservation(identity(epoch: 1, sequence: 1, source: .meshAnchor)))
        assertObservationRejected(.precisionUnavailable, policy.authorizeObservation(identity(epoch: 2, sequence: 0, source: .sceneDepth)))

        XCTAssertTrue(accepted(policy.apply(.trackingNormal, sequence: 4)).precisionAvailable)
        assertObservationAccepted(policy.authorizeObservation(identity(epoch: 2, sequence: 1, source: .meshAnchor)))

        let counters = policy.snapshot().counters
        XCTAssertEqual(counters.epochsCreated, 2)
        XCTAssertEqual(counters.transitionsByReason[.limitedRelocalizing], 1)
        XCTAssertEqual(counters.suppressionSignalsByReason[.trackingUnavailable], 1)
        XCTAssertEqual(counters.observationRejections[.staleEpoch], 1)
        XCTAssertEqual(counters.observationRejections[.precisionUnavailable], 1)
    }

    func testResetRestartFailureAndInterruptionHaveDeterministicEpochs() {
        let policy = EpochRelocalizationPolicy()
        _ = accepted(policy.apply(.sessionRunAccepted, sequence: 0))
        _ = accepted(policy.apply(.trackingNormal, sequence: 1))

        XCTAssertEqual(accepted(policy.apply(.interruptionBegan, sequence: 2)).currentEpoch, 2)
        XCTAssertEqual(accepted(policy.apply(.interruptionEnded, sequence: 3)).currentEpoch, 2)
        XCTAssertEqual(accepted(policy.apply(.sessionReset, sequence: 4)).currentEpoch, 3)
        XCTAssertEqual(accepted(policy.apply(.sessionRestartAccepted, sequence: 5)).currentEpoch, 4)
        XCTAssertEqual(accepted(policy.apply(.sessionFailure, sequence: 6)).currentEpoch, 5)
        XCTAssertFalse(policy.snapshot().precisionAvailable)

        let stopped = accepted(policy.apply(.sessionStopped, sequence: 7))
        XCTAssertEqual(stopped.currentEpoch, 5)
        XCTAssertEqual(stopped.epochRecords.map(\.reason), [
            .sessionRun, .interruption, .reset, .sessionRestart, .failure,
        ])
    }

    func testAcceptedTransformRequiresExplicitNondegenerateCorrespondenceAndEnablesUse() {
        let policy = twoEpochPolicy()
        assertUseRejected(.missingAcceptedTransform, policy.authorizeUse(from: 1, to: 2, purpose: .preciseOverlay))

        let proposal = relationshipProposal(sequence: 10)
        guard case .accepted(let relationship) = policy.evaluateRelationship(proposal) else {
            return XCTFail("Expected an accepted explicit epoch relationship")
        }
        XCTAssertEqual(relationship.version, 1)
        XCTAssertEqual(relationship.maximumResidualMeters, 0, accuracy: 0.000_001)
        XCTAssertTrue(relationship.sourceObservationsRemainAuthoritative)
        XCTAssertEqual(relationship.proposal.proposalID, proposal.proposalID)
        XCTAssertEqual(relationship.proposal.correspondences, proposal.correspondences)

        guard case .allowed(let usedRelationship?) = policy.authorizeUse(
            from: 2,
            to: 1,
            purpose: .metricCommit
        ) else {
            return XCTFail("An accepted explicit relation should authorize either epoch direction")
        }
        XCTAssertEqual(usedRelationship.proposal.proposalID, proposal.proposalID)
    }

    func testInvalidTransformsAndCorrespondencesCannotAuthorizeCrossEpochUse() {
        let policy = twoEpochPolicy()
        var reflection = identityMatrix
        reflection[0] = -1
        assertTransformRejected(.invalidTransform, policy.evaluateRelationship(
            relationshipProposal(sequence: 1, transform: reflection)
        ))

        let collinear = [
            correspondence(source: SIMD3(0, 0, 0)),
            correspondence(source: SIMD3(1, 0, 0)),
            correspondence(source: SIMD3(2, 0, 0)),
        ]
        assertTransformRejected(.degenerateCorrespondences, policy.evaluateRelationship(
            relationshipProposal(sequence: 2, correspondences: collinear)
        ))

        var mismatched = validCorrespondences
        mismatched[2] = EpochCorrespondence(
            sourceID: mismatched[2].sourceID,
            targetID: mismatched[2].targetID,
            sourcePoint: mismatched[2].sourcePoint,
            targetPoint: mismatched[2].targetPoint + SIMD3(0.20, 0, 0)
        )
        assertTransformRejected(.correspondenceResidualExceeded, policy.evaluateRelationship(
            relationshipProposal(sequence: 3, correspondences: mismatched)
        ))
        assertUseRejected(.missingAcceptedTransform, policy.authorizeUse(from: 1, to: 2, purpose: .metricCommit))
    }

    func testPolicyObservationAndProposalOrderingAreStrict() {
        let policy = EpochRelocalizationPolicy()
        _ = accepted(policy.apply(.sessionRunAccepted, sequence: 20))
        guard case .rejected(.nonMonotonicSequence, let unchanged) = policy.apply(.trackingNormal, sequence: 19) else {
            return XCTFail("Out-of-order policy event should be rejected")
        }
        XCTAssertFalse(unchanged.precisionAvailable)
        _ = accepted(policy.apply(.trackingNormal, sequence: 21))

        assertObservationAccepted(policy.authorizeObservation(identity(epoch: 1, sequence: 5, source: .sceneDepth)))
        assertObservationRejected(.nonMonotonicSequence, policy.authorizeObservation(identity(epoch: 1, sequence: 4, source: .meshAnchor)))

        _ = accepted(policy.apply(.trackingLimited(.relocalizing), sequence: 22))
        _ = accepted(policy.apply(.trackingNormal, sequence: 23))
        assertTransformRejected(.invalidTransform, policy.evaluateRelationship(
            relationshipProposal(sequence: 50, transform: Array(repeating: 0, count: 16))
        ))
        assertTransformRejected(.nonMonotonicSequence, policy.evaluateRelationship(
            relationshipProposal(sequence: 49)
        ))
    }

    func testCaptureIdentitySequenceResetsWithinNewEpochAndSharesOrdering() {
        let sequence = CaptureEventIdentitySequence(epoch: 7)
        XCTAssertEqual(sequence.next().epoch, 7)
        XCTAssertEqual(sequence.next().sequence, 1)
        sequence.transition(to: 8)
        XCTAssertEqual(sequence.next().epoch, 8)
        XCTAssertEqual(sequence.next().sequence, 1)
    }

    @MainActor
    func testCoordinatorPropagatesEpochBoundariesToOwnedDriver() {
        let driver = EpochStubSessionDriver()
        let coordinator = ARSessionCoordinator(
            readinessProvider: CaptureReadinessProvider(platform: EpochStubPlatform()),
            permissionRequester: EpochStubPermissionRequester(),
            supportProvider: EpochStubSupportProvider(),
            sessionDriver: driver
        )

        coordinator.startSession()
        XCTAssertEqual(driver.assignedEpochs, [1])
        driver.send(.tracking(.normal))
        XCTAssertTrue(coordinator.epochRelocalization.precisionAvailable)

        driver.send(.tracking(.limited(.relocalizing)))
        XCTAssertEqual(driver.assignedEpochs, [1, 2])
        XCTAssertFalse(coordinator.epochRelocalization.precisionAvailable)

        coordinator.recordTrackingReset()
        XCTAssertEqual(driver.assignedEpochs, [1, 2, 3])
        coordinator.stopSession()
        coordinator.startSession()
        XCTAssertEqual(driver.assignedEpochs, [1, 2, 3, 4])
        XCTAssertEqual(coordinator.epochRelocalization.lastTransitionReason, .sessionRestart)
    }

    private func twoEpochPolicy() -> EpochRelocalizationPolicy {
        let policy = EpochRelocalizationPolicy()
        _ = accepted(policy.apply(.sessionRunAccepted, sequence: 0))
        _ = accepted(policy.apply(.trackingNormal, sequence: 1))
        _ = accepted(policy.apply(.trackingLimited(.relocalizing), sequence: 2))
        _ = accepted(policy.apply(.trackingNormal, sequence: 3))
        return policy
    }

    private func relationshipProposal(
        sequence: UInt64,
        transform: [Float]? = nil,
        correspondences: [EpochCorrespondence]? = nil
    ) -> EpochRelationshipProposal {
        EpochRelationshipProposal(
            proposalID: UUID(),
            proposalSequence: sequence,
            sourceEpoch: 1,
            targetEpoch: 2,
            targetFromSource: transform ?? translationMatrix,
            correspondences: correspondences ?? validCorrespondences
        )
    }

    private var validCorrespondences: [EpochCorrespondence] {
        [
            correspondence(source: SIMD3(0, 0, 0)),
            correspondence(source: SIMD3(1, 0, 0)),
            correspondence(source: SIMD3(0, 1, 0)),
            correspondence(source: SIMD3(0, 0, 1)),
        ]
    }

    private func correspondence(source: SIMD3<Float>) -> EpochCorrespondence {
        EpochCorrespondence(
            sourceID: UUID(),
            targetID: UUID(),
            sourcePoint: source,
            targetPoint: source + SIMD3(1, 2, 3)
        )
    }

    private var identityMatrix: [Float] {
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    }

    private var translationMatrix: [Float] {
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1]
    }

    private func identity(epoch: UInt64, sequence: UInt64, source: EpochObservationSource) -> EpochObservationIdentity {
        EpochObservationIdentity(epoch: epoch, sequence: sequence, source: source)
    }

    private func accepted(
        _ decision: EpochPolicyEventDecision,
        file: StaticString = #filePath,
        line: UInt = #line
    ) -> EpochRelocalizationSnapshot {
        guard case .accepted(let snapshot) = decision else {
            XCTFail("Expected accepted policy event", file: file, line: line)
            fatalError("unreachable")
        }
        return snapshot
    }

    private func assertObservationAccepted(
        _ disposition: EpochObservationDisposition,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard case .accepted = disposition else {
            return XCTFail("Expected accepted observation", file: file, line: line)
        }
    }

    private func assertObservationRejected(
        _ expected: EpochObservationRejection,
        _ disposition: EpochObservationDisposition,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard case .retainedSourceSuppressed(_, let actual) = disposition else {
            return XCTFail("Expected suppressed observation", file: file, line: line)
        }
        XCTAssertEqual(actual, expected, file: file, line: line)
    }

    private func assertTransformRejected(
        _ expected: EpochTransformRejection,
        _ decision: EpochTransformDecision,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard case .rejected(let actual) = decision else {
            return XCTFail("Expected rejected transform", file: file, line: line)
        }
        XCTAssertEqual(actual, expected, file: file, line: line)
    }

    private func assertUseRejected(
        _ expected: EpochUseRejection,
        _ decision: EpochUseDecision,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard case .rejected(let actual) = decision else {
            return XCTFail("Expected rejected cross-epoch use", file: file, line: line)
        }
        XCTAssertEqual(actual, expected, file: file, line: line)
    }
}

private struct EpochStubPlatform: CapturePlatformInspecting {
    func cameraPermission() -> CameraPermissionState { .authorized }
    func supportsRequiredARFeatures() -> Bool { true }
    func hasUsableStorage() -> Bool { true }
    func initialTrackingState() -> InitialTrackingState { .notEstablished }
}

@MainActor
private struct EpochStubPermissionRequester: CameraPermissionRequesting {
    func authorizationStatus() -> CameraPermissionState { .authorized }
    func requestAccess() async -> CameraPermissionState { .authorized }
}

@MainActor
private struct EpochStubSupportProvider: CaptureFeatureSupportProviding {
    func currentSupport() -> CaptureFeatureSupport {
        CaptureFeatureSupport(worldTracking: true, sceneDepth: true, meshReconstruction: true)
    }
}

@MainActor
private final class EpochStubSessionDriver: CaptureSessionRunning {
    var eventHandler: ((CaptureSessionEvent) -> Void)?
    var sceneDepthIngestionHandler: ((SceneDepthIngestionResult) -> Void)?
    var meshAnchorLifecycleHandler: ((MeshAnchorLifecycleResult) -> Void)?
    var epochStampedMeshLifecycleHandler: ((EpochStampedMeshLifecycleResult) -> Void)?
    private(set) var assignedEpochs: [UInt64] = []

    func run(configuration: CaptureSessionRunConfiguration) throws -> CaptureSessionRunConfiguration {
        configuration
    }

    func setCaptureEpoch(_ epoch: UInt64) {
        assignedEpochs.append(epoch)
    }

    func stop() {
        eventHandler = nil
        sceneDepthIngestionHandler = nil
        meshAnchorLifecycleHandler = nil
        epochStampedMeshLifecycleHandler = nil
    }

    func send(_ event: CaptureSessionEvent) {
        eventHandler?(event)
    }
}

import Foundation
import simd

enum EpochTrackingLimitation: String, CaseIterable, Equatable, Hashable, Sendable {
    case initializing
    case excessiveMotion
    case insufficientFeatures
    case relocalizing
    case unknown
}

enum EpochPolicyEvent: Equatable, Sendable {
    case sessionRunAccepted
    case sessionRestartAccepted
    case trackingNormal
    case trackingLimited(EpochTrackingLimitation)
    case trackingUnavailable
    case interruptionBegan
    case interruptionEnded
    case sessionReset
    case sessionFailure
    case sessionStopped
}

enum EpochTransitionReason: String, CaseIterable, Equatable, Hashable, Sendable {
    case sessionRun
    case sessionRestart
    case limitedInitializing
    case limitedExcessiveMotion
    case limitedInsufficientFeatures
    case limitedRelocalizing
    case limitedUnknown
    case trackingUnavailable
    case interruption
    case reset
    case failure
}

enum EpochPolicyEventRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case nonMonotonicSequence
    case epochIdentityExhausted
}

struct CaptureEpochRecord: Equatable, Sendable {
    let epoch: UInt64
    let previousEpoch: UInt64?
    let reason: EpochTransitionReason
    let policyEventSequence: UInt64
}

enum CaptureEpochRelationshipState: UInt32, Equatable, Sendable {
    case unresolved = 0
    case accepted = 1
}

struct CaptureEpochControl: Equatable, Sendable {
    let sessionEpoch: UInt64
    let relationshipState: CaptureEpochRelationshipState
    /// Column-major T_project_world. It is identity only for the explicitly
    /// accepted first capture epoch; unresolved epochs cannot contribute
    /// project geometry until a validated relationship is supplied.
    let projectFromWorld: [Double]
    let transformRevision: UInt64
}

enum EpochObservationSource: String, Equatable, Sendable {
    case sceneDepth
    case meshAnchor
}

struct EpochObservationIdentity: Equatable, Sendable {
    let epoch: UInt64
    let sequence: UInt64
    let source: EpochObservationSource
}

enum EpochObservationRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case missingEpochIdentity
    case unknownEpoch
    case staleEpoch
    case precisionUnavailable
    case nonMonotonicSequence
}

enum EpochObservationDisposition: Equatable, Sendable {
    case accepted(EpochObservationIdentity)
    /// The original source payload remains owned by its ingestion/lifecycle
    /// layer. This guard suppresses only precision, overlay, and metric use.
    case retainedSourceSuppressed(EpochObservationIdentity?, EpochObservationRejection)
}

enum EpochUsePurpose: String, Equatable, Sendable {
    case preciseOverlay
    case metricCommit
}

enum EpochUseRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case unknownEpoch
    case precisionUnavailable
    case missingAcceptedTransform
}

struct EpochCorrespondence: Equatable, Sendable {
    let sourceID: UUID
    let targetID: UUID
    let sourcePoint: SIMD3<Float>
    let targetPoint: SIMD3<Float>
}

struct EpochRelationshipProposal: Equatable, Sendable {
    let proposalID: UUID
    let proposalSequence: UInt64
    let sourceEpoch: UInt64
    let targetEpoch: UInt64
    /// Caller-supplied, column-major rigid T_targetWorld_sourceWorld.
    let targetFromSource: [Float]
    /// Explicit source/target evidence retained verbatim for auditability.
    let correspondences: [EpochCorrespondence]
}

enum EpochTransformRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case nonMonotonicSequence
    case unknownEpoch
    case sameEpoch
    case invalidTransform
    case insufficientCorrespondences
    case duplicateCorrespondence
    case nonFiniteCorrespondence
    case degenerateCorrespondences
    case correspondenceResidualExceeded
}

struct AcceptedEpochRelationship: Equatable, Sendable {
    let proposal: EpochRelationshipProposal
    let version: UInt64
    let maximumResidualMeters: Float
    /// This policy validates and records a relation; it never rewrites source
    /// observations or manufactures transformed geometry.
    let sourceObservationsRemainAuthoritative: Bool
}

enum EpochTransformDecision: Equatable, Sendable {
    case accepted(AcceptedEpochRelationship)
    case rejected(EpochTransformRejection)
}

enum EpochUseDecision: Equatable, Sendable {
    case allowed(AcceptedEpochRelationship?)
    case rejected(EpochUseRejection)
}

struct EpochRelocalizationCounters: Equatable, Sendable {
    var epochsCreated: UInt64 = 0
    var policyEventsRejected: UInt64 = 0
    var transitionsByReason: [EpochTransitionReason: UInt64] = [:]
    var suppressionSignalsByReason: [EpochTransitionReason: UInt64] = [:]
    var observationsAccepted: UInt64 = 0
    var observationsSuppressed: UInt64 = 0
    var observationRejections: [EpochObservationRejection: UInt64] = [:]
    var transformsAccepted: UInt64 = 0
    var transformsRejected: UInt64 = 0
    var transformRejections: [EpochTransformRejection: UInt64] = [:]
    var crossEpochUsesAllowed: UInt64 = 0
    var crossEpochUsesRejected: UInt64 = 0
    var useRejections: [EpochUseRejection: UInt64] = [:]
}

struct EpochRelocalizationSnapshot: Equatable, Sendable {
    let currentEpoch: UInt64?
    let precisionAvailable: Bool
    let continuityIncidentOpen: Bool
    let lastTransitionReason: EpochTransitionReason?
    let epochRecords: [CaptureEpochRecord]
    let acceptedRelationships: [AcceptedEpochRelationship]
    let counters: EpochRelocalizationCounters
}

enum EpochPolicyEventDecision: Equatable, Sendable {
    case accepted(EpochRelocalizationSnapshot)
    case rejected(EpochPolicyEventRejection, EpochRelocalizationSnapshot)
}

struct EpochStampedMeshLifecycleResult: Sendable {
    let identity: EpochObservationIdentity
    let eventKind: MeshAnchorEventKind
    let anchorID: UUID
    let acceptedEvent: OwnedMeshAnchorEvent?
    let lifecycleResult: MeshAnchorLifecycleResult
}

/// Lock-protected, platform-neutral policy for capture-coordinate continuity.
/// Its inputs are synthetic-testable and it has no ARKit, geometry, storage,
/// renderer, or ABI ownership.
final class EpochRelocalizationPolicy: @unchecked Sendable {
    private struct EpochPair: Hashable {
        let low: UInt64
        let high: UInt64

        init(_ lhs: UInt64, _ rhs: UInt64) {
            low = min(lhs, rhs)
            high = max(lhs, rhs)
        }
    }

    private let lock = NSLock()
    private let minimumCorrespondenceCount: Int
    private let maximumCorrespondenceResidualMeters: Float
    private let degeneracyAreaToleranceSquareMeters: Float

    private var nextEpoch: UInt64 = 1
    private var currentEpoch: UInt64?
    private var precisionAvailable = false
    private var continuityIncidentOpen = false
    private var sessionActive = false
    private var lastPolicyEventSequence: UInt64?
    private var lastRelationshipProposalSequence: UInt64?
    private var lastObservationSequenceByEpoch: [UInt64: UInt64] = [:]
    private var epochRecords: [CaptureEpochRecord] = []
    private var relationships: [EpochPair: AcceptedEpochRelationship] = [:]
    private var counters = EpochRelocalizationCounters()
    private var lastTransitionReason: EpochTransitionReason?

    init(
        minimumCorrespondenceCount: Int = 3,
        maximumCorrespondenceResidualMeters: Float = 0.03,
        degeneracyAreaToleranceSquareMeters: Float = 0.000_001
    ) {
        precondition(minimumCorrespondenceCount >= 3)
        precondition(maximumCorrespondenceResidualMeters.isFinite && maximumCorrespondenceResidualMeters > 0)
        precondition(degeneracyAreaToleranceSquareMeters.isFinite && degeneracyAreaToleranceSquareMeters > 0)
        self.minimumCorrespondenceCount = minimumCorrespondenceCount
        self.maximumCorrespondenceResidualMeters = maximumCorrespondenceResidualMeters
        self.degeneracyAreaToleranceSquareMeters = degeneracyAreaToleranceSquareMeters
    }

    func apply(_ event: EpochPolicyEvent, sequence: UInt64) -> EpochPolicyEventDecision {
        lock.lock()
        defer { lock.unlock() }

        if let lastPolicyEventSequence, sequence <= lastPolicyEventSequence {
            counters.policyEventsRejected += 1
            return .rejected(.nonMonotonicSequence, snapshotLocked())
        }
        lastPolicyEventSequence = sequence

        switch event {
        case .sessionRunAccepted:
            sessionActive = true
            guard createEpoch(reason: .sessionRun, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }
            continuityIncidentOpen = false
            precisionAvailable = false

        case .sessionRestartAccepted:
            sessionActive = true
            guard createEpoch(reason: .sessionRestart, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }
            continuityIncidentOpen = false
            precisionAvailable = false

        case .trackingNormal:
            precisionAvailable = sessionActive && currentEpoch != nil
            continuityIncidentOpen = false

        case .trackingLimited(let limitation):
            guard suppressForContinuityLoss(reason: Self.reason(for: limitation), sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }

        case .trackingUnavailable:
            guard suppressForContinuityLoss(reason: .trackingUnavailable, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }

        case .interruptionBegan:
            guard suppressForContinuityLoss(reason: .interruption, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }

        case .interruptionEnded:
            precisionAvailable = false

        case .sessionReset:
            precisionAvailable = false
            guard createEpoch(reason: .reset, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }
            continuityIncidentOpen = true

        case .sessionFailure:
            sessionActive = false
            precisionAvailable = false
            guard createEpoch(reason: .failure, sequence: sequence) else {
                return rejectPolicyEvent(.epochIdentityExhausted)
            }
            continuityIncidentOpen = true

        case .sessionStopped:
            sessionActive = false
            precisionAvailable = false
            continuityIncidentOpen = true
        }
        return .accepted(snapshotLocked())
    }

    func authorizeObservation(_ identity: EpochObservationIdentity) -> EpochObservationDisposition {
        lock.lock()
        defer { lock.unlock() }

        guard epochRecords.contains(where: { $0.epoch == identity.epoch }) else {
            return suppressObservation(identity, reason: .unknownEpoch)
        }
        guard identity.epoch == currentEpoch else {
            return suppressObservation(identity, reason: .staleEpoch)
        }
        if let previous = lastObservationSequenceByEpoch[identity.epoch], identity.sequence <= previous {
            return suppressObservation(identity, reason: .nonMonotonicSequence)
        }
        lastObservationSequenceByEpoch[identity.epoch] = identity.sequence
        guard precisionAvailable else {
            return suppressObservation(identity, reason: .precisionUnavailable)
        }
        counters.observationsAccepted += 1
        return .accepted(identity)
    }

    func noteMissingObservationIdentity() -> EpochObservationDisposition {
        lock.lock()
        defer { lock.unlock() }
        counters.observationsSuppressed += 1
        counters.observationRejections[.missingEpochIdentity, default: 0] += 1
        return .retainedSourceSuppressed(nil, .missingEpochIdentity)
    }

    func evaluateRelationship(_ proposal: EpochRelationshipProposal) -> EpochTransformDecision {
        lock.lock()
        defer { lock.unlock() }

        if let lastRelationshipProposalSequence,
           proposal.proposalSequence <= lastRelationshipProposalSequence {
            return rejectTransform(.nonMonotonicSequence)
        }
        lastRelationshipProposalSequence = proposal.proposalSequence

        let knownEpochs = Set(epochRecords.map(\.epoch))
        guard knownEpochs.contains(proposal.sourceEpoch), knownEpochs.contains(proposal.targetEpoch) else {
            return rejectTransform(.unknownEpoch)
        }
        guard proposal.sourceEpoch != proposal.targetEpoch else { return rejectTransform(.sameEpoch) }
        guard Self.isRigidTransform(proposal.targetFromSource) else { return rejectTransform(.invalidTransform) }
        guard proposal.correspondences.count >= minimumCorrespondenceCount else {
            return rejectTransform(.insufficientCorrespondences)
        }
        let correspondenceKeys = proposal.correspondences.map { "\($0.sourceID.uuidString):\($0.targetID.uuidString)" }
        guard Set(correspondenceKeys).count == correspondenceKeys.count else {
            return rejectTransform(.duplicateCorrespondence)
        }
        guard proposal.correspondences.allSatisfy({ Self.isFinite($0.sourcePoint) && Self.isFinite($0.targetPoint) }) else {
            return rejectTransform(.nonFiniteCorrespondence)
        }
        let sourcePoints = proposal.correspondences.map(\.sourcePoint)
        let targetPoints = proposal.correspondences.map(\.targetPoint)
        guard Self.hasNonCollinearTriple(sourcePoints, areaTolerance: degeneracyAreaToleranceSquareMeters),
              Self.hasNonCollinearTriple(targetPoints, areaTolerance: degeneracyAreaToleranceSquareMeters) else {
            return rejectTransform(.degenerateCorrespondences)
        }
        let maximumResidual = proposal.correspondences.reduce(Float.zero) { partial, correspondence in
            let transformed = Self.transform(correspondence.sourcePoint, by: proposal.targetFromSource)
            return max(partial, simd_distance(transformed, correspondence.targetPoint))
        }
        guard maximumResidual.isFinite, maximumResidual <= maximumCorrespondenceResidualMeters else {
            return rejectTransform(.correspondenceResidualExceeded)
        }

        let key = EpochPair(proposal.sourceEpoch, proposal.targetEpoch)
        let relationship = AcceptedEpochRelationship(
            proposal: Self.ownedProposal(proposal),
            version: (relationships[key]?.version ?? 0) + 1,
            maximumResidualMeters: maximumResidual,
            sourceObservationsRemainAuthoritative: true
        )
        relationships[key] = relationship
        counters.transformsAccepted += 1
        return .accepted(relationship)
    }

    func authorizeUse(
        from sourceEpoch: UInt64,
        to targetEpoch: UInt64,
        purpose: EpochUsePurpose
    ) -> EpochUseDecision {
        lock.lock()
        defer { lock.unlock() }
        _ = purpose // Counted uniformly; purpose remains explicit at the call site.

        let knownEpochs = Set(epochRecords.map(\.epoch))
        guard knownEpochs.contains(sourceEpoch), knownEpochs.contains(targetEpoch) else {
            return rejectUse(.unknownEpoch)
        }
        guard precisionAvailable else { return rejectUse(.precisionUnavailable) }
        if sourceEpoch == targetEpoch { return .allowed(nil) }
        guard let relationship = relationships[EpochPair(sourceEpoch, targetEpoch)] else {
            return rejectUse(.missingAcceptedTransform)
        }
        counters.crossEpochUsesAllowed += 1
        return .allowed(relationship)
    }

    func snapshot() -> EpochRelocalizationSnapshot {
        lock.lock()
        defer { lock.unlock() }
        return snapshotLocked()
    }

    private func suppressForContinuityLoss(reason: EpochTransitionReason, sequence: UInt64) -> Bool {
        precisionAvailable = false
        counters.suppressionSignalsByReason[reason, default: 0] += 1
        lastTransitionReason = reason
        guard !continuityIncidentOpen else { return true }
        guard createEpoch(reason: reason, sequence: sequence) else { return false }
        continuityIncidentOpen = true
        return true
    }

    private func createEpoch(reason: EpochTransitionReason, sequence: UInt64) -> Bool {
        guard nextEpoch != 0 else { return false }
        let epoch = nextEpoch
        nextEpoch = epoch == UInt64.max ? 0 : epoch + 1
        epochRecords.append(CaptureEpochRecord(
            epoch: epoch,
            previousEpoch: currentEpoch,
            reason: reason,
            policyEventSequence: sequence
        ))
        currentEpoch = epoch
        lastTransitionReason = reason
        counters.epochsCreated += 1
        counters.transitionsByReason[reason, default: 0] += 1
        return true
    }

    private func rejectPolicyEvent(_ reason: EpochPolicyEventRejection) -> EpochPolicyEventDecision {
        counters.policyEventsRejected += 1
        return .rejected(reason, snapshotLocked())
    }

    private func suppressObservation(
        _ identity: EpochObservationIdentity,
        reason: EpochObservationRejection
    ) -> EpochObservationDisposition {
        counters.observationsSuppressed += 1
        counters.observationRejections[reason, default: 0] += 1
        return .retainedSourceSuppressed(identity, reason)
    }

    private func rejectTransform(_ reason: EpochTransformRejection) -> EpochTransformDecision {
        counters.transformsRejected += 1
        counters.transformRejections[reason, default: 0] += 1
        return .rejected(reason)
    }

    private func rejectUse(_ reason: EpochUseRejection) -> EpochUseDecision {
        counters.crossEpochUsesRejected += 1
        counters.useRejections[reason, default: 0] += 1
        return .rejected(reason)
    }

    private func snapshotLocked() -> EpochRelocalizationSnapshot {
        EpochRelocalizationSnapshot(
            currentEpoch: currentEpoch,
            precisionAvailable: precisionAvailable,
            continuityIncidentOpen: continuityIncidentOpen,
            lastTransitionReason: lastTransitionReason,
            epochRecords: epochRecords,
            acceptedRelationships: relationships.values.sorted {
                $0.proposal.proposalSequence < $1.proposal.proposalSequence
            },
            counters: counters
        )
    }

    private static func reason(for limitation: EpochTrackingLimitation) -> EpochTransitionReason {
        switch limitation {
        case .initializing: .limitedInitializing
        case .excessiveMotion: .limitedExcessiveMotion
        case .insufficientFeatures: .limitedInsufficientFeatures
        case .relocalizing: .limitedRelocalizing
        case .unknown: .limitedUnknown
        }
    }

    private static func ownedProposal(_ proposal: EpochRelationshipProposal) -> EpochRelationshipProposal {
        EpochRelationshipProposal(
            proposalID: proposal.proposalID,
            proposalSequence: proposal.proposalSequence,
            sourceEpoch: proposal.sourceEpoch,
            targetEpoch: proposal.targetEpoch,
            targetFromSource: proposal.targetFromSource.withUnsafeBufferPointer { Array($0) },
            correspondences: proposal.correspondences.withUnsafeBufferPointer { Array($0) }
        )
    }

    private static func isFinite(_ point: SIMD3<Float>) -> Bool {
        point.x.isFinite && point.y.isFinite && point.z.isFinite
    }

    private static func isRigidTransform(_ matrix: [Float]) -> Bool {
        guard matrix.count == 16, matrix.allSatisfy(\.isFinite),
              abs(matrix[3]) < 0.0001, abs(matrix[7]) < 0.0001,
              abs(matrix[11]) < 0.0001, abs(matrix[15] - 1) < 0.0001 else { return false }
        let x = SIMD3(matrix[0], matrix[1], matrix[2])
        let y = SIMD3(matrix[4], matrix[5], matrix[6])
        let z = SIMD3(matrix[8], matrix[9], matrix[10])
        let tolerance: Float = 0.001
        return abs(simd_length(x) - 1) < tolerance
            && abs(simd_length(y) - 1) < tolerance
            && abs(simd_length(z) - 1) < tolerance
            && abs(simd_dot(x, y)) < tolerance
            && abs(simd_dot(x, z)) < tolerance
            && abs(simd_dot(y, z)) < tolerance
            && simd_dot(simd_cross(x, y), z) > 0.999
    }

    private static func transform(_ point: SIMD3<Float>, by matrix: [Float]) -> SIMD3<Float> {
        SIMD3(
            matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
            matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
            matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14]
        )
    }

    private static func hasNonCollinearTriple(_ points: [SIMD3<Float>], areaTolerance: Float) -> Bool {
        guard points.count >= 3 else { return false }
        for first in 0..<(points.count - 2) {
            for second in (first + 1)..<(points.count - 1) {
                for third in (second + 1)..<points.count {
                    let areaVector = simd_cross(points[second] - points[first], points[third] - points[first])
                    if simd_length(areaVector) > areaTolerance { return true }
                }
            }
        }
        return false
    }
}

/// Shared acquisition-order identity source for depth and mesh callbacks.
/// Changing epoch resets only the local event sequence; it does not transform
/// or merge any payload from the previous epoch.
final class CaptureEventIdentitySequence: @unchecked Sendable {
    private let lock = NSLock()
    private var epoch: UInt64
    private var nextSequence: UInt64 = 0

    init(epoch: UInt64 = 0) {
        self.epoch = epoch
    }

    func transition(to epoch: UInt64) {
        lock.lock()
        defer { lock.unlock() }
        guard self.epoch != epoch else { return }
        self.epoch = epoch
        nextSequence = 0
    }

    func next() -> (epoch: UInt64, sequence: UInt64) {
        lock.lock()
        defer { lock.unlock() }
        let identity = (epoch, nextSequence)
        if nextSequence != UInt64.max { nextSequence += 1 }
        return identity
    }
}

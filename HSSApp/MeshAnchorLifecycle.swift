import Foundation
import simd

/// A platform-neutral, owned representation of one AR mesh-anchor revision.
/// No ARKit/Metal buffer or pointer may escape into this type.
struct MeshAnchorPayload: Equatable, Sendable {
    /// Mutable only while assembling synthetic/platform input. The lifecycle
    /// store always creates an independent immutable owned revision.
    var vertices: [SIMD3<Float>]
    let normals: [SIMD3<Float>]
    let triangleIndices: [UInt32]
    /// One optional ARKit classification byte per triangle. An empty array means
    /// classification was not provided; it is never fabricated from geometry.
    let classifications: [UInt8]
    let localBoundsMin: SIMD3<Float>
    let localBoundsMax: SIMD3<Float>
}

enum MeshAnchorEventKind: Equatable, Sendable {
    case add
    case update
    case remove
}

struct MeshAnchorEventInput: Sendable {
    let kind: MeshAnchorEventKind
    let anchorID: UUID
    let sessionEpoch: UInt64
    let sequence: UInt64
    let monotonicTimestamp: TimeInterval
    /// Column-major rigid T_world_anchor. This is retained alongside each
    /// payload so epoch relationships remain explicit at the core boundary.
    let worldFromAnchor: [Float]
    let payload: MeshAnchorPayload?
}

enum MeshAnchorLifecycleRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case invalidTimestamp
    case invalidTransform
    case nonMonotonicSequence
    case missingPayload
    case unexpectedPayloadForRemoval
    case duplicateAdd
    case updateWithoutLiveAnchor
    case removeWithoutLiveAnchor
    case vertexBudgetExceeded
    case triangleBudgetExceeded
    case invalidVertices
    case invalidNormals
    case invalidBounds
    case invalidIndices
    case invalidClassifications
}

struct MeshAnchorLifecycleCounters: Equatable, Sendable {
    let acceptedAdds: UInt64
    let acceptedUpdates: UInt64
    let acceptedRemovals: UInt64
    let rejectedEvents: UInt64
    let rejectedByReason: [MeshAnchorLifecycleRejection: UInt64]

    static let zero = MeshAnchorLifecycleCounters(
        acceptedAdds: 0, acceptedUpdates: 0, acceptedRemovals: 0,
        rejectedEvents: 0, rejectedByReason: [:]
    )

    func rejectionCount(for reason: MeshAnchorLifecycleRejection) -> UInt64 {
        rejectedByReason[reason, default: 0]
    }
}

struct OwnedMeshAnchorRevision: Equatable, Sendable {
    let anchorID: UUID
    /// Increments only for replacements of a currently-live anchor.
    let revision: UInt64
    /// Increments when the same ARKit UUID becomes live again after removal.
    let generation: UInt64
    let sessionEpoch: UInt64
    let sequence: UInt64
    let monotonicTimestamp: TimeInterval
    let worldFromAnchor: [Float]
    let payload: MeshAnchorPayload
}

enum MeshAnchorLifecycleResult: Sendable {
    case accepted(OwnedMeshAnchorRevision?, MeshAnchorLifecycleCounters)
    case rejected(MeshAnchorLifecycleRejection, MeshAnchorLifecycleCounters)
}

/// Serializes lifecycle transitions for the one ARSession owned by B-004.
/// Replacement is whole-payload atomic: an update never blends nearby anchors
/// or appends data to the previous revision.
final class MeshAnchorLifecycleStore: @unchecked Sendable {
    static let defaultMaximumVertexCount = 1_000_000
    static let defaultMaximumTriangleCount = 2_000_000

    private let lock = NSLock()
    private let maximumVertexCount: Int
    private let maximumTriangleCount: Int
    private var live: [UUID: OwnedMeshAnchorRevision] = [:]
    private var generations: [UUID: UInt64] = [:]
    private var lastSequenceByEpoch: [UInt64: UInt64] = [:]
    private var counters = MeshAnchorLifecycleCounters.zero

    init(
        maximumVertexCount: Int = MeshAnchorLifecycleStore.defaultMaximumVertexCount,
        maximumTriangleCount: Int = MeshAnchorLifecycleStore.defaultMaximumTriangleCount
    ) {
        precondition(maximumVertexCount > 0 && maximumTriangleCount > 0)
        self.maximumVertexCount = maximumVertexCount
        self.maximumTriangleCount = maximumTriangleCount
    }

    func apply(_ input: MeshAnchorEventInput) -> MeshAnchorLifecycleResult {
        lock.lock()
        defer { lock.unlock() }

        guard input.monotonicTimestamp.isFinite, input.monotonicTimestamp >= 0 else {
            return reject(.invalidTimestamp)
        }
        guard Self.isRigidTransform(input.worldFromAnchor) else { return reject(.invalidTransform) }
        if let previous = lastSequenceByEpoch[input.sessionEpoch], input.sequence <= previous {
            return reject(.nonMonotonicSequence)
        }

        switch input.kind {
        case .remove:
            guard input.payload == nil else { return reject(.unexpectedPayloadForRemoval) }
            guard live.removeValue(forKey: input.anchorID) != nil else { return reject(.removeWithoutLiveAnchor) }
            lastSequenceByEpoch[input.sessionEpoch] = input.sequence
            counters = MeshAnchorLifecycleCounters(
                acceptedAdds: counters.acceptedAdds, acceptedUpdates: counters.acceptedUpdates,
                acceptedRemovals: counters.acceptedRemovals + 1, rejectedEvents: counters.rejectedEvents,
                rejectedByReason: counters.rejectedByReason
            )
            return .accepted(nil, counters)

        case .add, .update:
            guard let sourcePayload = input.payload else { return reject(.missingPayload) }
            guard let payload = validatedOwnedCopy(of: sourcePayload) else { return rejectedPayload(sourcePayload) }
            let existing = live[input.anchorID]
            if input.kind == .add, existing != nil { return reject(.duplicateAdd) }
            if input.kind == .update, existing == nil { return reject(.updateWithoutLiveAnchor) }

            let generation: UInt64
            let revision: UInt64
            if let existing {
                generation = existing.generation
                revision = existing.revision + 1
            } else {
                generation = generations[input.anchorID, default: 0] + 1
                revision = 1
                generations[input.anchorID] = generation
            }
            let replacement = OwnedMeshAnchorRevision(
                anchorID: input.anchorID, revision: revision, generation: generation,
                sessionEpoch: input.sessionEpoch, sequence: input.sequence,
                monotonicTimestamp: input.monotonicTimestamp,
                worldFromAnchor: Self.ownedCopy(input.worldFromAnchor), payload: payload
            )
            live[input.anchorID] = replacement
            lastSequenceByEpoch[input.sessionEpoch] = input.sequence
            counters = MeshAnchorLifecycleCounters(
                acceptedAdds: counters.acceptedAdds + (input.kind == .add ? 1 : 0),
                acceptedUpdates: counters.acceptedUpdates + (input.kind == .update ? 1 : 0),
                acceptedRemovals: counters.acceptedRemovals, rejectedEvents: counters.rejectedEvents,
                rejectedByReason: counters.rejectedByReason
            )
            return .accepted(replacement, counters)
        }
    }

    func liveRevision(for anchorID: UUID) -> OwnedMeshAnchorRevision? {
        lock.lock()
        defer { lock.unlock() }
        return live[anchorID]
    }

    func countersSnapshot() -> MeshAnchorLifecycleCounters {
        lock.lock()
        defer { lock.unlock() }
        return counters
    }

    private func rejectedPayload(_ payload: MeshAnchorPayload) -> MeshAnchorLifecycleResult {
        if payload.vertices.count > maximumVertexCount { return reject(.vertexBudgetExceeded) }
        if payload.triangleIndices.count / 3 > maximumTriangleCount { return reject(.triangleBudgetExceeded) }
        if payload.vertices.isEmpty || !payload.vertices.allSatisfy(Self.isFinite) { return reject(.invalidVertices) }
        if !payload.normals.isEmpty && (payload.normals.count != payload.vertices.count || !payload.normals.allSatisfy(Self.isFinite)) {
            return reject(.invalidNormals)
        }
        if !Self.isFinite(payload.localBoundsMin) || !Self.isFinite(payload.localBoundsMax)
            || payload.localBoundsMin.x > payload.localBoundsMax.x
            || payload.localBoundsMin.y > payload.localBoundsMax.y
            || payload.localBoundsMin.z > payload.localBoundsMax.z { return reject(.invalidBounds) }
        if payload.triangleIndices.count % 3 != 0 || payload.triangleIndices.contains(where: { Int($0) >= payload.vertices.count }) {
            return reject(.invalidIndices)
        }
        return reject(.invalidClassifications)
    }

    private func validatedOwnedCopy(of payload: MeshAnchorPayload) -> MeshAnchorPayload? {
        guard payload.vertices.count <= maximumVertexCount,
              payload.triangleIndices.count / 3 <= maximumTriangleCount,
              !payload.vertices.isEmpty,
              payload.vertices.allSatisfy(Self.isFinite),
              (payload.normals.isEmpty || (payload.normals.count == payload.vertices.count && payload.normals.allSatisfy(Self.isFinite))),
              Self.isFinite(payload.localBoundsMin), Self.isFinite(payload.localBoundsMax),
              payload.localBoundsMin.x <= payload.localBoundsMax.x,
              payload.localBoundsMin.y <= payload.localBoundsMax.y,
              payload.localBoundsMin.z <= payload.localBoundsMax.z,
              payload.triangleIndices.count % 3 == 0,
              !payload.triangleIndices.contains(where: { Int($0) >= payload.vertices.count }),
              (payload.classifications.isEmpty || payload.classifications.count == payload.triangleIndices.count / 3)
        else { return nil }
        return MeshAnchorPayload(
            vertices: Self.ownedCopy(payload.vertices), normals: Self.ownedCopy(payload.normals),
            triangleIndices: Self.ownedCopy(payload.triangleIndices), classifications: Self.ownedCopy(payload.classifications),
            localBoundsMin: payload.localBoundsMin, localBoundsMax: payload.localBoundsMax
        )
    }

    private func reject(_ reason: MeshAnchorLifecycleRejection) -> MeshAnchorLifecycleResult {
        var reasons = counters.rejectedByReason
        reasons[reason, default: 0] += 1
        counters = MeshAnchorLifecycleCounters(
            acceptedAdds: counters.acceptedAdds, acceptedUpdates: counters.acceptedUpdates,
            acceptedRemovals: counters.acceptedRemovals, rejectedEvents: counters.rejectedEvents + 1,
            rejectedByReason: reasons
        )
        return .rejected(reason, counters)
    }

    private static func ownedCopy<T>(_ source: [T]) -> [T] {
        source.withUnsafeBufferPointer { Array($0) }
    }

    private static func isFinite(_ vector: SIMD3<Float>) -> Bool {
        vector.x.isFinite && vector.y.isFinite && vector.z.isFinite
    }

    private static func isRigidTransform(_ matrix: [Float]) -> Bool {
        guard matrix.count == 16, matrix.allSatisfy(\.isFinite), abs(matrix[3]) < 0.0001,
              abs(matrix[7]) < 0.0001, abs(matrix[11]) < 0.0001, abs(matrix[15] - 1) < 0.0001 else { return false }
        let x = SIMD3(matrix[0], matrix[1], matrix[2])
        let y = SIMD3(matrix[4], matrix[5], matrix[6])
        let z = SIMD3(matrix[8], matrix[9], matrix[10])
        let tolerance: Float = 0.001
        return abs(simd_length(x) - 1) < tolerance && abs(simd_length(y) - 1) < tolerance
            && abs(simd_length(z) - 1) < tolerance && abs(simd_dot(x, y)) < tolerance
            && abs(simd_dot(x, z)) < tolerance && abs(simd_dot(y, z)) < tolerance
            && simd_dot(simd_cross(x, y), z) > 0.999
    }
}

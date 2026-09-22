// Narrow ownership bridge for the C facade. It deliberately provides no
// capture adapter, reconstruction, persistence, or scanner-ready claim.
import Foundation
import HSSContracts

public enum HSSBridgeError: Error, Equatable {
    case native(HSSStatus)
    case closed
    case invalidValue(String)
}

@MainActor
public final class HSSSnapshotLease {
    private let handleAddress: UInt

    init(adoptingAddress handleAddress: UInt) {
        self.handleAddress = handleAddress
    }

    private var handle: OpaquePointer {
        guard let handle = OpaquePointer(bitPattern: handleAddress) else {
            preconditionFailure("HSS snapshot handle became null")
        }
        return handle
    }

    deinit {
        hss_snapshot_release(OpaquePointer(bitPattern: handleAddress))
    }

    public func counts() throws -> HSSCounts {
        var result = HSSCounts()
        let status = hss_snapshot_get_counts(handle, &result)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        return result
    }

    public func sceneJSON() throws -> Data {
        var required: UInt64 = 0
        let query = hss_snapshot_copy_scene_json(handle, nil, 0, &required)
        guard query == HSS_BUFFER_TOO_SMALL || (query == HSS_OK && required == 0) else {
            throw HSSBridgeError.native(query)
        }
        var bytes = Data(repeating: 0, count: Int(required))
        let status = bytes.withUnsafeMutableBytes { rawBuffer in
            hss_snapshot_copy_scene_json(handle, rawBuffer.bindMemory(to: UInt8.self).baseAddress,
                                         required, &required)
        }
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        return bytes
    }

    func sceneSnapshot(
        provenance: Set<ScannerSceneProvenance>,
        isFrozen: Bool
    ) throws -> ScannerSceneSnapshot {
        let counts = try counts()

        var requiredVertices: UInt64 = 0
        let vertexQuery = hss_snapshot_copy_vertices(handle, nil, 0, &requiredVertices)
        guard vertexQuery == HSS_BUFFER_TOO_SMALL || (vertexQuery == HSS_OK && requiredVertices == 0) else {
            throw HSSBridgeError.native(vertexQuery)
        }
        guard requiredVertices <= UInt64(Int.max) else {
            throw HSSBridgeError.invalidValue("Snapshot vertex count exceeds this process's addressable range.")
        }
        var nativeVertices = [HSSVec3d](repeating: HSSVec3d(), count: Int(requiredVertices))
        let vertexStatus = nativeVertices.withUnsafeMutableBufferPointer { buffer in
            hss_snapshot_copy_vertices(handle, buffer.baseAddress, UInt64(buffer.count), &requiredVertices)
        }
        guard vertexStatus == HSS_OK else { throw HSSBridgeError.native(vertexStatus) }

        var requiredIndices: UInt64 = 0
        let triangleQuery = hss_snapshot_copy_triangles(handle, nil, 0, &requiredIndices)
        guard triangleQuery == HSS_BUFFER_TOO_SMALL || (triangleQuery == HSS_OK && requiredIndices == 0) else {
            throw HSSBridgeError.native(triangleQuery)
        }
        guard requiredIndices <= UInt64(Int.max) else {
            throw HSSBridgeError.invalidValue("Snapshot index count exceeds this process's addressable range.")
        }
        var indices = [UInt32](repeating: 0, count: Int(requiredIndices))
        let triangleStatus = indices.withUnsafeMutableBufferPointer { buffer in
            hss_snapshot_copy_triangles(handle, buffer.baseAddress, UInt64(buffer.count), &requiredIndices)
        }
        guard triangleStatus == HSS_OK else { throw HSSBridgeError.native(triangleStatus) }

        return ScannerSceneSnapshot(
            revision: counts.scene_revision,
            verticesMetres: nativeVertices.map { SIMD3($0.x, $0.y, $0.z) },
            triangleIndices: indices,
            sceneJSON: try sceneJSON(),
            provenance: provenance,
            isFrozen: isFrozen
        )
    }
}

public actor HSSEngineOwner {
    private var handleAddress: UInt?

    public init(config: HSSConfig) throws {
        var copiedConfig = config
        var created: OpaquePointer?
        let status = hss_engine_create(&copiedConfig, &created)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        guard let created else { throw HSSBridgeError.closed }
        handleAddress = UInt(bitPattern: created)
    }

    private var handle: OpaquePointer? {
        handleAddress.flatMap(OpaquePointer.init(bitPattern:))
    }

    deinit {
        hss_engine_destroy(handleAddress.flatMap(OpaquePointer.init(bitPattern:)))
    }

    public func step(workBudgetUs: UInt32) throws {
        guard let handle else { throw HSSBridgeError.closed }
        let status = hss_engine_step(handle, workBudgetUs)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
    }

    public func stats() throws -> HSSEngineStats {
        guard let handle else { throw HSSBridgeError.closed }
        var result = HSSEngineStats()
        let status = hss_engine_get_stats(handle, &result)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        return result
    }

    public func acquireSnapshot() async throws -> HSSSnapshotLease {
        guard let handle else { throw HSSBridgeError.closed }
        var acquired: OpaquePointer?
        let status = hss_engine_acquire_snapshot(handle, &acquired)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        guard let acquired else { throw HSSBridgeError.closed }
        return await HSSSnapshotLease(adoptingAddress: UInt(bitPattern: acquired))
    }
}

enum HSSProjectRuntimeOpenMode: Sendable {
    case create
    case openExisting
}

struct HSSProjectCheckpoint: Equatable, Sendable {
    let durableRevision: UInt64
    let durableEventCount: UInt64
    let durableBytes: UInt64
}

struct HSSProjectReplay: Equatable, Sendable {
    let durableRevision: UInt64
    let durableEventCount: UInt64
    let appliedEventCount: UInt64
    let usedPreviousManifest: Bool
}

/// Serial owner of the native engine and its selected durable source store.
/// Engine mutation, persistence append, stepping, checkpoint and replay cannot
/// race because all entry points are isolated to this actor.
actor HSSProjectRuntime {
    private var engineAddress: UInt?
    private var storeAddress: UInt?
    let projectID: UUID
    let captureID: UUID
    let sourceIsSynthetic: Bool

    init(
        projectID: UUID,
        captureID: UUID,
        sourceIsSynthetic: Bool,
        trustedRoot: URL,
        openMode: HSSProjectRuntimeOpenMode
    ) throws {
        self.projectID = projectID
        self.captureID = captureID
        self.sourceIsSynthetic = sourceIsSynthetic

        var engineConfig = HSSConfig()
        engineConfig.abi_version = HSS_ABI_VERSION
        engineConfig.struct_size = UInt32(MemoryLayout<HSSConfig>.size)
        engineConfig.project_id = Self.nativeID(projectID)
        engineConfig.structure_voxel_m = 0.02
        engineConfig.detail_voxel_m = 0.01
        engineConfig.truncation_m = 0.04
        engineConfig.max_working_bytes = 512 * 1024 * 1024
        engineConfig.max_pending_frames = 2
        engineConfig.deterministic_seed = 0x48535331
        var createdEngine: OpaquePointer?
        let engineStatus = hss_engine_create(&engineConfig, &createdEngine)
        guard engineStatus == HSS_OK, let createdEngine else {
            throw HSSBridgeError.native(engineStatus)
        }
        engineAddress = UInt(bitPattern: createdEngine)

        let provenanceStatus = hss_engine_set_source_is_synthetic(
            createdEngine,
            sourceIsSynthetic ? 1 : 0
        )
        guard provenanceStatus == HSS_OK else {
            hss_engine_destroy(createdEngine)
            engineAddress = nil
            throw HSSBridgeError.native(provenanceStatus)
        }

        let path = Data(trustedRoot.path(percentEncoded: false).utf8)
        var storeConfig = HSSProjectStoreConfig()
        storeConfig.abi_version = HSS_PROJECT_STORE_ABI_VERSION
        storeConfig.struct_size = UInt32(MemoryLayout<HSSProjectStoreConfig>.size)
        storeConfig.project_id = Self.nativeID(projectID)
        storeConfig.open_mode = openMode == .create
            ? UInt32(HSS_PROJECT_STORE_CREATE_NEW)
            : UInt32(HSS_PROJECT_STORE_OPEN_EXISTING)
        storeConfig.source_is_synthetic = sourceIsSynthetic ? 1 : 0
        // The native store enforces a 20,000-record project-wide ceiling.
        // Keep each stream explicitly bounded while leaving headroom for all
        // accepted source and control records in one durable manifest.
        storeConfig.max_epoch_records = 1_024
        storeConfig.max_depth_records = 8_192
        storeConfig.max_mesh_records = 8_192
        storeConfig.max_command_records = 1_024
        storeConfig.max_retained_bytes = 8 * 1024 * 1024 * 1024
        storeConfig.max_record_bytes = 256 * 1024 * 1024
        storeConfig.max_pending_bytes = 512 * 1024 * 1024

        var createdStore: OpaquePointer?
        let storeStatus = path.withUnsafeBytes { bytes in
            storeConfig.trusted_root_directory_utf8 = HSSBytes(
                data: bytes.bindMemory(to: UInt8.self).baseAddress,
                length: UInt64(bytes.count)
            )
            return hss_project_store_create(&storeConfig, &createdStore)
        }
        guard storeStatus == HSS_OK, let createdStore else {
            hss_engine_destroy(createdEngine)
            engineAddress = nil
            throw HSSBridgeError.native(storeStatus)
        }
        storeAddress = UInt(bitPattern: createdStore)
    }

    deinit {
        hss_project_store_destroy(storeAddress.flatMap(OpaquePointer.init(bitPattern:)))
        hss_engine_destroy(engineAddress.flatMap(OpaquePointer.init(bitPattern:)))
    }

    private var engine: OpaquePointer? { engineAddress.flatMap(OpaquePointer.init(bitPattern:)) }
    private var store: OpaquePointer? { storeAddress.flatMap(OpaquePointer.init(bitPattern:)) }

    func setEpoch(_ control: CaptureEpochControl, eventSequence: UInt64, monotonicTime: TimeInterval) throws {
        guard let engine, let store else { throw HSSBridgeError.closed }
        guard control.sessionEpoch <= UInt64(UInt32.max) else {
            throw HSSBridgeError.invalidValue("Session epoch exceeds the C ABI range.")
        }
        var epoch = HSSEpoch()
        epoch.abi_version = HSS_ABI_VERSION
        epoch.struct_size = UInt32(MemoryLayout<HSSEpoch>.size)
        epoch.capture_id = Self.nativeID(captureID)
        epoch.event_sequence = eventSequence
        epoch.session_epoch = UInt32(control.sessionEpoch)
        epoch.relationship_state = control.relationshipState.rawValue
        epoch.T_project_world = try Self.nativeMatrix(control.projectFromWorld)
        epoch.transform_revision = control.transformRevision
        let beforeRevision = try stats().scene_revision
        let engineStatus = hss_engine_set_epoch(engine, &epoch)
        guard engineStatus == HSS_OK else { throw HSSBridgeError.native(engineStatus) }
        let acceptedRevision = try completeAcceptedEvent(after: beforeRevision)

        var context = Self.eventContext(
            captureID: captureID, eventSequence: eventSequence,
            sessionEpoch: UInt32(control.sessionEpoch), monotonicTime: monotonicTime,
            acceptedSceneRevision: acceptedRevision
        )
        var receipt = HSSProjectStoreReceipt()
        let storeStatus = hss_project_store_append_epoch(store, &context, &epoch, &receipt)
        guard storeStatus == HSS_OK else { throw HSSBridgeError.native(storeStatus) }
    }

    func submitFrame(_ normalized: NormalizedSceneDepthFrame, eventSequence: UInt64) throws {
        guard let engine, let store else { throw HSSBridgeError.closed }
        guard normalized.identity.sessionEpoch <= UInt64(UInt32.max),
              normalized.intrinsics.depthWidth <= Int(UInt32.max),
              normalized.intrinsics.depthHeight <= Int(UInt32.max) else {
            throw HSSBridgeError.invalidValue("Normalized frame dimensions or epoch exceed the C ABI range.")
        }
        let width = normalized.intrinsics.depthWidth
        let height = normalized.intrinsics.depthHeight
        guard width > 0, height > 0,
              normalized.depthMeters.count == width * height,
              normalized.confidence.count == width * height else {
            throw HSSBridgeError.invalidValue("Normalized frame storage does not match its declared dimensions.")
        }

        var frame = HSSFrame()
        frame.abi_version = HSS_ABI_VERSION
        frame.struct_size = UInt32(MemoryLayout<HSSFrame>.size)
        frame.capture_id = Self.nativeID(captureID)
        frame.event_sequence = eventSequence
        frame.session_epoch = UInt32(normalized.identity.sessionEpoch)
        frame.tracking_state = 0
        frame.timestamp_s = normalized.identity.monotonicTimestamp
        frame.T_world_camera = try Self.nativeMatrix(normalized.worldFromCamera.map(Double.init))
        frame.depth_intrinsics = HSSIntrinsics(
            fx: Double(normalized.intrinsics.fx), fy: Double(normalized.intrinsics.fy),
            cx: Double(normalized.intrinsics.cx), cy: Double(normalized.intrinsics.cy)
        )
        frame.depth_width = UInt32(width)
        frame.depth_height = UInt32(height)
        frame.depth_row_stride_bytes = UInt64(width * MemoryLayout<Float>.size)
        frame.depth_byte_length = UInt64(normalized.depthMeters.count * MemoryLayout<Float>.size)
        frame.confidence_row_stride_bytes = UInt64(width)
        frame.confidence_byte_length = UInt64(normalized.confidence.count)
        frame.depth_source = 0

        let beforeRevision = try stats().scene_revision
        let engineStatus = normalized.depthMeters.withUnsafeBytes { depthBytes in
            normalized.confidence.withUnsafeBytes { confidenceBytes in
                frame.depth_bytes = depthBytes.bindMemory(to: UInt8.self).baseAddress
                frame.confidence_bytes = confidenceBytes.bindMemory(to: UInt8.self).baseAddress
                return hss_engine_submit_frame(engine, &frame)
            }
        }
        guard engineStatus == HSS_OK else { throw HSSBridgeError.native(engineStatus) }
        let acceptedRevision = try completeAcceptedEvent(after: beforeRevision)
        var context = Self.eventContext(
            captureID: captureID, eventSequence: eventSequence,
            sessionEpoch: frame.session_epoch, monotonicTime: frame.timestamp_s,
            acceptedSceneRevision: acceptedRevision
        )
        var receipt = HSSProjectStoreReceipt()
        let storeStatus = normalized.depthMeters.withUnsafeBytes { depthBytes in
            normalized.confidence.withUnsafeBytes { confidenceBytes in
                frame.depth_bytes = depthBytes.bindMemory(to: UInt8.self).baseAddress
                frame.confidence_bytes = confidenceBytes.bindMemory(to: UInt8.self).baseAddress
                return hss_project_store_append_frame(store, &context, &frame, &receipt)
            }
        }
        guard storeStatus == HSS_OK else { throw HSSBridgeError.native(storeStatus) }
    }

    func submitMeshEvent(_ event: OwnedMeshAnchorEvent, eventSequence: UInt64) throws {
        guard let engine, let store else { throw HSSBridgeError.closed }
        guard event.sessionEpoch <= UInt64(UInt32.max) else {
            throw HSSBridgeError.invalidValue("Mesh epoch exceeds the C ABI range.")
        }
        var native = HSSMeshEvent()
        native.abi_version = HSS_ABI_VERSION
        native.struct_size = UInt32(MemoryLayout<HSSMeshEvent>.size)
        native.capture_id = Self.nativeID(captureID)
        native.anchor_id = Self.nativeID(event.anchorID)
        native.event_sequence = eventSequence
        native.anchor_revision = event.anchorRevision
        native.session_epoch = UInt32(event.sessionEpoch)
        native.kind = {
            switch event.kind { case .add: 0; case .update: 1; case .remove: 2 }
        }()
        native.T_world_anchor = try Self.nativeMatrix(event.worldFromAnchor.map(Double.init))

        var packedVertices: [Float] = []
        let indices: [UInt32]
        if let payload = event.payload {
            packedVertices.reserveCapacity(payload.vertices.count * 3)
            for vertex in payload.vertices {
                packedVertices.append(vertex.x)
                packedVertices.append(vertex.y)
                packedVertices.append(vertex.z)
            }
            indices = payload.triangleIndices
            native.vertex_count = UInt64(payload.vertices.count)
            native.triangle_count = UInt64(payload.triangleIndices.count / 3)
        } else {
            indices = []
        }

        let beforeRevision = try stats().scene_revision
        let engineStatus = packedVertices.withUnsafeBufferPointer { vertices in
            indices.withUnsafeBufferPointer { triangles in
                native.vertices_xyz = vertices.baseAddress
                native.triangle_indices = triangles.baseAddress
                return hss_engine_submit_mesh_event(engine, &native)
            }
        }
        guard engineStatus == HSS_OK else { throw HSSBridgeError.native(engineStatus) }
        let acceptedRevision = try completeAcceptedEvent(after: beforeRevision)
        var context = Self.eventContext(
            captureID: captureID, eventSequence: eventSequence,
            sessionEpoch: UInt32(event.sessionEpoch), monotonicTime: event.monotonicTimestamp,
            acceptedSceneRevision: acceptedRevision
        )
        var receipt = HSSProjectStoreReceipt()
        let storeStatus = packedVertices.withUnsafeBufferPointer { vertices in
            indices.withUnsafeBufferPointer { triangles in
                native.vertices_xyz = vertices.baseAddress
                native.triangle_indices = triangles.baseAddress
                return hss_project_store_append_mesh_event(store, &context, &native, &receipt)
            }
        }
        guard storeStatus == HSS_OK else { throw HSSBridgeError.native(storeStatus) }
    }

    func setPlanReference(
        projectFromFloor: [Double],
        cutHeightMetres: Double,
        sessionEpoch: UInt64,
        eventSequence: UInt64,
        monotonicTime: TimeInterval
    ) throws {
        guard let engine, let store else { throw HSSBridgeError.closed }
        guard sessionEpoch <= UInt64(UInt32.max), cutHeightMetres.isFinite, cutHeightMetres > 0 else {
            throw HSSBridgeError.invalidValue("Plan reference requires a valid epoch and positive finite cut height.")
        }
        var floorTransform = try Self.nativeMatrix(projectFromFloor)
        let beforeRevision = try stats().scene_revision
        var status = hss_engine_set_plan_reference(engine, &floorTransform, cutHeightMetres)
        var retries = 0
        while status == HSS_NOT_READY, retries < 1_000 {
            try step()
            retries += 1
            status = hss_engine_set_plan_reference(engine, &floorTransform, cutHeightMetres)
        }
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }

        var afterRevision = try stats().scene_revision
        retries = 0
        while afterRevision == beforeRevision, retries < 1_000 {
            try step()
            retries += 1
            afterRevision = try stats().scene_revision
        }
        guard afterRevision == beforeRevision + 1 else {
            throw HSSBridgeError.native(HSSStatus(HSS_INTERNAL_ERROR))
        }

        var context = Self.eventContext(
            captureID: captureID, eventSequence: eventSequence,
            sessionEpoch: UInt32(sessionEpoch), monotonicTime: monotonicTime,
            acceptedSceneRevision: afterRevision
        )
        var reference = HSSProjectPlanReference()
        reference.abi_version = HSS_PROJECT_STORE_ABI_VERSION
        reference.struct_size = UInt32(MemoryLayout<HSSProjectPlanReference>.size)
        reference.T_project_floor = floorTransform
        reference.cut_height_m = cutHeightMetres
        reference.before_scene_revision = beforeRevision
        reference.after_scene_revision = afterRevision
        var receipt = HSSProjectStoreReceipt()
        let storeStatus = hss_project_store_append_plan_reference(store, &context, &reference, &receipt)
        guard storeStatus == HSS_OK else { throw HSSBridgeError.native(storeStatus) }
    }

    func step(workBudgetUs: UInt32 = 5_000) throws {
        guard let engine else { throw HSSBridgeError.closed }
        let status = hss_engine_step(engine, workBudgetUs)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
    }

    func drain(maximumSteps: Int = 100_000, workBudgetUs: UInt32 = 5_000) throws {
        guard maximumSteps > 0 else { return }
        for _ in 0..<maximumSteps {
            let before = try stats()
            if before.pending_frames == 0 && before.lifecycle_state == 1 { return }
            try step(workBudgetUs: workBudgetUs)
        }
        throw HSSBridgeError.native(HSSStatus(HSS_RESOURCE_LIMIT))
    }

    func stats() throws -> HSSEngineStats {
        guard let engine else { throw HSSBridgeError.closed }
        var result = HSSEngineStats()
        let status = hss_engine_get_stats(engine, &result)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        return result
    }

    func snapshot(
        provenance: Set<ScannerSceneProvenance>,
        frozen: Bool
    ) async throws -> ScannerSceneSnapshot {
        guard let engine else { throw HSSBridgeError.closed }
        var acquired: OpaquePointer?
        let status = hss_engine_acquire_snapshot(engine, &acquired)
        guard status == HSS_OK, let acquired else { throw HSSBridgeError.native(status) }
        let lease = await HSSSnapshotLease(adoptingAddress: UInt(bitPattern: acquired))
        return try await lease.sceneSnapshot(provenance: provenance, isFrozen: frozen)
    }

    func checkpoint() throws -> HSSProjectCheckpoint {
        guard let store else { throw HSSBridgeError.closed }
        var result = HSSProjectCheckpointResult()
        let status = hss_project_store_checkpoint(store, &result)
        guard status == HSS_OK, result.durability_state == 1 else {
            throw HSSBridgeError.native(status == HSS_OK ? HSSStatus(HSS_INTERNAL_ERROR) : status)
        }
        return HSSProjectCheckpoint(
            durableRevision: result.durable_revision,
            durableEventCount: result.durable_event_count,
            durableBytes: result.durable_bytes
        )
    }

    func replayCommittedSource() throws -> HSSProjectReplay {
        guard let engine, let store else { throw HSSBridgeError.closed }
        var result = HSSProjectReplayResult()
        let status = hss_project_store_replay(store, engine, &result)
        guard status == HSS_OK else { throw HSSBridgeError.native(status) }
        return HSSProjectReplay(
            durableRevision: result.durable_revision,
            durableEventCount: result.durable_event_count,
            appliedEventCount: result.applied_event_count,
            usedPreviousManifest: result.used_previous_manifest != 0
        )
    }

    private static func nativeID(_ uuid: UUID) -> HSSId {
        var result = HSSId()
        var bytes = uuid.uuid
        withUnsafeBytes(of: &bytes) { source in
            withUnsafeMutableBytes(of: &result) { destination in
                destination.copyBytes(from: source)
            }
        }
        return result
    }

    private static func nativeMatrix(_ values: [Double]) throws -> HSSMat4d {
        guard values.count == 16, values.allSatisfy(\.isFinite) else {
            throw HSSBridgeError.invalidValue("A finite 4x4 column-major transform is required.")
        }
        var matrix = HSSMat4d()
        withUnsafeMutableBytes(of: &matrix) { raw in
            let doubles = raw.bindMemory(to: Double.self)
            for index in 0..<16 { doubles[index] = values[index] }
        }
        return matrix
    }

    private static func eventContext(
        captureID: UUID,
        eventSequence: UInt64,
        sessionEpoch: UInt32,
        monotonicTime: TimeInterval,
        acceptedSceneRevision: UInt64
    ) -> HSSProjectEventContext {
        var context = HSSProjectEventContext()
        context.abi_version = HSS_PROJECT_STORE_ABI_VERSION
        context.struct_size = UInt32(MemoryLayout<HSSProjectEventContext>.size)
        context.capture_id = nativeID(captureID)
        context.event_sequence = eventSequence
        context.session_epoch = sessionEpoch
        context.monotonic_time_s = monotonicTime
        context.accepted_scene_revision = acceptedSceneRevision
        return context
    }

    private func completeAcceptedEvent(
        after beforeRevision: UInt64,
        maximumSteps: Int = 100_000
    ) throws -> UInt64 {
        for _ in 0..<maximumSteps {
            let current = try stats()
            if current.scene_revision == beforeRevision + 1, current.lifecycle_state == 1 {
                return current.scene_revision
            }
            if current.scene_revision > beforeRevision + 1 {
                throw HSSBridgeError.native(HSSStatus(HSS_INTERNAL_ERROR))
            }
            try step()
        }
        throw HSSBridgeError.native(HSSStatus(HSS_RESOURCE_LIMIT))
    }
}

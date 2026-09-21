// Narrow ownership bridge for the C facade. It deliberately provides no
// capture adapter, reconstruction, persistence, or scanner-ready claim.
import Foundation
import HSSContracts

public enum HSSBridgeError: Error, Equatable {
    case native(HSSStatus)
    case closed
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

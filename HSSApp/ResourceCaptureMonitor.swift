import Foundation

/// One platform reading. Nil means the platform did not measure that sensor;
/// it must never be translated to a nominal reading by the native bridge.
struct ScannerResourceReading: Equatable, Sendable {
    let monotonicTime: TimeInterval
    let thermalState: UInt32?
    let memoryPressure: UInt32?
    let freeStorageBytes: UInt64?

    var hasUnsafeMeasuredPressure: Bool {
        thermalState == 3 || (freeStorageBytes.map { $0 <= 512 * 1024 * 1024 } ?? false)
    }
}

@MainActor
final class ScannerIOSResourceMonitor {
    private var receivedMemoryWarning = false

    func noteMemoryWarning() { receivedMemoryWarning = true }

    func read(storageDirectory: URL) -> ScannerResourceReading {
        let thermal: UInt32?
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: thermal = 0
        case .fair: thermal = 1
        case .serious: thermal = 2
        case .critical: thermal = 3
        @unknown default: thermal = nil
        }
        let available: UInt64?
        if let capacity = try? storageDirectory.resourceValues(
            forKeys: [.volumeAvailableCapacityForImportantUsageKey]
        ).volumeAvailableCapacityForImportantUsage, capacity >= 0 {
            available = UInt64(capacity)
        } else {
            available = nil
        }
        return ScannerResourceReading(
            monotonicTime: ProcessInfo.processInfo.systemUptime,
            thermalState: thermal,
            memoryPressure: receivedMemoryWarning ? 1 : nil,
            freeStorageBytes: available
        )
    }
}

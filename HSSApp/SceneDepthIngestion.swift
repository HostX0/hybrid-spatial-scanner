@preconcurrency import ARKit
import CoreVideo
import Foundation
import simd

let sceneDepthConfidenceUnavailable: UInt8 = 255

enum SceneDepthTrackingState: Equatable, Sendable {
    case normal
    case limited
    case unavailable
}

struct SceneDepthFrameIdentity: Equatable, Sendable {
    let sessionEpoch: UInt64
    let sequence: UInt64
    let monotonicTimestamp: TimeInterval
}

struct PixelPlaneLayout: Equatable, Sendable {
    let width: Int
    let height: Int
    let bytesPerRow: Int
}

struct PixelBufferLayout: Equatable, Sendable {
    let width: Int
    let height: Int
    let pixelFormat: OSType
    let planes: [PixelPlaneLayout]
}

/// A read-only pixel buffer whose pointer is valid only for the duration of the
/// supplied closure. Implementations must balance every successful lock with
/// an unlock even when the closure throws.
protocol SceneDepthPixelBuffer: AnyObject {
    var layout: PixelBufferLayout { get }
    func withLockedReadOnlyPlane0(_ body: (UnsafeRawBufferPointer) throws -> Void) throws
}

enum SceneDepthPixelBufferError: Error {
    case lockFailed(CVReturn)
    case missingBaseAddress
}

final class CVSceneDepthPixelBuffer: SceneDepthPixelBuffer {
    private let pixelBuffer: CVPixelBuffer

    init(_ pixelBuffer: CVPixelBuffer) {
        self.pixelBuffer = pixelBuffer
    }

    var layout: PixelBufferLayout {
        let planeCount = CVPixelBufferGetPlaneCount(pixelBuffer)
        let planes: [PixelPlaneLayout]
        if planeCount == 0 {
            planes = [PixelPlaneLayout(
                width: CVPixelBufferGetWidth(pixelBuffer),
                height: CVPixelBufferGetHeight(pixelBuffer),
                bytesPerRow: CVPixelBufferGetBytesPerRow(pixelBuffer)
            )]
        } else {
            planes = (0..<planeCount).map { plane in
                PixelPlaneLayout(
                    width: CVPixelBufferGetWidthOfPlane(pixelBuffer, plane),
                    height: CVPixelBufferGetHeightOfPlane(pixelBuffer, plane),
                    bytesPerRow: CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, plane)
                )
            }
        }
        return PixelBufferLayout(
            width: CVPixelBufferGetWidth(pixelBuffer),
            height: CVPixelBufferGetHeight(pixelBuffer),
            pixelFormat: CVPixelBufferGetPixelFormatType(pixelBuffer),
            planes: planes
        )
    }

    func withLockedReadOnlyPlane0(_ body: (UnsafeRawBufferPointer) throws -> Void) throws {
        let flags = CVPixelBufferLockFlags.readOnly
        let status = CVPixelBufferLockBaseAddress(pixelBuffer, flags)
        guard status == kCVReturnSuccess else {
            throw SceneDepthPixelBufferError.lockFailed(status)
        }
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, flags) }

        let planeCount = CVPixelBufferGetPlaneCount(pixelBuffer)
        let baseAddress = planeCount == 0
            ? CVPixelBufferGetBaseAddress(pixelBuffer)
            : CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0)
        guard let baseAddress else {
            throw SceneDepthPixelBufferError.missingBaseAddress
        }
        let plane = layout.planes[0]
        try body(UnsafeRawBufferPointer(start: baseAddress, count: plane.bytesPerRow * plane.height))
    }
}

struct SceneDepthCalibrationInput: Equatable, Sendable {
    /// Intrinsics are calibrated in this unrotated sensor-image coordinate system.
    let imageWidth: Int
    let imageHeight: Int
    let fx: Float
    let fy: Float
    let cx: Float
    let cy: Float

    /// Region of the calibrated sensor image represented by the depth map.
    /// ARKit's current adapter supplies the full sensor image; replay and future
    /// adapters can supply an explicit crop without using a viewport transform.
    let depthCropX: Float
    let depthCropY: Float
    let depthCropWidth: Float
    let depthCropHeight: Float
}

struct ScaledSceneDepthIntrinsics: Equatable, Sendable {
    let fx: Float
    let fy: Float
    let cx: Float
    let cy: Float
    let calibrationImageWidth: Int
    let calibrationImageHeight: Int
    let depthCropX: Float
    let depthCropY: Float
    let depthCropWidth: Float
    let depthCropHeight: Float
    let depthWidth: Int
    let depthHeight: Int
}

struct SceneDepthFrameInput {
    let identity: SceneDepthFrameIdentity
    let trackingState: SceneDepthTrackingState
    let calibration: SceneDepthCalibrationInput
    /// Column-major T_world_camera in ARKit's right-handed world coordinates.
    let worldFromCamera: [Float]
    let rgbBuffer: any SceneDepthPixelBuffer
    let sceneDepthBuffer: (any SceneDepthPixelBuffer)?
    let confidenceBuffer: (any SceneDepthPixelBuffer)?
}

struct NormalizedSceneDepthFrame: Sendable {
    let identity: SceneDepthFrameIdentity
    let intrinsics: ScaledSceneDepthIntrinsics
    let worldFromCamera: [Float]
    let rgbLayout: PixelBufferLayout
    let depthLayout: PixelBufferLayout
    let confidenceLayout: PixelBufferLayout?
    /// Invalid source depth is replaced by zero and must only be consumed where
    /// validDepthMask is one. No borrowed CVPixelBuffer pointer escapes ingest.
    let depthMeters: [Float]
    let confidence: [UInt8]
    let validDepthMask: [UInt8]
    let metricFusionMask: [UInt8]
    let provisionalPreviewMask: [UInt8]
    /// Optional owned, sampled RGB appearance from this exact ARFrame. It is
    /// never consulted by metric ingestion or confidence/depth validation.
    var appearancePhoto: ScannerAppearancePhoto? = nil
}

enum SceneDepthFrameRejection: String, CaseIterable, Equatable, Hashable, Sendable {
    case missingSceneDepth
    case invalidTracking
    case invalidTimestamp
    case invalidCalibration
    case invalidPose
    case invalidRGBLayout
    case invalidDepthLayout
    case invalidConfidenceLayout
    case sampleBudgetExceeded
    case bufferAccessFailure
    case noValidDepthSamples
}

struct SceneDepthIngestionCounters: Equatable, Sendable {
    let acceptedFrames: UInt64
    let rejectedFrames: UInt64
    let rejectedByReason: [SceneDepthFrameRejection: UInt64]
    let acceptedDepthSamples: UInt64
    let rejectedNonFiniteDepthSamples: UInt64
    let rejectedNonPositiveDepthSamples: UInt64
    let lowConfidenceSamplesExcludedFromFusion: UInt64

    static let zero = SceneDepthIngestionCounters(
        acceptedFrames: 0,
        rejectedFrames: 0,
        rejectedByReason: [:],
        acceptedDepthSamples: 0,
        rejectedNonFiniteDepthSamples: 0,
        rejectedNonPositiveDepthSamples: 0,
        lowConfidenceSamplesExcludedFromFusion: 0
    )

    func rejectionCount(for reason: SceneDepthFrameRejection) -> UInt64 {
        rejectedByReason[reason, default: 0]
    }
}

enum SceneDepthIngestionResult: Sendable {
    case accepted(NormalizedSceneDepthFrame, SceneDepthIngestionCounters)
    case rejected(SceneDepthFrameRejection, SceneDepthIngestionCounters)
}

/// A single-slot latest-value mailbox. At most one completed normalized frame
/// waits for MainActor delivery; a newer expendable frame replaces it under
/// load instead of growing an unbounded queue.
final class SceneDepthIngestionMailbox: @unchecked Sendable {
    private let lock = NSLock()
    private var pending: SceneDepthIngestionResult?
    private var deliveryScheduled = false

    func submit(
        _ result: SceneDepthIngestionResult,
        deliver: @escaping @MainActor @Sendable (SceneDepthIngestionResult) -> Void
    ) {
        lock.lock()
        pending = result
        let mustSchedule = !deliveryScheduled
        deliveryScheduled = true
        lock.unlock()
        guard mustSchedule else { return }

        Task { @MainActor in
            while let next = self.takeNext() {
                deliver(next)
            }
        }
    }

    private func takeNext() -> SceneDepthIngestionResult? {
        lock.lock()
        defer { lock.unlock() }
        guard let pending else {
            deliveryScheduled = false
            return nil
        }
        self.pending = nil
        return pending
    }
}

/// Synchronous, bounded scene-depth normalizer. It intentionally has no input
/// for smoothedSceneDepth, so that source cannot silently become measurement.
final class SceneDepthFrameNormalizer: @unchecked Sendable {
    static let defaultMaximumSampleCount = 640 * 480

    private let maximumSampleCount: Int
    private let lock = NSLock()
    private var acceptedFrames: UInt64 = 0
    private var rejectedFrames: UInt64 = 0
    private var rejectedByReason: [SceneDepthFrameRejection: UInt64] = [:]
    private var acceptedDepthSamples: UInt64 = 0
    private var rejectedNonFiniteDepthSamples: UInt64 = 0
    private var rejectedNonPositiveDepthSamples: UInt64 = 0
    private var lowConfidenceSamplesExcludedFromFusion: UInt64 = 0

    init(maximumSampleCount: Int = SceneDepthFrameNormalizer.defaultMaximumSampleCount) {
        precondition(maximumSampleCount > 0)
        self.maximumSampleCount = maximumSampleCount
    }

    func normalize(_ input: SceneDepthFrameInput) -> SceneDepthIngestionResult {
        guard input.sceneDepthBuffer != nil else { return reject(.missingSceneDepth) }
        guard input.trackingState == .normal else { return reject(.invalidTracking) }
        guard input.identity.monotonicTimestamp.isFinite, input.identity.monotonicTimestamp >= 0 else {
            return reject(.invalidTimestamp)
        }
        guard let intrinsics = Self.scaleIntrinsics(input.calibration, depthLayout: input.sceneDepthBuffer!.layout) else {
            return reject(.invalidCalibration)
        }
        guard Self.isRigidWorldFromCamera(input.worldFromCamera) else { return reject(.invalidPose) }
        guard Self.isValidMetadataLayout(input.rgbBuffer.layout) else { return reject(.invalidRGBLayout) }

        let depthBuffer = input.sceneDepthBuffer!
        let depthLayout = depthBuffer.layout
        guard Self.isValidDepthLayout(depthLayout) else { return reject(.invalidDepthLayout) }
        let sampleCount = depthLayout.width.multipliedReportingOverflow(by: depthLayout.height)
        guard !sampleCount.overflow, sampleCount.partialValue <= maximumSampleCount else {
            return reject(.sampleBudgetExceeded)
        }

        if let confidenceBuffer = input.confidenceBuffer,
           !Self.isValidConfidenceLayout(confidenceBuffer.layout, matching: depthLayout) {
            return reject(.invalidConfidenceLayout)
        }

        var depthMeters = Array(repeating: Float.zero, count: sampleCount.partialValue)
        var confidence = Array(repeating: sceneDepthConfidenceUnavailable, count: sampleCount.partialValue)
        var validDepthMask = Array(repeating: UInt8.zero, count: sampleCount.partialValue)
        var metricFusionMask = Array(repeating: UInt8.zero, count: sampleCount.partialValue)
        var provisionalPreviewMask = Array(repeating: UInt8.zero, count: sampleCount.partialValue)
        var nonFiniteCount: UInt64 = 0
        var nonPositiveCount: UInt64 = 0

        do {
            try depthBuffer.withLockedReadOnlyPlane0 { bytes in
                let stride = depthLayout.planes[0].bytesPerRow
                for row in 0..<depthLayout.height {
                    for column in 0..<depthLayout.width {
                        let index = row * depthLayout.width + column
                        let byteOffset = row * stride + column * MemoryLayout<Float>.stride
                        let value = bytes.loadUnaligned(fromByteOffset: byteOffset, as: Float.self)
                        if !value.isFinite {
                            nonFiniteCount += 1
                        } else if value <= 0 {
                            nonPositiveCount += 1
                        } else {
                            depthMeters[index] = value
                            validDepthMask[index] = 1
                        }
                    }
                }
            }
            if let confidenceBuffer = input.confidenceBuffer {
                let confidenceLayout = confidenceBuffer.layout
                try confidenceBuffer.withLockedReadOnlyPlane0 { bytes in
                    let stride = confidenceLayout.planes[0].bytesPerRow
                    for row in 0..<confidenceLayout.height {
                        for column in 0..<confidenceLayout.width {
                            confidence[row * confidenceLayout.width + column] = bytes[row * stride + column]
                        }
                    }
                }
            }
        } catch {
            return reject(.bufferAccessFailure)
        }

        var validCount: UInt64 = 0
        var lowConfidenceCount: UInt64 = 0
        for index in depthMeters.indices where validDepthMask[index] == 1 {
            validCount += 1
            if confidence[index] == ARConfidenceLevel.low.rawValue {
                provisionalPreviewMask[index] = 1
                lowConfidenceCount += 1
            } else {
                metricFusionMask[index] = 1
            }
        }
        guard validCount > 0 else {
            addSampleCounters(nonFinite: nonFiniteCount, nonPositive: nonPositiveCount, valid: 0, lowConfidence: 0)
            return reject(.noValidDepthSamples)
        }

        let normalized = NormalizedSceneDepthFrame(
            identity: input.identity,
            intrinsics: intrinsics,
            worldFromCamera: input.worldFromCamera,
            rgbLayout: input.rgbBuffer.layout,
            depthLayout: depthLayout,
            confidenceLayout: input.confidenceBuffer?.layout,
            depthMeters: depthMeters,
            confidence: confidence,
            validDepthMask: validDepthMask,
            metricFusionMask: metricFusionMask,
            provisionalPreviewMask: provisionalPreviewMask
        )
        lock.lock()
        acceptedFrames += 1
        acceptedDepthSamples += validCount
        rejectedNonFiniteDepthSamples += nonFiniteCount
        rejectedNonPositiveDepthSamples += nonPositiveCount
        lowConfidenceSamplesExcludedFromFusion += lowConfidenceCount
        let counters = snapshotLocked()
        lock.unlock()
        return .accepted(normalized, counters)
    }

    func countersSnapshot() -> SceneDepthIngestionCounters {
        lock.lock()
        defer { lock.unlock() }
        return snapshotLocked()
    }

    static func scaleIntrinsics(
        _ calibration: SceneDepthCalibrationInput,
        depthLayout: PixelBufferLayout
    ) -> ScaledSceneDepthIntrinsics? {
        let scalarValues = [calibration.fx, calibration.fy, calibration.cx, calibration.cy,
                            calibration.depthCropX, calibration.depthCropY,
                            calibration.depthCropWidth, calibration.depthCropHeight]
        guard calibration.imageWidth > 0, calibration.imageHeight > 0,
              depthLayout.width > 0, depthLayout.height > 0,
              scalarValues.allSatisfy(\.isFinite),
              calibration.fx > 0, calibration.fy > 0,
              calibration.depthCropX >= 0, calibration.depthCropY >= 0,
              calibration.depthCropWidth > 0, calibration.depthCropHeight > 0,
              calibration.depthCropX + calibration.depthCropWidth <= Float(calibration.imageWidth),
              calibration.depthCropY + calibration.depthCropHeight <= Float(calibration.imageHeight) else {
            return nil
        }
        let xScale = Float(depthLayout.width) / calibration.depthCropWidth
        let yScale = Float(depthLayout.height) / calibration.depthCropHeight
        let result = ScaledSceneDepthIntrinsics(
            fx: calibration.fx * xScale,
            fy: calibration.fy * yScale,
            cx: (calibration.cx - calibration.depthCropX) * xScale,
            cy: (calibration.cy - calibration.depthCropY) * yScale,
            calibrationImageWidth: calibration.imageWidth,
            calibrationImageHeight: calibration.imageHeight,
            depthCropX: calibration.depthCropX,
            depthCropY: calibration.depthCropY,
            depthCropWidth: calibration.depthCropWidth,
            depthCropHeight: calibration.depthCropHeight,
            depthWidth: depthLayout.width,
            depthHeight: depthLayout.height
        )
        guard [result.fx, result.fy, result.cx, result.cy].allSatisfy(\.isFinite),
              result.cx >= 0, result.cx < Float(depthLayout.width),
              result.cy >= 0, result.cy < Float(depthLayout.height) else {
            return nil
        }
        return result
    }

    private static func isValidMetadataLayout(_ layout: PixelBufferLayout) -> Bool {
        guard layout.width > 0, layout.height > 0, layout.pixelFormat != 0, !layout.planes.isEmpty else { return false }
        return layout.planes.allSatisfy {
            $0.width > 0 && $0.height > 0 && $0.bytesPerRow >= $0.width
        }
    }

    private static func isValidDepthLayout(_ layout: PixelBufferLayout) -> Bool {
        guard isValidMetadataLayout(layout), layout.pixelFormat == kCVPixelFormatType_DepthFloat32,
              layout.planes.count == 1, layout.planes[0].width == layout.width,
              layout.planes[0].height == layout.height else { return false }
        let minimumStride = layout.width.multipliedReportingOverflow(by: MemoryLayout<Float>.stride)
        return !minimumStride.overflow && layout.planes[0].bytesPerRow >= minimumStride.partialValue
    }

    private static func isValidConfidenceLayout(_ layout: PixelBufferLayout, matching depth: PixelBufferLayout) -> Bool {
        isValidMetadataLayout(layout)
            && layout.pixelFormat == kCVPixelFormatType_OneComponent8
            && layout.planes.count == 1
            && layout.width == depth.width
            && layout.height == depth.height
            && layout.planes[0].width == layout.width
            && layout.planes[0].height == layout.height
            && layout.planes[0].bytesPerRow >= layout.width
    }

    private static func isRigidWorldFromCamera(_ values: [Float]) -> Bool {
        guard values.count == 16, values.allSatisfy(\.isFinite) else { return false }
        let tolerance: Float = 1e-3
        guard abs(values[3]) <= tolerance, abs(values[7]) <= tolerance,
              abs(values[11]) <= tolerance, abs(values[15] - 1) <= tolerance else { return false }
        let x = SIMD3(values[0], values[1], values[2])
        let y = SIMD3(values[4], values[5], values[6])
        let z = SIMD3(values[8], values[9], values[10])
        return abs(simd_length(x) - 1) <= tolerance
            && abs(simd_length(y) - 1) <= tolerance
            && abs(simd_length(z) - 1) <= tolerance
            && abs(simd_dot(x, y)) <= tolerance
            && abs(simd_dot(x, z)) <= tolerance
            && abs(simd_dot(y, z)) <= tolerance
            && simd_dot(simd_cross(x, y), z) > 1 - tolerance
    }

    private func addSampleCounters(nonFinite: UInt64, nonPositive: UInt64, valid: UInt64, lowConfidence: UInt64) {
        lock.lock()
        rejectedNonFiniteDepthSamples += nonFinite
        rejectedNonPositiveDepthSamples += nonPositive
        acceptedDepthSamples += valid
        lowConfidenceSamplesExcludedFromFusion += lowConfidence
        lock.unlock()
    }

    private func reject(_ reason: SceneDepthFrameRejection) -> SceneDepthIngestionResult {
        lock.lock()
        rejectedFrames += 1
        rejectedByReason[reason, default: 0] += 1
        let counters = snapshotLocked()
        lock.unlock()
        return .rejected(reason, counters)
    }

    private func snapshotLocked() -> SceneDepthIngestionCounters {
        SceneDepthIngestionCounters(
            acceptedFrames: acceptedFrames,
            rejectedFrames: rejectedFrames,
            rejectedByReason: rejectedByReason,
            acceptedDepthSamples: acceptedDepthSamples,
            rejectedNonFiniteDepthSamples: rejectedNonFiniteDepthSamples,
            rejectedNonPositiveDepthSamples: rejectedNonPositiveDepthSamples,
            lowConfidenceSamplesExcludedFromFusion: lowConfidenceSamplesExcludedFromFusion
        )
    }
}

enum ARFrameSceneDepthAdapter {
    static func makeInput(
        frame: ARFrame,
        sessionEpoch: UInt64,
        sequence: UInt64
    ) -> SceneDepthFrameInput {
        let imageResolution = frame.camera.imageResolution
        let intrinsics = frame.camera.intrinsics
        let transform = frame.camera.transform
        let depthData = frame.sceneDepth
        return SceneDepthFrameInput(
            identity: SceneDepthFrameIdentity(
                sessionEpoch: sessionEpoch,
                sequence: sequence,
                monotonicTimestamp: frame.timestamp
            ),
            trackingState: trackingState(frame.camera.trackingState),
            calibration: SceneDepthCalibrationInput(
                imageWidth: Int(imageResolution.width),
                imageHeight: Int(imageResolution.height),
                fx: intrinsics[0, 0],
                fy: intrinsics[1, 1],
                cx: intrinsics[2, 0],
                cy: intrinsics[2, 1],
                depthCropX: 0,
                depthCropY: 0,
                depthCropWidth: Float(imageResolution.width),
                depthCropHeight: Float(imageResolution.height)
            ),
            worldFromCamera: (0..<4).flatMap { column in (0..<4).map { row in transform[column, row] } },
            rgbBuffer: CVSceneDepthPixelBuffer(frame.capturedImage),
            sceneDepthBuffer: depthData.map { CVSceneDepthPixelBuffer($0.depthMap) },
            confidenceBuffer: depthData?.confidenceMap.map(CVSceneDepthPixelBuffer.init)
        )
    }

    private static func trackingState(_ state: ARCamera.TrackingState) -> SceneDepthTrackingState {
        switch state {
        case .normal: .normal
        case .limited: .limited
        case .notAvailable: .unavailable
        }
    }
}

final class SceneDepthFrameIdentitySequence: @unchecked Sendable {
    private let lock = NSLock()
    private let sessionEpoch: UInt64
    private var nextSequence: UInt64 = 0

    init(sessionEpoch: UInt64) {
        self.sessionEpoch = sessionEpoch
    }

    func next() -> (epoch: UInt64, sequence: UInt64) {
        lock.lock()
        defer { lock.unlock() }
        let identity = (sessionEpoch, nextSequence)
        nextSequence &+= 1
        return identity
    }
}

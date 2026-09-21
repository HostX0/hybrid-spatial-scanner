import CoreVideo
import XCTest
@testable import HybridSpatialScanner

final class SceneDepthIngestionTests: XCTestCase {
    func testScalesIntrinsicsThroughExplicitCropWithoutOrientationTransform() throws {
        let depthLayout = makeLayout(width: 4, height: 2, stride: 16, format: kCVPixelFormatType_DepthFloat32)
        let calibration = SceneDepthCalibrationInput(
            imageWidth: 12,
            imageHeight: 8,
            fx: 8,
            fy: 6,
            cx: 6,
            cy: 4,
            depthCropX: 2,
            depthCropY: 2,
            depthCropWidth: 8,
            depthCropHeight: 4
        )

        let scaled = try XCTUnwrap(SceneDepthFrameNormalizer.scaleIntrinsics(calibration, depthLayout: depthLayout))

        XCTAssertEqual(scaled.fx, 4, accuracy: 1e-6)
        XCTAssertEqual(scaled.fy, 3, accuracy: 1e-6)
        XCTAssertEqual(scaled.cx, 2, accuracy: 1e-6)
        XCTAssertEqual(scaled.cy, 1, accuracy: 1e-6)
        XCTAssertEqual(scaled.depthCropX, 2)
        XCTAssertEqual(scaled.depthCropY, 2)
    }

    func testPaddedStridesAreCopiedAndInvalidDepthIsRejectedPerSample() throws {
        let depth = SyntheticPixelBuffer.depth(
            width: 3,
            height: 2,
            stride: 16,
            values: [1, .nan, -0.5, 2, 3, 0]
        )
        let confidence = SyntheticPixelBuffer.confidence(
            width: 3,
            height: 2,
            stride: 5,
            values: [2, 1, 0, 0, 2, 1]
        )
        let rgb = SyntheticPixelBuffer.metadata(
            width: 12,
            height: 8,
            stride: 16,
            format: kCVPixelFormatType_OneComponent8
        )
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 8)

        let result = normalizer.normalize(makeInput(rgb: rgb, depth: depth, confidence: confidence))
        guard case .accepted(let frame, let counters) = result else {
            return XCTFail("Expected a normalized frame")
        }

        XCTAssertEqual(frame.identity.sessionEpoch, 7)
        XCTAssertEqual(frame.identity.sequence, 11)
        XCTAssertEqual(frame.depthLayout.planes[0].bytesPerRow, 16)
        XCTAssertEqual(frame.confidenceLayout?.planes[0].bytesPerRow, 5)
        XCTAssertEqual(frame.depthMeters, [1, 0, 0, 2, 3, 0])
        XCTAssertEqual(frame.validDepthMask, [1, 0, 0, 1, 1, 0])
        XCTAssertEqual(frame.metricFusionMask, [1, 0, 0, 0, 1, 0])
        XCTAssertEqual(frame.provisionalPreviewMask, [0, 0, 0, 1, 0, 0])
        XCTAssertEqual(counters.acceptedDepthSamples, 3)
        XCTAssertEqual(counters.rejectedNonFiniteDepthSamples, 1)
        XCTAssertEqual(counters.rejectedNonPositiveDepthSamples, 2)
        XCTAssertEqual(counters.lowConfidenceSamplesExcludedFromFusion, 1)
        XCTAssertEqual(depth.lockCount, 1)
        XCTAssertEqual(depth.unlockCount, 1)
        XCTAssertEqual(confidence.lockCount, 1)
        XCTAssertEqual(confidence.unlockCount, 1)
    }

    func testMissingConfidenceUsesUnavailableWithoutChangingValidDepth() {
        let depth = SyntheticPixelBuffer.depth(width: 2, height: 1, stride: 8, values: [1, 2])
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 2)

        let result = normalizer.normalize(makeInput(depth: depth, confidence: nil))
        guard case .accepted(let frame, _) = result else { return XCTFail("Expected acceptance") }

        XCTAssertEqual(frame.confidence, [255, 255])
        XCTAssertEqual(frame.validDepthMask, [1, 1])
        XCTAssertEqual(frame.metricFusionMask, [1, 1])
        XCTAssertEqual(frame.provisionalPreviewMask, [0, 0])
    }

    func testOutputOwnsCopyAfterSourceMutationAndUnlock() {
        let depth = SyntheticPixelBuffer.depth(width: 1, height: 1, stride: 4, values: [1.25])
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 1)

        let result = normalizer.normalize(makeInput(depth: depth, confidence: nil))
        depth.replaceDepth(at: 0, with: 9)

        guard case .accepted(let frame, _) = result else { return XCTFail("Expected acceptance") }
        XCTAssertEqual(frame.depthMeters, [1.25])
        XCTAssertEqual(depth.lockCount, depth.unlockCount)
    }

    func testRejectsFrameLevelFailuresWithReasonCounters() {
        let validDepth = SyntheticPixelBuffer.depth(width: 2, height: 1, stride: 8, values: [1, 2])
        let badStride = SyntheticPixelBuffer.depth(width: 2, height: 1, stride: 4, values: [1, 2])
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 2)

        assertRejected(.invalidTracking, normalizer.normalize(makeInput(tracking: .limited, depth: validDepth)))
        assertRejected(.invalidCalibration, normalizer.normalize(makeInput(calibrationFX: 0, depth: validDepth)))
        assertRejected(.invalidDepthLayout, normalizer.normalize(makeInput(depth: badStride)))
        assertRejected(.invalidPose, normalizer.normalize(makeInput(pose: Array(repeating: 0, count: 16), depth: validDepth)))

        let counters = normalizer.countersSnapshot()
        XCTAssertEqual(counters.rejectedFrames, 4)
        XCTAssertEqual(counters.rejectionCount(for: .invalidTracking), 1)
        XCTAssertEqual(counters.rejectionCount(for: .invalidCalibration), 1)
        XCTAssertEqual(counters.rejectionCount(for: .invalidDepthLayout), 1)
        XCTAssertEqual(counters.rejectionCount(for: .invalidPose), 1)
        XCTAssertEqual(validDepth.lockCount, 0, "Frame-level validation must reject before locking source buffers")
    }

    func testRejectsAllInvalidDepthAndCountsBothClasses() {
        let depth = SyntheticPixelBuffer.depth(width: 3, height: 1, stride: 12, values: [.infinity, .nan, 0])
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 3)

        let result = normalizer.normalize(makeInput(depth: depth))

        assertRejected(.noValidDepthSamples, result)
        let counters = normalizer.countersSnapshot()
        XCTAssertEqual(counters.rejectedNonFiniteDepthSamples, 2)
        XCTAssertEqual(counters.rejectedNonPositiveDepthSamples, 1)
    }

    func testRejectsConfidenceDimensionMismatchAndSampleBudgetOverflow() {
        let depth = SyntheticPixelBuffer.depth(width: 2, height: 2, stride: 8, values: [1, 1, 1, 1])
        let wrongConfidence = SyntheticPixelBuffer.confidence(width: 1, height: 2, stride: 1, values: [2, 2])
        let normalizer = SceneDepthFrameNormalizer(maximumSampleCount: 3)

        assertRejected(.sampleBudgetExceeded, normalizer.normalize(makeInput(depth: depth, confidence: nil)))

        let roomyNormalizer = SceneDepthFrameNormalizer(maximumSampleCount: 4)
        assertRejected(.invalidConfidenceLayout, roomyNormalizer.normalize(makeInput(depth: depth, confidence: wrongConfidence)))
    }

    @MainActor
    func testMailboxKeepsOnlyLatestPendingFrame() async {
        let mailbox = SceneDepthIngestionMailbox()
        let delivered = expectation(description: "latest result delivered")
        delivered.expectedFulfillmentCount = 1
        let recorder = MainActorRejectionRecorder()
        let deliver: @MainActor @Sendable (SceneDepthIngestionResult) -> Void = { result in
            guard case .rejected(let reason, _) = result else { return }
            recorder.reasons.append(reason)
            delivered.fulfill()
        }

        mailbox.submit(.rejected(.invalidTracking, .zero), deliver: deliver)
        mailbox.submit(.rejected(.invalidPose, .zero), deliver: deliver)
        mailbox.submit(.rejected(.invalidCalibration, .zero), deliver: deliver)

        await fulfillment(of: [delivered], timeout: 1)
        XCTAssertEqual(recorder.reasons, [.invalidCalibration])
    }

    private func assertRejected(
        _ expected: SceneDepthFrameRejection,
        _ result: SceneDepthIngestionResult,
        file: StaticString = #filePath,
        line: UInt = #line
    ) {
        guard case .rejected(let reason, _) = result else {
            return XCTFail("Expected rejection", file: file, line: line)
        }
        XCTAssertEqual(reason, expected, file: file, line: line)
    }

    private func makeInput(
        tracking: SceneDepthTrackingState = .normal,
        calibrationFX: Float = 8,
        pose: [Float] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5, 0.25, -1, 1],
        rgb: SyntheticPixelBuffer? = nil,
        depth: SyntheticPixelBuffer?,
        confidence: SyntheticPixelBuffer? = nil
    ) -> SceneDepthFrameInput {
        SceneDepthFrameInput(
            identity: SceneDepthFrameIdentity(sessionEpoch: 7, sequence: 11, monotonicTimestamp: 3.5),
            trackingState: tracking,
            calibration: SceneDepthCalibrationInput(
                imageWidth: 12,
                imageHeight: 8,
                fx: calibrationFX,
                fy: 6,
                cx: 6,
                cy: 4,
                depthCropX: 0,
                depthCropY: 0,
                depthCropWidth: 12,
                depthCropHeight: 8
            ),
            worldFromCamera: pose,
            rgbBuffer: rgb ?? .metadata(width: 12, height: 8, stride: 16, format: kCVPixelFormatType_OneComponent8),
            sceneDepthBuffer: depth,
            confidenceBuffer: confidence
        )
    }

    private func makeLayout(width: Int, height: Int, stride: Int, format: OSType) -> PixelBufferLayout {
        PixelBufferLayout(
            width: width,
            height: height,
            pixelFormat: format,
            planes: [PixelPlaneLayout(width: width, height: height, bytesPerRow: stride)]
        )
    }
}

@MainActor
private final class MainActorRejectionRecorder {
    var reasons: [SceneDepthFrameRejection] = []
}

private final class SyntheticPixelBuffer: SceneDepthPixelBuffer {
    let layout: PixelBufferLayout
    private var storage: Data
    private(set) var lockCount = 0
    private(set) var unlockCount = 0

    private init(layout: PixelBufferLayout, storage: Data) {
        self.layout = layout
        self.storage = storage
    }

    static func metadata(width: Int, height: Int, stride: Int, format: OSType) -> SyntheticPixelBuffer {
        let layout = PixelBufferLayout(
            width: width,
            height: height,
            pixelFormat: format,
            planes: [PixelPlaneLayout(width: width, height: height, bytesPerRow: stride)]
        )
        return SyntheticPixelBuffer(layout: layout, storage: Data(repeating: 0, count: stride * height))
    }

    static func depth(width: Int, height: Int, stride: Int, values: [Float]) -> SyntheticPixelBuffer {
        precondition(values.count == width * height)
        var bytes = Data(repeating: 0xA5, count: max(stride * height, width * MemoryLayout<Float>.size * height))
        for row in 0..<height {
            for column in 0..<width {
                var value = values[row * width + column]
                withUnsafeBytes(of: &value) { source in
                    let offset = row * stride + column * MemoryLayout<Float>.stride
                    guard offset + source.count <= bytes.count else { return }
                    bytes.replaceSubrange(offset..<(offset + source.count), with: source)
                }
            }
        }
        let layout = PixelBufferLayout(
            width: width,
            height: height,
            pixelFormat: kCVPixelFormatType_DepthFloat32,
            planes: [PixelPlaneLayout(width: width, height: height, bytesPerRow: stride)]
        )
        return SyntheticPixelBuffer(layout: layout, storage: bytes)
    }

    static func confidence(width: Int, height: Int, stride: Int, values: [UInt8]) -> SyntheticPixelBuffer {
        precondition(values.count == width * height)
        var bytes = Data(repeating: 0xA5, count: stride * height)
        for row in 0..<height {
            for column in 0..<width {
                bytes[row * stride + column] = values[row * width + column]
            }
        }
        let layout = PixelBufferLayout(
            width: width,
            height: height,
            pixelFormat: kCVPixelFormatType_OneComponent8,
            planes: [PixelPlaneLayout(width: width, height: height, bytesPerRow: stride)]
        )
        return SyntheticPixelBuffer(layout: layout, storage: bytes)
    }

    func withLockedReadOnlyPlane0(_ body: (UnsafeRawBufferPointer) throws -> Void) throws {
        lockCount += 1
        defer { unlockCount += 1 }
        try storage.withUnsafeBytes { bytes in try body(bytes) }
    }

    func replaceDepth(at index: Int, with replacement: Float) {
        var replacement = replacement
        withUnsafeBytes(of: &replacement) { bytes in
            storage.replaceSubrange(index * MemoryLayout<Float>.stride..<(index + 1) * MemoryLayout<Float>.stride, with: bytes)
        }
    }
}

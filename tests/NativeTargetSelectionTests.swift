import XCTest
@testable import HybridSpatialScanner

final class NativeTargetSelectionTests: XCTestCase {
    func testObservedTapRequiresExactAcceptedSourceBeforeRevisionBinding() throws {
        let frame = normalizedFrame()
        let viewport = OverlayViewport(
            width: 400, height: 400, orientation: .portrait, generation: 9
        )
        let overlayFrame = OverlayFrameIdentity(epoch: 3, sequence: 12, timestamp: 5)
        let transform = OverlayDisplayTransform(
            frame: overlayFrame, viewport: viewport,
            a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0
        )

        let prepared = NativeTargetSelectionAdapter.prepare(
            tap: NativeTargetTap(screenPoint: SIMD2(250, 250), viewport: viewport),
            normalizedFrame: frame,
            overlayCalibration: calibration(),
            overlayFrame: overlayFrame,
            displayTransform: transform,
            currentEpoch: 3,
            projectFromWorld: identityMatrix(),
            now: 5,
            maximumFrameAge: 0
        )
        let candidate = try XCTUnwrap(try? prepared.get())

        XCTAssertEqual(candidate.observedDepthPixel, SIMD2(2, 2))
        XCTAssertEqual(candidate.projectPositionMetres, SIMD3(0, 0, -2))
        XCTAssertEqual(candidate.provenance, .observed)
        XCTAssertNil(NativeTargetSelectionAdapter.confirm(
            candidate,
            acceptedSourceFrame: SceneDepthFrameIdentity(
                sessionEpoch: 3, sequence: 11, monotonicTimestamp: 5
            ),
            currentSceneRevision: 7
        ))

        let selection = try XCTUnwrap(NativeTargetSelectionAdapter.confirm(
            candidate,
            acceptedSourceFrame: frame.identity,
            currentSceneRevision: 7
        ))
        XCTAssertEqual(selection.sceneRevision, 7)
        XCTAssertEqual(selection.supportIDs, ["depth-3-12"])
        XCTAssertEqual(selection.projectPositionMetres, SIMD3(0, 0, -2))
    }

    func testRejectsFrameViewportEpochAndLowConfidenceMismatch() {
        let viewport = OverlayViewport(
            width: 400, height: 400, orientation: .portrait, generation: 9
        )
        let overlayFrame = OverlayFrameIdentity(epoch: 3, sequence: 12, timestamp: 5)
        let transform = OverlayDisplayTransform(
            frame: overlayFrame, viewport: viewport,
            a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0
        )

        XCTAssertEqual(rejection(
            frame: normalizedFrame(sequence: 11), viewport: viewport,
            overlayFrame: overlayFrame, transform: transform, currentEpoch: 3
        ), .frameMismatch)
        XCTAssertEqual(rejection(
            frame: normalizedFrame(),
            viewport: OverlayViewport(width: 400, height: 400, orientation: .portrait, generation: 10),
            overlayFrame: overlayFrame, transform: transform, currentEpoch: 3
        ), .viewportChanged)
        XCTAssertEqual(rejection(
            frame: normalizedFrame(), viewport: viewport,
            overlayFrame: overlayFrame, transform: transform, currentEpoch: 4
        ), .staleEpoch)
        XCTAssertEqual(rejection(
            frame: normalizedFrame(metricMaskAtSelectedPixel: 0), viewport: viewport,
            overlayFrame: overlayFrame, transform: transform, currentEpoch: 3
        ), .lowConfidenceDepth)
    }

    func testNewerFrameCannotMasqueradeAndDelayedRetainedFrameCanConfirm() throws {
        let retainedFrame = normalizedFrame()
        let viewport = OverlayViewport(
            width: 400, height: 400, orientation: .portrait, generation: 9
        )
        let newerOverlayFrame = OverlayFrameIdentity(epoch: 3, sequence: 13, timestamp: 5.01)
        let newerTransform = OverlayDisplayTransform(
            frame: newerOverlayFrame, viewport: viewport,
            a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0
        )

        XCTAssertEqual(rejection(
            frame: retainedFrame, viewport: viewport,
            overlayFrame: newerOverlayFrame, transform: newerTransform, currentEpoch: 3,
            now: 5.01, maximumFrameAge: 0.25
        ), .frameMismatch, "A newer ARFrame must not inherit the retained frame's identity.")

        let retainedOverlayFrame = OverlayFrameIdentity(epoch: 3, sequence: 12, timestamp: 5)
        let retainedTransform = OverlayDisplayTransform(
            frame: retainedOverlayFrame, viewport: viewport,
            a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0
        )
        let prepared = NativeTargetSelectionAdapter.prepare(
            tap: NativeTargetTap(screenPoint: SIMD2(250, 250), viewport: viewport),
            normalizedFrame: retainedFrame,
            overlayCalibration: calibration(),
            overlayFrame: retainedOverlayFrame,
            displayTransform: retainedTransform,
            currentEpoch: 3,
            projectFromWorld: identityMatrix(),
            now: 5.20,
            maximumFrameAge: 0.25
        )
        let candidate = try XCTUnwrap(try? prepared.get())
        let confirmed = NativeTargetSelectionAdapter.confirm(
            candidate,
            acceptedSourceFrame: retainedFrame.identity,
            currentSceneRevision: 7
        )
        XCTAssertEqual(confirmed?.sceneRevision, 7)
        XCTAssertEqual(rejection(
            frame: retainedFrame, viewport: viewport,
            overlayFrame: retainedOverlayFrame, transform: retainedTransform, currentEpoch: 3,
            now: 5.26, maximumFrameAge: 0.25
        ), .staleFrame)
    }

    func testReviewMaskChangesDerivedTrianglesWithoutChangingRawTopology() {
        let snapshot = ScannerSceneSnapshot(
            revision: 4,
            verticesMetres: [
                SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 1, 0), SIMD3(1, 1, 0),
            ],
            triangleIndices: [0, 1, 2, 1, 3, 2],
            reviewTriangleInclusion: [1, 0],
            dynamicMaskSummary: ScannerDynamicMaskSummary(
                sceneRevision: 4, triangleCount: 2,
                includedTriangleCount: 1, excludedTriangleCount: 1,
                appliedExclusionCount: 1, staleExclusionCount: 0
            ),
            sceneJSON: Data("{}".utf8),
            provenance: [.capturedARKitMesh],
            isFrozen: true
        )

        XCTAssertEqual(ScannerMeshSceneBuilder.displayTriangleIndices(from: snapshot), [0, 1, 2])
        XCTAssertEqual(snapshot.triangleIndices, [0, 1, 2, 1, 3, 2], "Raw export topology remains unchanged.")
    }

    private func rejection(
        frame: NormalizedSceneDepthFrame,
        viewport: OverlayViewport,
        overlayFrame: OverlayFrameIdentity,
        transform: OverlayDisplayTransform,
        currentEpoch: UInt64,
        now: TimeInterval = 5,
        maximumFrameAge: TimeInterval = 0
    ) -> NativeTargetPendingReason? {
        let result = NativeTargetSelectionAdapter.prepare(
            tap: NativeTargetTap(screenPoint: SIMD2(250, 250), viewport: viewport),
            normalizedFrame: frame,
            overlayCalibration: calibration(),
            overlayFrame: overlayFrame,
            displayTransform: transform,
            currentEpoch: currentEpoch,
            projectFromWorld: identityMatrix(),
            now: now,
            maximumFrameAge: maximumFrameAge
        )
        if case .failure(let reason) = result { return reason }
        return nil
    }

    private func normalizedFrame(
        sequence: UInt64 = 12,
        metricMaskAtSelectedPixel: UInt8 = 1
    ) -> NormalizedSceneDepthFrame {
        let layout = PixelBufferLayout(
            width: 4, height: 4, pixelFormat: 0,
            planes: [PixelPlaneLayout(width: 4, height: 4, bytesPerRow: 16)]
        )
        var metricMask = [UInt8](repeating: 1, count: 16)
        metricMask[10] = metricMaskAtSelectedPixel
        return NormalizedSceneDepthFrame(
            identity: SceneDepthFrameIdentity(
                sessionEpoch: 3, sequence: sequence, monotonicTimestamp: 5
            ),
            intrinsics: ScaledSceneDepthIntrinsics(
                fx: 2, fy: 2, cx: 2, cy: 2,
                calibrationImageWidth: 4, calibrationImageHeight: 4,
                depthCropX: 0, depthCropY: 0,
                depthCropWidth: 4, depthCropHeight: 4,
                depthWidth: 4, depthHeight: 4
            ),
            worldFromCamera: identityMatrix().map(Float.init),
            rgbLayout: layout,
            depthLayout: layout,
            confidenceLayout: nil,
            depthMeters: [Float](repeating: 2, count: 16),
            confidence: [UInt8](repeating: 2, count: 16),
            validDepthMask: [UInt8](repeating: 1, count: 16),
            metricFusionMask: metricMask,
            provisionalPreviewMask: [UInt8](repeating: 0, count: 16)
        )
    }

    private func calibration() -> OverlayDepthCalibration {
        OverlayDepthCalibration(
            imageWidth: 4, imageHeight: 4,
            depthWidth: 4, depthHeight: 4,
            fx: 2, fy: 2, cx: 2, cy: 2,
            depthCropX: 0, depthCropY: 0,
            depthCropWidth: 4, depthCropHeight: 4
        )
    }

    private func identityMatrix() -> [Double] {
        [
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        ]
    }
}

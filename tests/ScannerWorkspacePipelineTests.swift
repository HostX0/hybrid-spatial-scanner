import XCTest
@testable import HybridSpatialScanner

final class ScannerWorkspacePipelineTests: XCTestCase {
    func testSyntheticProjectCheckpointReopenAndReplay() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-workspace-tests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID()
        let captureID = UUID()

        let createdSnapshot = try await createSyntheticProject(
            root: root, projectID: projectID, captureID: captureID
        )
        XCTAssertEqual(createdSnapshot.revision, 3)
        XCTAssertEqual(createdSnapshot.verticesMetres.count, 3)
        XCTAssertEqual(createdSnapshot.triangleIndices, [0, 1, 2])

        let reopened = try HSSProjectRuntime(
            projectID: projectID,
            captureID: captureID,
            sourceIsSynthetic: true,
            trustedRoot: root,
            openMode: .openExisting
        )
        let replay = try await reopened.replayCommittedSource()
        XCTAssertEqual(replay.appliedEventCount, 3)
        let replayedSnapshot = try await reopened.snapshot(
            provenance: [.syntheticTest, .importedReplay],
            frozen: true
        )
        XCTAssertEqual(replayedSnapshot.revision, createdSnapshot.revision)
        XCTAssertEqual(replayedSnapshot.verticesMetres, createdSnapshot.verticesMetres)
        XCTAssertEqual(replayedSnapshot.triangleIndices, createdSnapshot.triangleIndices)
        XCTAssertTrue(replayedSnapshot.isFrozen)
    }

    func testKeyframeSelectorRejectsStationaryRepeatAndAcceptsUsefulMotion() {
        let selector = SceneDepthKeyframeSelector()
        let identity = matrix()

        XCTAssertTrue(selector.shouldRetain(frame(pose: identity)))
        XCTAssertFalse(selector.shouldRetain(frame(pose: identity)), "A stationary repeat is correlated evidence, not a new keyframe.")

        var translated = identity
        translated[12] = 0.16
        XCTAssertTrue(selector.shouldRetain(frame(pose: translated)))

        let angle: Float = 11 * .pi / 180
        var rotated = translated
        rotated[0] = cos(angle)
        rotated[2] = -sin(angle)
        rotated[8] = sin(angle)
        rotated[10] = cos(angle)
        XCTAssertTrue(selector.shouldRetain(frame(pose: rotated)))
    }

    func testKeyframeSelectorAcceptsNewCoarseSupportWithoutInventingDepth() {
        let selector = SceneDepthKeyframeSelector()
        let width = 8
        let height = 6
        var partial = [UInt8](repeating: 0, count: width * height)
        for y in 0..<height {
            for x in 0..<(width / 2) { partial[y * width + x] = 1 }
        }
        let full = [UInt8](repeating: 1, count: width * height)

        XCTAssertTrue(selector.shouldRetain(frame(width: width, height: height, mask: partial)))
        XCTAssertTrue(selector.shouldRetain(frame(width: width, height: height, mask: full)))
        XCTAssertFalse(selector.shouldRetain(frame(width: width, height: height, mask: full)))
    }

    func testKeyframeSelectorStopsAtExplicitRetainedFrameCapacity() {
        let selector = SceneDepthKeyframeSelector(maximumRetainedFrames: 2)
        XCTAssertTrue(selector.shouldRetain(frame()))
        var moved = matrix()
        moved[12] = 0.16
        XCTAssertTrue(selector.shouldRetain(frame(pose: moved)))
        moved[12] = 0.32
        XCTAssertFalse(selector.shouldRetain(frame(pose: moved)))
        XCTAssertTrue(selector.hasReachedCapacity)
        XCTAssertEqual(selector.retainedFrameCount, 2)
    }

    private func frame(
        width: Int = 8,
        height: Int = 6,
        pose: [Float]? = nil,
        mask: [UInt8]? = nil
    ) -> NormalizedSceneDepthFrame {
        let sampleCount = width * height
        let selectedMask = mask ?? [UInt8](repeating: 1, count: sampleCount)
        let layout = PixelBufferLayout(
            width: width,
            height: height,
            pixelFormat: 0,
            planes: [PixelPlaneLayout(width: width, height: height, bytesPerRow: width * 4)]
        )
        return NormalizedSceneDepthFrame(
            identity: SceneDepthFrameIdentity(sessionEpoch: 1, sequence: 1, monotonicTimestamp: 1),
            intrinsics: ScaledSceneDepthIntrinsics(
                fx: 4, fy: 4, cx: 4, cy: 3,
                calibrationImageWidth: width, calibrationImageHeight: height,
                depthCropX: 0, depthCropY: 0,
                depthCropWidth: Float(width), depthCropHeight: Float(height),
                depthWidth: width, depthHeight: height
            ),
            worldFromCamera: pose ?? matrix(),
            rgbLayout: layout,
            depthLayout: layout,
            confidenceLayout: nil,
            depthMeters: [Float](repeating: 1, count: sampleCount),
            confidence: [UInt8](repeating: 2, count: sampleCount),
            validDepthMask: selectedMask,
            metricFusionMask: selectedMask,
            provisionalPreviewMask: [UInt8](repeating: 0, count: sampleCount)
        )
    }

    private func createSyntheticProject(
        root: URL,
        projectID: UUID,
        captureID: UUID
    ) async throws -> ScannerSceneSnapshot {
        let runtime = try HSSProjectRuntime(
            projectID: projectID,
            captureID: captureID,
            sourceIsSynthetic: true,
            trustedRoot: root,
            openMode: .create
        )
        let identity = matrix().map(Double.init)
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1,
                relationshipState: .unresolved,
                projectFromWorld: identity,
                transformRevision: 1
            ),
            eventSequence: 1,
            monotonicTime: 1
        )
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1,
                relationshipState: .accepted,
                projectFromWorld: identity,
                transformRevision: 2
            ),
            eventSequence: 2,
            monotonicTime: 2
        )
        let mesh = OwnedMeshAnchorEvent(
            kind: .add,
            anchorID: UUID(),
            anchorRevision: 1,
            generation: 1,
            sessionEpoch: 1,
            sequence: 3,
            monotonicTimestamp: 3,
            worldFromAnchor: matrix(),
            payload: MeshAnchorPayload(
                vertices: [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 0, -1)],
                normals: [],
                triangleIndices: [0, 1, 2],
                classifications: [],
                localBoundsMin: SIMD3(0, 0, -1),
                localBoundsMax: SIMD3(1, 0, 0)
            )
        )
        try await runtime.submitMeshEvent(mesh, eventSequence: 3)
        let checkpoint = try await runtime.checkpoint()
        XCTAssertEqual(checkpoint.durableEventCount, 3)
        return try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
    }

    private func matrix() -> [Float] {
        [
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        ]
    }
}

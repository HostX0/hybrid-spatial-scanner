import XCTest
import UniformTypeIdentifiers
@testable import HybridSpatialScanner

final class ScannerWorkspacePipelineTests: XCTestCase {
    func testEmptyDraftCreatesAndReopensBeforeAnySourceEvent() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-empty-draft-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let model = await MainActor.run {
            ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true)
        }
        await MainActor.run { model.createProject(named: "Empty draft") }
        for _ in 0..<100 {
            if await MainActor.run(body: { model.phase == .ready }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let createdID = await MainActor.run { model.selectedProjectID }
        let createdPhase = await MainActor.run { model.phase }
        let projectID = try XCTUnwrap(createdID)
        XCTAssertEqual(createdPhase, .ready)
        await MainActor.run {
            model.showLibrary()
            model.openProject(id: projectID)
        }
        for _ in 0..<100 {
            if await MainActor.run(body: { model.phase == .frozen }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let reopenedPhase = await MainActor.run { model.phase }
        let reopenedRevision = await MainActor.run { model.latestSnapshot?.revision }
        XCTAssertEqual(reopenedPhase, .frozen)
        XCTAssertEqual(reopenedRevision, 0)
    }

    func testNativeResourceCheckpointAndUnmeasuredRecoveryStayPaused() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-resource-native-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let runtime = try HSSProjectRuntime(
            projectID: UUID(), captureID: UUID(), sourceIsSynthetic: true,
            trustedRoot: root, openMode: .create
        )
        let identity = matrix().map(Double.init)
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1, relationshipState: .unresolved,
                projectFromWorld: identity, transformRevision: 1
            ), eventSequence: 1, monotonicTime: 1
        )
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1, relationshipState: .accepted,
                projectFromWorld: identity, transformRevision: 2
            ), eventSequence: 2, monotonicTime: 2
        )
        let nominal = ScannerResourceReading(
            monotonicTime: 1, thermalState: 0,
            memoryPressure: nil, freeStorageBytes: 2 * 1024 * 1024 * 1024
        )
        let initial = try await runtime.observeResource(nominal)
        XCTAssertEqual(initial.lastAcceptedSequence, 1)
        XCTAssertFalse(initial.monitoringComplete, "No memory-warning event is not a nominal measurement.")
        XCTAssertTrue(initial.acceptMetricObservations)

        let unsafe = try await runtime.observeResource(.init(
            monotonicTime: 2, thermalState: 3,
            memoryPressure: nil, freeStorageBytes: 2 * 1024 * 1024 * 1024
        ))
        XCTAssertEqual(unsafe.lastAcceptedSequence, 2)
        XCTAssertTrue(unsafe.requestCheckpoint)
        XCTAssertFalse(unsafe.acceptMetricObservations)
        XCTAssertTrue(unsafe.retainExistingSources)
        let outcome = try await runtime.completeResourceCheckpoint()
        XCTAssertNotNil(outcome.checkpoint, "The actual local store checkpoint precedes acknowledgement.")
        XCTAssertNil(outcome.failure)
        XCTAssertTrue(outcome.decision.pauseCapture)
        XCTAssertFalse(outcome.decision.requestCheckpoint)

        for time in 3...6 {
            let recovery = try await runtime.observeResource(.init(
                monotonicTime: Double(time), thermalState: 0,
                memoryPressure: nil, freeStorageBytes: 2 * 1024 * 1024 * 1024
            ))
            XCTAssertTrue(recovery.pauseCapture)
            XCTAssertFalse(recovery.monitoringComplete)
        }
    }

    func testResourceWarningDoesNotInventNominalMemory() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-resource-reading-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        await MainActor.run {
            let monitor = ScannerIOSResourceMonitor()
            XCTAssertNil(monitor.read(storageDirectory: root).memoryPressure)
            monitor.noteMemoryWarning()
            XCTAssertEqual(monitor.read(storageDirectory: root).memoryPressure, 1)
        }
    }

    func testResourceCheckpointCanReuseVerifiedUnchangedRevision() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-resource-durable-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let runtime = try HSSProjectRuntime(
            projectID: UUID(), captureID: UUID(), sourceIsSynthetic: true,
            trustedRoot: root, openMode: .create
        )
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1, relationshipState: .unresolved,
                projectFromWorld: matrix().map(Double.init), transformRevision: 1
            ), eventSequence: 1, monotonicTime: 1
        )
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 1, relationshipState: .accepted,
                projectFromWorld: matrix().map(Double.init), transformRevision: 2
            ), eventSequence: 2, monotonicTime: 2
        )
        let verified = try await runtime.checkpoint()
        XCTAssertEqual(verified.durableRevision, 1)
        XCTAssertEqual(verified.durableEventCount, 2)
        let sceneRevision = try await runtime.stats().scene_revision
        XCTAssertEqual(sceneRevision, 2)
        let unsafe = try await runtime.observeResource(.init(
            monotonicTime: 3, thermalState: 3,
            memoryPressure: nil, freeStorageBytes: 2 * 1024 * 1024 * 1024
        ))
        XCTAssertTrue(unsafe.requestCheckpoint)
        let outcome = try await runtime.completeResourceCheckpoint()
        XCTAssertEqual(outcome.checkpoint, verified)
        XCTAssertNil(outcome.failure)
        XCTAssertTrue(outcome.decision.pauseCapture)
    }

    func testResourceCheckpointFailureIsAcknowledgedOnlyAfterAttempt() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-resource-empty-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let runtime = try HSSProjectRuntime(
            projectID: UUID(), captureID: UUID(), sourceIsSynthetic: true,
            trustedRoot: root, openMode: .create
        )
        _ = try await runtime.observeResource(.init(
            monotonicTime: 1, thermalState: 3,
            memoryPressure: nil, freeStorageBytes: 2 * 1024 * 1024 * 1024
        ))
        let outcome = try await runtime.completeResourceCheckpoint()
        XCTAssertNil(outcome.checkpoint)
        XCTAssertNotNil(outcome.failure)
        XCTAssertTrue(outcome.decision.pauseCapture)
        XCTAssertFalse(outcome.decision.requestCheckpoint)
    }

    func testHSSCANFileIsSelectableForValidatedImport() throws {
        let type = try XCTUnwrap(UTType(filenameExtension: "hsscan"))
        XCTAssertTrue(ScannerProjectImportContentTypes.accepted.contains {
            type.conforms(to: $0)
        })
    }

    func testDuplicateSemanticFragmentIDSuppressesReview() {
        let fragmentID = UUID().uuidString.lowercased()
        let fragment = """
        {"id":"\(fragmentID)","state":"supported","geometry_origin":"fitted",\
        "segment_m":[[0,0],[1,0]],"source_revision":"7"}
        """
        func snapshot(_ fragments: String) -> ScannerSceneSnapshot {
            let plan = """
            {"scene_revision":"7","source_is_synthetic":false,"boundary_state":"open",\
            "validity":"unavailable","height":{"reference_state":"floor_unknown"},\
            "fragments":[\(fragments)],"area":{"availability":"unavailable","area_m2":null}}
            """
            return ScannerSceneSnapshot(
                revision: 7,
                verticesMetres: [],
                triangleIndices: [],
                sceneJSON: Data("{}".utf8),
                planAnnotation: ScannerPlanAnnotationSnapshot(
                    sceneRevision: 7, planAvailable: true, annotationLayoutAvailable: false,
                    referenceState: 1, boundaryState: 1, planAvailability: 2,
                    areaAvailability: 2, areaMetresSquared: nil, cutHeightMetres: 1.2,
                    isSynthetic: false, planJSON: Data(plan.utf8), annotationJSON: nil
                ),
                provenance: [],
                isFrozen: true
            )
        }
        XCTAssertNotNil(ScannerAcceptedPlanDecoder.decode(snapshot(fragment)))
        XCTAssertNil(ScannerAcceptedPlanDecoder.decode(snapshot("\(fragment),\(fragment)")))
    }

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
        let provisional = try XCTUnwrap(createdSnapshot.planAnnotation)
        XCTAssertEqual(provisional.sceneRevision, 3)
        XCTAssertTrue(provisional.planAvailable)
        XCTAssertEqual(provisional.referenceState, 1, "The floor remains unknown until confirmed.")
        XCTAssertNil(provisional.areaMetresSquared)
        XCTAssertNotNil(provisional.planJSON)
        XCTAssertNotNil(provisional.annotationJSON)
        let review = try XCTUnwrap(ScannerAcceptedPlanDecoder.decode(createdSnapshot, language: .arabic))
        XCTAssertEqual(review.plan.dimensionAnnotations.count, 0,
                       "An unavailable plan suppresses the B-032 numeric length.")
        XCTAssertNil(review.measurements.last?.value)
        XCTAssertTrue(review.measurements.last?.detail?.contains("غير معروف") == true)
        let exported = try ScannerOfflineExporter.export(
            snapshot: createdSnapshot, projectID: projectID, trustedRoot: root
        )
        let semanticManifest = try XCTUnwrap(JSONSerialization.jsonObject(
            with: Data(contentsOf: exported.appendingPathComponent("semantic.provenance.json"))
        ) as? [String: Any])
        XCTAssertEqual(semanticManifest["scene_revision"] as? String, "3")
        XCTAssertEqual(semanticManifest["plan_available"] as? Bool, true)
        XCTAssertTrue(semanticManifest["area_m2"] is NSNull)
        XCTAssertNotNil(semanticManifest["plan"] as? [String: Any])
        XCTAssertNotNil(semanticManifest["annotations"] as? [String: Any])
        let stale = ScannerSceneSnapshot(
            revision: createdSnapshot.revision + 1,
            verticesMetres: createdSnapshot.verticesMetres,
            triangleIndices: createdSnapshot.triangleIndices,
            sceneJSON: createdSnapshot.sceneJSON,
            planAnnotation: createdSnapshot.planAnnotation,
            provenance: createdSnapshot.provenance,
            isFrozen: true
        )
        XCTAssertNil(ScannerAcceptedPlanDecoder.decode(stale))
        XCTAssertThrowsError(try ScannerOfflineExporter.export(
            snapshot: stale, projectID: projectID, trustedRoot: root
        ))

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
        XCTAssertEqual(replayedSnapshot.planAnnotation, createdSnapshot.planAnnotation)
        try await reopened.setPlanReference(
            projectFromFloor: matrix().map(Double.init),
            cutHeightMetres: 1.2,
            sessionEpoch: 1,
            eventSequence: 4,
            monotonicTime: 4
        )
        let confirmed = try await reopened.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(confirmed.revision, 4)
        XCTAssertEqual(confirmed.planAnnotation?.referenceState, 0)
        XCTAssertNil(confirmed.planAnnotation?.areaMetresSquared,
                     "A confirmed floor does not establish finished-room area.")
        XCTAssertEqual(createdSnapshot.planAnnotation?.sceneRevision, 3)
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

    func testCommandJournalDispositionControlsRevisionBoundary() throws {
        let command = Data("{\"command_id\":\"fixture\"}".utf8)
        let result = Data("{\"status\":\"fixture\"}".utf8)

        let applied = try HSSCommandJournalEntry(
            disposition: .applied,
            commandJSON: command,
            resultJSON: result,
            beforeSceneRevision: 7,
            afterSceneRevision: 8
        )
        XCTAssertEqual(applied.afterSceneRevision, 8)

        for disposition in [HSSCommandJournalDisposition.conflict, .rejected, .pendingExclusion] {
            let audit = try HSSCommandJournalEntry(
                disposition: disposition,
                commandJSON: command,
                resultJSON: result,
                beforeSceneRevision: 8,
                afterSceneRevision: 8
            )
            XCTAssertEqual(audit.beforeSceneRevision, audit.afterSceneRevision)
        }

        XCTAssertThrowsError(try HSSCommandJournalEntry(
            disposition: .conflict,
            commandJSON: command,
            resultJSON: result,
            beforeSceneRevision: 8,
            afterSceneRevision: 9
        ))
    }

    func testOfflineExportEligibilityRequiresFrozenPersistedMeasuredRevision() throws {
        let projectID = UUID()
        let snapshot = exportSnapshot(projectID: projectID)
        let project = ScannerProjectSummary(
            id: projectID,
            name: "Export fixture",
            updatedAt: Date(timeIntervalSince1970: 1),
            sceneRevision: snapshot.revision,
            state: .frozen
        )

        XCTAssertTrue(ScannerExportEligibility.isEligible(
            snapshot: snapshot,
            project: project,
            saveState: .saved(Date(timeIntervalSince1970: 1)),
            phase: .frozen
        ))
        XCTAssertFalse(ScannerExportEligibility.isEligible(
            snapshot: snapshot,
            project: project,
            saveState: .hasUnsavedChanges,
            phase: .frozen
        ))

        let unknownJSON = String(decoding: snapshot.sceneJSON, as: UTF8.self)
            .replacingOccurrences(of: "\"geometry_origin\":\"fitted\"", with: "\"geometry_origin\":\"unknown\"")
        let unknownOnly = ScannerSceneSnapshot(
            revision: snapshot.revision,
            verticesMetres: snapshot.verticesMetres,
            triangleIndices: snapshot.triangleIndices,
            sceneJSON: Data(unknownJSON.utf8),
            provenance: snapshot.provenance,
            isFrozen: true
        )
        XCTAssertFalse(ScannerExportEligibility.isEligible(
            snapshot: unknownOnly,
            project: project,
            saveState: .clean,
            phase: .frozen
        ), "Unknown plan evidence must not qualify as measured export geometry.")
    }

    func testOfflineExportPublishesCompleteLocalBundleAtomically() throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-export-tests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID()
        let projectDirectory = root.appendingPathComponent(
            ScannerOfflineExportStore.projectDirectoryName(projectID),
            isDirectory: true
        )
        try FileManager.default.createDirectory(at: projectDirectory, withIntermediateDirectories: true)
        try Data("{}".utf8).write(to: projectDirectory.appendingPathComponent("project.json"))

        let destination = try ScannerOfflineExporter.export(
            snapshot: exportSnapshot(projectID: projectID),
            projectID: projectID,
            trustedRoot: root
        )
        let names = try Set(FileManager.default.contentsOfDirectory(atPath: destination.path))
        XCTAssertEqual(names, ScannerOfflineArtifactSet.requiredNames.union(["export-manifest.json"]))
        XCTAssertTrue(try String(contentsOf: destination.appendingPathComponent("plan.pdf"), encoding: .utf8)
            .hasPrefix("%PDF-1.4"))
        let svg = try String(contentsOf: destination.appendingPathComponent("plan.svg"), encoding: .utf8)
        XCTAssertTrue(svg.contains("fragment-measured-edge"))
        XCTAssertFalse(svg.contains("fragment-unknown-edge"), "Unknown geometry stays in provenance and is not drawn.")
        let planManifest = try String(
            contentsOf: destination.appendingPathComponent("plan.provenance.json"),
            encoding: .utf8
        )
        XCTAssertTrue(planManifest.contains("unknown-edge"))
        XCTAssertTrue(planManifest.contains("\"has_explicit_segment\":false"))
        let semantic = try XCTUnwrap(JSONSerialization.jsonObject(
            with: Data(contentsOf: destination.appendingPathComponent("semantic.provenance.json"))
        ) as? [String: Any])
        XCTAssertEqual(semantic["scene_revision"] as? String, "7")
        XCTAssertEqual(semantic["authority"] as? String, "derived_plan_annotation_snapshot")
        XCTAssertEqual(semantic["measured_scene_authority"] as? String, "scene.provenance.json")
        XCTAssertTrue(semantic["area_m2"] is NSNull)
        let dxfManifest = try String(
            contentsOf: destination.appendingPathComponent("dxf.provenance.json"),
            encoding: .utf8
        )
        XCTAssertTrue(dxfManifest.contains("\"id\":\"unknown-edge\""))
        XCTAssertTrue(dxfManifest.contains("\"has_explicit_segment\":false"))
        let dxfJSON = try XCTUnwrap(
            JSONSerialization.jsonObject(with: Data(dxfManifest.utf8)) as? [String: Any]
        )
        let primitiveIDs = try XCTUnwrap(dxfJSON["primitives"] as? [[String: Any]])
            .compactMap { $0["id"] as? String }
        XCTAssertEqual(primitiveIDs, ["measured-edge"])
        let obj = try String(contentsOf: destination.appendingPathComponent("scene.obj"), encoding: .utf8)
        XCTAssertTrue(obj.contains("f 1 2 3"))

        let exports = projectDirectory.appendingPathComponent("exports", isDirectory: true)
        XCTAssertFalse(try FileManager.default.contentsOfDirectory(atPath: exports.path)
            .contains(where: { $0.hasPrefix(".pending-") }))
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
                vertices: [SIMD3(0, 0, 0), SIMD3(1, 2, 0), SIMD3(0, 2, -1)],
                normals: [],
                triangleIndices: [0, 1, 2],
                classifications: [],
                localBoundsMin: SIMD3(0, 0, -1),
                localBoundsMax: SIMD3(1, 2, 0)
            )
        )
        try await runtime.submitMeshEvent(mesh, eventSequence: 3)
        let checkpoint = try await runtime.checkpoint()
        XCTAssertEqual(checkpoint.durableEventCount, 3)
        return try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
    }

    private func exportSnapshot(projectID: UUID) -> ScannerSceneSnapshot {
        let json = """
        {"schema_version":"1.1","project_id":"\(projectID.uuidString.lowercased())","scene_revision":"7","source_is_synthetic":true,"units":"m","basis":{"handedness":"right","up":"+Y","floor_origin_m":[0,0,0],"floor_u":[1,0,0],"floor_v":[0,0,-1],"floor_n":[0,1,0]},"epochs":[],"nodes":[{"id":"node-a","position_m":[0,0,0],"provenance":{"geometry_origin":"fitted","support_ids":["mesh-1"]}},{"id":"node-b","position_m":[1,0,0],"provenance":{"geometry_origin":"fitted","support_ids":["mesh-1"]}},{"id":"node-c","position_m":[2,0,0],"provenance":{"geometry_origin":"unknown","support_ids":["gap-1"]}}],"edges":[{"id":"measured-edge","start_node_id":"node-a","end_node_id":"node-b","provenance":{"geometry_origin":"fitted","support_ids":["mesh-1"]}},{"id":"unknown-edge","start_node_id":"node-b","end_node_id":"node-c","provenance":{"geometry_origin":"unknown","support_ids":["gap-1"]}}],"surfaces":[],"features":[],"regions":[],"objects":[],"constraints":[],"issues":[],"quality_state":"provisional"}
        """
        return ScannerSceneSnapshot(
            revision: 7,
            verticesMetres: [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 1, 0)],
            triangleIndices: [0, 1, 2],
            sceneJSON: Data(json.utf8),
            provenance: [.syntheticTest],
            isFrozen: true
        )
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

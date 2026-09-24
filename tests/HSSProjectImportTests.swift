import CryptoKit
import simd
import XCTest
@testable import HybridSpatialScanner

final class HSSProjectImportTests: XCTestCase {
    func testChecksummedProjectMetadataPreservesProjectIdentityAndProvenance() throws {
        let projectID = UUID(uuidString: "01234567-89AB-CDEF-0123-456789ABCDEF")!
        let data = metadata(projectID: projectID, synthetic: false)

        let parsed = try HSSImportedProjectMetadata.parse(data)

        XCTAssertEqual(parsed.projectID, projectID)
        XCTAssertFalse(parsed.sourceIsSynthetic)
        XCTAssertEqual(projectID.hssDirectoryName, "0123456789abcdef0123456789abcdef")
    }

    func testTamperedMetadataCannotChooseAnAdoptionDirectory() {
        var data = metadata(projectID: UUID(), synthetic: true)
        data[data.startIndex] = UInt8(ascii: "[")

        XCTAssertThrowsError(try HSSImportedProjectMetadata.parse(data))
    }

    @MainActor
    func testArchiveWorkLeavesWorkspaceActorBeforeRunningBlockingOperation() async throws {
        let ranOffMainThread = try await HSSProjectImportBackgroundWork.perform {
            !Thread.isMainThread
        }

        XCTAssertTrue(ranOffMainThread)
    }

    @MainActor
    func testArchiveExportWorkLeavesWorkspaceActorBeforeRunningBlockingOperation() async throws {
        let ranOffMainThread = try await HSSProjectArchiveExport.perform {
            !Thread.isMainThread
        }

        XCTAssertTrue(ranOffMainThread)
    }

    @MainActor
    func testPublicationFailureRollsBackOnlyThisNewlyMovedProject() async throws {
        enum InjectedFailure: Error { case catalog }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-import-adoption-\(UUID().uuidString)")
        let staged = root.appendingPathComponent("stage/project")
        let destination = root.appendingPathComponent("library/project")
        try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
        XCTAssertTrue(FileManager.default.fileExists(atPath: destination.deletingLastPathComponent().path))
        try Data("staged".utf8).write(to: staged.appendingPathComponent("marker"))
        defer { try? FileManager.default.removeItem(at: root) }

        do {
            _ = try await HSSProjectImportAdoption.adopt(
                stagedProject: staged, destination: destination,
                validate: { "validated" },
                publish: { _ in throw InjectedFailure.catalog }
            )
            XCTFail("Injected catalog publication failure must escape adoption.")
        } catch InjectedFailure.catalog {
            XCTAssertFalse(FileManager.default.fileExists(atPath: destination.path))
        }
    }

    @MainActor
    func testExistingDestinationIsNeverRemovedByFailedAdoption() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-import-existing-\(UUID().uuidString)")
        let staged = root.appendingPathComponent("stage/project")
        let destination = root.appendingPathComponent("library/project")
        try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        try Data("existing".utf8).write(to: destination.appendingPathComponent("marker"))
        defer { try? FileManager.default.removeItem(at: root) }

        do {
            _ = try await HSSProjectImportAdoption.adopt(
                stagedProject: staged, destination: destination,
                validate: { "unreachable" }, publish: { _ in }
            )
            XCTFail("An existing project must prevent adoption.")
        } catch HSSProjectImportError.projectAlreadyExists {
            XCTAssertTrue(FileManager.default.fileExists(atPath: staged.path))
            XCTAssertEqual(try Data(contentsOf: destination.appendingPathComponent("marker")), Data("existing".utf8))
        }
    }

    func testCommittedProjectExportsAndImportsWithReplayAndCorruptionRejection() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hsscan-round-trip-\(UUID().uuidString)", isDirectory: true)
        let sourceRoot = root.appendingPathComponent("source", isDirectory: true)
        let importedRoot = root.appendingPathComponent("imported", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        try FileManager.default.createDirectory(at: sourceRoot, withIntermediateDirectories: true)

        let projectID = UUID()
        let captureID = UUID()
        let source = try await createCommittedSyntheticProject(
            root: sourceRoot, projectID: projectID, captureID: captureID
        )
        let sourceProject = sourceRoot.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        let firstName = ScannerProjectArchiveExportName.make(
            projectID: projectID,
            sceneRevision: source.snapshot.revision,
            exportID: UUID(uuidString: "01234567-89AB-CDEF-0123-456789ABCDEF")!
        )
        let secondName = ScannerProjectArchiveExportName.make(
            projectID: projectID,
            sceneRevision: source.snapshot.revision,
            exportID: UUID(uuidString: "FEDCBA98-7654-3210-FEDC-BA9876543210")!
        )
        XCTAssertNotEqual(firstName, secondName)
        XCTAssertTrue(firstName.hasPrefix("\(projectID.hssDirectoryName)-r\(source.snapshot.revision)-"))
        XCTAssertTrue(firstName.hasSuffix(".hsscan"))
        let archive = root.appendingPathComponent(firstName, isDirectory: false)
        let secondArchive = root.appendingPathComponent(secondName, isDirectory: false)
        try HSSProjectArchiveExportBridge.writeHSSCANArchive(
            forProjectDirectory: sourceProject,
            to: archive
        )
        try HSSProjectArchiveExportBridge.writeHSSCANArchive(
            forProjectDirectory: sourceProject,
            to: secondArchive
        )
        XCTAssertTrue(FileManager.default.fileExists(atPath: archive.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: secondArchive.path))

        let staging = root.appendingPathComponent("staging", isDirectory: true)
        try HSSProjectImportBridge.extractHSSCANArchive(at: archive, toStagingDirectory: staging)
        let metadata = try HSSImportedProjectMetadata.parse(Data(contentsOf: staging.appendingPathComponent("project.json")))
        XCTAssertEqual(metadata.projectID, projectID)
        XCTAssertTrue(metadata.sourceIsSynthetic)
        try FileManager.default.createDirectory(at: importedRoot, withIntermediateDirectories: true)
        try FileManager.default.moveItem(
            at: staging,
            to: importedRoot.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        )

        let imported = try HSSProjectRuntime(
            projectID: metadata.projectID,
            captureID: UUID(),
            sourceIsSynthetic: metadata.sourceIsSynthetic,
            trustedRoot: importedRoot,
            openMode: .openExisting
        )
        let replay = try await imported.replayCommittedSource()
        let importedSnapshot = try await imported.snapshot(
            provenance: [.syntheticTest, .importedReplay],
            frozen: true
        )
        XCTAssertEqual(replay.durableRevision, source.durableRevision)
        XCTAssertEqual(importedSnapshot.revision, source.snapshot.revision)
        XCTAssertEqual(importedSnapshot.verticesMetres, source.snapshot.verticesMetres)
        XCTAssertEqual(importedSnapshot.triangleIndices, source.snapshot.triangleIndices)
        XCTAssertEqual(importedSnapshot.provenance, [.syntheticTest, .importedReplay])

        var corrupt = try Data(contentsOf: archive)
        XCTAssertGreaterThan(corrupt.count, 30)
        let firstNameBytes = Int(corrupt[26]) | (Int(corrupt[27]) << 8)
        let firstExtraBytes = Int(corrupt[28]) | (Int(corrupt[29]) << 8)
        let firstPayload = 30 + firstNameBytes + firstExtraBytes
        XCTAssertLessThan(firstPayload, corrupt.count)
        corrupt[firstPayload] = corrupt[firstPayload] ^ 0x01
        let corruptedArchive = root.appendingPathComponent("corrupt.hsscan", isDirectory: false)
        try corrupt.write(to: corruptedArchive, options: [.atomic])
        XCTAssertThrowsError(
            try HSSProjectImportBridge.extractHSSCANArchive(
                at: corruptedArchive,
                toStagingDirectory: root.appendingPathComponent("corrupt-staging", isDirectory: true)
            )
        )
    }

    func testImportedCatalogTracksSceneRevisionAndRecoveredSequence() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-import-resume-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let sourceRoot = root.appendingPathComponent("source", isDirectory: true)
        let libraryRoot = root.appendingPathComponent("library", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceRoot, withIntermediateDirectories: true)
        let projectID = UUID()
        let captureID = UUID()
        _ = try await createCommittedSyntheticProject(
            root: sourceRoot, projectID: projectID, captureID: captureID
        )
        let source = try HSSProjectRuntime(
            projectID: projectID, captureID: captureID, sourceIsSynthetic: true,
            trustedRoot: sourceRoot, openMode: .openExisting
        )
        let firstReplay = try await source.replayCommittedSource()
        XCTAssertEqual(firstReplay.lastEventSequence, 3)
        try await source.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 2, relationshipState: .unresolved,
                projectFromWorld: matrix().map(Double.init), transformRevision: 1
            ), eventSequence: 10, monotonicTime: 10
        )
        let sourceCheckpoint = try await source.checkpoint()
        XCTAssertEqual(sourceCheckpoint.durableRevision, 2)
        let archive = root.appendingPathComponent("source.hsscan")
        try HSSProjectArchiveExportBridge.writeHSSCANArchive(
            forProjectDirectory: sourceRoot.appendingPathComponent(projectID.hssDirectoryName),
            to: archive
        )

        let workspace = await MainActor.run {
            ScannerWorkspaceModel(trustedRoot: libraryRoot, sourceIsSynthetic: true)
        }
        await MainActor.run { workspace.importProjectArchive(at: archive) }
        for _ in 0..<200 {
            if await MainActor.run(body: { workspace.phase == .frozen }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let imported = await MainActor.run {
            (workspace.latestSnapshot, workspace.localProjects.first(where: { $0.id == projectID }),
             workspace.canExportProjectArchive, workspace.phase)
        }
        XCTAssertEqual(imported.3, .frozen)
        XCTAssertEqual(imported.0?.revision, 4)
        XCTAssertEqual(imported.1?.sceneRevision, 4,
                       "Catalog stores accepted scene revision, not manifest revision 2.")
        XCTAssertTrue(imported.2, "The imported frozen project remains archive-export eligible.")
        let frozen = try XCTUnwrap(imported.0)
        let catalog = try XCTUnwrap(imported.1)
        let measuredScene = ScannerSceneSnapshot(
            revision: frozen.revision,
            verticesMetres: frozen.verticesMetres,
            triangleIndices: frozen.triangleIndices,
            sceneJSON: Data("""
            {"project_id":"\(projectID.uuidString.lowercased())","scene_revision":"4",\
            "nodes":[{"id":"a","position_m":[0,0,0]},{"id":"b","position_m":[1,0,0]}],\
            "edges":[{"start_node_id":"a","end_node_id":"b",\
            "provenance":{"geometry_origin":"fitted","support_ids":["mesh"]}}]}
            """.utf8),
            provenance: frozen.provenance,
            isFrozen: true
        )
        XCTAssertTrue(ScannerExportEligibility.isEligible(
            snapshot: measuredScene, project: catalog,
            saveState: .clean, phase: .frozen
        ))
        let staleManifestRevision = ScannerProjectSummary(
            id: catalog.id, name: catalog.name, updatedAt: catalog.updatedAt,
            sceneRevision: sourceCheckpoint.durableRevision, state: catalog.state
        )
        XCTAssertFalse(ScannerExportEligibility.isEligible(
            snapshot: measuredScene, project: staleManifestRevision,
            saveState: .clean, phase: .frozen
        ))

        await MainActor.run { workspace.showLibrary() }
        let resumed = try HSSProjectRuntime(
            projectID: projectID, captureID: UUID(), sourceIsSynthetic: true,
            trustedRoot: libraryRoot, openMode: .openExisting
        )
        let cursor = try await resumed.replayCommittedSource()
        XCTAssertEqual(cursor.captureID, captureID)
        XCTAssertEqual(cursor.lastEventSequence, 10)
        XCTAssertEqual(cursor.acceptedSceneRevision, 4)
        XCTAssertEqual(cursor.durableRevision, 2)
        try await resumed.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 3, relationshipState: .unresolved,
                projectFromWorld: matrix().map(Double.init), transformRevision: 1
            ), eventSequence: cursor.lastEventSequence + 1, monotonicTime: 11
        )
        let resumedCheckpoint = try await resumed.checkpoint()
        XCTAssertEqual(resumedCheckpoint.durableRevision, 3)
        await MainActor.run { workspace.openProject(id: projectID) }
        for _ in 0..<200 {
            if await MainActor.run(body: { workspace.phase == .frozen }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let reopened = await MainActor.run {
            (workspace.latestSnapshot?.revision,
             workspace.localProjects.first(where: { $0.id == projectID })?.sceneRevision,
             workspace.canExportProjectArchive)
        }
        XCTAssertEqual(reopened.0, 5)
        XCTAssertEqual(reopened.1, 5)
        XCTAssertTrue(reopened.2)
    }

    private func metadata(projectID: UUID, synthetic: Bool) -> Data {
        let hex = projectID.hssDirectoryName
        let sourceKind = synthetic ? "true" : "false"
        let body = "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\"\(hex)\",\"source_is_synthetic\":\(sourceKind)}"
        let checksum = SHA256.hash(data: Data(body.utf8)).map { String(format: "%02x", $0) }.joined()
        return Data("\(body.dropLast()),\"metadata_sha256\":\"\(checksum)\"}\n".utf8)
    }

    private func createCommittedSyntheticProject(
        root: URL,
        projectID: UUID,
        captureID: UUID
    ) async throws -> (snapshot: ScannerSceneSnapshot, durableRevision: UInt64) {
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
        try await runtime.submitMeshEvent(mesh, eventSequence: 3, projectTime: 3)
        let checkpoint = try await runtime.checkpoint()
        XCTAssertEqual(checkpoint.durableEventCount, 3)
        return (
            try await runtime.snapshot(provenance: [.syntheticTest], frozen: true),
            checkpoint.durableRevision
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

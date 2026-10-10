import XCTest
import UniformTypeIdentifiers
import simd
@testable import HybridSpatialScanner

final class ScannerWorkspacePipelineTests: XCTestCase {
    func testTypedFloorIssueOffersOnlyCurrentSupportedRecovery() throws {
        let id = UUID()
        let snapshot = try assessmentSnapshot(issues: [assessmentIssue(id: id, code: "floor_reference_unknown")])
        let assessment = ScannerSceneAssessment.decode(snapshot)
        let issue = try XCTUnwrap(assessment.issues.first)
        XCTAssertEqual(issue.nativeCode, "floor_reference_unknown")
        XCTAssertEqual(issue.nativeSceneRevision, 7)
        XCTAssertNotNil(issue.guidance)
        XCTAssertNil(issue.recoveryTitle, "Decoding alone must not promise executable UI.")
        XCTAssertTrue(ScannerQualityActions.offersFloorReference(issue, assessment: assessment, revision: 7, canConfirm: true))
        XCTAssertFalse(ScannerQualityActions.offersFloorReference(issue, assessment: assessment, revision: 8, canConfirm: true))
        XCTAssertFalse(ScannerQualityActions.offersFloorReference(issue, assessment: assessment, revision: 7, canConfirm: false))
    }

    func testFloorIssueActionRejectsSyntheticResolvedAndUnrelatedCards() throws {
        let id = UUID()
        let active = assessmentIssue(id: id, code: "floor_reference_unknown")
        let original = ScannerSceneAssessment.decode(try assessmentSnapshot(issues: [active]))
        let issue = try XCTUnwrap(original.issues.first)
        for assessment in [
            ScannerSceneAssessment.decode(try assessmentSnapshot(synthetic: true, issues: [active])),
            ScannerSceneAssessment.decode(try assessmentSnapshot(issues: [assessmentIssue(id: id, code: "floor_reference_unknown", state: "resolved")])),
            ScannerSceneAssessment.decode(try assessmentSnapshot(issues: [assessmentIssue(id: UUID(), code: "floor_reference_unknown")])),
            ScannerSceneAssessment.decode(try assessmentSnapshot(revision: "6", issues: [active]))
        ] {
            XCTAssertFalse(ScannerQualityActions.offersFloorReference(issue, assessment: assessment, revision: 7, canConfirm: true))
        }
    }

    func testNativeActionTextCannotCreateAnExecutableCorrection() throws {
        var record = assessmentIssue(id: UUID(), code: "future_custom_code")
        record["action"] = "Set observed floor reference; execute arbitrary command"
        let assessment = ScannerSceneAssessment.decode(try assessmentSnapshot(issues: [record]))
        let issue = try XCTUnwrap(assessment.issues.first)
        XCTAssertTrue(issue.detail.contains("execute arbitrary command"), "Original diagnostic text is preserved, not interpreted.")
        XCTAssertNil(issue.guidance)
        XCTAssertNil(issue.recoveryTitle)
        XCTAssertFalse(ScannerQualityActions.offersFloorReference(issue, assessment: assessment, revision: 7, canConfirm: true))
    }

    func testKnownIssueCoachingPreservesNativeReasonAndSourceIdentity() throws {
        for code in ["floor_reference_unknown", "unresolved_epoch", "provisional_capture",
                     "open_or_ambiguous_slice", "unclassified_slice_loops",
                     "plan_semantics_unavailable", "partial_plane_support"] {
            let id = UUID()
            let assessment = ScannerSceneAssessment.decode(try assessmentSnapshot(issues: [assessmentIssue(id: id, code: code)]))
            let issue = try XCTUnwrap(assessment.issues.first)
            XCTAssertEqual(issue.id, id)
            XCTAssertEqual(issue.nativeSceneRevision, 7)
            XCTAssertNotNil(issue.guidance)
            XCTAssertTrue(issue.detail.contains("Retained support requires review"))
            XCTAssertTrue(issue.detail.contains("Capture another useful view"))
        }
    }

    func testDriverFailurePriorityAndBuiltInIssueIdentityRemainStable() {
        XCTAssertEqual(ScannerQualityActions.driverPriority(phase: .failed("failure"), recoveryAction: nil), .blocking)
        XCTAssertEqual(ScannerQualityActions.driverPriority(phase: .frozen, recoveryAction: .retrySave), .important)
        XCTAssertEqual(ScannerQualityActions.driverPriority(phase: .frozen, recoveryAction: nil), .notice)
        XCTAssertNotEqual(ScannerQualityActions.saveFailureID, ScannerQualityActions.referenceSceneID)
        XCTAssertEqual(ScannerQualityActions.saveFailureID.uuidString, "00000000-0000-4000-8000-000000000013")
    }

    func testFloorTapMapsVisibleFaceToRawRetainedVerticesWithoutRetargetingHiddenMesh() {
        let points: [SIMD3<Double>] = [SIMD3(0, 2, 0), SIMD3(1, 2, 0), SIMD3(0, 2, -1),
            SIMD3(5, 2, 0), SIMD3(6, 2, 0), SIMD3(5, 2, -1)]
        let snapshot = ScannerSceneSnapshot(revision: 9, verticesMetres: points, triangleIndices: [0, 1, 2, 3, 4, 5],
            reviewTriangleInclusion: [0, 1], sceneJSON: Data(), provenance: [.syntheticTest], isFrozen: true)
        XCTAssertEqual(ScannerFloorReferenceProposal.nearestVisibleVertex(snapshot: snapshot, faceIndex: 0, localPoint: SIMD3(5.1, 2, -0.1)), 3)
        XCTAssertNil(ScannerFloorReferenceProposal.nearestVisibleVertex(snapshot: snapshot, faceIndex: 1, localPoint: SIMD3(5, 2, 0)))
        XCTAssertNil(ScannerFloorReferenceProposal.nearestVisibleVertex(snapshot: snapshot, faceIndex: 0, localPoint: SIMD3(.nan, 2, 0)))
        XCTAssertNil(ScannerFloorReferenceProposal.make(snapshot: snapshot, vertexIndices: [0, 1, 2]))
        XCTAssertNotNil(ScannerFloorReferenceProposal.make(snapshot: snapshot, vertexIndices: [3, 4, 5]))
        XCTAssertEqual(snapshot.triangleIndices, [0, 1, 2, 3, 4, 5])
    }

    func testFloorProposalUsesRetainedPointsAndRightHandedArbitrarySlope() throws {
        let points: [SIMD3<Double>] = [SIMD3(10, 2, -5), SIMD3(11, 2.3, -5), SIMD3(10, 2.1, -6)]
        let snapshot = floorProposalFixture(points: points)
        let proposal = try XCTUnwrap(ScannerFloorReferenceProposal.make(snapshot: snapshot, vertexIndices: [0, 1, 2]))
        XCTAssertEqual(proposal.sceneRevision, snapshot.revision)
        XCTAssertEqual(proposal.projectFromFloor.columns.3.y, 2, "Physical floor is not the arbitrary world Y=0.")
        let transform = proposal.projectFromFloor
        let rotation = simd_double3x3(columns: (SIMD3(transform.columns.0.x, transform.columns.0.y, transform.columns.0.z),
            SIMD3(transform.columns.1.x, transform.columns.1.y, transform.columns.1.z),
            SIMD3(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)))
        XCTAssertEqual(simd_determinant(rotation), 1, accuracy: 1e-12)
        XCTAssertGreaterThan(transform.columns.1.y, 0)
        for point in points {
            let floor = transform.inverse * SIMD4(point.x, point.y, point.z, 1)
            XCTAssertEqual(floor.y, 0, accuracy: 1e-12, "Numerical plane fit, not a physical precision claim.")
        }
        XCTAssertEqual(snapshot.verticesMetres, points, "The proposal never edits retained observations.")
    }

    func testFloorProposalRejectsDegenerateUnretainedAndVerticalSelection() {
        let ordinary = floorProposalFixture(points: [SIMD3(0, 2, 0), SIMD3(1, 2, 0), SIMD3(0, 2, -1)])
        for selection in [[0, 0, 2], [0, 1], [0, 1, 3], [-1, 1, 2]] {
            XCTAssertNil(ScannerFloorReferenceProposal.make(snapshot: ordinary, vertexIndices: selection))
        }
        for points: [SIMD3<Double>] in [
            [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(2, 0, 0)],
            [SIMD3(0, 0, 0), SIMD3(0, 1, 0), SIMD3(0, 0, 1)],
            [SIMD3(.nan, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 0, -1)]
        ] {
            XCTAssertNil(ScannerFloorReferenceProposal.make(snapshot: floorProposalFixture(points: points), vertexIndices: [0, 1, 2]))
        }
    }

    private func floorProposalFixture(points: [SIMD3<Double>]) -> ScannerSceneSnapshot {
        ScannerSceneSnapshot(revision: 9, verticesMetres: points, triangleIndices: [0, 1, 2],
            sceneJSON: Data(), provenance: [.syntheticTest], isFrozen: true)
    }

    func testReopenedProjectAcceptsExplicitPlanReferenceWithoutStartingCamera() async throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID(), captureID = UUID()
        let before = try await createSyntheticProject(root: root, projectID: projectID, captureID: captureID)
        let catalog = try JSONSerialization.data(withJSONObject: ["projects": [[
            "id": projectID.uuidString, "captureID": captureID.uuidString, "name": "Retained reference fixture",
            "updatedAt": 0, "sceneRevision": before.revision, "state": "frozen", "sourceIsSynthetic": true
        ]]])
        try catalog.write(to: root.appendingPathComponent("workspace-catalog.json"))
        let workspace = await MainActor.run { ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true) }
        await MainActor.run { workspace.openProject(id: projectID) }
        for _ in 0..<100 {
            if await MainActor.run(body: { workspace.phase == .frozen }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let mayConfirm = await MainActor.run { workspace.canConfirmPlanReference }
        let initialPhase = await MainActor.run { workspace.phase }
        XCTAssertEqual(initialPhase, .frozen)
        XCTAssertTrue(mayConfirm, "Retained accepted epochs must not require a running camera.")
        guard mayConfirm else { return }
        await MainActor.run { workspace.confirmPlanReference(projectFromFloor: matrix_identity_double4x4, cutHeightMetres: 1.2) }
        for _ in 0..<200 {
            if await MainActor.run(body: { workspace.latestSnapshot?.revision == before.revision + 1 && workspace.latestSnapshot?.isFrozen == true }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let capturedAfter = await MainActor.run { workspace.latestSnapshot }
        let after = try XCTUnwrap(capturedAfter)
        XCTAssertEqual(after.revision, before.revision + 1)
        XCTAssertTrue(after.isFrozen)
        XCTAssertEqual(after.verticesMetres, before.verticesMetres)
        XCTAssertEqual(after.triangleIndices, before.triangleIndices)
        let unsaved = await MainActor.run { workspace.saveState }
        XCTAssertEqual(unsaved, .hasUnsavedChanges)
        await MainActor.run { workspace.saveSelectedProject() }
        for _ in 0..<200 {
            if await MainActor.run(body: { workspace.phase == .frozen && workspace.saveState != .saving }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let reopened = await MainActor.run { ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true) }
        await MainActor.run { reopened.openProject(id: projectID) }
        for _ in 0..<100 {
            if await MainActor.run(body: { reopened.phase == .frozen }) { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        let reopenedSnapshot = await MainActor.run { reopened.latestSnapshot }
        let canConfirmAgain = await MainActor.run { reopened.canConfirmPlanReference }
        XCTAssertEqual(reopenedSnapshot?.revision, after.revision)
        XCTAssertEqual(reopenedSnapshot?.sceneJSON, after.sceneJSON)
        XCTAssertTrue(canConfirmAgain)
    }

    func testRetainedPlanEpochRejectsUnrelatedStaleAndUnresolvedContexts() async throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID(), captureID = UUID()
        let snapshot = try await createSyntheticProject(root: root, projectID: projectID, captureID: captureID)
        XCTAssertEqual(ScannerRetainedPlanEpoch.resolve(snapshot: snapshot, projectID: projectID, captureID: captureID), 1)
        XCTAssertNil(ScannerRetainedPlanEpoch.resolve(snapshot: snapshot, projectID: UUID(), captureID: captureID))
        XCTAssertNil(ScannerRetainedPlanEpoch.resolve(snapshot: snapshot, projectID: projectID, captureID: UUID()))
        let original = try XCTUnwrap(JSONSerialization.jsonObject(with: snapshot.sceneJSON) as? [String: Any])
        for mutation in ["revision", "unresolved", "matrix", "transformRevision"] {
            var document = original
            if mutation == "revision" {
                document["scene_revision"] = String(snapshot.revision + 1)
            } else {
                var epochs = try XCTUnwrap(document["epochs"] as? [[String: Any]])
                if mutation == "unresolved" { epochs[0]["relationship_state"] = "unresolved" }
                if mutation == "matrix" { epochs[0]["T_project_world"] = NSNull() }
                if mutation == "transformRevision" { epochs[0]["transform_revision"] = "01" }
                document["epochs"] = epochs
            }
            let changed = ScannerSceneSnapshot(
                revision: snapshot.revision, verticesMetres: snapshot.verticesMetres,
                triangleIndices: snapshot.triangleIndices,
                sceneJSON: try JSONSerialization.data(withJSONObject: document), provenance: [.syntheticTest], isFrozen: true)
            XCTAssertNil(ScannerRetainedPlanEpoch.resolve(snapshot: changed, projectID: projectID, captureID: captureID), mutation)
        }
    }

    func testObjectLibraryDiscoversLegacyPhotosWithoutMetricAuthority() throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        // Existing ISO8601 records intentionally have second precision.
        let paths = try ObjectCaptureStorage.createSession(now: Date(timeIntervalSince1970: 1_700_000_000), capturesRootDirectory: root)
        try Data([1, 2, 3]).write(to: paths.imagesDirectory.appendingPathComponent("sample.HEIC"))
        try Data([4]).write(to: paths.imagesDirectory.appendingPathComponent("metadata.json"))
        let entry = try XCTUnwrap(ObjectCaptureStorage.library(capturesRootDirectory: root).first)
        XCTAssertEqual(entry.id, paths.id)
        XCTAssertEqual(entry.state, .retainedPhotos)
        XCTAssertEqual(entry.photoCount, 1)
        XCTAssertNil(entry.artifact)
        XCTAssertEqual(try ObjectCaptureStorage.reconstructionPaths(id: paths.id, capturesRootDirectory: root), paths)
        XCTAssertThrowsError(try ObjectCaptureStorage.createSession(id: paths.id, capturesRootDirectory: root))
    }

    func testObjectArtifactRebasesUntrustedAbsolutePathsIntoOwnedSession() throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let paths = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        try Data([1, 2, 3]).write(to: paths.modelURL)
        let artifact = ObjectCaptureArtifact(
            id: paths.id, sessionDirectory: URL(fileURLWithPath: "/outside/session"),
            modelURL: URL(fileURLWithPath: "/outside/private.usdz"), imagesDirectory: URL(fileURLWithPath: "/outside/photos"),
            createdAt: paths.createdAt, completedAt: Date(), capturedImageCount: 8,
            photoRetentionPolicy: .retainedUntilExplicitSessionDeletion,
            provenance: .realityKitObjectCapture(), processingWarnings: ["Hidden surfaces remain unknown"])
        try ObjectCaptureStorage.persistManifest(for: artifact, at: paths.manifestURL)
        let entry = try XCTUnwrap(ObjectCaptureStorage.library(capturesRootDirectory: root).first)
        XCTAssertEqual(entry.state, .completed)
        XCTAssertEqual(entry.artifact?.modelURL, paths.modelURL)
        XCTAssertEqual(entry.artifact?.imagesDirectory, paths.imagesDirectory)
        XCTAssertEqual(entry.artifact?.provenance.metricAuthority, false)
        XCTAssertEqual(entry.artifact?.processingWarnings, artifact.processingWarnings)
    }

    func testObjectDeletionRemovesOnlyUUIDOwnedFilesAndDoesNotFollowNestedLinks() throws {
        let root = try objectRoot(), outside = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root); try? FileManager.default.removeItem(at: outside) }
        let first = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        let other = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        let external = outside.appendingPathComponent("shared.usdz")
        try Data([3]).write(to: external)
        try FileManager.default.createSymbolicLink(at: first.imagesDirectory.appendingPathComponent("redirect"), withDestinationURL: outside)
        XCTAssertThrowsError(try ObjectCaptureStorage.reconstructionPaths(id: first.id, capturesRootDirectory: root))
        try ObjectCaptureStorage.deleteSession(id: first.id, capturesRootDirectory: root)
        try ObjectCaptureStorage.deleteSession(id: first.id, capturesRootDirectory: root)
        XCTAssertTrue(FileManager.default.fileExists(atPath: external.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: other.sessionDirectory.path))
        XCTAssertEqual(try ObjectCaptureStorage.library(capturesRootDirectory: root).map(\.id), [other.id])
    }

    func testObjectDeletionIntentSurvivesPartialCleanupAndBlocksReconstruction() throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let paths = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        try Data([1]).write(to: paths.imagesDirectory.appendingPathComponent("sample.heic"))
        let intent = root.appendingPathComponent(".deleting-" + paths.id.uuidString)
        try Data("{}".utf8).write(to: intent)
        XCTAssertEqual(try ObjectCaptureStorage.library(capturesRootDirectory: root).first?.state, .deleting)
        XCTAssertThrowsError(try ObjectCaptureStorage.reconstructionPaths(id: paths.id, capturesRootDirectory: root))
        try FileManager.default.removeItem(at: paths.sessionDirectory)
        XCTAssertEqual(try ObjectCaptureStorage.library(capturesRootDirectory: root).first?.state, .deleting)
        try ObjectCaptureStorage.deleteSession(id: paths.id, capturesRootDirectory: root)
        XCTAssertTrue(try ObjectCaptureStorage.library(capturesRootDirectory: root).isEmpty)
    }

    func testObjectLibraryRejectsRedirectedRootSessionAndCheckpoint() throws {
        let root = try objectRoot(), outside = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root); try? FileManager.default.removeItem(at: outside) }
        let redirect = root.appendingPathComponent("redirect")
        try FileManager.default.createSymbolicLink(at: redirect, withDestinationURL: outside)
        XCTAssertThrowsError(try ObjectCaptureStorage.library(capturesRootDirectory: redirect))
        XCTAssertThrowsError(try ObjectCaptureStorage.createSession(capturesRootDirectory: redirect))
        let id = UUID()
        try FileManager.default.createSymbolicLink(at: root.appendingPathComponent(id.uuidString), withDestinationURL: outside)
        XCTAssertEqual(try ObjectCaptureStorage.library(capturesRootDirectory: root).first?.state, .unavailable)
        XCTAssertThrowsError(try ObjectCaptureStorage.deleteSession(id: id, capturesRootDirectory: root))
        let paths = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        try Data([1]).write(to: paths.imagesDirectory.appendingPathComponent("sample.heic"))
        try FileManager.default.removeItem(at: paths.captureCheckpointDirectory)
        try FileManager.default.createSymbolicLink(at: paths.captureCheckpointDirectory, withDestinationURL: outside)
        XCTAssertThrowsError(try ObjectCaptureStorage.reconstructionPaths(id: paths.id, capturesRootDirectory: root))
        XCTAssertTrue(FileManager.default.fileExists(atPath: outside.path))
    }

    func testObjectCorruptRecordIsVisibleButCannotResume() throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let paths = try ObjectCaptureStorage.createSession(capturesRootDirectory: root)
        try Data("broken".utf8).write(to: paths.sessionRecordURL)
        let entry = try XCTUnwrap(ObjectCaptureStorage.library(capturesRootDirectory: root).first)
        XCTAssertEqual(entry.state, .unavailable)
        XCTAssertThrowsError(try ObjectCaptureStorage.reconstructionPaths(id: paths.id, capturesRootDirectory: root))
        try ObjectCaptureStorage.deleteSession(id: paths.id, capturesRootDirectory: root)
        XCTAssertTrue(try ObjectCaptureStorage.library(capturesRootDirectory: root).isEmpty)
    }

    func testObjectDanglingSessionLinkIsNotReportedAsSuccessfulDeletion() throws {
        let root = try objectRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let id = UUID()
        let session = root.appendingPathComponent(id.uuidString)
        try FileManager.default.createSymbolicLink(at: session, withDestinationURL: root.appendingPathComponent("missing"))
        XCTAssertEqual(try ObjectCaptureStorage.library(capturesRootDirectory: root).first?.state, .unavailable)
        XCTAssertThrowsError(try ObjectCaptureStorage.deleteSession(id: id, capturesRootDirectory: root))
        XCTAssertThrowsError(try ObjectCaptureStorage.createSession(id: id, capturesRootDirectory: root))
        XCTAssertNoThrow(try FileManager.default.destinationOfSymbolicLink(atPath: session.path))
    }

    private func objectRoot() throws -> URL {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-object-test-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        return root
    }

    func testCapturedMeshReviewDoesNotDependOnSemanticPlan() throws {
        let snapshot = try assessmentSnapshot()
        XCTAssertNil(ScannerAcceptedPlanDecoder.decode(snapshot))
        XCTAssertTrue(ScannerSceneAssessment.decode(snapshot).hasCapturedGeometry)
    }

    func testAssessmentPreservesIssueIdentitySortsAndFiltersSourceAndStaleRecords() throws {
        let importantID = UUID(), blockingID = UUID()
        let issues: [[String: Any]] = [
            assessmentIssue(id: importantID, code: "floor_reference_unknown"),
            assessmentIssue(id: UUID(), code: "mesh_source"),
            assessmentIssue(id: UUID(), code: "unresolved_epoch", revision: "6"),
            assessmentIssue(id: blockingID, code: "unresolved_epoch"),
            assessmentIssue(id: UUID(), code: "open_or_ambiguous_slice", state: "resolved")
        ]
        let result = ScannerSceneAssessment.decode(try assessmentSnapshot(issues: issues))
        XCTAssertEqual(result.issues.map(\.id), [blockingID, importantID])
        XCTAssertEqual(result.issues.first?.priority, .blocking)
        XCTAssertTrue(result.issues.allSatisfy { $0.recoveryTitle == nil }, "Unwired repair tools are not exposed.")
    }

    func testSyntheticFlagInNativeDocumentSuppressesCapturedClaim() throws {
        let result = ScannerSceneAssessment.decode(try assessmentSnapshot(synthetic: true))
        XCTAssertFalse(result.hasCapturedGeometry, "A missing Swift test marker cannot override native synthetic provenance.")
    }

    func testAssessmentFailsClosedForMismatchedAndMalformedReports() throws {
        for snapshot in [
            try assessmentSnapshot(revision: "6"),
            try assessmentSnapshot(issues: [assessmentIssue(id: UUID(), code: "future", severity: "unexpected")])
        ] {
            let result = ScannerSceneAssessment.decode(snapshot)
            XCTAssertEqual(result.issues.count, 1)
            XCTAssertEqual(result.issues.first?.priority, .blocking)
            XCTAssertFalse(result.hasCapturedGeometry)
        }
    }

    private func assessmentIssue(id: UUID, code: String, revision: String = "7", severity: String = "warning", state: String = "active") -> [String: Any] {
        ["id": id.uuidString, "code": code, "scene_revision": revision,
         "severity": severity, "reason": "Retained support requires review", "action": "Capture another useful view", "state": state]
    }

    private func assessmentSnapshot(revision: String = "7", synthetic: Bool = false, issues: [[String: Any]] = []) throws -> ScannerSceneSnapshot {
        ScannerSceneSnapshot(
            revision: 7, verticesMetres: [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 1, 0)],
            triangleIndices: [0, 1, 2],
            sceneJSON: try JSONSerialization.data(withJSONObject: [
                "scene_revision": revision, "source_is_synthetic": synthetic, "units": "m", "issues": issues
            ]), provenance: [.capturedARKitMesh], isFrozen: true
        )
    }

    @MainActor
    func testLibraryMigratesLegacyCatalogAndPersistsRenameTrashRestore() throws {
        let root = try makeLibraryFixture()
        defer { try? FileManager.default.removeItem(at: root) }
        let workspace = ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true)
        workspace.reloadProjects()
        let project = try XCTUnwrap(workspace.localProjects.first)
        XCTAssertTrue(workspace.deletedProjects.isEmpty)
        workspace.renameProject(id: project.id, name: "  Workshop / الطابق الثاني  ")
        XCTAssertEqual(workspace.localProjects.first?.name, "Workshop / الطابق الثاني")
        XCTAssertEqual(workspace.localProjects.first?.sceneRevision, project.sceneRevision)
        workspace.moveProjectToTrash(id: project.id)
        XCTAssertTrue(workspace.localProjects.isEmpty)
        XCTAssertEqual(workspace.deletedProjects.map(\.id), [project.id])
        let reopened = ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true)
        reopened.reloadProjects()
        XCTAssertTrue(reopened.localProjects.isEmpty)
        XCTAssertEqual(reopened.deletedProjects.first?.name, "Workshop / الطابق الثاني")
        reopened.restoreProject(id: project.id)
        XCTAssertEqual(reopened.localProjects.map(\.id), [project.id])
        XCTAssertTrue(reopened.deletedProjects.isEmpty)
        reopened.renameProject(id: project.id, name: " \n ")
        XCTAssertNotNil(reopened.currentIssue)
        XCTAssertEqual(reopened.localProjects.first?.name, "Workshop / الطابق الثاني")
    }

    @MainActor
    func testLibraryPermanentPurgeRemovesCatalogOnlyAfterSuccessfulCleanup() async throws {
        let root = try makeLibraryFixture()
        defer { try? FileManager.default.removeItem(at: root) }
        let workspace = ScannerWorkspaceModel(trustedRoot: root, sourceIsSynthetic: true)
        workspace.reloadProjects()
        let id = try XCTUnwrap(workspace.localProjects.first?.id)
        let directory = root.appendingPathComponent(id.hssDirectoryName)
        let target = root.appendingPathComponent("unrelated")
        try FileManager.default.createDirectory(at: target, withIntermediateDirectories: true)
        try FileManager.default.createSymbolicLink(at: directory, withDestinationURL: target)
        workspace.moveProjectToTrash(id: id)
        workspace.permanentlyDeleteProject(id: id)
        for _ in 0..<200 {
            if !workspace.isManagingLibrary { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        XCTAssertFalse(workspace.isManagingLibrary)
        XCTAssertNotNil(workspace.currentIssue)
        XCTAssertEqual(workspace.deletedProjects.map(\.id), [id])
        XCTAssertEqual(workspace.deletedProjects.first?.canRestore, false)
        workspace.restoreProject(id: id)
        XCTAssertTrue(workspace.localProjects.isEmpty, "A partial purge cannot restore a plausibly intact project.")
        let duringFailure = ScannerWorkspaceModel(trustedRoot: root)
        duringFailure.reloadProjects()
        XCTAssertEqual(duringFailure.deletedProjects.first?.canRestore, false, "Purge intent survives process restart.")
        XCTAssertTrue(FileManager.default.fileExists(atPath: target.path))
        try FileManager.default.removeItem(at: directory) // remove test symlink only
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        workspace.permanentlyDeleteProject(id: id)
        for _ in 0..<200 {
            if !workspace.isManagingLibrary { break }
            try await Task.sleep(for: .milliseconds(10))
        }
        XCTAssertFalse(workspace.isManagingLibrary)
        XCTAssertTrue(workspace.deletedProjects.isEmpty)
        XCTAssertNil(workspace.currentIssue)
        XCTAssertFalse(FileManager.default.fileExists(atPath: directory.path))
        let reopened = ScannerWorkspaceModel(trustedRoot: root)
        reopened.reloadProjects()
        XCTAssertTrue(reopened.localProjects.isEmpty)
        XCTAssertTrue(reopened.deletedProjects.isEmpty)
    }

    func testPurgeIsIdempotentAndPreservesOtherProjectsAndSharedCopies() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-purge-\(UUID())")
        defer { try? FileManager.default.removeItem(at: root) }
        let id = UUID(), otherID = UUID()
        let project = root.appendingPathComponent(id.hssDirectoryName)
        let other = root.appendingPathComponent(otherID.hssDirectoryName)
        let archives = root.appendingPathComponent("archives")
        for url in [project, other, archives] {
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
        let owned = archives.appendingPathComponent(ScannerProjectArchiveExportName.make(projectID: id, sceneRevision: 9))
        let unrelated = archives.appendingPathComponent(ScannerProjectArchiveExportName.make(projectID: otherID, sceneRevision: 9))
        let invalidName = archives.appendingPathComponent("\(id.hssDirectoryName)-r9-not-an-export.hsscan")
        let shared = root.appendingPathComponent("owner-shared.hsscan")
        for url in [owned, unrelated, invalidName, shared] { try Data([42]).write(to: url) }
        try HSSLocalProjectRemoval.remove(projectID: id, trustedRoot: root)
        try HSSLocalProjectRemoval.remove(projectID: id, trustedRoot: root)
        XCTAssertFalse(FileManager.default.fileExists(atPath: project.path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: owned.path))
        for url in [other, unrelated, invalidName, shared] {
            XCTAssertTrue(FileManager.default.fileExists(atPath: url.path))
        }
    }

    func testArchiveSymlinkRejectsPurgeBeforeRemovingProject() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-purge-link-\(UUID())")
        defer { try? FileManager.default.removeItem(at: root) }
        let id = UUID()
        let project = root.appendingPathComponent(id.hssDirectoryName)
        let outside = root.appendingPathComponent("unrelated")
        for url in [project, outside] {
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
        try FileManager.default.createSymbolicLink(at: root.appendingPathComponent("archives"), withDestinationURL: outside)
        XCTAssertThrowsError(try HSSLocalProjectRemoval.remove(projectID: id, trustedRoot: root))
        XCTAssertTrue(FileManager.default.fileExists(atPath: project.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: outside.path))
    }

    func testPurgeDoesNotFollowNestedProjectSymlink() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-purge-nested-\(UUID())")
        defer { try? FileManager.default.removeItem(at: root) }
        let id = UUID()
        let project = root.appendingPathComponent(id.hssDirectoryName)
        let outside = root.appendingPathComponent("unrelated")
        for url in [project, outside] {
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
        let retained = outside.appendingPathComponent("retained.bin")
        try Data([42]).write(to: retained)
        try FileManager.default.createSymbolicLink(at: project.appendingPathComponent("exports"), withDestinationURL: outside)
        try HSSLocalProjectRemoval.remove(projectID: id, trustedRoot: root)
        XCTAssertEqual(try Data(contentsOf: retained), Data([42]))
    }

    private func makeLibraryFixture() throws -> URL {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-library-\(UUID())")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        // Legacy catalog intentionally has no deletedAt key. This is local UI
        // metadata, not evidence of a physical capture or reconstructed scene.
        let data = try JSONSerialization.data(withJSONObject: ["projects": [[
            "id": UUID().uuidString, "captureID": UUID().uuidString,
            "name": "Original", "updatedAt": 0, "sceneRevision": 7,
            "state": "frozen", "sourceIsSynthetic": true
        ]]])
        try data.write(to: root.appendingPathComponent("workspace-catalog.json"))
        return root
    }

    func testRebootedSourceClockPreservesSessionIntervals() throws {
        var clock = ProjectCaptureClock(lastDurableTime: 86_400)
        let epoch = try XCTUnwrap(clock.projectTime(for: 12))
        let depth = try XCTUnwrap(clock.projectTime(for: 12.25))
        let mesh = try XCTUnwrap(clock.projectTime(for: 12.5))
        XCTAssertEqual([epoch, depth, mesh], [86_400, 86_400.25, 86_400.5])
        XCTAssertNil(clock.projectTime(for: .infinity))
        XCTAssertNil(clock.projectTime(for: 0), "A second clock reset within one session must fail closed.")
        XCTAssertEqual(clock.lastTime, 86_400.5)
        var nextSession = ProjectCaptureClock(lastDurableTime: clock.lastTime)
        XCTAssertEqual(nextSession.projectTime(for: 2), 86_400.5)
        XCTAssertEqual(nextSession.projectTime(for: 3), 86_401.5)
    }

    func testMixedEpochDepthMeshDeliveryIsNondecreasing() throws {
        var clock = ProjectCaptureClock(lastDurableTime: 50)
        let sources: [(String, UInt64, UInt64, Double)] = [
            ("epoch", 7, 101, 4.0),
            ("depth", 7, 102, 4.2),
            ("mesh", 7, 103, 4.1),
            ("epoch", 8, 104, 4.3),
            ("mesh", 8, 105, 4.35),
            ("depth", 8, 106, 4.6),
        ]
        let mapped = try sources.map { try XCTUnwrap(clock.projectTime(for: $0.3)) }
        XCTAssertEqual(mapped, [50, 50.2, 50.2, 50.3, 50.35, 50.6])
        XCTAssertEqual(sources.map { $0.1 }, [7, 7, 7, 8, 8, 8])
        XCTAssertEqual(sources.map { $0.2 }, [101, 102, 103, 104, 105, 106])
    }

    func testRebootedEpochFrameAndMeshAppendAndReplay() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-reboot-clock-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID()
        let captureID = UUID()
        let runtime = try HSSProjectRuntime(
            projectID: projectID, captureID: captureID, sourceIsSynthetic: true,
            trustedRoot: root, openMode: .create
        )
        let transform = matrix().map(Double.init)
        try await runtime.setEpoch(
            CaptureEpochControl(sessionEpoch: 1, relationshipState: .unresolved,
                                projectFromWorld: transform, transformRevision: 1),
            eventSequence: 1, monotonicTime: 100_000
        )
        var clock = ProjectCaptureClock(lastDurableTime: 100_000)
        let newEpoch = CaptureEpochControl(
            sessionEpoch: 2, relationshipState: .unresolved,
            projectFromWorld: transform, transformRevision: 1
        )
        try await runtime.setEpoch(newEpoch, eventSequence: 2,
                                   monotonicTime: try XCTUnwrap(clock.projectTime(for: 5)))
        // This identity transform is an explicit synthetic relationship in the
        // test; the production coordinator still owns the acceptance gate.
        try await runtime.setEpoch(
            CaptureEpochControl(sessionEpoch: 2, relationshipState: .accepted,
                                projectFromWorld: transform, transformRevision: 2),
            eventSequence: 3, monotonicTime: try XCTUnwrap(clock.projectTime(for: 5.1))
        )
        let depth = frame(identity: SceneDepthFrameIdentity(
            sessionEpoch: 2, sequence: 1, monotonicTimestamp: 5.2
        ))
        try await runtime.submitFrame(depth, eventSequence: 4,
                                      projectTime: try XCTUnwrap(clock.projectTime(for: 5.2)))
        XCTAssertEqual(depth.identity.monotonicTimestamp, 5.2)
        let mesh = OwnedMeshAnchorEvent(
            kind: .add, anchorID: UUID(), anchorRevision: 1, generation: 1,
            sessionEpoch: 2, sequence: 2, monotonicTimestamp: 5.15,
            worldFromAnchor: matrix(),
            payload: MeshAnchorPayload(
                vertices: [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 1, 0)],
                normals: [], triangleIndices: [0, 1, 2], classifications: [],
                localBoundsMin: SIMD3(0, 0, 0), localBoundsMax: SIMD3(1, 1, 0)
            )
        )
        try await runtime.submitMeshEvent(mesh, eventSequence: 5,
                                          projectTime: try XCTUnwrap(clock.projectTime(for: 5.15)))
        XCTAssertEqual(mesh.monotonicTimestamp, 5.15)
        _ = try await runtime.checkpoint()
        let reopened = try HSSProjectRuntime(
            projectID: projectID, captureID: captureID, sourceIsSynthetic: true,
            trustedRoot: root, openMode: .openExisting
        )
        let replay = try await reopened.replayCommittedSource()
        XCTAssertEqual(replay.highestSessionEpoch, 2)
        XCTAssertEqual(replay.lastEventSequence, 5)
        XCTAssertEqual(replay.lastMonotonicTime, 100_000.2, accuracy: 0.000_001)
    }

    func testFrozenMissingEvidenceJournalIsExactAndReplayable() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-frozen-missing-evidence-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let projectID = UUID()
        let captureID = UUID()
        let runtime = try HSSProjectRuntime(
            projectID: projectID,
            captureID: captureID,
            sourceIsSynthetic: true,
            trustedRoot: root,
            openMode: .create
        )
        let transform = matrix().map(Double.init)
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 7,
                relationshipState: .unresolved,
                projectFromWorld: transform,
                transformRevision: 1
            ),
            eventSequence: 1,
            monotonicTime: 1
        )
        try await runtime.setEpoch(
            CaptureEpochControl(
                sessionEpoch: 7,
                relationshipState: .accepted,
                projectFromWorld: transform,
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
            sessionEpoch: 7,
            sequence: 3,
            monotonicTimestamp: 3,
            worldFromAnchor: matrix(),
            payload: MeshAnchorPayload(
                vertices: [SIMD3(0, 0, 0), SIMD3(1, 0, 0), SIMD3(0, 1, 0)],
                normals: [],
                triangleIndices: [0, 1, 2],
                classifications: [],
                localBoundsMin: SIMD3(0, 0, 0),
                localBoundsMax: SIMD3(1, 1, 0)
            )
        )
        try await runtime.submitMeshEvent(mesh, eventSequence: 3, projectTime: 3)
        let before = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)

        let outcome = try await runtime.recordFrozenMissingEvidence(
            reason: "North alcove lacks supporting capture.",
            eventSequence: 4,
            monotonicTime: 4
        )
        let application: HSSFrozenUnknownCommandApplication
        switch outcome {
        case .journaled(let value):
            application = value
        case .journalRetryRequired(_, let failure):
            XCTFail("The local project store unexpectedly rejected the command journal: \(failure)")
            return
        case .resultRecoveryRequired(let failure):
            XCTFail("The native command result unexpectedly needed recovery: \(failure)")
            return
        }
        XCTAssertEqual(application.beforeSceneRevision, before.revision)
        XCTAssertEqual(application.afterSceneRevision, before.revision + 1)
        XCTAssertEqual(application.sessionEpoch, 7)
        XCTAssertEqual(application.eventSequence, 4)
        XCTAssertEqual(application.monotonicTime, 4)

        let command = try XCTUnwrap(
            JSONSerialization.jsonObject(with: application.commandJSON) as? [String: Any]
        )
        XCTAssertEqual(command["schema_version"] as? String, "1.1")
        XCTAssertEqual(command["project_id"] as? String, projectID.uuidString.lowercased())
        XCTAssertEqual(command["expected_scene_revision"] as? String, String(before.revision))
        XCTAssertEqual(command["event_sequence"] as? String, "4")
        XCTAssertEqual(command["type"] as? String, "mark_unknown")
        XCTAssertEqual(command["targets"] as? [String], [])
        XCTAssertEqual((command["payload"] as? [String: Any])?["reason"] as? String,
                       "North alcove lacks supporting capture.")

        let result = try XCTUnwrap(
            JSONSerialization.jsonObject(with: application.resultJSON) as? [String: Any]
        )
        XCTAssertEqual(result["state"] as? String, "applied")
        XCTAssertEqual(result["command_id"] as? String, command["command_id"] as? String)
        XCTAssertEqual(result["durability"] as? String, "not_committed")
        XCTAssertEqual(result["scene_revision"] as? String, String(application.afterSceneRevision))
        let issueIDs = try XCTUnwrap(result["issue_ids"] as? [String])
        XCTAssertEqual(issueIDs.count, 1)
        XCTAssertNotNil(UUID(uuidString: try XCTUnwrap(issueIDs.first)))

        let after = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(after.revision, application.afterSceneRevision)
        XCTAssertEqual(after.verticesMetres, before.verticesMetres)
        XCTAssertEqual(after.triangleIndices, before.triangleIndices)
        let afterScene = try XCTUnwrap(String(data: after.sceneJSON, encoding: .utf8))
        XCTAssertTrue(afterScene.contains("HSS_UNKNOWN_REGION"))
        XCTAssertTrue(afterScene.contains("North alcove lacks supporting capture."))

        let checkpoint = try await runtime.checkpoint()
        XCTAssertEqual(checkpoint.durableEventCount, 4)
        XCTAssertEqual(checkpoint.durableRevision, 1,
                       "Manifest revisions are checkpoint generations, not scene revisions.")

        let reopened = try HSSProjectRuntime(
            projectID: projectID,
            captureID: captureID,
            sourceIsSynthetic: true,
            trustedRoot: root,
            openMode: .openExisting
        )
        let replay = try await reopened.replayCommittedSource()
        XCTAssertEqual(replay.highestSessionEpoch, 7)
        XCTAssertEqual(replay.lastEventSequence, 4)
        XCTAssertEqual(replay.lastMonotonicTime, 4)
        XCTAssertEqual(replay.acceptedSceneRevision, application.afterSceneRevision)
        let restoredResult = try await reopened.latestCommandResultJSON()
        XCTAssertEqual(restoredResult, application.resultJSON)
        let replayed = try await reopened.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(replayed.revision, after.revision)
        XCTAssertEqual(replayed.verticesMetres, after.verticesMetres)
        XCTAssertEqual(replayed.triangleIndices, after.triangleIndices)
        XCTAssertEqual(replayed.sceneJSON, after.sceneJSON)

        let next = try await reopened.recordFrozenMissingEvidence(
            reason: "A second frozen-review note verifies the recovered epoch.",
            eventSequence: 5,
            monotonicTime: 5
        )
        guard case .journaled(let nextApplication) = next else {
            return XCTFail("The recovered runtime did not journal its next frozen-review command.")
        }
        XCTAssertEqual(nextApplication.sessionEpoch, 7)
        XCTAssertEqual(nextApplication.beforeSceneRevision, replayed.revision)
        XCTAssertEqual(nextApplication.afterSceneRevision, replayed.revision + 1)
    }

    func testFrozenMissingEvidenceRejectsWhitespaceBeforeNativeMutation() async throws {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("hss-frozen-missing-evidence-whitespace-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let runtime = try HSSProjectRuntime(
            projectID: UUID(),
            captureID: UUID(),
            sourceIsSynthetic: true,
            trustedRoot: root,
            openMode: .create
        )
        let before = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        do {
            _ = try await runtime.recordFrozenMissingEvidence(
                reason: " \n\t ",
                eventSequence: 1,
                monotonicTime: 1
            )
            XCTFail("Whitespace-only missing-evidence text must fail before native mutation.")
        } catch {
            // Expected: the bridge rejects whitespace before it calls the C ABI.
        }
        let after = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(after.revision, before.revision)
        XCTAssertEqual(after.sceneJSON, before.sceneJSON)
    }

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
        mask: [UInt8]? = nil,
        identity: SceneDepthFrameIdentity = SceneDepthFrameIdentity(
            sessionEpoch: 1, sequence: 1, monotonicTimestamp: 1
        )
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
            identity: identity,
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
        try await runtime.submitMeshEvent(mesh, eventSequence: 3, projectTime: 3)
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

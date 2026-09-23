import CryptoKit
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

    private func metadata(projectID: UUID, synthetic: Bool) -> Data {
        let hex = projectID.hssDirectoryName
        let sourceKind = synthetic ? "true" : "false"
        let body = "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\"\(hex)\",\"source_is_synthetic\":\(sourceKind)}"
        let checksum = SHA256.hash(data: Data(body.utf8)).map { String(format: "%02x", $0) }.joined()
        return Data("\(body.dropLast()),\"metadata_sha256\":\"\(checksum)\"}\n".utf8)
    }
}

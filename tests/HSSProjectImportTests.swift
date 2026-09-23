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

    private func metadata(projectID: UUID, synthetic: Bool) -> Data {
        let hex = projectID.hssDirectoryName
        let sourceKind = synthetic ? "true" : "false"
        let body = "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\"\(hex)\",\"source_is_synthetic\":\(sourceKind)}"
        let checksum = SHA256.hash(data: Data(body.utf8)).map { String(format: "%02x", $0) }.joined()
        return Data("\(body.dropLast()),\"metadata_sha256\":\"\(checksum)\"}\n".utf8)
    }
}

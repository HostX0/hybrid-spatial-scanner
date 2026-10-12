import Foundation
import XCTest
@testable import HybridSpatialScanner

/// Local synthetic packaging fixtures, not sensor or physical accuracy tests.
/// ZIP attachments are retained for an independent external unzip consumer.
final class ScannerDerivedExportShareTests: XCTestCase {
    func testColorlessBundleProducesIndependentLocalZIPAndPreservesSource() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let before = try sourceBytes(fixture.directory)
        let first = try package(fixture)
        let second = try package(fixture)

        XCTAssertNotEqual(first, second, "A new share must never overwrite an older job.")
        XCTAssertEqual(first.pathExtension, "zip")
        XCTAssertEqual(first.deletingLastPathComponent(), fixture.project.appendingPathComponent("exports", isDirectory: true)
            .appendingPathComponent("shares", isDirectory: true))
        XCTAssertTrue(try Data(contentsOf: first).starts(with: [0x50, 0x4b, 0x03, 0x04]))
        XCTAssertEqual(try sourceBytes(fixture.directory), before)
        XCTAssertEqual(try Set(FileManager.default.contentsOfDirectory(atPath: first.deletingLastPathComponent().path)),
            [first.lastPathComponent, second.lastPathComponent])
        attachZIP(first, name: "colorless-derived-export.zip")
        attachExpectedFiles(before, name: "colorless-expected-files.json")
    }

    func testExplicitTexturedBundlePreservesRelativeCompanions() throws {
        let fixture = try makeFixture(includeTexture: true)
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let before = try sourceBytes(fixture.directory)
        let zip = try package(fixture)

        XCTAssertEqual(try sourceBytes(fixture.directory), before)
        XCTAssertNotNil(before[fixture.texturePath])
        XCTAssertEqual(String(data: before["scene.mtl"]!, encoding: .utf8), "newmtl surface\nmap_Kd \(fixture.texturePath)\n")
        XCTAssertFalse(before.keys.contains("project.json"))
        XCTAssertFalse(before.keys.contains("active.manifest"))
        attachZIP(zip, name: "textured-derived-export.zip")
        attachExpectedFiles(before, name: "textured-expected-files.json")
    }

    func testIncompleteBundleCannotProduceShareAndNeverChangesSource() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        try FileManager.default.removeItem(at: fixture.directory.appendingPathComponent("plan.svg"))
        let before = try sourceBytes(fixture.directory)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertEqual(try sourceBytes(fixture.directory), before)
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testUnlistedRawCaptureFileIsRejectedNotSilentlyShared() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let privateData = Data("private raw capture fixture".utf8)
        try privateData.write(to: fixture.directory.appendingPathComponent("capture.jpg"))
        let before = try sourceBytes(fixture.directory)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertEqual(try sourceBytes(fixture.directory), before)
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testSymlinkCannotAliasPrivateCaptureThroughApprovedFilename() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let raw = fixture.project.appendingPathComponent("private-capture")
        let privateData = Data("private capture".utf8)
        try privateData.write(to: raw)
        let plan = fixture.directory.appendingPathComponent("plan.svg")
        try FileManager.default.removeItem(at: plan)
        try FileManager.default.createSymbolicLink(at: plan, withDestinationURL: raw)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertEqual(try Data(contentsOf: raw), privateData)
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testHardLinkCannotAliasPrivateCaptureThroughApprovedFilename() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let raw = fixture.project.appendingPathComponent("private-capture")
        let privateData = Data("fixture:plan.svg".utf8)
        try privateData.write(to: raw)
        let plan = fixture.directory.appendingPathComponent("plan.svg")
        try FileManager.default.removeItem(at: plan)
        try FileManager.default.linkItem(at: raw, to: plan)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertEqual(try Data(contentsOf: raw), privateData)
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testWrongProjectRevisionOrSourceLocationCannotBePackaged() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }

        XCTAssertThrowsError(try ScannerDerivedExportShare.package(directory: fixture.directory,
            projectID: UUID(), sceneRevision: 7, trustedRoot: fixture.root))
        XCTAssertThrowsError(try ScannerDerivedExportShare.package(directory: fixture.directory,
            projectID: fixture.projectID, sceneRevision: 8, trustedRoot: fixture.root))
        XCTAssertThrowsError(try ScannerDerivedExportShare.package(directory: fixture.project,
            projectID: fixture.projectID, sceneRevision: 7, trustedRoot: fixture.root))
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testManifestCannotAuthorizeTraversalOrDuplicateEntries() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let url = fixture.directory.appendingPathComponent("export-manifest.json")
        var document = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
        var records = try XCTUnwrap(document["files"] as? [[String: Any]])
        records.append(["path": "../project.json", "byte_count": 2])
        document["files"] = records
        try JSONSerialization.data(withJSONObject: document).write(to: url)
        XCTAssertThrowsError(try package(fixture))
        records.removeLast()
        records.append(records[0])
        document["files"] = records
        try JSONSerialization.data(withJSONObject: document).write(to: url)
        XCTAssertThrowsError(try package(fixture))
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    func testSymlinkedShareDirectoryCannotRedirectPackageOutsideProjectDeletionTree() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let outside = fixture.root.appendingPathComponent("outside")
        try FileManager.default.createDirectory(at: outside, withIntermediateDirectories: false)
        try FileManager.default.createSymbolicLink(at: fixture.project.appendingPathComponent("exports/shares"),
            withDestinationURL: outside)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: outside.path).isEmpty)
    }

    func testMissingMTLTextureCompanionIsRejected() throws {
        let fixture = try makeFixture()
        defer { try? FileManager.default.removeItem(at: fixture.root) }
        let mtl = Data("newmtl surface\nmap_Kd textures/00000000-0000-0000-0000-000000000001.jpg\n".utf8)
        try mtl.write(to: fixture.directory.appendingPathComponent("scene.mtl"))
        let manifestURL = fixture.directory.appendingPathComponent("export-manifest.json")
        var document = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: manifestURL)) as? [String: Any])
        var records = try XCTUnwrap(document["files"] as? [[String: Any]])
        let index = try XCTUnwrap(records.firstIndex { $0["path"] as? String == "scene.mtl" })
        records[index]["byte_count"] = mtl.count
        document["files"] = records
        try JSONSerialization.data(withJSONObject: document).write(to: manifestURL)

        XCTAssertThrowsError(try package(fixture))
        XCTAssertTrue(try shares(fixture).isEmpty)
    }

    private struct Fixture {
        let root: URL
        let projectID: UUID
        let project: URL
        let directory: URL
        let texturePath = "textures/00000000-0000-0000-0000-000000000001.jpg"
    }

    private func makeFixture(includeTexture: Bool = false) throws -> Fixture {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-derived-share-\(UUID().uuidString)", isDirectory: true)
        let projectID = UUID()
        let project = root.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        try FileManager.default.createDirectory(at: project, withIntermediateDirectories: true)
        try Data("{}".utf8).write(to: project.appendingPathComponent("project.json"))
        try Data("raw capture must not be shared".utf8).write(to: project.appendingPathComponent("private-capture"))
        var files = Dictionary(uniqueKeysWithValues: ScannerOfflineArtifactSet.requiredNames.map { ($0, Data("fixture:\($0)".utf8)) })
        files["scene.obj"] = Data("# synthetic packing fixture\nmtllib scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n".utf8)
        files["scene.mtl"] = Data("newmtl surface\nKd 0.7 0.7 0.7\n".utf8)
        if includeTexture {
            // Opaque derivative bytes exercise packaging, not JPEG validation
            // or RGB/depth registration (those belong to appearance tests).
            files["textures/00000000-0000-0000-0000-000000000001.jpg"] = Data([0xff, 0xd8, 0xff, 0xd9])
            files["appearance.provenance.json"] = Data("{\"texture_mode\":\"included\",\"source_is_synthetic\":true}".utf8)
            files["scene.mtl"] = Data("newmtl surface\nmap_Kd textures/00000000-0000-0000-0000-000000000001.jpg\n".utf8)
        }
        let directory = try ScannerOfflineExportStore.publish(ScannerOfflineArtifactSet(files: files),
            projectID: projectID, sceneRevision: 7, trustedRoot: root)
        return Fixture(root: root, projectID: projectID, project: project, directory: directory)
    }

    private func package(_ fixture: Fixture) throws -> URL {
        try ScannerDerivedExportShare.package(directory: fixture.directory, projectID: fixture.projectID,
            sceneRevision: 7, trustedRoot: fixture.root)
    }

    private func shares(_ fixture: Fixture) throws -> [String] {
        let directory = fixture.project.appendingPathComponent("exports/shares")
        guard FileManager.default.fileExists(atPath: directory.path) else { return [] }
        return try FileManager.default.contentsOfDirectory(atPath: directory.path)
    }

    private func sourceBytes(_ directory: URL) throws -> [String: Data] {
        var result: [String: Data] = [:]
        for name in try FileManager.default.contentsOfDirectory(atPath: directory.path) {
            if name == "textures" {
                for texture in try FileManager.default.contentsOfDirectory(atPath: directory.appendingPathComponent(name).path) {
                    result["textures/\(texture)"] = try Data(contentsOf: directory.appendingPathComponent("textures/\(texture)"))
                }
            } else {
                result[name] = try Data(contentsOf: directory.appendingPathComponent(name))
            }
        }
        return result
    }

    private func attachZIP(_ url: URL, name: String) {
        let attachment = XCTAttachment(contentsOfFile: url)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func attachExpectedFiles(_ files: [String: Data], name: String) {
        let document = files.mapValues { $0.base64EncodedString() }
        let attachment = XCTAttachment(data: try! JSONSerialization.data(withJSONObject: document, options: [.sortedKeys]),
            uniformTypeIdentifier: "public.json")
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}

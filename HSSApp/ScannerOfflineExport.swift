import Foundation
import simd

enum ScannerExportState: Equatable, Sendable {
    case idle
    case exporting
    case exported(directory: URL, at: Date)
    case failed(String)
}

/// A shareable HSSCAN package remains local until the owner chooses a system
/// share destination. It is independent from the frozen PDF/SVG/DXF/OBJ bundle.
enum ScannerProjectArchiveExportState: Equatable, Sendable {
    case idle
    case exporting
    case exported(URL)
    case failed(String)
}

struct ScannerOfflineArtifactSet: Sendable {
    let files: [String: Data]

    static let requiredNames: Set<String> = [
        "plan.pdf",
        "plan.svg",
        "plan.provenance.json",
        "plan.dxf",
        "dxf.provenance.json",
        "scene.obj",
        "scene.mtl",
        "scene.provenance.json",
    ]

    init(files: [String: Data]) throws {
        guard Set(files.keys) == Self.requiredNames,
              files.values.allSatisfy({ !$0.isEmpty }) else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        self.files = files
    }
}

enum ScannerOfflineExportError: LocalizedError, Equatable {
    case ineligibleRevision
    case incompleteArtifacts
    case projectDirectoryUnavailable

    var errorDescription: String? {
        switch self {
        case .ineligibleRevision:
            String(localized: "Only a saved, frozen accepted revision with measured plan and mesh geometry can be exported.")
        case .incompleteArtifacts:
            String(localized: "The native exporters did not produce a complete offline bundle.")
        case .projectDirectoryUnavailable:
            String(localized: "The selected local project directory is unavailable.")
        }
    }
}

enum ScannerExportEligibility {
    static func isEligible(
        snapshot: ScannerSceneSnapshot?,
        project: ScannerProjectSummary?,
        saveState: ScannerWorkspaceSaveState,
        phase: ScannerWorkspacePhase
    ) -> Bool {
        guard phase == .frozen,
              let snapshot,
              let project,
              snapshot.isFrozen,
              snapshot.revision > 0,
              project.state == .frozen,
              project.sceneRevision == snapshot.revision,
              isPersisted(saveState),
              validMesh(snapshot),
              hasMeasuredPlanGeometry(snapshot: snapshot, projectID: project.id) else {
            return false
        }
        return true
    }

    private static func isPersisted(_ state: ScannerWorkspaceSaveState) -> Bool {
        switch state {
        case .clean, .saved:
            true
        case .noProject, .hasUnsavedChanges, .saving, .failed:
            false
        }
    }

    private static func validMesh(_ snapshot: ScannerSceneSnapshot) -> Bool {
        !snapshot.verticesMetres.isEmpty
            && snapshot.verticesMetres.allSatisfy { $0.x.isFinite && $0.y.isFinite && $0.z.isFinite }
            && !snapshot.triangleIndices.isEmpty
            && snapshot.triangleIndices.count.isMultiple(of: 3)
            && snapshot.triangleIndices.allSatisfy { Int($0) < snapshot.verticesMetres.count }
    }

    /// Eligibility requires at least one explicit edge with metric authority.
    /// Unknown and inferred edges remain in native provenance manifests, but
    /// they never make an export eligible and never become drawing geometry.
    private static func hasMeasuredPlanGeometry(
        snapshot: ScannerSceneSnapshot,
        projectID: UUID
    ) -> Bool {
        guard let root = try? JSONSerialization.jsonObject(with: snapshot.sceneJSON) as? [String: Any],
              root["project_id"] as? String == projectID.uuidString.lowercased(),
              root["scene_revision"] as? String == String(snapshot.revision),
              let nodes = root["nodes"] as? [[String: Any]],
              let edges = root["edges"] as? [[String: Any]] else {
            return false
        }
        let positions: [String: SIMD3<Double>] = Dictionary(uniqueKeysWithValues: nodes.compactMap { node in
            guard let id = node["id"] as? String,
                  let values = node["position_m"] as? [Double],
                  values.count == 3,
                  values.allSatisfy(\.isFinite) else { return nil }
            return (id, SIMD3(values[0], values[1], values[2]))
        })
        return edges.contains { edge in
            guard let startID = edge["start_node_id"] as? String,
                  let endID = edge["end_node_id"] as? String,
                  let start = positions[startID],
                  let end = positions[endID],
                  start != end,
                  let provenance = edge["provenance"] as? [String: Any],
                  let origin = provenance["geometry_origin"] as? String else {
                return false
            }
            return origin == "observed" || origin == "fitted" || origin == "user_measured"
        }
    }
}

enum ScannerOfflineExporter {
    static func export(
        snapshot: ScannerSceneSnapshot,
        projectID: UUID,
        trustedRoot: URL
    ) throws -> URL {
        let flattenedVertices = snapshot.verticesMetres.flatMap { [$0.x, $0.y, $0.z] }
        let vertexData = flattenedVertices.withUnsafeBufferPointer { Data(buffer: $0) }
        let indexData = snapshot.triangleIndices.withUnsafeBufferPointer { Data(buffer: $0) }
        let artifacts = try HSSExportBridge.buildArtifacts(
            forProjectID: projectID.uuidString.lowercased(),
            sceneRevision: snapshot.revision,
            sceneJSON: snapshot.sceneJSON,
            verticesXYZ: vertexData,
            triangleIndices: indexData
        )
        let set = try ScannerOfflineArtifactSet(files: [
            "plan.pdf": artifacts.pdfData,
            "plan.svg": artifacts.svgData,
            "plan.provenance.json": artifacts.pdfSVGManifestData,
            "plan.dxf": artifacts.dxfData,
            "dxf.provenance.json": artifacts.dxfManifestData,
            "scene.obj": artifacts.objData,
            "scene.mtl": artifacts.mtlData,
            "scene.provenance.json": artifacts.objManifestData,
        ])
        return try ScannerOfflineExportStore.publish(
            set,
            projectID: projectID,
            sceneRevision: snapshot.revision,
            trustedRoot: trustedRoot
        )
    }
}

enum ScannerOfflineExportStore {
    private struct Manifest: Codable {
        let format: String
        let version: Int
        let projectID: UUID
        let sceneRevision: UInt64
        let createdAt: Date
        let files: [FileRecord]

        enum CodingKeys: String, CodingKey {
            case format, version, files
            case projectID = "project_id"
            case sceneRevision = "scene_revision"
            case createdAt = "created_at"
        }
    }

    private struct FileRecord: Codable {
        let path: String
        let byteCount: Int

        enum CodingKeys: String, CodingKey {
            case path
            case byteCount = "byte_count"
        }
    }

    static func publish(
        _ artifacts: ScannerOfflineArtifactSet,
        projectID: UUID,
        sceneRevision: UInt64,
        trustedRoot: URL,
        now: Date = Date(),
        transactionID: UUID = UUID()
    ) throws -> URL {
        guard sceneRevision > 0 else { throw ScannerOfflineExportError.ineligibleRevision }
        let fileManager = FileManager.default
        let projectDirectory = trustedRoot.appendingPathComponent(projectDirectoryName(projectID), isDirectory: true)
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: projectDirectory.path, isDirectory: &isDirectory),
              isDirectory.boolValue,
              fileManager.fileExists(atPath: projectDirectory.appendingPathComponent("project.json").path) else {
            throw ScannerOfflineExportError.projectDirectoryUnavailable
        }

        let exportsDirectory = projectDirectory.appendingPathComponent("exports", isDirectory: true)
        try fileManager.createDirectory(at: exportsDirectory, withIntermediateDirectories: true)
        let token = transactionID.uuidString.lowercased()
        let staging = exportsDirectory.appendingPathComponent(".pending-\(token)", isDirectory: true)
        let destination = exportsDirectory.appendingPathComponent(
            "revision-\(sceneRevision)-\(token)",
            isDirectory: true
        )
        try fileManager.createDirectory(at: staging, withIntermediateDirectories: false)
        var published = false
        defer {
            if !published { try? fileManager.removeItem(at: staging) }
        }

        for name in artifacts.files.keys.sorted() {
            try artifacts.files[name]!.write(
                to: staging.appendingPathComponent(name, isDirectory: false),
                options: [.atomic]
            )
        }
        let manifest = Manifest(
            format: "hss.offline-export",
            version: 1,
            projectID: projectID,
            sceneRevision: sceneRevision,
            createdAt: now,
            files: artifacts.files.keys.sorted().map {
                FileRecord(path: $0, byteCount: artifacts.files[$0]!.count)
            }
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(manifest).write(
            to: staging.appendingPathComponent("export-manifest.json", isDirectory: false),
            options: [.atomic]
        )
        try fileManager.moveItem(at: staging, to: destination)
        published = true
        return destination
    }

    static func projectDirectoryName(_ projectID: UUID) -> String {
        var bytes = projectID.uuid
        return withUnsafeBytes(of: &bytes) { buffer in
            buffer.map { String(format: "%02x", $0) }.joined()
        }
    }
}

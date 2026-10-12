import Foundation
import CryptoKit
import simd
import Darwin

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

enum ScannerProjectArchiveExportName {
    static func make(projectID: UUID, sceneRevision: UInt64, exportID: UUID = UUID()) -> String {
        "\(projectID.hssDirectoryName)-r\(sceneRevision)-\(exportID.uuidString.lowercased()).hsscan"
    }
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
        "semantic.provenance.json",
    ]

    init(files: [String: Data]) throws {
        guard Self.validFileLengths(files.mapValues(\.count)) else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        self.files = files
    }

    /// The same bounded allowlist applies to publication and local sharing.
    static func validFileLengths(_ lengths: [String: Int]) -> Bool {
        let extras = Set(lengths.keys).subtracting(Self.requiredNames)
        let images = extras.subtracting(["appearance.provenance.json"])
        guard lengths.count <= Self.requiredNames.count + 25,
              Self.requiredNames.isSubset(of: Set(lengths.keys)),
              lengths.values.allSatisfy({ $0 > 0 && $0 <= 64 * 1024 * 1024 }),
              lengths.values.reduce(0, +) <= 192 * 1024 * 1024 else { return false }
        return extras.isEmpty || (extras.contains("appearance.provenance.json") && !images.isEmpty && images.count <= 24 &&
            images.allSatisfy { Self.isTexturePath($0) && lengths[$0]! <= 4 * 1024 * 1024 })
    }

    static func isTexturePath(_ name: String) -> Bool {
        guard name.hasPrefix("textures/"), name.hasSuffix(".jpg") else { return false }
        let id = String(name.dropFirst(9).dropLast(4))
        return UUID(uuidString: id)?.uuidString.lowercased() == id
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
        trustedRoot: URL,
        appearance: ScannerTexturedMesh? = nil
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
        let semanticProvenance = try semanticProvenance(snapshot)
        var files: [String: Data] = [
            "plan.pdf": artifacts.pdfData,
            "plan.svg": artifacts.svgData,
            "plan.provenance.json": artifacts.pdfSVGManifestData,
            "plan.dxf": artifacts.dxfData,
            "dxf.provenance.json": artifacts.dxfManifestData,
            "scene.obj": artifacts.objData,
            "scene.mtl": artifacts.mtlData,
            "scene.provenance.json": artifacts.objManifestData,
            "semantic.provenance.json": semanticProvenance,
        ]
        // Explicit opt-in by the owner. A stale/corrupt optional layer never
        // blocks the ordinary measured bundle and never changes its geometry.
        if let appearance, appearance.projectID == projectID, appearance.matches(snapshot),
           let imageFiles = verifiedAppearanceFiles(appearance, snapshot: snapshot) {
            files["scene.obj"] = appearance.obj
            files["scene.mtl"] = appearance.mtl
            files["scene.provenance.json"] = appearance.manifest
            files["appearance.provenance.json"] = appearance.manifest
            files.merge(imageFiles) { _, new in new }
        }
        let set = try ScannerOfflineArtifactSet(files: files)
        return try ScannerOfflineExportStore.publish(
            set,
            projectID: projectID,
            sceneRevision: snapshot.revision,
            trustedRoot: trustedRoot
        )
    }

    private static func verifiedAppearanceFiles(_ layer: ScannerTexturedMesh, snapshot: ScannerSceneSnapshot) -> [String: Data]? {
        guard !layer.faces.isEmpty, !layer.images.isEmpty, layer.images.count <= 24,
              layer.manifest.count <= 16 * 1024 * 1024,
              layer.obj.count <= 64 * 1024 * 1024, layer.mtl.count <= 16 * 1024 * 1024,
              let obj = String(data: layer.obj, encoding: .utf8), obj.contains("\nmtllib scene.mtl\n"),
              let document = try? JSONSerialization.jsonObject(with: layer.manifest) as? [String: Any],
              document["project_id"] as? String == layer.projectID.uuidString.lowercased(),
              document["scene_revision"] as? String == String(snapshot.revision),
              document["texture_mode"] as? String == "included",
              document["units"] as? String == "m",
              let slots = document["appearance"] as? [[String: Any]] else { return nil }
        var references: [String: String] = [:]
        for slot in slots {
            guard slot["state"] as? String == "verified", let texture = slot["texture"] as? [String: Any],
                  let path = texture["path"] as? String, ScannerOfflineArtifactSet.isTexturePath(path),
                  let hash = texture["sha256"] as? String else { return nil }
            if let previous = references[path], previous != hash { return nil }
            references[path] = hash
        }
        guard references.count == layer.images.count else { return nil }
        var result: [String: Data] = [:]
        for (id, image) in layer.images {
            let path = "textures/\(id).jpg"
            let hash = SHA256.hash(data: image).map { String(format: "%02x", $0) }.joined()
            guard ScannerAppearanceStore.jpegDimensions(image) != nil, references[path] == hash else { return nil }
            result[path] = image
        }
        return result
    }

    /// Frozen derived interpretation accompanies the measured exporters; it
    /// never supplies geometry to their PDF, SVG, DXF, or OBJ inputs.
    private static func semanticProvenance(_ snapshot: ScannerSceneSnapshot) throws -> Data {
        let semantic = snapshot.planAnnotation
        guard snapshot.isFrozen,
              semantic == nil || semantic?.sceneRevision == snapshot.revision else {
            throw ScannerOfflineExportError.ineligibleRevision
        }
        let plan = try semantic?.planJSON.map { try JSONSerialization.jsonObject(with: $0) }
        let annotations = try semantic?.annotationJSON.map { try JSONSerialization.jsonObject(with: $0) }
        if let semantic {
            guard semantic.planAvailable == (plan != nil),
                  semantic.annotationLayoutAvailable == (annotations != nil),
                  plan == nil || (plan as? [String: Any])?["scene_revision"] as? String == String(snapshot.revision),
                  annotations == nil || (annotations as? [String: Any])?["scene_revision"] as? String == String(snapshot.revision) else {
                throw ScannerOfflineExportError.ineligibleRevision
            }
        }
        let record: [String: Any] = [
            "scene_revision": String(snapshot.revision),
            "authority": "derived_plan_annotation_snapshot",
            "measured_scene_authority": "scene.provenance.json",
            "plan_available": semantic?.planAvailable ?? false,
            "annotation_layout_available": semantic?.annotationLayoutAvailable ?? false,
            "reference_state": semantic?.referenceState as Any? ?? NSNull(),
            "boundary_state": semantic?.boundaryState as Any? ?? NSNull(),
            "plan_availability": semantic?.planAvailability as Any? ?? NSNull(),
            "area_availability": semantic?.areaAvailability as Any? ?? NSNull(),
            "area_m2": semantic?.areaMetresSquared as Any? ?? NSNull(),
            "plan": plan ?? NSNull(),
            "annotations": annotations ?? NSNull(),
        ]
        return try JSONSerialization.data(withJSONObject: record, options: [.sortedKeys])
    }
}

enum ScannerOfflineExportStore {
    fileprivate struct Manifest: Codable {
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

    fileprivate struct FileRecord: Codable {
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

        if artifacts.files.keys.contains(where: { $0.hasPrefix("textures/") }) {
            try fileManager.createDirectory(at: staging.appendingPathComponent("textures", isDirectory: true),
                                            withIntermediateDirectories: false)
        }

        for name in artifacts.files.keys.sorted() {
            try artifacts.files[name]!.write(
                to: staging.appendingPathComponent(name, isDirectory: false),
                options: [.atomic,.completeFileProtectionUntilFirstUserAuthentication]
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

/// A derived PDF/SVG/DXF/OBJ bundle is not a source-project HSSCAN archive.
/// Packaging is local and blocking; the caller runs it off the UI actor and
/// presents the returned file only after the owner explicitly chooses Export.
/// The caller removes that one ZIP when sharing completes or is cancelled.
/// All temporary/output files stay inside the project's ordinary purge tree.
enum ScannerDerivedExportShare {
    static func package(
        directory: URL,
        projectID: UUID,
        sceneRevision: UInt64,
        trustedRoot: URL
    ) throws -> URL {
        guard directory.isFileURL, trustedRoot.isFileURL, sceneRevision > 0 else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        let fileManager = FileManager.default
        let root = trustedRoot.standardizedFileURL
        let project = root.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        let exports = project.appendingPathComponent("exports", isDirectory: true)
        let source = directory.standardizedFileURL
        let prefix = "revision-\(sceneRevision)-"
        let name = source.lastPathComponent
        guard source.deletingLastPathComponent() == exports,
              name.hasPrefix(prefix),
              let transaction = UUID(uuidString: String(name.dropFirst(prefix.count))),
              name == prefix + transaction.uuidString.lowercased() else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        for folder in [root, project, exports, source] { try requireDirectory(folder) }

        // Snapshot only the manifest-approved derivative files. Never ask the
        // system to ZIP the project, the source-capture tree, or unlisted files.
        let shares = exports.appendingPathComponent("shares", isDirectory: true)
        if fileManager.fileExists(atPath: shares.path) {
            try requireDirectory(shares)
        } else {
            try fileManager.createDirectory(at: shares, withIntermediateDirectories: false,
                attributes: [.posixPermissions: 0o700, .protectionKey: FileProtectionType.completeUntilFirstUserAuthentication])
        }
        let token = UUID().uuidString.lowercased()
        let staging = shares.appendingPathComponent(".pending-\(token)", isDirectory: true)
        try fileManager.createDirectory(at: staging, withIntermediateDirectories: false,
            attributes: [.posixPermissions: 0o700, .protectionKey: FileProtectionType.completeUntilFirstUserAuthentication])
        defer { try? fileManager.removeItem(at: staging) }
        let snapshot = staging.appendingPathComponent("HSS-export-r\(sceneRevision)", isDirectory: true)
        try fileManager.createDirectory(at: snapshot, withIntermediateDirectories: false)

        try coordinateRead(source, options: []) { coordinatedSource in
            guard coordinatedSource.standardizedFileURL == source else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            let manifestData = try boundedRegularData(source.appendingPathComponent("export-manifest.json"), maximum: 65_536)
            guard let shape = try JSONSerialization.jsonObject(with: manifestData) as? [String: Any],
                  Set(shape.keys) == ["format", "version", "project_id", "scene_revision", "created_at", "files"],
                  let records = shape["files"] as? [[String: Any]],
                  records.allSatisfy({ Set($0.keys) == ["path", "byte_count"] }) else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            let decoder = JSONDecoder()
            decoder.dateDecodingStrategy = .iso8601
            let manifest = try decoder.decode(ScannerOfflineExportStore.Manifest.self, from: manifestData)
            guard manifest.format == "hss.offline-export", manifest.version == 1,
                  manifest.projectID == projectID, manifest.sceneRevision == sceneRevision,
                  manifest.files.count <= ScannerOfflineArtifactSet.requiredNames.count + 25,
                  Set(manifest.files.map(\.path)).count == manifest.files.count else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            let lengths = Dictionary(uniqueKeysWithValues: manifest.files.map { ($0.path, $0.byteCount) })
            guard ScannerOfflineArtifactSet.validFileLengths(lengths) else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            let topLevel = Set(lengths.keys.map { $0.hasPrefix("textures/") ? "textures" : $0 })
                .union(["export-manifest.json"])
            guard try Set(fileManager.contentsOfDirectory(atPath: source.path)) == topLevel else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            let textureNames = Set(lengths.keys.filter { $0.hasPrefix("textures/") }.map { String($0.dropFirst(9)) })
            if !textureNames.isEmpty {
                let textures = source.appendingPathComponent("textures", isDirectory: true)
                try requireDirectory(textures)
                guard try Set(fileManager.contentsOfDirectory(atPath: textures.path)) == textureNames else {
                    throw ScannerOfflineExportError.incompleteArtifacts
                }
                try fileManager.createDirectory(at: snapshot.appendingPathComponent("textures", isDirectory: true),
                    withIntermediateDirectories: false)
            }
            for record in manifest.files {
                let data = try boundedRegularData(source.appendingPathComponent(record.path), maximum: record.byteCount)
                guard data.count == record.byteCount else { throw ScannerOfflineExportError.incompleteArtifacts }
                if record.path == "scene.obj" {
                    guard let text = String(data: data, encoding: .utf8),
                          text.split(separator: "\n").filter({ $0.hasPrefix("mtllib ") }) == ["mtllib scene.mtl"] else {
                        throw ScannerOfflineExportError.incompleteArtifacts
                    }
                }
                if record.path == "scene.mtl" {
                    guard let text = String(data: data, encoding: .utf8) else {
                        throw ScannerOfflineExportError.incompleteArtifacts
                    }
                    let references = text.split(separator: "\n").filter { $0.hasPrefix("map_Kd ") }
                        .map { String($0.dropFirst(7)) }
                    guard references.allSatisfy({ ScannerOfflineArtifactSet.isTexturePath($0) && lengths[$0] != nil }) else {
                        throw ScannerOfflineExportError.incompleteArtifacts
                    }
                }
                try data.write(to: snapshot.appendingPathComponent(record.path),
                    options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
            }
            try manifestData.write(to: snapshot.appendingPathComponent("export-manifest.json"),
                options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
        }

        let stagedZIP = staging.appendingPathComponent("share.zip")
        // Despite its API name, .forUploading only snapshots locally. This
        // function performs no upload; the owner's system share choice is later.
        try coordinateRead(snapshot, options: [.forUploading]) { zippedSnapshot in
            // The coordinator unlinks its temporary ZIP after this accessor.
            // Preserve our own copy before returning, without transmitting it.
            try fileManager.copyItem(at: zippedSnapshot, to: stagedZIP)
        }
        let attributes = try fileManager.attributesOfItem(atPath: stagedZIP.path)
        guard attributes[.type] as? FileAttributeType == .typeRegular,
              let byteCount = attributes[.size] as? NSNumber,
              byteCount.int64Value > 4, byteCount.int64Value <= 256 * 1024 * 1024 else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        let zip = try FileHandle(forReadingFrom: stagedZIP)
        defer { try? zip.close() }
        guard try zip.read(upToCount: 4) == Data([0x50, 0x4b, 0x03, 0x04]) else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        try fileManager.setAttributes([.posixPermissions: 0o600,
            .protectionKey: FileProtectionType.completeUntilFirstUserAuthentication], ofItemAtPath: stagedZIP.path)
        let destination = shares.appendingPathComponent("HSS-export-r\(sceneRevision)-\(token).zip")
        try fileManager.moveItem(at: stagedZIP, to: destination)
        return destination
    }

    private static func coordinateRead(
        _ url: URL,
        options: NSFileCoordinator.ReadingOptions,
        operation: (URL) throws -> Void
    ) throws {
        var coordinationError: NSError?
        var operationError: Error?
        var accessed = false
        NSFileCoordinator().coordinate(readingItemAt: url, options: options, error: &coordinationError) { value in
            accessed = true
            do { try operation(value) } catch { operationError = error }
        }
        if let coordinationError { throw coordinationError }
        if let operationError { throw operationError }
        guard accessed else { throw ScannerOfflineExportError.incompleteArtifacts }
    }

    private static func requireDirectory(_ url: URL) throws {
        var metadata = stat()
        guard lstat(url.path, &metadata) == 0, metadata.st_mode & S_IFMT == S_IFDIR else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
    }

    /// No symbolic or hard-link alias may turn an approved derivative filename
    /// into a read of a raw capture/private asset. One bounded file is copied at
    /// a time; a source length change fails rather than growing the ZIP budget.
    private static func boundedRegularData(_ url: URL, maximum: Int) throws -> Data {
        let descriptor = open(url.path, O_RDONLY | O_NOFOLLOW)
        guard descriptor >= 0 else { throw ScannerOfflineExportError.incompleteArtifacts }
        let file = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
        defer { try? file.close() }
        var metadata = stat()
        guard fstat(descriptor, &metadata) == 0, metadata.st_mode & S_IFMT == S_IFREG,
              metadata.st_nlink == 1, metadata.st_size > 0, metadata.st_size <= maximum else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        var data = Data()
        while data.count < metadata.st_size {
            guard let chunk = try file.read(upToCount: min(65_536, Int(metadata.st_size) - data.count)), !chunk.isEmpty else {
                throw ScannerOfflineExportError.incompleteArtifacts
            }
            data.append(chunk)
        }
        guard data.count == metadata.st_size, data.count <= maximum,
              (try file.read(upToCount: 1) ?? Data()).isEmpty else {
            throw ScannerOfflineExportError.incompleteArtifacts
        }
        return data
    }
}

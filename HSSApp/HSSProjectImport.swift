import CryptoKit
import Foundation

enum HSSProjectImportError: LocalizedError {
    case invalidProjectMetadata
    case projectAlreadyExists

    var errorDescription: String? {
        switch self {
        case .invalidProjectMetadata:
            "The staged project metadata is invalid."
        case .projectAlreadyExists:
            "A local project with this identity already exists."
        }
    }
}

/// Archive parsing, decompression, stream verification, and staging can read
/// gigabytes. Keep that synchronous native work off the workspace actor while
/// handing it only copied file paths; the caller owns security-scoped access
/// until this operation returns.
enum HSSProjectImportBackgroundWork {
    static func perform<Value: Sendable>(
        _ operation: @escaping @Sendable () throws -> Value
    ) async throws -> Value {
        try Task.checkCancellation()
        let value = try await Task.detached(priority: .userInitiated, operation: operation).value
        try Task.checkCancellation()
        return value
    }

    static func extractArchive(sourcePath: String, stagingPath: String) async throws {
        try await perform {
            try HSSProjectImportBridge.extractHSSCANArchive(
                at: URL(fileURLWithPath: sourcePath),
                toStagingDirectory: URL(fileURLWithPath: stagingPath)
            )
        }
    }
}

enum HSSProjectArchiveExport {
    static func perform<Value: Sendable>(
        _ operation: @escaping @Sendable () throws -> Value
    ) async throws -> Value {
        try await HSSProjectImportBackgroundWork.perform(operation)
    }

    static func writeArchive(projectPath: String, destinationPath: String) async throws {
        try await perform {
            try HSSProjectArchiveExportBridge.writeHSSCANArchive(
                forProjectDirectory: URL(fileURLWithPath: projectPath),
                to: URL(fileURLWithPath: destinationPath)
            )
        }
    }
}

/// Final import adoption is intentionally separate from archive staging. Once
/// the private staged directory is moved into the library, every following
/// validation and catalog write belongs to the same rollback boundary.
@MainActor
enum HSSProjectImportAdoption {
    static func adopt<Value>(
        fileManager: FileManager = .default,
        stagedProject: URL,
        destination: URL,
        validate: () async throws -> Value,
        publish: (Value) throws -> Void
    ) async throws -> Value {
        guard !fileManager.fileExists(atPath: destination.path) else {
            throw HSSProjectImportError.projectAlreadyExists
        }
        var movedDestination = false
        do {
            try fileManager.moveItem(at: stagedProject, to: destination)
            movedDestination = true
            let value = try await validate()
            try publish(value)
            return value
        } catch {
            // Never remove a pre-existing project. `movedDestination` becomes
            // true only after this call moved the private staging directory.
            if movedDestination {
                try? fileManager.removeItem(at: destination)
            }
            throw error
        }
    }
}

struct HSSImportedProjectMetadata: Equatable, Sendable {
    let projectID: UUID
    let sourceIsSynthetic: Bool

    /// This reads only the fixed, checksummed project metadata format. It does
    /// not make the project usable: the native store still validates every
    /// committed manifest and replays retained source before adoption.
    static func parse(_ data: Data) throws -> HSSImportedProjectMetadata {
        guard let text = String(data: data, encoding: .utf8), text.hasSuffix("\n") else {
            throw HSSProjectImportError.invalidProjectMetadata
        }
        let prefix = "{\"format\":\"hss.project\",\"version\":1,\"project_id\":\""
        let syntheticPrefix = "\",\"source_is_synthetic\":"
        let checksumPrefix = ",\"metadata_sha256\":\""
        guard text.hasPrefix(prefix),
              let syntheticRange = text.range(of: syntheticPrefix, range: text.index(text.startIndex, offsetBy: prefix.count)..<text.endIndex),
              let checksumRange = text.range(of: checksumPrefix, range: syntheticRange.upperBound..<text.endIndex),
              text.hasSuffix("\"}\n") else {
            throw HSSProjectImportError.invalidProjectMetadata
        }
        let projectHex = String(text[text.index(text.startIndex, offsetBy: prefix.count)..<syntheticRange.lowerBound])
        let syntheticText = String(text[syntheticRange.upperBound..<checksumRange.lowerBound])
        let checksumStart = checksumRange.upperBound
        let checksumEnd = text.index(text.endIndex, offsetBy: -3)
        let checksum = String(text[checksumStart..<checksumEnd])
        guard projectHex.count == 32, checksum.count == 64,
              let projectID = UUID(hssHex: projectHex),
              let sourceIsSynthetic = Bool(hssLiteral: syntheticText) else {
            throw HSSProjectImportError.invalidProjectMetadata
        }
        let body = String(text[..<checksumRange.lowerBound]) + "}"
        let digest = SHA256.hash(data: Data(body.utf8)).map { String(format: "%02x", $0) }.joined()
        guard digest == checksum else { throw HSSProjectImportError.invalidProjectMetadata }
        return HSSImportedProjectMetadata(projectID: projectID, sourceIsSynthetic: sourceIsSynthetic)
    }
}

extension UUID {
    init?(hssHex: String) {
        guard hssHex.unicodeScalars.allSatisfy({ ("0"..."9").contains($0) || ("a"..."f").contains($0) }) else {
            return nil
        }
        let dashed = "\(hssHex.prefix(8))-\(hssHex.dropFirst(8).prefix(4))-\(hssHex.dropFirst(12).prefix(4))-\(hssHex.dropFirst(16).prefix(4))-\(hssHex.dropFirst(20))"
        self.init(uuidString: dashed)
    }

    var hssDirectoryName: String {
        uuidString.replacingOccurrences(of: "-", with: "").lowercased()
    }
}

/// Purges only app-owned UUID project storage and its generated local archives.
/// User-shared copies in Files/providers and separate Object Capture sessions
/// are outside this ownership boundary. No path from imported metadata is used.
enum HSSLocalProjectRemoval {
    static func remove(projectID: UUID, trustedRoot: URL, fileManager: FileManager = .default) throws {
        guard trustedRoot.isFileURL else { throw CocoaError(.fileReadInvalidFileName) }
        let root = trustedRoot.standardizedFileURL.resolvingSymlinksInPath()
        let project = root.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        let archives = root.appendingPathComponent("archives", isDirectory: true)
        var targets: [URL] = []
        if fileManager.fileExists(atPath: project.path) {
            let values = try project.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
            guard values.isSymbolicLink != true, values.isDirectory == true,
                  project.resolvingSymlinksInPath().deletingLastPathComponent() == root else {
                throw CocoaError(.fileReadNoPermission)
            }
            targets.append(project)
        }
        if fileManager.fileExists(atPath: archives.path) {
            let values = try archives.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
            guard values.isSymbolicLink != true, values.isDirectory == true else {
                throw CocoaError(.fileReadNoPermission)
            }
            let prefix = projectID.hssDirectoryName + "-r"
            for url in try fileManager.contentsOfDirectory(at: archives, includingPropertiesForKeys: [.isSymbolicLinkKey, .isRegularFileKey]) {
                let name = url.lastPathComponent
                guard name.hasPrefix(prefix), name.hasSuffix(".hsscan") else { continue }
                let payload = name.dropFirst(prefix.count).dropLast(".hsscan".count)
                guard let dash = payload.firstIndex(of: "-"),
                      UInt64(payload[..<dash]) != nil,
                      UUID(uuidString: String(payload[payload.index(after: dash)...])) != nil else { continue }
                let values = try url.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey])
                guard values.isSymbolicLink != true, values.isRegularFile == true else {
                    throw CocoaError(.fileReadNoPermission)
                }
                targets.append(url)
            }
        }
        // Validate the complete target list before the first destructive step.
        for target in targets { try fileManager.removeItem(at: target) }
    }
}

private extension Bool {
    init?(hssLiteral: String) {
        switch hssLiteral {
        case "true": self = true
        case "false": self = false
        default: return nil
        }
    }
}

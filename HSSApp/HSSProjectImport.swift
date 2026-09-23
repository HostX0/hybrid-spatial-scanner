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

private extension Bool {
    init?(hssLiteral: String) {
        switch hssLiteral {
        case "true": self = true
        case "false": self = false
        default: return nil
        }
    }
}

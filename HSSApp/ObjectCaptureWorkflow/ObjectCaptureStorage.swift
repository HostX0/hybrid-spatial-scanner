import Foundation
import Darwin

struct ObjectCaptureSessionPaths: Equatable, Sendable {
  let id: UUID
  let createdAt: Date
  let sessionDirectory: URL
  let imagesDirectory: URL
  let captureCheckpointDirectory: URL
  let reconstructionCheckpointDirectory: URL
  let outputDirectory: URL
  let modelURL: URL
  let sessionRecordURL: URL
  let manifestURL: URL
}

private struct ObjectCaptureSessionRecord: Codable {
  let id: UUID
  let createdAt: Date
  let status: String
  let photoRetentionPolicy: ObjectCapturePhotoRetentionPolicy
  let metricAuthority: Bool
}

struct ObjectCaptureLibraryEntry: Identifiable, Sendable {
  enum State: Equatable, Sendable { case retainedPhotos, completed, unavailable, deleting }
  let id: UUID
  let createdAt: Date?
  let state: State
  let photoCount: Int
  let artifact: ObjectCaptureArtifact?
}

enum ObjectCaptureStorage {
  private static func rootURL(_ supplied: URL?, fileManager: FileManager) throws -> URL {
    if let supplied { return supplied }
    guard let support = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
      throw CocoaError(.fileNoSuchFile)
    }
    return support.appendingPathComponent("ObjectCaptures", isDirectory: true)
  }

  private static func paths(id: UUID, date: Date, root: URL) -> ObjectCaptureSessionPaths {
    let session = root.appendingPathComponent(id.uuidString, isDirectory: true)
    let output = session.appendingPathComponent("Output", isDirectory: true)
    return ObjectCaptureSessionPaths(
      id: id, createdAt: date, sessionDirectory: session,
      imagesDirectory: session.appendingPathComponent("Images", isDirectory: true),
      captureCheckpointDirectory: session.appendingPathComponent("CaptureCheckpoints", isDirectory: true),
      reconstructionCheckpointDirectory: session.appendingPathComponent("ReconstructionCheckpoints", isDirectory: true),
      outputDirectory: output, modelURL: output.appendingPathComponent("Object.usdz"),
      sessionRecordURL: session.appendingPathComponent("session.json"),
      manifestURL: session.appendingPathComponent("manifest.json")
    )
  }

  // Never accept paths from a decoded manifest. iOS may relocate the container,
  // and an untrusted record must not grant access outside this UUID-owned tree.
  private static func requireDirectory(_ url: URL) throws {
    let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
    guard values.isDirectory == true, values.isSymbolicLink != true else {
      throw CocoaError(.fileReadInvalidFileName)
    }
  }

  private static func existsWithoutFollowingLinks(_ url: URL) throws -> Bool {
    var metadata = stat()
    if lstat(url.path, &metadata) == 0 { return true }
    if errno == ENOENT { return false }
    throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
  }

  private static func boundedData(_ url: URL) throws -> Data {
    let values = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
    guard values.isRegularFile == true, values.isSymbolicLink != true,
          let size = values.fileSize, size <= 1_048_576 else { throw CocoaError(.fileReadCorruptFile) }
    return try Data(contentsOf: url)
  }

  private static func decoder() -> JSONDecoder {
    let decoder = JSONDecoder()
    decoder.dateDecodingStrategy = .iso8601
    return decoder
  }

  static func library(capturesRootDirectory: URL? = nil) throws -> [ObjectCaptureLibraryEntry] {
    let root = try rootURL(capturesRootDirectory, fileManager: .default)
    guard try existsWithoutFollowingLinks(root) else { return [] }
    try requireDirectory(root)
    let children = try FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
    guard children.count <= 10_000 else { throw CocoaError(.fileReadTooLarge) }
    let ids = Set(children.compactMap { directory -> UUID? in
      let original = directory.lastPathComponent
      let name = original.hasPrefix(".deleting-") ? String(original.dropFirst(10)) : original
      guard let id = UUID(uuidString: name), name == id.uuidString else { return nil }
      return id
    })
    return ids.map { id in
      return entry(id: id, root: root)
    }.sorted {
      if $0.createdAt != $1.createdAt { return ($0.createdAt ?? .distantPast) > ($1.createdAt ?? .distantPast) }
      return $0.id.uuidString < $1.id.uuidString
    }
  }

  private static func entry(id: UUID, root: URL) -> ObjectCaptureLibraryEntry {
    let provisional = paths(id: id, date: .distantPast, root: root)
    do {
      if try existsWithoutFollowingLinks(deletionIntent(id: id, root: root)) {
        return ObjectCaptureLibraryEntry(id: id, createdAt: nil, state: .deleting, photoCount: 0, artifact: nil)
      }
      try requireDirectory(provisional.sessionDirectory)
      let record = try decoder().decode(ObjectCaptureSessionRecord.self, from: boundedData(provisional.sessionRecordURL))
      guard record.id == id, !record.metricAuthority else { throw CocoaError(.fileReadCorruptFile) }
      let owned = paths(id: id, date: record.createdAt, root: root)
      let count = try imageCount(at: owned.imagesDirectory)
      var artifact: ObjectCaptureArtifact?
      if FileManager.default.fileExists(atPath: owned.manifestURL.path) {
        let stored = try decoder().decode(ObjectCaptureArtifact.self, from: boundedData(owned.manifestURL))
        guard stored.id == id, !stored.provenance.metricAuthority, stored.capturedImageCount >= 0 else {
          throw CocoaError(.fileReadCorruptFile)
        }
        try requireDirectory(owned.outputDirectory)
        let model = try owned.modelURL.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
        if model.isRegularFile == true, model.isSymbolicLink != true, (model.fileSize ?? 0) > 0 {
          artifact = ObjectCaptureArtifact(
            id: id, sessionDirectory: owned.sessionDirectory, modelURL: owned.modelURL,
            imagesDirectory: owned.imagesDirectory, createdAt: record.createdAt,
            completedAt: stored.completedAt, capturedImageCount: stored.capturedImageCount,
            photoRetentionPolicy: stored.photoRetentionPolicy, provenance: stored.provenance,
            processingWarnings: stored.processingWarnings
          )
        }
      }
      return ObjectCaptureLibraryEntry(id: id, createdAt: record.createdAt,
        state: artifact == nil ? .retainedPhotos : .completed, photoCount: count, artifact: artifact)
    } catch {
      return ObjectCaptureLibraryEntry(id: id, createdAt: nil, state: .unavailable, photoCount: 0, artifact: nil)
    }
  }

  static func imageCount(at directory: URL) throws -> Int {
    try requireDirectory(directory)
    let images = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey])
    guard images.count <= 10_000 else { throw CocoaError(.fileReadTooLarge) }
    return try images.reduce(0) { count, image in
      let values = try image.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
      guard values.isSymbolicLink != true else { throw CocoaError(.fileReadInvalidFileName) }
      return count + (values.isRegularFile == true && ["heic", "heif", "jpg", "jpeg", "png"].contains(image.pathExtension.lowercased()) ? 1 : 0)
    }
  }

  static func reconstructionPaths(id: UUID, capturesRootDirectory: URL? = nil) throws -> ObjectCaptureSessionPaths {
    let root = try rootURL(capturesRootDirectory, fileManager: .default)
    try requireDirectory(root)
    let record = entry(id: id, root: root)
    guard case .retainedPhotos = record.state, record.photoCount > 0, let date = record.createdAt else {
      throw CocoaError(.fileReadCorruptFile)
    }
    let owned = paths(id: id, date: date, root: root)
    // RealityKit reads/writes these folders; reject redirected checkpoints/output.
    for directory in [owned.captureCheckpointDirectory, owned.reconstructionCheckpointDirectory, owned.outputDirectory] {
      try requireDirectory(directory)
    }
    var enumerationError: Error?
    guard let enumerator = FileManager.default.enumerator(at: owned.sessionDirectory,
      includingPropertiesForKeys: [.isSymbolicLinkKey], options: [], errorHandler: { _, error in
        enumerationError = error
        return false
      }) else {
      throw CocoaError(.fileReadUnknown)
    }
    var fileCount = 0
    for case let file as URL in enumerator {
      fileCount += 1
      guard fileCount <= 20_000,
        try file.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
        throw CocoaError(.fileReadInvalidFileName)
      }
    }
    if let enumerationError { throw enumerationError }
    return owned
  }

  private static func deletionIntent(id: UUID, root: URL) -> URL {
    root.appendingPathComponent(".deleting-" + id.uuidString)
  }

  private static func synchronize(_ url: URL) throws {
    let descriptor = open(url.path, O_RDONLY | O_NOFOLLOW)
    guard descriptor >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
    defer { close(descriptor) }
    guard fsync(descriptor) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
  }

  static func deleteSession(id: UUID, capturesRootDirectory: URL? = nil) throws {
    let root = try rootURL(capturesRootDirectory, fileManager: .default)
    try requireDirectory(root)
    let session = paths(id: id, date: .distantPast, root: root).sessionDirectory
    let intent = deletionIntent(id: id, root: root)
    let exists = try existsWithoutFollowingLinks(session)
    if exists { try requireDirectory(session) }
    // Intent survives an interrupted recursive delete. The library cannot resume
    // a partially purged session. Nested links are removed, never traversed.
    try Data("{\"deletionStarted\":true}".utf8).write(
      to: intent, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    try synchronize(intent)
    try synchronize(root)
    if exists { try FileManager.default.removeItem(at: session) }
    try synchronize(root)
    try FileManager.default.removeItem(at: intent)
    try synchronize(root)
  }

  static func createSession(
    fileManager: FileManager = .default,
    now: Date = Date(),
    id: UUID = UUID(),
    capturesRootDirectory: URL? = nil
  ) throws -> ObjectCaptureSessionPaths {
    let root = try rootURL(capturesRootDirectory, fileManager: fileManager)
    if try existsWithoutFollowingLinks(root) { try requireDirectory(root) }
    let session = root.appendingPathComponent(id.uuidString, isDirectory: true)
    guard try !existsWithoutFollowingLinks(session) else { throw CocoaError(.fileWriteFileExists) }
    let images = session.appendingPathComponent("Images", isDirectory: true)
    let captureCheckpoints = session.appendingPathComponent(
      "CaptureCheckpoints",
      isDirectory: true
    )
    let reconstructionCheckpoints = session.appendingPathComponent(
      "ReconstructionCheckpoints",
      isDirectory: true
    )
    let output = session.appendingPathComponent("Output", isDirectory: true)

    for directory in [
      root,
      session,
      images,
      captureCheckpoints,
      reconstructionCheckpoints,
      output,
    ] {
      try fileManager.createDirectory(
        at: directory,
        withIntermediateDirectories: true,
        attributes: [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication]
      )
    }

    let paths = ObjectCaptureSessionPaths(
      id: id,
      createdAt: now,
      sessionDirectory: session,
      imagesDirectory: images,
      captureCheckpointDirectory: captureCheckpoints,
      reconstructionCheckpointDirectory: reconstructionCheckpoints,
      outputDirectory: output,
      modelURL: output.appendingPathComponent("Object.usdz", isDirectory: false),
      sessionRecordURL: session.appendingPathComponent("session.json", isDirectory: false),
      manifestURL: session.appendingPathComponent("manifest.json", isDirectory: false)
    )
    let record = ObjectCaptureSessionRecord(
      id: id,
      createdAt: now,
      status: "capture-created",
      photoRetentionPolicy: .retainedUntilExplicitSessionDeletion,
      metricAuthority: false
    )
    try persist(record, at: paths.sessionRecordURL)
    return paths
  }

  static func persistManifest(
    for artifact: ObjectCaptureArtifact,
    at url: URL
  ) throws {
    let encoder = JSONEncoder()
    try persist(artifact, at: url, encoder: encoder)
  }

  private static func persist<T: Encodable>(
    _ value: T,
    at url: URL,
    encoder: JSONEncoder = JSONEncoder()
  ) throws {
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
    encoder.dateEncodingStrategy = .iso8601
    try encoder.encode(value).write(to: url, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
  }
}

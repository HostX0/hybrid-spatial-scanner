import Foundation

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

enum ObjectCaptureStorage {
  static func createSession(
    fileManager: FileManager = .default,
    now: Date = Date(),
    id: UUID = UUID(),
    capturesRootDirectory: URL? = nil
  ) throws -> ObjectCaptureSessionPaths {
    let root: URL
    if let capturesRootDirectory {
      root = capturesRootDirectory
    } else {
      guard
        let applicationSupport = fileManager.urls(
          for: .applicationSupportDirectory,
          in: .userDomainMask
        ).first
      else {
        throw CocoaError(.fileNoSuchFile)
      }
      root =
        applicationSupport
        .appendingPathComponent("ObjectCaptures", isDirectory: true)
    }
    let session = root.appendingPathComponent(id.uuidString, isDirectory: true)
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
        withIntermediateDirectories: true
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
    try encoder.encode(value).write(to: url, options: .atomic)
  }
}

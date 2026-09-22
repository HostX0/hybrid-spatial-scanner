import Foundation

/// A local appearance artifact created by RealityKit Object Capture.
///
/// This value intentionally has no conversion to the room engine's metric scene. A generated
/// USDZ may be useful for visual detail, but it is not accepted measured room geometry.
public struct ObjectCaptureArtifact: Codable, Equatable, Sendable {
  public let id: UUID
  public let sessionDirectory: URL
  public let modelURL: URL
  public let imagesDirectory: URL
  public let createdAt: Date
  public let completedAt: Date
  public let capturedImageCount: Int
  public let photoRetentionPolicy: ObjectCapturePhotoRetentionPolicy
  public let provenance: ObjectCaptureProvenance
  public let processingWarnings: [String]
}

public enum ObjectCapturePhotoRetentionPolicy: String, Codable, Equatable, Sendable {
  /// Source photographs stay inside the session directory until the user explicitly deletes
  /// that capture. Successful reconstruction never silently removes the source evidence.
  case retainedUntilExplicitSessionDeletion

  public var userFacingDescription: String {
    "Captured photos are kept with this object until you explicitly delete the session."
  }
}

public struct ObjectCaptureProvenance: Codable, Equatable, Sendable {
  public let pipeline: String
  public let pipelineVersion: Int
  public let reconstructionDetail: String
  public let operatingSystem: String
  public let captureSupportVerified: Bool
  public let reconstructionSupportVerified: Bool
  public let metricAuthority: Bool
  public let limitations: [String]

  static func realityKitObjectCapture() -> Self {
    Self(
      pipeline: "Apple RealityKit Object Capture",
      pipelineVersion: 1,
      reconstructionDetail: "reduced",
      operatingSystem: ProcessInfo.processInfo.operatingSystemVersionString,
      captureSupportVerified: true,
      reconstructionSupportVerified: true,
      metricAuthority: false,
      limitations: [
        "The USDZ is a photogrammetry appearance artifact, not canonical measured room geometry.",
        "Reflective, transparent, very thin, deformable, or hidden surfaces may be incomplete or incorrect.",
        "No metric precision has been established without separate physical validation.",
      ]
    )
  }
}

import Foundation
import RealityKit
import SwiftUI

@MainActor
public final class ObjectCaptureWorkflowModel: ObservableObject {
  public enum Phase {
    case unavailable(reason: String)
    case ready
    case preparing
    case selectingObject
    case detecting
    case startingCapture
    case capturing
    case waitingForFlip
    case finishingCapture
    case processing(progress: Double?, stage: String?)
    case completed(ObjectCaptureArtifact)
    case cancelled
    case failed(message: String)
  }

  @Published public private(set) var phase: Phase
  @Published public private(set) var feedbackMessages: [String] = []
  @Published public private(set) var capturedImageCount = 0
  @Published public private(set) var userCompletedScanPass = false
  @Published public private(set) var warnings: [String] = []
  @Published public private(set) var captureSession: ObjectCaptureSession?

  private let onComplete: (ObjectCaptureArtifact) -> Void
  private let resumingSessionID: UUID?
  private var paths: ObjectCaptureSessionPaths?
  private var photogrammetrySession: PhotogrammetrySession?
  private var captureStateTask: Task<Void, Never>?
  private var feedbackTask: Task<Void, Never>?
  private var imageCountTask: Task<Void, Never>?
  private var scanPassTask: Task<Void, Never>?
  private var reconstructionTask: Task<Void, Never>?
  private var didBeginReconstruction = false
  private var didDeliverCompletion = false

  public init(resumingSessionID: UUID? = nil, onComplete: @escaping (ObjectCaptureArtifact) -> Void) {
    self.onComplete = onComplete
    self.resumingSessionID = resumingSessionID

    guard resumingSessionID != nil || ObjectCaptureSession.isSupported else {
      phase = .unavailable(
        reason:
          "Guided Object Capture is unavailable on this device. Room scanning remains available."
      )
      return
    }
    guard PhotogrammetrySession.isSupported else {
      phase = .unavailable(
        reason:
          "On-device object reconstruction is unavailable on this device. Room scanning remains available."
      )
      return
    }
    phase = .ready
  }

  public func start() {
    guard case .ready = phase else { return }
    if let resumingSessionID {
      do {
        let retained = try ObjectCaptureStorage.reconstructionPaths(id: resumingSessionID)
        self.paths = retained
        capturedImageCount = try ObjectCaptureStorage.imageCount(at: retained.imagesDirectory)
        beginReconstruction()
      } catch {
        fail("Retained photos could not be opened safely: \(error.localizedDescription)")
      }
      return
    }
    guard ObjectCaptureSession.isSupported, PhotogrammetrySession.isSupported else {
      phase = .unavailable(
        reason: "Object Capture is unavailable on this device. Room scanning remains available."
      )
      return
    }

    do {
      let paths = try ObjectCaptureStorage.createSession()
      let session = ObjectCaptureSession()
      var configuration = ObjectCaptureSession.Configuration()
      configuration.checkpointDirectory = paths.captureCheckpointDirectory
      configuration.isOverCaptureEnabled = true

      self.paths = paths
      captureSession = session
      phase = .preparing
      observe(session)
      session.start(
        imagesDirectory: paths.imagesDirectory,
        configuration: configuration
      )
    } catch {
      fail("The local capture session could not be created: \(error.localizedDescription)")
    }
  }

  public func startCapturing() {
    guard case .detecting = phase, let captureSession else { return }
    phase = .startingCapture
    captureSession.startCapturing()
  }

  public func detectObject() {
    guard case .selectingObject = phase, let captureSession else { return }
    if captureSession.startDetecting() {
      phase = .detecting
    } else {
      feedbackMessages = [
        "Point the camera at the object on a visible horizontal surface, then try again."
      ]
    }
  }

  public func beginNewScanPass() {
    guard case .capturing = phase, userCompletedScanPass, let captureSession else { return }
    userCompletedScanPass = false
    captureSession.beginNewScanPass()
  }

  public func prepareToFlip() {
    guard case .capturing = phase, userCompletedScanPass, let captureSession else { return }
    captureSession.pause()
    phase = .waitingForFlip
  }

  public func continueAfterFlip() {
    guard case .waitingForFlip = phase, let captureSession else { return }
    phase = .preparing
    captureSession.beginNewScanPassAfterFlip()
  }

  public func finishCapture() {
    let mayFinish: Bool
    switch phase {
    case .capturing, .waitingForFlip:
      mayFinish = true
    default:
      mayFinish = false
    }
    guard mayFinish, let captureSession else { return }
    phase = .finishingCapture
    captureSession.finish()
  }

  public func cancel() {
    switch phase {
    case .completed, .cancelled, .unavailable:
      return
    default:
      break
    }

    captureStateTask?.cancel()
    feedbackTask?.cancel()
    imageCountTask?.cancel()
    scanPassTask?.cancel()
    reconstructionTask?.cancel()
    captureSession?.cancel()
    photogrammetrySession?.cancel()
    captureSession = nil
    photogrammetrySession = nil
    phase = .cancelled
  }

  public func restart() {
    switch phase {
    case .failed, .cancelled:
      break
    default:
      return
    }
    resetTransientState()
    if (resumingSessionID != nil || ObjectCaptureSession.isSupported), PhotogrammetrySession.isSupported {
      phase = .ready
    } else {
      phase = .unavailable(
        reason: "Object Capture is unavailable on this device. Room scanning remains available."
      )
    }
  }

  private func observe(_ session: ObjectCaptureSession) {
    captureStateTask = Task { [weak self, weak session] in
      guard let session else { return }
      for await state in session.stateUpdates {
        guard !Task.isCancelled else { return }
        self?.handleCaptureState(state, session: session)
      }
    }

    feedbackTask = Task { [weak self, weak session] in
      guard let session else { return }
      for await feedback in session.feedbackUpdates {
        guard !Task.isCancelled else { return }
        self?.feedbackMessages = Self.messages(for: feedback)
      }
    }

    imageCountTask = Task { [weak self, weak session] in
      guard let session else { return }
      for await count in session.numberOfShotsTakenUpdates {
        guard !Task.isCancelled else { return }
        self?.capturedImageCount = count
      }
    }

    scanPassTask = Task { [weak self, weak session] in
      guard let session else { return }
      for await completed in session.userCompletedScanPassUpdates {
        guard !Task.isCancelled else { return }
        self?.userCompletedScanPass = completed
      }
    }
  }

  private func handleCaptureState(
    _ state: ObjectCaptureSession.CaptureState,
    session: ObjectCaptureSession
  ) {
    switch state {
    case .initializing:
      phase = .preparing
    case .ready:
      phase = .selectingObject
      detectObject()
    case .detecting:
      if case .startingCapture = phase {
        break
      }
      phase = .detecting
    case .capturing:
      phase = .capturing
    case .finishing:
      phase = .finishingCapture
    case .completed:
      beginReconstruction()
    case .failed(let error):
      fail("Capture failed: \(error.localizedDescription)")
    @unknown default:
      fail(
        "RealityKit entered an unsupported capture state. Captured files remain in the local session directory."
      )
    }
  }

  private func beginReconstruction() {
    guard !didBeginReconstruction else { return }
    didBeginReconstruction = true
    captureStateTask?.cancel()
    feedbackTask?.cancel()
    imageCountTask?.cancel()
    scanPassTask?.cancel()
    captureSession = nil

    guard let paths else {
      fail("The capture completed without a valid local session directory.")
      return
    }
    guard PhotogrammetrySession.isSupported else {
      fail(
        "On-device reconstruction became unavailable. Captured photos remain in the session directory."
      )
      return
    }

    do {
      var configuration = PhotogrammetrySession.Configuration()
      configuration.checkpointDirectory = paths.reconstructionCheckpointDirectory
      configuration.sampleOrdering = .sequential
      configuration.featureSensitivity = .normal
      configuration.isObjectMaskingEnabled = true

      let session = try PhotogrammetrySession(
        input: paths.imagesDirectory,
        configuration: configuration
      )
      photogrammetrySession = session
      phase = .processing(progress: nil, stage: "Preparing images")

      let request = PhotogrammetrySession.Request.modelFile(
        url: paths.modelURL,
        detail: .reduced
      )
      reconstructionTask = Task { [weak self, session] in
        do {
          try session.process(requests: [request])
          for try await output in session.outputs {
            guard !Task.isCancelled else {
              session.cancel()
              return
            }
            self?.handlePhotogrammetryOutput(output, request: request)
          }
        } catch is CancellationError {
          session.cancel()
        } catch {
          self?.fail("Object reconstruction failed: \(error.localizedDescription)")
        }
      }
    } catch {
      fail("Object reconstruction could not start: \(error.localizedDescription)")
    }
  }

  private func handlePhotogrammetryOutput(
    _ output: PhotogrammetrySession.Output,
    request: PhotogrammetrySession.Request
  ) {
    switch output {
    case .requestProgress(let progressRequest, let fractionComplete)
    where progressRequest == request:
      phase = .processing(
        progress: min(max(fractionComplete, 0), 1),
        stage: processingStage(from: phase)
      )
    case .requestProgressInfo(let progressRequest, let info)
    where progressRequest == request:
      phase = .processing(
        progress: processingProgress(from: phase),
        stage: Self.label(for: info.processingStage)
      )
    case .requestError(let failedRequest, let error) where failedRequest == request:
      fail("Object reconstruction failed: \(error.localizedDescription)")
    case .requestComplete(let completedRequest, let result) where completedRequest == request:
      guard case .modelFile(let modelURL) = result else {
        fail("RealityKit completed with an unexpected reconstruction result.")
        return
      }
      complete(with: modelURL)
    case .invalidSample(_, let reason):
      appendWarning("One captured image was invalid: \(reason)")
    case .skippedSample:
      appendWarning("RealityKit skipped one captured image during reconstruction.")
    case .automaticDownsampling:
      appendWarning("RealityKit downsampled input images to fit device resources.")
    case .stitchingIncomplete:
      appendWarning(
        "Some views could not be stitched. Hidden or difficult surfaces may be incomplete.")
    case .processingCancelled:
      switch phase {
      case .cancelled, .completed, .failed:
        break
      default:
        phase = .cancelled
      }
    case .processingComplete:
      if !didDeliverCompletion {
        fail("RealityKit finished processing without producing the requested USDZ file.")
      }
    case .inputComplete, .requestProgress, .requestProgressInfo,
      .requestError, .requestComplete:
      break
    @unknown default:
      appendWarning("RealityKit reported an unrecognized reconstruction event.")
    }
  }

  private func complete(with modelURL: URL) {
    guard !didDeliverCompletion, let paths else { return }
    guard FileManager.default.fileExists(atPath: modelURL.path) else {
      fail("RealityKit reported completion, but the USDZ file was not found.")
      return
    }

    let artifact = ObjectCaptureArtifact(
      id: paths.id,
      sessionDirectory: paths.sessionDirectory,
      modelURL: modelURL,
      imagesDirectory: paths.imagesDirectory,
      createdAt: paths.createdAt,
      completedAt: Date(),
      capturedImageCount: capturedImageCount,
      photoRetentionPolicy: .retainedUntilExplicitSessionDeletion,
      provenance: .realityKitObjectCapture(),
      processingWarnings: warnings
    )

    do {
      try ObjectCaptureStorage.persistManifest(for: artifact, at: paths.manifestURL)
    } catch {
      fail(
        "The USDZ was created, but its provenance manifest could not be saved: \(error.localizedDescription)"
      )
      return
    }

    didDeliverCompletion = true
    photogrammetrySession = nil
    phase = .completed(artifact)
    onComplete(artifact)
  }

  private func appendWarning(_ warning: String) {
    if !warnings.contains(warning) {
      warnings.append(warning)
    }
  }

  private func fail(_ message: String) {
    captureStateTask?.cancel()
    feedbackTask?.cancel()
    imageCountTask?.cancel()
    scanPassTask?.cancel()
    reconstructionTask?.cancel()
    captureSession?.cancel()
    photogrammetrySession?.cancel()
    captureSession = nil
    photogrammetrySession = nil
    phase = .failed(message: message)
  }

  private func resetTransientState() {
    captureStateTask?.cancel()
    feedbackTask?.cancel()
    imageCountTask?.cancel()
    scanPassTask?.cancel()
    reconstructionTask?.cancel()
    captureSession = nil
    photogrammetrySession = nil
    paths = nil
    feedbackMessages = []
    capturedImageCount = 0
    userCompletedScanPass = false
    warnings = []
    didBeginReconstruction = false
    didDeliverCompletion = false
  }

  private func processingProgress(from phase: Phase) -> Double? {
    guard case .processing(let progress, _) = phase else { return nil }
    return progress
  }

  private func processingStage(from phase: Phase) -> String? {
    guard case .processing(_, let stage) = phase else { return nil }
    return stage
  }

  private static func label(
    for stage: PhotogrammetrySession.Output.ProcessingStage?
  ) -> String? {
    guard let stage else { return nil }
    switch stage {
    case .preProcessing: return "Preparing images"
    case .imageAlignment: return "Aligning images"
    case .pointCloudGeneration: return "Building point cloud"
    case .meshGeneration: return "Building mesh"
    case .textureMapping: return "Mapping textures"
    case .optimization: return "Optimizing model"
    @unknown default: return "Processing model"
    }
  }

  private static func messages(
    for feedback: Set<ObjectCaptureSession.Feedback>
  ) -> [String] {
    feedback.map { item in
      switch item {
      case .objectTooClose:
        "Move a little farther from the object."
      case .objectTooFar:
        "Move closer so the object fills more of the frame."
      case .movingTooFast:
        "Move slowly and keep the camera steady."
      case .environmentLowLight:
        "Add soft, even light if possible."
      case .environmentTooDark:
        "The scene is too dark. Add diffuse light before continuing."
      case .outOfFieldOfView:
        "Keep the entire object in the frame."
      case .objectNotFlippable:
        "Use passes at different heights instead of flipping this object."
      case .overCapturing:
        "This view already has enough photos; move to a new angle."
      default:
        "Keep the object centered and clearly separated from its background."
      }
    }.sorted()
  }
}

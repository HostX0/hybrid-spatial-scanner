import RealityKit
import SwiftUI

/// A self-contained, offline small-object workflow.
///
/// Present this view only after pausing or ending room capture. The view owns RealityKit's object
/// capture and reconstruction sessions; it never creates a room `ARSession`.
@MainActor
public struct ObjectCaptureWorkflowView: View {
  @StateObject private var model: ObjectCaptureWorkflowModel

  public init(onComplete: @escaping (ObjectCaptureArtifact) -> Void) {
    _model = StateObject(
      wrappedValue: ObjectCaptureWorkflowModel(onComplete: onComplete)
    )
  }

  public var body: some View {
    Group {
      switch model.phase {
      case .unavailable(let reason):
        UnavailableObjectCaptureView(reason: reason)
      case .ready:
        ObjectCaptureIntroductionView(start: model.start)
      case .preparing, .selectingObject, .detecting, .startingCapture, .capturing,
        .waitingForFlip, .finishingCapture:
        captureView
      case .processing(let progress, let stage):
        ObjectReconstructionProgressView(
          progress: progress,
          stage: stage,
          warnings: model.warnings,
          cancel: model.cancel
        )
      case .completed(let artifact):
        ObjectCaptureCompletedView(artifact: artifact, warnings: model.warnings)
      case .cancelled:
        ObjectCaptureMessageView(
          title: "Object capture cancelled",
          message:
            "Room scanning is still available. Any photos already saved remain in the local session directory.",
          actionTitle: "Start a new session",
          action: model.restart
        )
      case .failed(let message):
        ObjectCaptureMessageView(
          title: "Object capture could not finish",
          message: message,
          actionTitle: "Start a new session",
          action: model.restart
        )
      }
    }
    .navigationTitle("Detailed Object")
    .navigationBarTitleDisplayMode(.inline)
  }

  @ViewBuilder
  private var captureView: some View {
    if let session = model.captureSession {
      ZStack(alignment: .bottom) {
        ObjectCaptureView(session: session)
          .ignoresSafeArea(edges: .bottom)

        ObjectCaptureControls(model: model)
          .padding()
          .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 20))
          .padding()
      }
    } else {
      ProgressView("Preparing local capture…")
    }
  }
}

private struct ObjectCaptureIntroductionView: View {
  let start: () -> Void

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 20) {
        Label("On-device object model", systemImage: "cube.transparent")
          .font(.title2.bold())

        Text(
          "Create a detailed USDZ of a statuette or other small object. Capture and reconstruction stay on this device."
        )

        ObjectCaptureGuidance()

        Label(
          "This model is a visual detail artifact. It does not replace the measured room plan or establish metric precision.",
          systemImage: "ruler"
        )
        .font(.callout)
        .foregroundStyle(.secondary)

        Button("Start Object Capture", action: start)
          .buttonStyle(.borderedProminent)
          .controlSize(.large)
          .frame(maxWidth: .infinity)
      }
      .padding()
    }
  }
}

private struct ObjectCaptureControls: View {
  @ObservedObject var model: ObjectCaptureWorkflowModel

  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      status

      if !model.feedbackMessages.isEmpty {
        ForEach(model.feedbackMessages, id: \.self) { message in
          Label(message, systemImage: "viewfinder")
            .font(.callout)
        }
      }

      if case .capturing = model.phase {
        Text("\(model.capturedImageCount) photos saved locally")
          .font(.caption)
          .foregroundStyle(.secondary)
      }

      controls
    }
    .frame(maxWidth: .infinity, alignment: .leading)
  }

  @ViewBuilder
  private var status: some View {
    switch model.phase {
    case .preparing:
      Label("Preparing camera", systemImage: "camera")
    case .selectingObject:
      Label("Place the object on a visible surface", systemImage: "square.dashed")
    case .detecting:
      Label("Center the object inside the guide", systemImage: "scope")
    case .startingCapture:
      Label("Starting guided capture", systemImage: "camera.viewfinder")
    case .capturing:
      Label("Move slowly around every visible side", systemImage: "rotate.3d")
    case .waitingForFlip:
      Label("Set the object on a different side", systemImage: "arrow.triangle.2.circlepath")
    case .finishingCapture:
      Label("Saving captured photos", systemImage: "externaldrive")
    default:
      EmptyView()
    }
  }

  @ViewBuilder
  private var controls: some View {
    switch model.phase {
    case .selectingObject:
      VStack(alignment: .leading, spacing: 10) {
        Text("Keep the supporting surface and the whole object in view.")
          .font(.callout)
        HStack {
          Button("Cancel", role: .cancel, action: model.cancel)
          Spacer()
          Button("Find Object", action: model.detectObject)
            .buttonStyle(.borderedProminent)
        }
      }
    case .detecting:
      HStack {
        Button("Cancel", role: .cancel, action: model.cancel)
        Spacer()
        Button("Begin Capturing", action: model.startCapturing)
          .buttonStyle(.borderedProminent)
      }
    case .capturing:
      VStack(spacing: 8) {
        if model.userCompletedScanPass {
          HStack {
            Button("New Height", action: model.beginNewScanPass)
            Button("Prepare to Flip", action: model.prepareToFlip)
          }
          .buttonStyle(.bordered)
        }
        HStack {
          Button("Cancel", role: .cancel, action: model.cancel)
          Spacer()
          Button("Finish Capture", action: model.finishCapture)
            .buttonStyle(.borderedProminent)
        }
      }
    case .waitingForFlip:
      VStack(alignment: .leading, spacing: 10) {
        Text(
          "Flip a rigid object to reveal its underside. Keep some previously seen detail visible so RealityKit can stitch the passes."
        )
        .font(.callout)
        HStack {
          Button("Finish Instead", action: model.finishCapture)
          Spacer()
          Button("Object Is Flipped", action: model.continueAfterFlip)
            .buttonStyle(.borderedProminent)
        }
      }
    case .preparing, .startingCapture, .finishingCapture:
      HStack {
        ProgressView()
        Spacer()
        Button("Cancel", role: .cancel, action: model.cancel)
      }
    default:
      EmptyView()
    }
  }
}

private struct ObjectCaptureGuidance: View {
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Label("Use soft, even light and a textured background.", systemImage: "sun.max")
      Label(
        "Move slowly and capture the object from several heights.",
        systemImage: "figure.walk.motion")
      Label(
        "Show backs and undersides directly; hidden detail remains missing.",
        systemImage: "eye.slash")
      Label(
        "Reflective, transparent, and very thin surfaces may reconstruct poorly.",
        systemImage: "exclamationmark.triangle")
    }
    .font(.callout)
  }
}

private struct ObjectReconstructionProgressView: View {
  let progress: Double?
  let stage: String?
  let warnings: [String]
  let cancel: () -> Void

  var body: some View {
    VStack(spacing: 18) {
      Text(stage ?? "Processing object")
        .font(.headline)

      if let progress {
        ProgressView(value: progress)
        Text("\(Int(progress * 100))% processing complete")
          .font(.caption)
          .foregroundStyle(.secondary)
      } else {
        ProgressView()
      }

      Text("RealityKit is building the USDZ locally. You can cancel without uploading any photos.")
        .multilineTextAlignment(.center)
        .foregroundStyle(.secondary)

      ForEach(warnings, id: \.self) { warning in
        Label(warning, systemImage: "exclamationmark.triangle")
          .font(.callout)
      }

      Button("Cancel Processing", role: .cancel, action: cancel)
    }
    .padding()
  }
}

private struct ObjectCaptureCompletedView: View {
  let artifact: ObjectCaptureArtifact
  let warnings: [String]

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 16) {
        Label("USDZ saved on this device", systemImage: "checkmark.circle.fill")
          .font(.title2.bold())
          .foregroundStyle(.green)

        Text(artifact.photoRetentionPolicy.userFacingDescription)

        Label(
          "The result remains separate from canonical measured room geometry.",
          systemImage: "ruler"
        )
        .foregroundStyle(.secondary)

        ForEach(warnings, id: \.self) { warning in
          Label(warning, systemImage: "exclamationmark.triangle")
            .font(.callout)
        }

        LabeledContent("Captured photos", value: "\(artifact.capturedImageCount)")
        LabeledContent("Model", value: artifact.modelURL.lastPathComponent)
      }
      .padding()
    }
  }
}

private struct UnavailableObjectCaptureView: View {
  let reason: String

  var body: some View {
    ObjectCaptureMessageView(
      title: "Detailed object capture unavailable",
      message: reason,
      actionTitle: nil,
      action: nil
    )
  }
}

private struct ObjectCaptureMessageView: View {
  let title: String
  let message: String
  let actionTitle: String?
  let action: (() -> Void)?

  var body: some View {
    ContentUnavailableView {
      Label(title, systemImage: "cube.transparent")
    } description: {
      Text(message)
    } actions: {
      if let actionTitle, let action {
        Button(actionTitle, action: action)
          .buttonStyle(.borderedProminent)
      }
    }
  }
}

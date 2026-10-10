import RealityKit
import SwiftUI
import QuickLook

/// A self-contained, offline small-object workflow.
///
/// Present this view only after pausing or ending room capture. The view owns RealityKit's object
/// capture and reconstruction sessions; it never creates a room `ARSession`.
@MainActor
public struct ObjectCaptureWorkflowView: View {
  @StateObject private var model: ObjectCaptureWorkflowModel
  private let isResuming: Bool

  public init(resumingSessionID: UUID? = nil, onComplete: @escaping (ObjectCaptureArtifact) -> Void) {
    isResuming = resumingSessionID != nil
    _model = StateObject(
      wrappedValue: ObjectCaptureWorkflowModel(resumingSessionID: resumingSessionID, onComplete: onComplete)
    )
  }

  public var body: some View {
    Group {
      switch model.phase {
      case .unavailable(let reason):
        UnavailableObjectCaptureView(reason: reason)
      case .ready:
        if isResuming {
          ObjectCaptureMessageView(
            title: String(localized: "Rebuild retained object"),
            message: String(localized: "Reconstruction uses the photos already saved in this session. Missing views remain missing; no new camera capture is started."),
            actionTitle: String(localized: "Rebuild from photos"), action: model.start)
        } else {
          ObjectCaptureIntroductionView(start: model.start)
        }
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
    .onDisappear { model.cancel() }
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
          Label(LocalizedStringKey(message), systemImage: "viewfinder")
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
      Text(LocalizedStringKey(stage ?? "Processing object"))
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
  @State private var previewURL: URL?

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 16) {
        Label("USDZ saved on this device", systemImage: "checkmark.circle.fill")
          .font(.title2.bold())
          .foregroundStyle(.green)

        Text(LocalizedStringKey(artifact.photoRetentionPolicy.userFacingDescription))

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
        Button("Preview object", systemImage: "cube.transparent") { previewURL = artifact.modelURL }
          .buttonStyle(.borderedProminent)
        ShareLink(item: artifact.modelURL) { Label("Share USDZ model", systemImage: "square.and.arrow.up") }
        Text("Only the visual model is shared here, not the source photos. Shared copies are retained by their destination.")
          .font(.caption).foregroundStyle(.secondary)
      }
      .padding()
    }
    .quickLookPreview($previewURL)
  }
}

/// An independent library. It never inserts photogrammetry into metric room geometry.
@MainActor
struct ObjectCaptureLibraryView: View {
  @Environment(\.dismiss) private var dismiss
  @State private var entries: [ObjectCaptureLibraryEntry] = []
  @State private var isWorking = false
  @State private var errorMessage: String?
  @State private var deleting: ObjectCaptureLibraryEntry?
  @State private var workflow: Workflow?
  @State private var previewURL: URL?

  private struct Workflow: Identifiable {
    let id = UUID()
    let sessionID: UUID?
  }

  var body: some View {
    NavigationStack {
      ScrollView {
        VStack(alignment: .leading, spacing: 20) {
          Label("Visual detail, separate from room measurements", systemImage: "ruler")
            .font(.subheadline).foregroundStyle(.secondary)
          Button { workflow = Workflow(sessionID: nil) } label: {
            Label("New object capture", systemImage: "camera.viewfinder")
              .foregroundStyle(ScannerProductTheme.canvas)
              .frame(maxWidth: .infinity, minHeight: 48)
          }.buttonStyle(.borderedProminent).accessibilityIdentifier("object-library.new-capture")
          Text("Photos, checkpoints and models stay in each local session until you delete it. Deleting a room does not remove object captures.")
            .font(.callout).foregroundStyle(.secondary)
          if isWorking { ProgressView("Loading local sessions…") }
          if let errorMessage {
            Label(errorMessage, systemImage: "exclamationmark.triangle").foregroundStyle(.orange)
            Button("Try again") { Task { await reload() } }
          }
          if entries.isEmpty && !isWorking && errorMessage == nil {
            ContentUnavailableView("Your object library", systemImage: "cube.transparent",
              description: Text("Capture a small object or return here to rebuild retained photos."))
          }
          LazyVStack(spacing: 16) {
            ForEach(entries) { entry in
              VStack(alignment: .leading, spacing: 12) {
                HStack(alignment: .top) {
                  Label(stateTitle(entry), systemImage: entry.artifact == nil ? "photo.stack" : "cube.transparent.fill")
                    .font(.headline)
                  Spacer()
                  Button(role: .destructive) { deleting = entry } label: {
                    Image(systemName: "trash").frame(width: 44, height: 44)
                  }.accessibilityLabel("Delete object session")
                }
                if let date = entry.createdAt {
                  Text(date, format: .dateTime.day().month().year().hour().minute()).font(.subheadline)
                }
                Text(entry.id.uuidString).font(.caption.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                if entry.state == .retainedPhotos || entry.state == .completed {
                  Text("\(entry.photoCount) source photos retained").font(.callout)
                }
                if let artifact = entry.artifact {
                  ViewThatFits(in: .horizontal) {
                    HStack { objectActions(artifact) }
                    VStack(alignment: .leading) { objectActions(artifact) }
                  }
                } else if entry.state == .retainedPhotos && entry.photoCount > 0 {
                  Button("Rebuild from photos") { workflow = Workflow(sessionID: entry.id) }
                    .buttonStyle(.bordered)
                }
              }
              .padding(18).frame(maxWidth: .infinity, alignment: .leading)
              .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 20))
            }
          }
        }.padding(24).frame(maxWidth: 740).frame(maxWidth: .infinity)
      }
      .navigationTitle("Object library")
      .toolbar { ToolbarItem(placement: .confirmationAction) {
        Button { dismiss() } label: {
          Label("Done", systemImage: "xmark")
            .labelStyle(.iconOnly)
            .font(.body.weight(.semibold))
            .frame(minWidth: 44, minHeight: 44)
        }
          .accessibilityIdentifier("object-library.done")
      } }
      .disabled(isWorking)
      .task { await reload() }
      .refreshable { await reload() }
      .quickLookPreview($previewURL)
      .sheet(item: $workflow, onDismiss: { Task { await reload() } }) { selected in
        NavigationStack {
          ObjectCaptureWorkflowView(resumingSessionID: selected.sessionID) { _ in }
            .toolbar { ToolbarItem(placement: .cancellationAction) {
              Button { workflow = nil } label: {
                Label("Close", systemImage: "xmark")
                  .labelStyle(.iconOnly)
                  .font(.body.weight(.semibold))
                  .frame(minWidth: 44, minHeight: 44)
              }
                .accessibilityIdentifier("object-capture.close")
            } }
        }
      }
      .alert("Delete object and source photos?", isPresented: Binding(
        get: { deleting != nil }, set: { if !$0 { deleting = nil } }
      )) {
        Button("Delete permanently", role: .destructive) {
          if let deleting { Task { await remove(deleting.id) } }
          deleting = nil
        }
        Button("Cancel", role: .cancel) { deleting = nil }
      } message: {
        Text("This permanently removes this session's photos, checkpoints, provenance and model. Room projects, other object sessions, backups and shared copies are not removed. This cannot be undone.")
      }
    }
    .tint(ScannerProductTheme.accent)
    .interactiveDismissDisabled(isWorking)
  }

  @ViewBuilder private func objectActions(_ artifact: ObjectCaptureArtifact) -> some View {
    Button("Preview object", systemImage: "cube.transparent") { previewURL = artifact.modelURL }
      .buttonStyle(.bordered)
    ShareLink(item: artifact.modelURL) { Label("Share USDZ model", systemImage: "square.and.arrow.up") }
  }

  private func stateTitle(_ entry: ObjectCaptureLibraryEntry) -> String {
    switch entry.state {
    case .completed: String(localized: "Visual model saved")
    case .retainedPhotos: String(localized: "Retained source photos")
    case .unavailable: String(localized: "Session unavailable — files retained")
    case .deleting: String(localized: "Deletion started — retry to finish")
    }
  }

  private func reload() async {
    guard !isWorking else { return }
    isWorking = true
    defer { isWorking = false }
    do {
      entries = try await Task.detached(priority: .utility) { try ObjectCaptureStorage.library() }.value
      errorMessage = nil
    } catch { errorMessage = String(localized: "Object sessions could not be read. No files were deleted.") }
  }

  private func remove(_ id: UUID) async {
    guard !isWorking else { return }
    isWorking = true
    do {
      try await Task.detached(priority: .utility) { try ObjectCaptureStorage.deleteSession(id: id) }.value
      errorMessage = nil
    } catch { errorMessage = String(localized: "Deletion could not finish safely. Files remain local; retry after resolving storage access.") }
    // Do not clear a deletion failure while refreshing the retryable library row.
    do { entries = try await Task.detached(priority: .utility) { try ObjectCaptureStorage.library() }.value }
    catch { errorMessage = String(localized: "Object sessions could not be read. No files were deleted.") }
    isWorking = false
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
      Label(LocalizedStringKey(title), systemImage: "cube.transparent")
    } description: {
      Text(LocalizedStringKey(message))
    } actions: {
      if let actionTitle, let action {
        Button(LocalizedStringKey(actionTitle), action: action)
          .buttonStyle(.borderedProminent)
      }
    }
  }
}

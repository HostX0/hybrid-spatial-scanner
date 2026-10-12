import ARKit
import HSSContracts
import SceneKit
import SwiftUI
import UniformTypeIdentifiers
import UIKit
import simd

/// Binds the product shell to the native workspace without introducing another
/// geometry, project, or ARSession owner.
@MainActor
struct ScannerWorkspaceProductHost<Model: ScannerWorkspaceModeling>: View {
    @ObservedObject var workspace: Model
    @Environment(\.locale) private var locale
    @State private var isCreatingProject = false
    @State private var isImportingProject = false
    @State private var isObjectCapturePresented = false
    @State private var isFloorReferencePresented = false
    @State private var floorReferenceSnapshot: ScannerSceneSnapshot?
    @State private var currentCameraViewport: OverlayViewport?
    @State private var projectArchiveToShare: URL?
    @State private var derivedExportToShare: URL?
    @State private var isPreparingDerivedShare = false
    @State private var derivedShareIssue: String?
    @State private var isRecordingMissingEvidence = false
    @State private var missingEvidenceReason = ""
    // A single item binds first presentation to its immutable snapshot. Two
    // separate state writes could present an empty sheet on the first tap.
    private struct BoundaryReviewPresentation: Identifiable {
        let id: UUID
        var snapshot: ScannerSceneSnapshot
    }
    @State private var boundaryReviewPresentation: BoundaryReviewPresentation?
    @State private var showsSourceColor = true

    var body: some View {
        ScannerWorkspaceView(
            presentation: presentation,
            actions: actions,
            cameraContent: {
                ScannerCameraPreview(
                    session: workspace.captureSession,
                    onTargetTap: workspace.selectTarget,
                    onViewportChange: { currentCameraViewport = $0 }
                )
                .overlay(alignment: .top) {
                    if let advice = workspace.captureAdvice {
                        HStack(alignment: .top, spacing: 12) {
                            Image(systemName: advice.symbol).font(.title3)
                            VStack(alignment: .leading, spacing: 4) {
                                Text(advice.title).font(.headline)
                                Text(advice.detail).font(.subheadline)
                            }
                        }
                        .foregroundStyle(.primary)
                        .padding(16)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
                        .padding(16)
                        .accessibilityElement(children: .combine)
                        .accessibilityIdentifier("capture-coach-current-evidence")
                        .allowsHitTesting(false)
                    }
                }
            },
            sceneContent: {
                if let snapshot = workspace.latestSnapshot {
                    ScannerSnapshotMeshPreview(
                        snapshot: snapshot,
                        admittedAppearanceBinding: workspace.reviewAppearanceBinding,
                        appearanceTextures: workspace.reviewAppearanceTextures,
                        texturedMesh: showsSourceColor ? workspace.reviewTexturedMesh : nil
                    )
                    .overlay(alignment: .bottom) {
                        if snapshot.isFrozen {
                            VStack(alignment: .leading, spacing: 8) {
                                if workspace.reviewTexturedMesh?.faces.isEmpty == false {
                                    Picker("3D appearance", selection: $showsSourceColor) {
                                        Text("Source color").tag(true)
                                        Text("Measured mesh").tag(false)
                                    }
                                    .pickerStyle(.segmented)
                                    .accessibilityIdentifier("review.appearance-mode")
                                    Toggle("Include source images in 3D export", isOn: Binding(
                                        get: { workspace.includeSourceImagesInExport },
                                        set: { workspace.includeSourceImagesInExport = $0 }
                                    ))
                                    .font(.caption)
                                    .accessibilityIdentifier("review.include-source-images")
                                }
                                if let message = workspace.appearanceMessage {
                                    Text(message).font(.caption).foregroundStyle(.primary)
                                }
                            }
                            .padding(12)
                            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
                            .padding(12)
                        }
                    }
                }
            }
        )
        // The presented correction editor owns accessibility focus. Do not
        // expose the covered workspace's controls through the modal sheet;
        // they become available again when the editor is dismissed.
        .accessibilityHidden(boundaryReviewPresentation != nil)
        .sheet(isPresented: $isCreatingProject) {
            ScannerProjectNameSheet(
                name: Binding(
                    get: { workspace.selectedProjectName },
                    set: { workspace.selectedProjectName = $0 }
                ),
                create: {
                    workspace.createProject(named: workspace.selectedProjectName)
                    isCreatingProject = false
                },
                cancel: { isCreatingProject = false }
            )
            .presentationDetents([.medium])
        }
        .sheet(isPresented: $isImportingProject) {
            ScannerProjectImportPicker { url in
                isImportingProject = false
                guard let url else { return }
                workspace.importProjectArchive(at: url)
            }
            .ignoresSafeArea()
        }
        .sheet(isPresented: $isRecordingMissingEvidence, onDismiss: {
            missingEvidenceReason = ""
        }) {
            NavigationStack {
                Form {
                    Section {
                        Text("This records an unknown-evidence note for the frozen review. It does not draw, close, or otherwise modify captured geometry.")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    Section("What remains unobserved?") {
                        TextEditor(text: $missingEvidenceReason)
                            .frame(minHeight: 120)
                            .accessibilityLabel("Missing evidence reason")
                    }
                }
                .navigationTitle("Record missing evidence")
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Cancel") { isRecordingMissingEvidence = false }
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Record") {
                            workspace.recordMissingEvidence(reason: missingEvidenceReason)
                            isRecordingMissingEvidence = false
                        }
                        .disabled(missingEvidenceReason.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    }
                }
            }
        }
        .sheet(isPresented: $isObjectCapturePresented) {
            // This is deliberately a separate RealityKit-owned workflow. It is
            // offered only after room capture has stopped and never enters the
            // workspace's metric scene or project save path.
            ObjectCaptureLibraryView()
        }
        .sheet(item: $boundaryReviewPresentation) { review in
            let snapshot = review.snapshot
            ScannerBoundaryReviewSheet(snapshot: snapshot,
                edges: ScannerBoundaryEdge.decode(snapshot: snapshot, projectID: workspace.selectedProjectID ?? UUID()),
                isCurrent: workspace.latestSnapshot?.revision == snapshot.revision,
                canApply: workspace.canRecordMissingEvidence && workspace.latestSnapshot?.revision == snapshot.revision,
                issue: workspace.currentIssue?.message,
                history: workspace.commandHistory,
                apply: { workspace.applyBoundaryEdit($0, expectedRevision: snapshot.revision) })
        }
        .onChange(of: workspace.latestSnapshot?.revision) { _, _ in
            // Keep an accepted correction sheet connected to the new immutable
            // publication. The sheet's selection resets; it never retargets an
            // old edge ID to the nearest new geometry.
            if let review = boundaryReviewPresentation {
                boundaryReviewPresentation = workspace.latestSnapshot.map {
                    BoundaryReviewPresentation(id: review.id, snapshot: $0)
                }
            }
        }
        .sheet(isPresented: $isFloorReferencePresented, onDismiss: { floorReferenceSnapshot = nil }) {
            if let snapshot = floorReferenceSnapshot {
                ScannerFloorReferenceSheet(
                    snapshot: snapshot,
                    isCurrent: workspace.canConfirmPlanReference && workspace.latestSnapshot?.revision == snapshot.revision
                ) { transform, height in
                    guard workspace.canConfirmPlanReference, workspace.latestSnapshot?.revision == snapshot.revision else { return }
                    workspace.confirmPlanReference(projectFromFloor: transform, cutHeightMetres: height)
                    isFloorReferencePresented = false
                }
            }
        }
        .sheet(isPresented: Binding(
            get: { projectArchiveToShare != nil },
            set: { if !$0 { projectArchiveToShare = nil } }
        )) {
            if let projectArchiveToShare {
                ScannerProjectArchiveShareSheet(archiveURL: projectArchiveToShare)
            }
        }
        .onChange(of: workspace.projectArchiveExportState) { _, state in
            if case .exported(let archiveURL) = state {
                projectArchiveToShare = archiveURL
            }
        }
        .sheet(isPresented: Binding(
            get: { derivedExportToShare != nil },
            set: { if !$0 { clearDerivedShare() } }
        ), onDismiss: clearDerivedShare) {
            if let derivedExportToShare {
                ScannerProjectArchiveShareSheet(archiveURL: derivedExportToShare)
            }
        }
        .task(id: workspace.exportState) {
            derivedShareIssue = nil
            guard case .exported(let directory, _) = workspace.exportState,
                  let projectID = workspace.selectedProjectID,
                  let revision = workspace.latestSnapshot?.revision else {
                isPreparingDerivedShare = false
                return
            }
            let completedState = workspace.exportState
            // The validated source must be <root>/<project>/exports/revision-*.
            // No project/capture directory or arbitrary provider URL is shared.
            let root = directory.deletingLastPathComponent()
                .deletingLastPathComponent().deletingLastPathComponent()
            isPreparingDerivedShare = true
            do {
                let archive = try await Task.detached(priority: .userInitiated) {
                    try ScannerDerivedExportShare.package(directory: directory,
                        projectID: projectID, sceneRevision: revision, trustedRoot: root)
                }.value
                guard !Task.isCancelled, workspace.exportState == completedState,
                      workspace.selectedProjectID == projectID,
                      workspace.latestSnapshot?.revision == revision else {
                    try? FileManager.default.removeItem(at: archive)
                    return
                }
                clearDerivedShare()
                derivedExportToShare = archive
                isPreparingDerivedShare = false
            } catch {
                guard !Task.isCancelled, workspace.exportState == completedState,
                      workspace.selectedProjectID == projectID else { return }
                isPreparingDerivedShare = false
                derivedShareIssue = error.localizedDescription
            }
        }
        .task { workspace.reloadProjects() }
    }

    private func clearDerivedShare() {
        // This exact transient ZIP was returned by our local packager, never a
        // user-chosen source URL. Originals and completed derivative bundles stay.
        if let derivedExportToShare { try? FileManager.default.removeItem(at: derivedExportToShare) }
        derivedExportToShare = nil
    }

    private var presentation: ScannerWorkspacePresentation {
        let selectedProject = workspace.selectedProjectID.flatMap { selectedID in
            workspace.localProjects.first(where: { $0.id == selectedID })
        }
        let reviewData = workspace.latestSnapshot.flatMap { ScannerAcceptedPlanDecoder.decode($0, language: locale.language.languageCode?.identifier == "ar" ? .arabic : .english) }
        let isReferenceScene = reviewData?.isSynthetic == true
            || workspace.latestSnapshot?.provenance.contains(.syntheticTest) == true
        let assessment = workspace.latestSnapshot.map(ScannerSceneAssessment.decode)
        return ScannerWorkspacePresentation(
            mode: mode,
            captureState: captureState,
            statusDetail: statusDetail,
            project: selectedProject.map(projectPresentation),
            projects: workspace.localProjects.map(projectPresentation),
            deletedProjects: workspace.deletedProjects.map(projectPresentation),
            isManagingLibrary: workspace.isManagingLibrary,
            issues: issues(assessment: assessment, isReferenceScene: isReferenceScene),
            measurements: reviewData?.measurements ?? [],
            provenanceFields: reviewData?.provenanceFields ?? [],
            provenanceLanguage: locale.language.languageCode?.identifier == "ar" ? .arabic : .english,
            targetGuidance: targetGuidance,
            plan: reviewData?.plan,
            hasCapturedScene: assessment?.hasCapturedGeometry == true,
            isReferenceScene: isReferenceScene,
            assessment: assessment,
            objectDetailDescription: nil,
            exportStatus: exportStatus,
            projectArchiveExportStatus: projectArchiveExportStatus,
            canStartCapture: !workspace.isManagingLibrary && (workspace.phase == .ready || workspace.phase == .browsing),
            canFinishCapture: isCaptureActive,
            canAssist: !workspace.editableBoundary.isEmpty,
            canUndo: workspace.canRecordMissingEvidence && workspace.commandHistory?.undoID != nil,
            canRecordMissingEvidence: workspace.canRecordMissingEvidence,
            canSave: workspace.saveState == .hasUnsavedChanges,
            canExport: workspace.canExport && !isPreparingDerivedShare,
            isExporting: workspace.exportState == .exporting || isPreparingDerivedShare,
            canExportProjectArchive: workspace.canExportProjectArchive,
            isExportingProjectArchive: workspace.projectArchiveExportState == .exporting,
            canOpenLibrary: workspace.canShowLibrary,
            canCaptureObject: canCaptureObject,
            canConfirmPlanReference: workspace.canConfirmPlanReference && assessment?.hasCapturedGeometry == true
        )
    }

    private var actions: ScannerWorkspaceActions {
        ScannerWorkspaceActions(
            startCapture: {
                if workspace.selectedProjectID == nil || workspace.phase == .browsing {
                    isCreatingProject = true
                } else {
                    workspace.startCapture()
                }
            },
            importProject: { isImportingProject = true },
            finishCapture: workspace.stopCapture,
            assist: {
                guard !workspace.editableBoundary.isEmpty, let snapshot = workspace.latestSnapshot else { return }
                boundaryReviewPresentation = BoundaryReviewPresentation(id: UUID(), snapshot: snapshot)
            },
            undo: {
                guard let id = workspace.commandHistory?.undoID, let revision = workspace.latestSnapshot?.revision else { return }
                workspace.applyBoundaryEdit(.undo(original: id), expectedRevision: revision)
            },
            recordMissingEvidence: {
                missingEvidenceReason = ""
                isRecordingMissingEvidence = true
            },
            save: workspace.saveSelectedProject,
            export: workspace.exportSelectedProject,
            exportProjectArchive: workspace.exportSelectedProjectArchive,
            openLibrary: workspace.showLibrary,
            openProject: workspace.openProject,
            renameProject: workspace.renameProject,
            trashProject: workspace.moveProjectToTrash,
            restoreProject: workspace.restoreProject,
            permanentlyDeleteProject: workspace.permanentlyDeleteProject,
            captureObject: { if canCaptureObject { isObjectCapturePresented = true } },
            recover: recover,
            confirmPlanReference: presentFloorReference
        )
    }

    private var targetGuidance: ScannerWorkspacePresentation.TargetGuidance? {
        guard isCaptureActive else { return nil }
        let title = String(localized: "Target assistance")
        switch workspace.targetSelectionState {
        case .awaitingSelection:
            return ScannerWorkspacePresentation.TargetGuidance(
                state: .awaitingSelection,
                title: title,
                detail: String(localized: "Tap a visible surface to select current calibrated depth support."),
                overlay: nil,
                currentViewport: currentCameraViewport
            )
        case .pending(let reason):
            return ScannerWorkspacePresentation.TargetGuidance(
                state: .pendingNoDepth,
                title: title,
                detail: targetPendingDetail(reason),
                overlay: nil,
                currentViewport: currentCameraViewport
            )
        case .stale:
            return ScannerWorkspacePresentation.TargetGuidance(
                state: .staleSelection,
                title: title,
                detail: String(localized: "The accepted scene revision changed. Tap the feature again to confirm it against the current revision."),
                overlay: nil,
                currentViewport: currentCameraViewport
            )
        case .confirmed(let selection):
            return ScannerWorkspacePresentation.TargetGuidance(
                state: .confirmed(selection.provenance == .observed ? .observed : .fitted),
                title: title,
                detail: String(localized: "This target is bound to accepted revision \(selection.sceneRevision) and its retained source frame."),
                overlay: ScannerWorkspacePresentation.TargetGuidance.CalibratedOverlay(
                    observedDepthPixel: selection.observedDepthPixel,
                    calibration: selection.calibration,
                    frame: selection.overlayFrame,
                    displayTransform: selection.displayTransform,
                    currentEpoch: selection.sourceFrame.sessionEpoch,
                    maximumFrameAge: 1
                ),
                currentViewport: currentCameraViewport
            )
        }
    }

    private func targetPendingDetail(_ reason: NativeTargetPendingReason) -> String {
        switch reason {
        case .captureUnavailable:
            String(localized: "Precise capture must be active before a target can be selected.")
        case .missingCurrentDepthFrame, .frameMismatch, .staleFrame:
            String(localized: "No matching current depth frame is available. Hold the phone steady and tap again.")
        case .invalidCalibration:
            String(localized: "The current depth calibration is invalid, so this tap remains pending.")
        case .invalidViewport, .viewportChanged:
            String(localized: "The camera viewport changed during selection. Tap again in the current orientation.")
        case .staleEpoch, .missingAcceptedEpochTransform:
            String(localized: "Tracking coordinates are not accepted for this epoch yet. Wait for relocalization before selecting.")
        case .outsideDepthSupport:
            String(localized: "That pixel has no supported depth. Aim at a visible surface and tap again.")
        case .lowConfidenceDepth:
            String(localized: "Only low-confidence preview depth is available at that pixel; it was not promoted to measured support.")
        case .awaitingSourceAcceptance:
            String(localized: "The calibrated source frame is being retained and bound to an accepted scene revision.")
        case .sourceCapacityReached:
            String(localized: "The retained depth-frame limit was reached. Finish and save this draft before starting another capture.")
        }
    }

    private var exportStatus: String? {
        if let derivedShareIssue { return String(localized: "Offline export failed: \(derivedShareIssue)") }
        if isPreparingDerivedShare { return String(localized: "Preparing a local ZIP for the share sheet.") }
        return switch workspace.exportState {
        case .idle:
            nil
        case .exporting:
            String(localized: "Creating PDF, SVG, DXF, OBJ, MTL, and provenance files locally.")
        case .exported:
            String(localized: "Export ready. Choose Files or another destination in the share sheet.")
        case .failed(let message):
            String(localized: "Offline export failed: \(message)")
        }
    }

    private var projectArchiveExportStatus: String? {
        switch workspace.projectArchiveExportState {
        case .idle:
            nil
        case .exporting:
            String(localized: "Creating a local HSSCAN archive for Files.")
        case .exported:
            String(localized: "HSSCAN archive is ready to share or save to Files.")
        case .failed(let message):
            String(localized: "HSSCAN archive failed: \(message)")
        }
    }

    private var mode: ScannerWorkspacePresentation.Mode {
        switch workspace.phase {
        case .browsing: .library
        case .startingCapture, .capturing, .captureLimited, .captureInterrupted, .stoppingCapture: .scanning
        case .frozen, .saving, .openingProject: .review
        case .preparingProject, .ready, .captureUnsupported, .failed: .readiness
        }
    }

    private var captureState: ScannerWorkspacePresentation.CaptureState {
        switch workspace.phase {
        case .browsing, .preparingProject, .ready: .notStarted
        case .startingCapture: .starting
        case .capturing: .trackingNormal
        case .captureLimited(let reason): .trackingLimited(reason)
        case .captureInterrupted: .interrupted
        case .stoppingCapture, .frozen, .saving, .openingProject: .interrupted
        case .captureUnsupported(let reason), .failed(let reason): .unavailable(reason)
        }
    }

    private var statusDetail: String? {
        switch workspace.phase {
        case .captureUnsupported(let reason), .failed(let reason): reason
        case .captureLimited(let reason): reason
        case .captureInterrupted: String(localized: "The camera was interrupted. Finish this scan or resume when capture returns.")
        case .preparingProject: String(localized: "Creating this project locally before capture begins.")
        case .startingCapture: String(localized: "The camera will begin after the session is ready.")
        case .stoppingCapture: String(localized: "Finishing this capture without changing the accepted snapshot.")
        case .frozen: String(localized: "Review the accepted snapshot and its remaining warnings.")
        case .saving: String(localized: "Saving source observations and the accepted revision locally.")
        case .openingProject: String(localized: "Opening the locally saved project.")
        case .browsing, .ready, .capturing: nil
        }
    }

    private func presentFloorReference() {
        guard workspace.canConfirmPlanReference, let snapshot = workspace.latestSnapshot,
              ScannerSceneAssessment.decode(snapshot).hasCapturedGeometry else { return }
        floorReferenceSnapshot = snapshot
        isFloorReferencePresented = true
    }

    private func recover(_ issue: ScannerWorkspacePresentation.Issue) {
        // Never dispatch the current driver recovery for a different card.
        if issue.nativeCode == nil, issue.id == workspace.currentIssue?.id {
            workspace.recoverFromCurrentIssue()
        } else if issue.nativeCode == nil, issue.id == ScannerQualityActions.saveFailureID,
                  workspace.currentIssue == nil, case .failed = workspace.saveState {
            workspace.saveSelectedProject()
        } else if let snapshot = workspace.latestSnapshot,
                  ScannerQualityActions.offersFloorReference(
                    issue, assessment: ScannerSceneAssessment.decode(snapshot),
                    revision: snapshot.revision, canConfirm: workspace.canConfirmPlanReference) {
            presentFloorReference()
        }
    }

    private func issues(assessment: ScannerSceneAssessment?, isReferenceScene: Bool) -> [ScannerWorkspacePresentation.Issue] {
        var visible: [ScannerWorkspacePresentation.Issue] = []
        if let issue = workspace.currentIssue {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: issue.id,
                    title: issue.title,
                    detail: issue.message,
                    priority: ScannerQualityActions.driverPriority(
                        phase: workspace.phase, recoveryAction: issue.recoveryAction),
                    recoveryTitle: recoveryTitle(for: issue.recoveryAction)
                )
            )
        }
        if case .failed(let message) = workspace.saveState, workspace.currentIssue == nil {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: ScannerQualityActions.saveFailureID,
                    title: String(localized: "Local save needs attention"),
                    detail: message,
                    priority: .important,
                    recoveryTitle: String(localized: "Try again")
                )
            )
        }
        if isReferenceScene {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: ScannerQualityActions.referenceSceneID,
                    title: String(localized: "Reference scene"),
                    detail: String(localized: "This local reference data is not a captured room scan."),
                    priority: .notice,
                    recoveryTitle: nil
                )
            )
        }
        if let snapshot = workspace.latestSnapshot, let assessment {
            visible.append(contentsOf: assessment.issues.map { original in
                var issue = original
                if ScannerQualityActions.offersFloorReference(
                    issue, assessment: assessment, revision: snapshot.revision,
                    canConfirm: workspace.canConfirmPlanReference) {
                    issue.recoveryTitle = String(localized: "Set observed floor reference")
                }
                return issue
            })
        }
        return visible.sorted {
            $0.priority.rank == $1.priority.rank ? $0.id.uuidString < $1.id.uuidString
                : $0.priority.rank < $1.priority.rank
        }
    }

    private func projectPresentation(_ project: ScannerProjectSummary) -> ScannerWorkspacePresentation.Project {
        ScannerWorkspacePresentation.Project(
            id: project.id,
            name: project.name,
            revision: String(project.sceneRevision),
            savedDescription: project.updatedAt.formatted(date: .abbreviated, time: .shortened),
            quality: project.state == .draft ? .draft : .ready,
            canRestore: project.canRestore
        )
    }

    private func recoveryTitle(for action: ScannerWorkspaceRecoveryAction?) -> String? {
        switch action {
        case .retrySave: String(localized: "Try again")
        case .retryCommandJournal: String(localized: "Retry journal")
        case .reopenProject: String(localized: "Reopen project")
        case .returnToProjectList: String(localized: "View projects")
        case .stopCaptureAndSaveDraft: String(localized: "Save draft")
        case nil: nil
        }
    }

    private var canCaptureObject: Bool {
        switch workspace.phase {
        case .browsing:
            !workspace.isManagingLibrary
        case .frozen:
            workspace.canShowLibrary
        case .saving, .openingProject, .preparingProject, .ready, .startingCapture, .capturing,
             .captureLimited, .captureInterrupted, .stoppingCapture, .captureUnsupported, .failed:
            false
        }
    }

    private var isCaptureActive: Bool {
        switch workspace.phase {
        case .capturing, .captureLimited, .captureInterrupted:
            true
        case .browsing, .preparingProject, .ready, .startingCapture, .stoppingCapture,
             .frozen, .saving, .openingProject, .captureUnsupported, .failed:
            false
        }
    }
}

enum ScannerProjectImportContentTypes {
    // A .hsscan extension resolves to a dynamic data type on iOS. The native
    // archive parser and project validator check the selected bytes.
    static let accepted: [UTType] = [.data]
}

private struct ScannerProjectImportPicker: UIViewControllerRepresentable {
    let completed: (URL?) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(completed: completed) }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: ScannerProjectImportContentTypes.accepted,
            asCopy: false
        )
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let completed: (URL?) -> Void

        init(completed: @escaping (URL?) -> Void) {
            self.completed = completed
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            completed(nil)
        }

        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            completed(urls.first)
        }
    }
}

private struct ScannerProjectArchiveShareSheet: UIViewControllerRepresentable {
    let archiveURL: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        let controller = UIActivityViewController(activityItems: [archiveURL], applicationActivities: nil)
        controller.view.accessibilityIdentifier = "export.share-sheet"
        return controller
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

private struct ScannerProjectNameSheet: View {
    @Binding var name: String
    let create: () -> Void
    let cancel: () -> Void
    @FocusState private var isNameFocused: Bool

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Space name", text: $name, prompt: Text("For example, reception"))
                        .focused($isNameFocused)
                        .textInputAutocapitalization(.words)
                        .accessibilityLabel("Space name")
                        .accessibilityIdentifier("project-name.input")
                } footer: {
                    Text("A local project is created before capture so its source data and accepted revisions stay together.")
                }
            }
            .navigationTitle("New space")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel", action: cancel)
                        .accessibilityIdentifier("project-name.cancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Create", action: create)
                        .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                        .accessibilityIdentifier("project-name.create")
                }
            }
            .onAppear { isNameFocused = true }
        }
    }
}

/// This view only attaches a display surface to the session supplied by the
/// workspace. It never configures, starts, pauses, or delegates that session.
private struct ScannerCameraPreview: UIViewRepresentable {
    let session: ARSession?
    let onTargetTap: (NativeTargetTap) -> Void
    let onViewportChange: (OverlayViewport?) -> Void

    func makeUIView(context: Context) -> ScannerTargetingARSCNView {
        let view = ScannerTargetingARSCNView(frame: .zero)
        view.automaticallyUpdatesLighting = true
        view.rendersCameraGrain = false
        view.onTargetTap = onTargetTap
        view.onViewportChange = onViewportChange
        if let session { view.session = session }
        view.publishCurrentViewport()
        return view
    }

    func updateUIView(_ view: ScannerTargetingARSCNView, context: Context) {
        view.onTargetTap = onTargetTap
        view.onViewportChange = onViewportChange
        view.publishCurrentViewport()
        guard let session, view.session !== session else { return }
        view.session = session
    }
}

private final class ScannerTargetingARSCNView: ARSCNView {
    var onTargetTap: ((NativeTargetTap) -> Void)?
    var onViewportChange: ((OverlayViewport?) -> Void)?
    private var viewportGeneration: UInt64 = 1
    private var lastViewportSize: CGSize = .zero
    private var lastOrientation: OverlayPresentationOrientation?
    private var lastPublishedViewport: OverlayViewport?

    override init(frame: CGRect, options: [String: Any]? = nil) {
        super.init(frame: frame, options: options)
        addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(didTap(_:))))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { nil }

    override func layoutSubviews() {
        super.layoutSubviews()
        updateViewportGeneration()
    }

    @objc private func didTap(_ recognizer: UITapGestureRecognizer) {
        updateViewportGeneration()
        guard let viewport = currentViewport() else { return }
        let location = recognizer.location(in: self)
        onTargetTap?(NativeTargetTap(
            screenPoint: SIMD2(Float(location.x), Float(location.y)),
            viewport: viewport
        ))
    }

    private func updateViewportGeneration() {
        let orientation = interfaceOrientation()
        if bounds.size != lastViewportSize || orientation != lastOrientation {
            if viewportGeneration != UInt64.max { viewportGeneration += 1 }
            lastViewportSize = bounds.size
            lastOrientation = orientation
            publishCurrentViewport()
        }
    }

    func publishCurrentViewport() {
        let viewport = currentViewport()
        guard viewport != lastPublishedViewport else { return }
        lastPublishedViewport = viewport
        DispatchQueue.main.async { [weak self] in
            self?.onViewportChange?(viewport)
        }
    }

    private func currentViewport() -> OverlayViewport? {
        guard let orientation = interfaceOrientation(), bounds.width > 0, bounds.height > 0 else { return nil }
        return OverlayViewport(
            width: Float(bounds.width), height: Float(bounds.height),
            orientation: orientation, generation: viewportGeneration
        )
    }

    private func interfaceOrientation() -> OverlayPresentationOrientation? {
        switch window?.windowScene?.interfaceOrientation {
        case .portrait: .portrait
        case .portraitUpsideDown: .portraitUpsideDown
        case .landscapeLeft: .landscapeLeft
        case .landscapeRight: .landscapeRight
        default: nil
        }
    }
}

private struct ScannerSnapshotMeshPreview: UIViewRepresentable {
    let snapshot: ScannerSceneSnapshot
    let admittedAppearanceBinding: AppearanceAdmittedBinding?
    let appearanceTextures: [AppearanceTextureResource]
    let texturedMesh: ScannerTexturedMesh?

    func makeCoordinator() -> Coordinator { Coordinator() }
    final class Coordinator {
        var snapshot: ScannerSceneSnapshot?
        var assetSignature: UUID?
        var binding: AppearanceAdmittedBinding?
        var resources: [AppearanceTextureResource] = []
    }

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView(frame: .zero)
        view.backgroundColor = UIColor(ScannerProductTheme.surface)
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        rebuild(view, context: context)
        view.accessibilityLabel = String(localized: "3D geometry from accepted snapshot")
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        rebuild(view, context: context)
    }

    private func rebuild(_ view: SCNView, context: Context) {
        // IDs/count alone miss changed UVs or face-to-image assignments. Each
        // immutable appearance layer owns an independent presentation identity.
        let signature = texturedMesh?.presentationID
        guard context.coordinator.snapshot != snapshot || context.coordinator.assetSignature != signature ||
              context.coordinator.binding != admittedAppearanceBinding || context.coordinator.resources != appearanceTextures else { return }
        let pose = view.pointOfView?.simdTransform
        view.scene = ScannerMeshSceneBuilder.scene(
            from: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures,
            texturedMesh: texturedMesh
        )
        if let pose { view.pointOfView?.simdTransform = pose }
        context.coordinator.snapshot = snapshot
        context.coordinator.assetSignature = signature
        context.coordinator.binding = admittedAppearanceBinding
        context.coordinator.resources = appearanceTextures
    }
}

/// A reference proposal from three exact retained vertices, never world Y=0.
/// The fit is geometric, not a calibrated physical measurement or room semantic.
struct ScannerFloorReferenceProposal {
    let sceneRevision: UInt64
    let vertexIndices: [Int]
    let projectFromFloor: simd_double4x4

    static func nearestVisibleVertex(snapshot: ScannerSceneSnapshot, faceIndex: Int, localPoint: SIMD3<Double>) -> Int? {
        guard localPoint.x.isFinite, localPoint.y.isFinite, localPoint.z.isFinite,
              let indices = ScannerMeshSceneBuilder.displayTriangleIndices(from: snapshot),
              faceIndex >= 0, faceIndex < indices.count / 3 else { return nil }
        let offset = faceIndex * 3
        let candidates = indices[offset..<(offset + 3)].map(Int.init)
        guard candidates.allSatisfy({ snapshot.verticesMetres.indices.contains($0) }) else { return nil }
        return candidates.min {
            simd_length_squared(snapshot.verticesMetres[$0] - localPoint) < simd_length_squared(snapshot.verticesMetres[$1] - localPoint)
        }
    }

    static func make(snapshot: ScannerSceneSnapshot, vertexIndices: [Int]) -> Self? {
        guard vertexIndices.count == 3, Set(vertexIndices).count == 3,
              vertexIndices.allSatisfy({ snapshot.verticesMetres.indices.contains($0) }),
              let visibleIndices = ScannerMeshSceneBuilder.displayTriangleIndices(from: snapshot),
              vertexIndices.allSatisfy({ $0 <= Int(UInt32.max) && visibleIndices.contains(UInt32($0)) }) else { return nil }
        let points = vertexIndices.map { snapshot.verticesMetres[$0] }
        guard points.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite }) else { return nil }
        let first = points[1] - points[0], second = points[2] - points[0]
        let firstLength = simd_length(first), secondLength = simd_length(second)
        let product = firstLength * secondLength
        let cross = simd_cross(first, second), crossLength = simd_length(cross)
        // Numeric conditioning only. This is not a sensor-accuracy tolerance.
        guard product.isFinite, product > 0, crossLength.isFinite,
              crossLength / product > 1e-8 else { return nil }
        var normal = cross / crossLength
        guard abs(normal.y) > 1e-8 else { return nil } // A vertical plane is not a floor reference.
        if normal.y < 0 { normal = -normal }
        let u = first / firstLength
        let v = simd_cross(normal, u)
        let origin = points[0]
        return Self(sceneRevision: snapshot.revision, vertexIndices: vertexIndices,
            projectFromFloor: simd_double4x4(columns: (
                SIMD4(u.x, u.y, u.z, 0), SIMD4(normal.x, normal.y, normal.z, 0),
                SIMD4(-v.x, -v.y, -v.z, 0), SIMD4(origin.x, origin.y, origin.z, 1)
            )))
    }
}

/// Keep editable values at full contrast at the scroll edge. The native
/// opaque navigation bar already separates them from the stationary controls;
/// an additional translucent fade can obscure partially visible field labels.
private struct ScannerBoundaryScrollContrast: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.scrollEdgeEffectHidden(true, for: .all)
        } else {
            content
        }
    }
}

/// The editor owns multiline row heights rather than a reused Form cell. Keep
/// the measured controls eager, while the potentially large boundary list below
/// remains lazy. No text-size limit or alternate accessibility text is applied.
private struct ScannerBoundaryActionStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .fixedSize(horizontal: false, vertical: true)
            .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
            .contentShape(Rectangle())
            .opacity(configuration.isPressed ? 0.8 : 1)
    }
}

private struct ScannerBoundaryCard<Content: View, Header: View>: View {
    let content: Content
    let header: Header

    init(@ViewBuilder content: () -> Content, @ViewBuilder header: () -> Header) {
        self.content = content()
        self.header = header()
    }

    init(@ViewBuilder content: () -> Content) where Header == EmptyView {
        self.content = content()
        self.header = EmptyView()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            header.padding(.horizontal, 4)
            VStack(alignment: .leading, spacing: 18) { content }
                .font(.body)
                .buttonStyle(ScannerBoundaryActionStyle())
                .padding(16)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 20, style: .continuous))
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// Revision-bound correction controls for retained, provenance-supported
/// boundary graph edges. Targets are never inferred from displayed mesh data.
@MainActor
private struct ScannerBoundaryReviewSheet: View {
    let snapshot: ScannerSceneSnapshot
    let edges: [ScannerBoundaryEdge]
    let isCurrent: Bool
    let canApply: Bool
    let issue: String?
    let history: ScannerCommandHistory?
    let apply: (ScannerBoundaryEdit) -> Void

    @Environment(\.dismiss) private var dismiss
    @Environment(\.locale) private var locale
    @State private var selectedEdgeID: UUID?
    @State private var splitPosition = 0.5
    @State private var confirmsPosition = false
    @State private var confirmsExactDimension = false
    @State private var inputIssue: String?
    @State private var dimensionText = ""
    @State private var uncertaintyText = ""
    @State private var source: ScannerBoundaryEdit.Source = .tape
    @State private var mode: ScannerBoundaryEdit.Mode = .softMeasurement

    private var selectedEdge: ScannerBoundaryEdge? { edges.first { $0.id == selectedEdgeID } }
    private var dimensionValue: Double? { ScannerMeasurementEntry.metres(dimensionText, locale: locale) }
    private var uncertaintyValue: Double? { ScannerMeasurementEntry.metres(uncertaintyText, locale: locale) }
    private var hasUncertainty: Bool { !uncertaintyText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                ScannerBoundaryCard {
                    Text("Boundary corrections").font(.title2.weight(.bold))
                        .fixedSize(horizontal: false, vertical: true)
                    HStack(alignment: .top, spacing: 12) {
                        Image(systemName: "scope").font(.system(size: 20))
                            .accessibilityHidden(true)
                        Text("Choose a captured boundary, then add a measurement or refine its shape. Your original capture remains available.")
                            .fixedSize(horizontal: false, vertical: true)
                            .padding(.vertical, 4)
                    }
                    .font(.subheadline).foregroundStyle(.secondary)
                    ScannerBoundaryPicker(snapshot: snapshot, edges: edges, selectedEdgeID: selectedEdgeID) { id in
                        guard canApply else { return }
                        selectedEdgeID = id
                        confirmsPosition = false
                        confirmsExactDimension = false
                    }
                    .frame(height: 260).clipShape(RoundedRectangle(cornerRadius: 16))
                    .accessibilityElement(children: .ignore)
                    .accessibilityAddTraits(.isImage)
                    .accessibilityLabel("Captured boundaries")
                    .accessibilityHint("Rotate to inspect the scan. Select a boundary from the list below.")
                    if !isCurrent {
                        Label("The scan changed. Select the boundary again before applying a correction.", systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(ScannerProductTheme.warning)
                    }
                    if isCurrent && !canApply && issue == nil { ProgressView("Preparing review") }
                    if let issue { Label(issue, systemImage: "info.circle").foregroundStyle(ScannerProductTheme.warning) }
                }

                ScannerBoundaryCard {
                    if edges.isEmpty {
                        ContentUnavailableView("No captured boundary available", systemImage: "point.3.connected.trianglepath.dotted",
                            description: Text("Capture more of the room or review its floor reference to make boundaries available for correction."))
                    } else {
                        LazyVStack(alignment: .leading, spacing: 16) {
                        ForEach(Array(edges.enumerated()), id: \.element.id) { index, edge in
                            Button {
                                selectedEdgeID = edge.id
                                confirmsPosition = false
                                confirmsExactDimension = false
                            } label: {
                                HStack(spacing: 12) {
                                    Image(systemName: selectedEdgeID == edge.id ? "largecircle.fill.circle" : "circle")
                                        .foregroundStyle(selectedEdgeID == edge.id ? ScannerProductTheme.accent : .secondary)
                                    VStack(alignment: .leading, spacing: 4) {
                                        ScannerDynamicText(String(localized: "Boundary \(index + 1)"),
                                            style: .headline,
                                            identifier: "boundary-review.edge-name.\(edge.id.uuidString)")
                                        ScannerDynamicText(String(localized: "Length: \(edge.lengthMetres.formatted(.number.precision(.fractionLength(2)))) m"),
                                            style: .caption1, color: .secondaryLabel,
                                            identifier: "boundary-review.edge-length.\(edge.id.uuidString)")
                                    }
                                    Spacer(minLength: 0)
                                }
                                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .disabled(!canApply)
                            .accessibilityIdentifier("boundary-review.edge.\(edge.id.uuidString)")
                        }
                        }
                    }
                } header: {
                    ScannerDynamicText(String(localized: "Captured boundaries"), style: .headline,
                        identifier: "boundary-review.captured-header", isHeader: true)
                        .textCase(nil)
                }

                if let edge = selectedEdge {
                    ScannerBoundaryCard {
                        Button {
                            apply(.protect(edge: edge.id))
                        } label: {
                            Label("Keep this boundary during refinement", systemImage: "lock.shield")
                        }
                        .disabled(!canApply)
                        .accessibilityIdentifier("boundary-review.protect")

                        VStack(alignment: .leading, spacing: 10) {
                            Text("Split position along edge")
                            Slider(value: $splitPosition, in: 0.1...0.9) { Text("Split position") }
                            ScannerDynamicText(String(localized: "\(Int((splitPosition * 100).rounded()))% from point A"),
                                style: .caption1, identifier: "boundary-review.split-position-detail")
                            Button { apply(.split(edge: edge.id, parameter: splitPosition)) } label: {
                                ScannerDynamicText(String(localized: "Split selected edge"),
                                    style: .body, color: UIColor(ScannerProductTheme.accent),
                                    identifier: "boundary-review.split-label")
                            }
                                .disabled(!canApply)
                                .accessibilityLabel("Split selected edge")
                                .accessibilityIdentifier("boundary-review.split")
                        }

                        Toggle(isOn: $confirmsPosition) {
                            Text("Confirm current position of point A")
                                .font(.body).fixedSize(horizontal: false, vertical: true)
                        }
                        .accessibilityLabel("Confirm current position of point A")
                        .accessibilityIdentifier("boundary-review.confirm-position")
                        Button {
                            guard confirmsPosition else {
                                inputIssue = String(localized: "Confirm the current position of point A before locking it.")
                                return
                            }
                            apply(.lock(node: edge.startID, position: edge.start, confirmed: confirmsPosition))
                        } label: {
                            Text("Lock point A position")
                                .font(.body).fixedSize(horizontal: false, vertical: true)
                                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        }
                        .buttonStyle(.plain)
                        .foregroundStyle(ScannerProductTheme.accent)
                        .disabled(!canApply)
                        .accessibilityIdentifier("boundary-review.lock-position")
                        .accessibilityLabel("Lock point A position")
                    } header: {
                        ScannerDynamicText(String(localized: "Refine selected boundary"), style: .headline,
                            identifier: "boundary-review.refine-header", isHeader: true)
                    }

                    ScannerBoundaryCard {
                        VStack(alignment: .leading, spacing: 8) {
                            ScannerDynamicText(String(localized: "Measured length in metres"), style: .body,
                                identifier: "boundary-review.dimension-heading")
                            ScannerBoundaryMeasurementField(text: $dimensionText,
                                label: String(localized: "Measured length in metres"),
                                identifier: "boundary-review.dimension-value")
                        }
                        VStack(alignment: .leading, spacing: 8) {
                            ScannerDynamicText(String(localized: "Measurement source"), style: .body,
                                identifier: "boundary-review.source-heading")
                            Menu {
                                Button("Tape measure") { source = .tape }
                                Button("Laser measure") { source = .laser }
                                Button("Survey reference") { source = .referenceSurvey }
                                Button("Estimate") { source = .guess }
                            } label: {
                                HStack {
                                    Text(sourceTitle)
                                        .font(.body).fixedSize(horizontal: false, vertical: true)
                                    Image(systemName: "chevron.up.chevron.down")
                                }
                            }
                            .accessibilityIdentifier("boundary-review.source")
                            .accessibilityLabel("Measurement source")
                            .accessibilityValue(sourceTitle)
                        }
                        VStack(alignment: .leading, spacing: 8) {
                            ScannerDynamicText(String(localized: "Adjustment mode"), style: .body,
                                identifier: "boundary-review.mode-heading")
                            Menu {
                                Button("Soft measurement") { mode = .softMeasurement }
                                Button("Exact model") { mode = .exactModel }
                            } label: {
                                HStack {
                                    Text(modeTitle)
                                        .font(.body).fixedSize(horizontal: false, vertical: true)
                                    Image(systemName: "chevron.up.chevron.down")
                                }
                            }
                            .accessibilityIdentifier("boundary-review.mode")
                            .accessibilityLabel("Adjustment mode")
                            .accessibilityValue(modeTitle)
                        }
                        VStack(alignment: .leading, spacing: 8) {
                            ScannerDynamicText(String(localized: "Optional uncertainty in metres"), style: .body,
                                identifier: "boundary-review.uncertainty-heading")
                            ScannerBoundaryMeasurementField(text: $uncertaintyText,
                                label: String(localized: "Optional uncertainty in metres"),
                                identifier: "boundary-review.uncertainty-value")
                        }
                        if mode == .exactModel {
                            Toggle("Confirm exact model constraint", isOn: $confirmsExactDimension)
                            Text("Exact constraints may conflict with protected geometry. A rejected proposal preserves the last accepted shape.")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Button {
                            guard let metres = dimensionValue, metres > 0 else {
                                inputIssue = String(localized: "Enter a positive measured length in metres.")
                                return
                            }
                            guard !hasUncertainty || uncertaintyValue != nil else {
                                inputIssue = String(localized: "Enter a valid uncertainty in metres or leave it empty.")
                                return
                            }
                            guard mode != .exactModel || confirmsExactDimension else {
                                inputIssue = String(localized: "Confirm the exact model constraint before applying it.")
                                return
                            }
                            apply(.dimension(a: edge.startID, b: edge.endID, metres: metres, source: source,
                                uncertainty: hasUncertainty ? uncertaintyValue : nil, mode: mode))
                        } label: {
                            Text("Apply reference dimension")
                                .font(.body).fixedSize(horizontal: false, vertical: true)
                                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        }
                        .buttonStyle(.plain)
                        .foregroundStyle(ScannerProductTheme.accent)
                        .disabled(!canApply)
                        .accessibilityIdentifier("boundary-review.set-dimension")
                        .accessibilityLabel("Apply reference dimension")
                    } header: {
                        ScannerDynamicText(String(localized: "Set a reference dimension"), style: .headline,
                            identifier: "boundary-review.dimension-header", isHeader: true)
                    }
                }

                ScannerBoundaryCard {
                    if let id = history?.undoID {
                        Button("Undo last supported correction") { apply(.undo(original: id)) }
                            .disabled(!canApply).accessibilityIdentifier("boundary-review.undo")
                    }
                    if let id = history?.redoID {
                        Button("Redo correction") { apply(.redo(original: id)) }
                            .disabled(!canApply).accessibilityIdentifier("boundary-review.redo")
                    }
                    if history?.undoID == nil && history?.redoID == nil {
                        ScannerDynamicText(String(localized: "No correction is available to undo here."),
                            style: .subheadline, identifier: "boundary-review.empty-history")
                    }
                    ScannerDynamicText(String(localized: "Review the result, then save your changes. If a correction conflicts with an earlier measurement or lock, the last accepted shape is kept."),
                        style: .caption1, identifier: "boundary-review.history-help")
                } header: {
                    ScannerDynamicText(String(localized: "Correction history"), style: .headline,
                        identifier: "boundary-review.history-header", isHeader: true)
                }
                }
                .padding(20)
                .frame(maxWidth: 740)
                .frame(maxWidth: .infinity, alignment: .center)
            }
            .accessibilityIdentifier("boundary-review.content")
            .background(Color(uiColor: .systemGroupedBackground))
            .modifier(ScannerBoundaryScrollContrast())
            .navigationTitle("Review")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(Color(uiColor: .systemGroupedBackground), for: .navigationBar)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbar { ToolbarItem(placement: .confirmationAction) {
                Button { dismiss() } label: {
                    Label("Done", systemImage: "xmark").labelStyle(.iconOnly)
                        .font(.body.weight(.semibold)).frame(minWidth: 44, minHeight: 44)
                }.accessibilityIdentifier("boundary-review.done")
            } }
            .onChange(of: snapshot.revision) { _, _ in
                selectedEdgeID = nil
                confirmsPosition = false
                confirmsExactDimension = false
                dimensionText = ""
                uncertaintyText = ""
            }
        }
        .tint(ScannerProductTheme.accent)
        .presentationDetents([.large])
        .alert("Check correction", isPresented: Binding(
            get: { inputIssue != nil },
            set: { if !$0 { inputIssue = nil } }
        )) {
            Button("OK", role: .cancel) { inputIssue = nil }
        } message: {
            Text(inputIssue ?? "")
        }
    }

    private var sourceTitle: String {
        switch source {
        case .tape: String(localized: "Tape measure")
        case .laser: String(localized: "Laser measure")
        case .referenceSurvey: String(localized: "Survey reference")
        case .guess: String(localized: "Estimate")
        }
    }

    private var modeTitle: String {
        switch mode {
        case .softMeasurement: String(localized: "Soft measurement")
        case .exactModel: String(localized: "Exact model")
        }
    }
}

/// Native numeric input follows the same semantic content-size mapping as its
/// visible label (which names metres). No low-contrast placeholder or inferred
/// dimension is used; value and height both update with the rendered font.
private struct ScannerBoundaryMeasurementField: UIViewRepresentable {
    @Binding var text: String
    let label: String
    let identifier: String
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize

    func makeCoordinator() -> Coordinator { Coordinator(text: $text) }

    func makeUIView(context: Context) -> UITextField {
        let field = UITextField()
        field.keyboardType = .decimalPad
        field.textAlignment = .natural
        field.adjustsFontForContentSizeCategory = true
        field.setContentCompressionResistancePriority(.required, for: .vertical)
        field.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        field.addTarget(context.coordinator, action: #selector(Coordinator.changed(_:)), for: .editingChanged)
        let toolbar = UIToolbar()
        let done = UIBarButtonItem(title: String(localized: "Done"), style: .done,
            target: field, action: #selector(UIResponder.resignFirstResponder))
        done.accessibilityIdentifier = "boundary-review.keyboard-done"
        toolbar.items = [UIBarButtonItem(systemItem: .flexibleSpace), done]
        toolbar.sizeToFit()
        field.inputAccessoryView = toolbar
        return field
    }

    func updateUIView(_ field: UITextField, context: Context) {
        context.coordinator.text = $text
        if field.text != text { field.text = text }
        field.font = .preferredFont(forTextStyle: .body,
            compatibleWith: UITraitCollection(preferredContentSizeCategory: dynamicTypeSize.scannerContentSizeCategory))
        field.textColor = .label
        field.placeholder = nil
        field.accessibilityLabel = label
        field.accessibilityIdentifier = identifier
        field.invalidateIntrinsicContentSize()
    }

    func sizeThatFits(_ proposal: ProposedViewSize, uiView: UITextField, context: Context) -> CGSize? {
        CGSize(width: proposal.width ?? uiView.intrinsicContentSize.width,
            height: max(44, (uiView.font?.lineHeight ?? 20) + 8))
    }

    @MainActor
    final class Coordinator: NSObject {
        var text: Binding<String>
        init(text: Binding<String>) { self.text = text }
        @objc func changed(_ field: UITextField) { text.wrappedValue = field.text ?? "" }
    }
}

@MainActor
private struct ScannerBoundaryPicker: UIViewRepresentable {
    let snapshot: ScannerSceneSnapshot
    let edges: [ScannerBoundaryEdge]
    let selectedEdgeID: UUID?
    let select: (UUID) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(edges: edges, select: select) }
    func makeUIView(context: Context) -> SCNView {
        let view = SCNView()
        // Represent the preview as one image. Individual SceneKit nodes are
        // not text controls; the accessible edge list supplies their measures.
        view.isAccessibilityElement = true
        view.accessibilityTraits = .image
        view.accessibilityLabel = String(localized: "Captured boundaries")
        view.accessibilityHint = String(localized: "Rotate to inspect the scan. Select a boundary from the list below.")
        view.backgroundColor = UIColor(ScannerProductTheme.canvas)
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.addGestureRecognizer(UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tap(_:))))
        rebuild(view)
        return view
    }
    func updateUIView(_ view: SCNView, context: Context) {
        context.coordinator.edges = edges
        context.coordinator.select = select
        rebuild(view)
    }
    private func rebuild(_ view: SCNView) {
        let pose = view.pointOfView?.simdTransform
        let scene = ScannerMeshSceneBuilder.scene(from: snapshot)
        for node in scene.rootNode.childNodes where node.geometry != nil {
            node.geometry?.firstMaterial?.transparency = 0.24
        }
        for edge in edges {
            let start = SIMD3(Float(edge.start.x), Float(edge.start.y), Float(edge.start.z))
            let end = SIMD3(Float(edge.end.x), Float(edge.end.y), Float(edge.end.z)), delta = end - start
            let length = simd_length(delta)
            guard length.isFinite, length > 0 else { continue }
            let cylinder = SCNCylinder(radius: selectedEdgeID == edge.id ? 0.018 : 0.012, height: CGFloat(length))
            cylinder.firstMaterial?.diffuse.contents = UIColor(selectedEdgeID == edge.id ? ScannerProductTheme.warning : ScannerProductTheme.accent)
            let node = SCNNode(geometry: cylinder)
            node.name = "review-boundary:" + edge.id.uuidString
            node.simdPosition = (start + end) / 2
            node.simdOrientation = simd_quatf(from: SIMD3(0, 1, 0), to: delta / length)
            scene.rootNode.addChildNode(node)
            if selectedEdgeID == edge.id {
                for (name, point) in [("A", start), ("B", end)] {
                    let marker = SCNNode(geometry: SCNSphere(radius: 0.03))
                    marker.geometry?.firstMaterial?.diffuse.contents = UIColor(ScannerProductTheme.warning)
                    marker.simdPosition = point
                    let text = SCNText(string: name, extrusionDepth: 0)
                    text.font = UIFont.systemFont(ofSize: 8, weight: .bold)
                    text.firstMaterial?.diffuse.contents = UIColor.white
                    let label = SCNNode(geometry: text)
                    label.simdScale = SIMD3(repeating: 0.02)
                    label.simdPosition = point + SIMD3(0.04, 0.04, 0)
                    label.constraints = [SCNBillboardConstraint()]
                    scene.rootNode.addChildNode(marker)
                    scene.rootNode.addChildNode(label)
                }
            }
        }
        view.scene = scene
        let camera = scene.rootNode.childNodes.first { $0.camera != nil }
        if let pose { camera?.simdTransform = pose }
        view.pointOfView = camera
    }
    @MainActor final class Coordinator: NSObject {
        var edges: [ScannerBoundaryEdge]
        var select: (UUID) -> Void
        init(edges: [ScannerBoundaryEdge], select: @escaping (UUID) -> Void) { self.edges = edges; self.select = select }
        @objc func tap(_ gesture: UITapGestureRecognizer) {
            guard let view = gesture.view as? SCNView else { return }
            for hit in view.hitTest(gesture.location(in: view)) {
                guard let name = hit.node.name, name.hasPrefix("review-boundary:"),
                      let id = UUID(uuidString: String(name.dropFirst("review-boundary:".count))),
                      edges.contains(where: { $0.id == id }) else { continue }
                select(id)
                return
            }
        }
    }
}

@MainActor
private struct ScannerFloorReferenceSheet: View {
    let snapshot: ScannerSceneSnapshot
    let isCurrent: Bool
    let confirm: (simd_double4x4, Double) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var selectedVertices: [Int] = []
    @State private var confirmsPhysicalFloor = false
    @State private var cutHeight = 1.2

    private var proposal: ScannerFloorReferenceProposal? {
        ScannerFloorReferenceProposal.make(snapshot: snapshot, vertexIndices: selectedVertices)
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("Select three distinct points spread across the visible physical floor. Rotate the captured model to inspect it; do not select a table, shelf or ceiling.")
                    ScannerFloorVertexPicker(snapshot: snapshot, selectedVertices: selectedVertices) { index in
                        guard isCurrent, selectedVertices.count < 3, !selectedVertices.contains(index) else { return }
                        selectedVertices.append(index)
                        confirmsPhysicalFloor = false
                    }
                    .frame(height: 320).clipShape(RoundedRectangle(cornerRadius: 20))
                    .accessibilityLabel("Captured geometry for floor selection")
                    Text("\(selectedVertices.count) of 3 retained points selected").font(.headline)
                    Button("Clear selection") { selectedVertices = []; confirmsPhysicalFloor = false }
                        .buttonStyle(.bordered).disabled(selectedVertices.isEmpty)
                    if selectedVertices.count == 3 && proposal == nil {
                        Label("These points do not define a conditioned nonvertical plane. Clear the selection and choose more widely separated floor points.", systemImage: "exclamationmark.triangle")
                            .foregroundStyle(ScannerProductTheme.warning)
                    }
                    if !isCurrent {
                        Label("The accepted revision changed. Close this view and select the floor again.", systemImage: "exclamationmark.triangle")
                            .foregroundStyle(ScannerProductTheme.warning)
                    }
                    Stepper(value: $cutHeight, in: 0.1...100, step: 0.05) {
                        Text("Architectural cut: \(cutHeight.formatted(.number.precision(.fractionLength(2)))) m")
                    }
                    Toggle("I confirm these selected points lie on the physical floor", isOn: $confirmsPhysicalFloor)
                    Text("This changes only the plan's reference plane and cut height. Captured geometry is unchanged. Floor precision, room semantics and area remain unverified until supported separately.")
                        .font(.callout).foregroundStyle(.secondary)
                    Button("Apply reference") {
                        if let proposal, isCurrent, confirmsPhysicalFloor {
                            confirm(proposal.projectFromFloor, cutHeight)
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .foregroundStyle(ScannerProductTheme.canvas)
                    .disabled(proposal == nil || !isCurrent || !confirmsPhysicalFloor)
                    Text("Applied is not Saved. Save the updated draft after the native engine accepts this reference.")
                        .font(.caption).foregroundStyle(.secondary)
                }.padding(24).frame(maxWidth: 740).frame(maxWidth: .infinity)
            }
            .navigationTitle("Observed floor reference")
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }.tint(ScannerProductTheme.accent)
    }
}

@MainActor
private struct ScannerFloorVertexPicker: UIViewRepresentable {
    let snapshot: ScannerSceneSnapshot
    let selectedVertices: [Int]
    let select: (Int) -> Void

    func makeCoordinator() -> Coordinator { Coordinator(snapshot: snapshot, select: select) }

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView()
        view.backgroundColor = UIColor(ScannerProductTheme.canvas)
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.addGestureRecognizer(UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tap(_:))))
        rebuild(view)
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        context.coordinator.snapshot = snapshot
        context.coordinator.select = select
        rebuild(view)
    }

    private func rebuild(_ view: SCNView) {
        let pose = view.pointOfView?.simdTransform
        let scene = ScannerMeshSceneBuilder.scene(from: snapshot)
        let mesh = scene.rootNode.childNodes.first { $0.geometry != nil }
        mesh?.name = "retained-floor-selection-mesh"
        for index in selectedVertices where snapshot.verticesMetres.indices.contains(index) {
            let point = snapshot.verticesMetres[index]
            let marker = SCNNode(geometry: SCNSphere(radius: 0.025))
            marker.name = "selection-marker-not-metric-geometry"
            marker.geometry?.firstMaterial?.diffuse.contents = UIColor(ScannerProductTheme.warning)
            marker.position = SCNVector3(Float(point.x), Float(point.y), Float(point.z))
            scene.rootNode.addChildNode(marker)
        }
        view.scene = scene
        let camera = scene.rootNode.childNodes.first { $0.camera != nil }
        if let pose { camera?.simdTransform = pose }
        view.pointOfView = camera
    }

    @MainActor final class Coordinator: NSObject {
        var snapshot: ScannerSceneSnapshot
        var select: (Int) -> Void
        init(snapshot: ScannerSceneSnapshot, select: @escaping (Int) -> Void) {
            self.snapshot = snapshot; self.select = select
        }
        @objc func tap(_ gesture: UITapGestureRecognizer) {
            guard let view = gesture.view as? SCNView,
                  let hit = view.hitTest(gesture.location(in: view)).first(where: { $0.node.name == "retained-floor-selection-mesh" }) else { return }
            let point = SIMD3(Double(hit.localCoordinates.x), Double(hit.localCoordinates.y), Double(hit.localCoordinates.z))
            if let index = ScannerFloorReferenceProposal.nearestVisibleVertex(snapshot: snapshot, faceIndex: hit.faceIndex, localPoint: point) { select(index) }
        }
    }
}

enum ScannerMeshSceneBuilder {
    static func scene(
        from snapshot: ScannerSceneSnapshot,
        admittedAppearanceBinding: AppearanceAdmittedBinding? = nil,
        appearanceTextures: [AppearanceTextureResource] = [],
        texturedMesh: ScannerTexturedMesh? = nil
    ) -> SCNScene {
        let scene = SCNScene()
        guard let geometry = geometry(
            from: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures,
            texturedMesh: texturedMesh
        ) else { return scene }
        let node = SCNNode(geometry: geometry)
        scene.rootNode.addChildNode(node)

        let (minimum, maximum) = node.boundingBox
        let center = SCNVector3(
            (minimum.x + maximum.x) / 2,
            (minimum.y + maximum.y) / 2,
            (minimum.z + maximum.z) / 2
        )
        let extent = max(maximum.x - minimum.x, max(maximum.y - minimum.y, maximum.z - minimum.z))
        let cameraNode = SCNNode()
        cameraNode.camera = SCNCamera()
        cameraNode.position = SCNVector3(center.x, center.y + max(extent, 0.5) * 0.65, center.z + max(extent, 0.5) * 1.7)
        cameraNode.look(at: center)
        scene.rootNode.addChildNode(cameraNode)
        return scene
    }

    static func displayTriangleIndices(from snapshot: ScannerSceneSnapshot) -> [UInt32]? {
        guard snapshot.triangleIndices.count.isMultiple(of: 3) else { return nil }
        if snapshot.reviewTriangleInclusion.isEmpty { return snapshot.triangleIndices }
        guard snapshot.reviewTriangleInclusion.count == snapshot.triangleIndices.count / 3,
              snapshot.reviewTriangleInclusion.allSatisfy({ $0 == 0 || $0 == 1 }) else { return nil }
        var result: [UInt32] = []
        result.reserveCapacity(snapshot.triangleIndices.count)
        for triangle in snapshot.reviewTriangleInclusion.indices where snapshot.reviewTriangleInclusion[triangle] != 0 {
            let start = triangle * 3
            result.append(contentsOf: snapshot.triangleIndices[start..<(start + 3)])
        }
        return result
    }

    private static func geometry(
        from snapshot: ScannerSceneSnapshot,
        admittedAppearanceBinding: AppearanceAdmittedBinding?,
        appearanceTextures: [AppearanceTextureResource],
        texturedMesh: ScannerTexturedMesh?
    ) -> SCNGeometry? {
        guard let displayIndices = displayTriangleIndices(from: snapshot) else { return nil }
        guard !snapshot.verticesMetres.isEmpty,
              snapshot.verticesMetres.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite }),
              !displayIndices.isEmpty,
              displayIndices.allSatisfy({ Int($0) < snapshot.verticesMetres.count }) else {
            return nil
        }
        if let layer = texturedMesh, snapshot.isFrozen, layer.matches(snapshot),
           let colored = texturedGeometry(snapshot: snapshot, layer: layer) { return colored }
        let vertices = snapshot.verticesMetres.map { SCNVector3(Float($0.x), Float($0.y), Float($0.z)) }
        let source = SCNGeometrySource(vertices: vertices)
        let indexData = displayIndices.withUnsafeBufferPointer { Data(buffer: $0) }
        let element = SCNGeometryElement(
            data: indexData,
            primitiveType: .triangles,
            primitiveCount: displayIndices.count / 3,
            bytesPerIndex: MemoryLayout<UInt32>.size
        )
        let geometry = SCNGeometry(sources: [source], elements: [element])
        geometry.materials = [reviewMaterial(
            snapshot: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures
        )]
        return geometry
    }

    /// UV seams duplicate vertices only in the disposable renderer buffer.
    /// Every displayed position is copied from an exact accepted raw index;
    /// the source snapshot, masks and geometry exports remain immutable.
    private static func texturedGeometry(snapshot: ScannerSceneSnapshot, layer: ScannerTexturedMesh) -> SCNGeometry? {
        guard snapshot.triangleIndices.count / 3 <= 65_536, layer.faces.count <= 16_384,
              layer.images.count <= 24, Set(layer.faces.map(\.faceIndex)).count == layer.faces.count,
              layer.faces.allSatisfy({ $0.faceIndex >= 0 && $0.faceIndex < snapshot.triangleIndices.count / 3 &&
                  $0.uv.count == 3 && $0.uv.allSatisfy { $0.count == 2 && $0.allSatisfy { $0.isFinite && (0...1).contains($0) } } }) else { return nil }
        var materials = [SCNMaterial()]
        materials[0].diffuse.contents = UIColor.secondaryLabel
        materials[0].isDoubleSided = true
        var materialIndices: [String: Int] = [:]
        for id in layer.images.keys.sorted() {
            guard let bytes = layer.images[id],
                  ScannerAppearanceStore.jpegDimensions(bytes) != nil,
                  let image = UIImage(data: bytes), image.size.width <= 960, image.size.height <= 960 else { return nil }
            let material = SCNMaterial()
            material.diffuse.contents = image
            material.lightingModel = .constant
            material.isDoubleSided = true
            material.diffuse.wrapS = .clamp
            material.diffuse.wrapT = .clamp
            materialIndices[id] = materials.count
            materials.append(material)
        }
        let faces = Dictionary(uniqueKeysWithValues: layer.faces.map { ($0.faceIndex, $0) })
        var vertices: [SCNVector3] = []
        var uv: [CGPoint] = []
        var groups = [[UInt32]](repeating: [], count: materials.count)
        for f in 0..<(snapshot.triangleIndices.count / 3) {
            if !snapshot.reviewTriangleInclusion.isEmpty && snapshot.reviewTriangleInclusion[f] == 0 { continue }
            let face = faces[f]
            let materialIndex = face.flatMap { materialIndices[$0.assetID] } ?? 0
            for corner in 0..<3 {
                let sourceIndex = snapshot.triangleIndices[f * 3 + corner]
                let position = snapshot.verticesMetres[Int(sourceIndex)]
                groups[materialIndex].append(UInt32(vertices.count))
                vertices.append(SCNVector3(Float(position.x), Float(position.y), Float(position.z)))
                uv.append(materialIndex == 0 ? .zero : CGPoint(x: CGFloat(face!.uv[corner][0]), y: CGFloat(face!.uv[corner][1])))
            }
        }
        let active = groups.indices.filter { !groups[$0].isEmpty }
        guard !active.isEmpty else { return nil }
        let elements = active.map { group in
            let indices = groups[group]
            let bytes = indices.withUnsafeBufferPointer { Data(buffer: $0) }
            return SCNGeometryElement(data: bytes, primitiveType: .triangles,
                                      primitiveCount: indices.count / 3, bytesPerIndex: MemoryLayout<UInt32>.size)
        }
        let geometry = SCNGeometry(sources: [SCNGeometrySource(vertices: vertices), SCNGeometrySource(textureCoordinates: uv)], elements: elements)
        geometry.materials = active.map { materials[$0] }
        return geometry
    }

    private static func reviewMaterial(
        snapshot: ScannerSceneSnapshot,
        admittedAppearanceBinding: AppearanceAdmittedBinding?,
        appearanceTextures: [AppearanceTextureResource]
    ) -> SCNMaterial {
        let material = SCNMaterial()
        material.diffuse.contents = UIColor(ScannerProductTheme.accent.opacity(0.75))
        material.fillMode = .lines
        material.isDoubleSided = true
        guard let identity = AppearanceMetricSnapshotIdentity(
            topologyToken: "snapshot-\(snapshot.revision)-\(snapshot.verticesMetres.count)-\(snapshot.triangleIndices.count / 3)",
            sceneRevision: snapshot.revision
        ), let target = AppearanceRenderTarget(entityID: "captured-mesh", metricSnapshot: identity) else {
            return material
        }
        let presentation = AppearancePresentationAdapter.present(
            target: target,
            admittedBinding: admittedAppearanceBinding,
            textures: appearanceTextures
        )
        guard case .present(let admitted) = presentation,
              case .textured(let rendererTextureID) = admitted.material,
              let texture = UIImage(named: rendererTextureID) else {
            return material
        }
        material.diffuse.contents = texture
        material.fillMode = .fill
        return material
    }
}

/// Read-only assessment of a single accepted snapshot. Native issue IDs and
/// revision bindings survive presentation; source-lineage records are not
/// misrepresented as user-facing quality failures. No automatic repair runs.
struct ScannerSceneAssessment: Equatable {
    let hasCapturedGeometry: Bool
    let geometryDescription: String
    let referenceDescription: String
    let boundaryDescription: String
    let issues: [ScannerWorkspacePresentation.Issue]

    private struct Document: Decodable {
        let scene_revision: String
        let source_is_synthetic: Bool
        let units: String
        let issues: [NativeIssue]
    }
    private struct NativeIssue: Decodable {
        let id: UUID
        let code: String
        let scene_revision: String
        let severity: String
        let reason: String
        let action: String
        let state: String
    }

    static func decode(_ snapshot: ScannerSceneSnapshot) -> Self {
        let document = try? JSONDecoder().decode(Document.self, from: snapshot.sceneJSON)
        let isBound = document?.scene_revision == String(snapshot.revision) && document?.units == "m"
            && document?.issues.allSatisfy({
                ["info", "warning", "critical"].contains($0.severity)
                    && ["active", "suppressed", "resolved"].contains($0.state)
                    && !$0.code.isEmpty && !$0.reason.isEmpty && !$0.action.isEmpty
            }) == true
        // Synthetic source must never be marketed as a captured-room result,
        // even when a separate Swift provenance set omitted the test marker.
        let synthetic = document?.source_is_synthetic == true
            || snapshot.provenance.contains(.syntheticTest)
            || snapshot.planAnnotation?.isSynthetic == true
        let hasMesh = isBound && !synthetic && !snapshot.verticesMetres.isEmpty
            && snapshot.triangleIndices.count >= 3
        var visible: [ScannerWorkspacePresentation.Issue] = []
        if isBound, let document {
            var seen: Set<UUID> = []
            for issue in document.issues {
                guard issue.state == "active", issue.scene_revision == String(snapshot.revision),
                      issue.code != "mesh_source", seen.insert(issue.id).inserted else { continue }
                let priority: ScannerWorkspacePresentation.Issue.Priority
                if issue.severity == "critical" || issue.code == "unresolved_epoch" {
                    priority = .blocking
                } else if issue.severity == "warning" {
                    priority = .important
                } else {
                    priority = .notice
                }
                // Actions remain explanatory copy, not executable external
                // instructions or a claim that an unwired correction exists.
                visible.append(.init(
                    id: issue.id, title: title(for: issue.code),
                    detail: String(issue.reason.prefix(2048)) + "\n" + String(issue.action.prefix(512)),
                    priority: priority, recoveryTitle: nil,
                    nativeCode: issue.code, nativeSceneRevision: snapshot.revision,
                    guidance: guidance(for: issue.code)
                ))
            }
        } else {
            visible.append(.init(
                id: UUID(uuidString: "00000000-0000-4000-8000-000000000012")!,
                title: String(localized: "Quality report unavailable"),
                detail: String(localized: "The quality report does not match this accepted revision. Do not interpret missing warnings as a complete scan."),
                priority: .blocking, recoveryTitle: nil
            ))
        }
        visible.sort {
            $0.priority.rank == $1.priority.rank
                ? $0.id.uuidString < $1.id.uuidString
                : $0.priority.rank < $1.priority.rank
        }
        let plan = snapshot.planAnnotation.flatMap { $0.sceneRevision == snapshot.revision ? $0 : nil }
        let reference = plan?.referenceState == UInt32(HSS_PLAN_REFERENCE_CONFIRMED_FLOOR)
            ? String(localized: "Confirmed floor reference")
            : String(localized: "Floor reference required — area unavailable")
        let boundary: String
        if plan?.boundaryState == UInt32(HSS_PLAN_BOUNDARY_CLOSED_ACCEPTED) {
            boundary = String(localized: "Closed section candidate — room semantics unverified")
        } else if plan?.boundaryState == UInt32(HSS_PLAN_BOUNDARY_OPEN) {
            boundary = String(localized: "Open section — missing support is not filled")
        } else {
            boundary = String(localized: "Boundary unavailable or unresolved")
        }
        return Self(
            hasCapturedGeometry: hasMesh,
            geometryDescription: synthetic ? String(localized: "Reference geometry — not a captured room")
                : (hasMesh ? String(localized: "Surface mesh retained independently of the plan")
                           : String(localized: "No captured surface mesh in this revision")),
            referenceDescription: reference, boundaryDescription: boundary, issues: visible
        )
    }

    private static func guidance(for code: String) -> String? {
        switch code {
        case "floor_reference_unknown":
            String(localized: "Select three spread-out points on the observed floor and confirm the surface. A starting camera position is not a floor reference. Area remains unavailable until room interpretation is valid.")
        case "unresolved_epoch":
            String(localized: "This capture has an unresolved coordinate relationship. Keep it as evidence; do not combine dimensions across unrelated capture frames.")
        case "provisional_capture":
            String(localized: "Use these values as estimates only. Sensor calibration, an independent reference and physical validation are still required for certified dimensions.")
        case "open_or_ambiguous_slice":
            String(localized: "Review the unsupported boundary and capture it from another useful angle. Missing geometry is not filled, and an open section is not a room-area result.")
        case "unclassified_slice_loops", "plan_semantics_unavailable":
            String(localized: "Retained surfaces can be inspected in 3D, but separate boundaries are not yet a verified room interpretation. Do not treat them as finished-room area.")
        case "partial_plane_support":
            String(localized: "Only part of this surface supports the fit. Review the observed extent before relying on the estimate; a fitted plane does not extend the captured surface.")
        case "HSS_CORRECTION_LINEAGE_UNRESOLVED", "HSS_TARGET_LINEAGE_UNRESOLVED":
            String(localized: "The scan changed after a previous correction. Review the updated boundary and confirm the intended measurement or protection again. The original correction remains in your project history.")
        default: nil
        }
    }

    private static func title(for code: String) -> String {
        switch code {
        case "unresolved_epoch": String(localized: "Coordinate relationship unresolved")
        case "floor_reference_unknown": String(localized: "Floor reference required")
        case "provisional_capture": String(localized: "Measurement estimates, not certified dimensions")
        case "open_or_ambiguous_slice": String(localized: "Boundary needs another view")
        case "unclassified_slice_loops": String(localized: "Separate boundaries need classification")
        case "metric_work_limit": String(localized: "Plan processing limit reached")
        case "partial_plane_support": String(localized: "Surface fit has incomplete support")
        case "non_manifold_mesh", "ambiguous_mesh_component", "degenerate_mesh_triangle": String(localized: "Captured topology needs review")
        case "plan_semantics_unavailable": String(localized: "Plan interpretation unavailable")
        case "HSS_CORRECTION_LINEAGE_UNRESOLVED", "HSS_TARGET_LINEAGE_UNRESOLVED": String(localized: "Review previous correction")
        default: String(localized: "Engine review: \(code)")
        }
    }
}

/// Closed allowlist of implemented native actions. Native reason/action text is
/// retained diagnostic data, never parsed as a command or an executable route.
enum ScannerQualityActions {
    static let saveFailureID = UUID(uuidString: "00000000-0000-4000-8000-000000000013")!
    static let referenceSceneID = UUID(uuidString: "00000000-0000-4000-8000-000000000014")!

    static func driverPriority(
        phase: ScannerWorkspacePhase, recoveryAction: ScannerWorkspaceRecoveryAction?
    ) -> ScannerWorkspacePresentation.Issue.Priority {
        if case .failed = phase { return .blocking }
        return recoveryAction == nil ? .notice : .important
    }

    static func offersFloorReference(
        _ issue: ScannerWorkspacePresentation.Issue, assessment: ScannerSceneAssessment,
        revision: UInt64, canConfirm: Bool
    ) -> Bool {
        canConfirm && assessment.hasCapturedGeometry
            && issue.nativeCode == "floor_reference_unknown"
            && issue.nativeSceneRevision == revision
            && assessment.issues.contains {
                $0.id == issue.id && $0.nativeCode == issue.nativeCode
                    && $0.nativeSceneRevision == revision
            }
    }
}

enum ScannerAcceptedPlanDecoder {
    private struct PlanDocument: Decodable {
        let sceneRevision: String
        let sourceIsSynthetic: Bool
        let boundaryState: String
        let validity: String
        let height: Height
        let fragments: [Fragment]
        let area: Area
        enum CodingKeys: String, CodingKey {
            case sceneRevision = "scene_revision"
            case sourceIsSynthetic = "source_is_synthetic"
            case boundaryState = "boundary_state"
            case validity, height, fragments, area
        }
    }
    private struct Height: Decodable {
        let referenceState: String
        enum CodingKeys: String, CodingKey { case referenceState = "reference_state" }
    }
    private struct Fragment: Decodable {
        let id: String
        let state: String
        let geometryOrigin: String
        let segment: [[Double]]?
        let sourceRevision: String
        enum CodingKeys: String, CodingKey {
            case id, state
            case geometryOrigin = "geometry_origin"
            case segment = "segment_m"
            case sourceRevision = "source_revision"
        }
    }
    private struct Area: Decodable {
        let availability: String
        let areaMetresSquared: Double?
        enum CodingKeys: String, CodingKey {
            case availability
            case areaMetresSquared = "area_m2"
        }
    }
    private struct AnnotationDocument: Decodable {
        let sceneRevision: String
        let annotations: [Annotation]
        enum CodingKeys: String, CodingKey {
            case sceneRevision = "scene_revision"
            case annotations
        }
    }
    private struct Annotation: Decodable {
        let id: String
        let kind: String
        let evidenceSource: String
        let sourceRevision: String
        let anchors: [Anchor]
        let availability: String
        let metricValue: Double?
        enum CodingKeys: String, CodingKey {
            case id, kind, anchors, availability
            case evidenceSource = "evidence_source"
            case sourceRevision = "source_revision"
            case metricValue = "metric_value"
        }
    }
    private struct Anchor: Decodable {
        let geometryID: String
        let position: [Double]
        enum CodingKeys: String, CodingKey {
            case geometryID = "geometry_id"
            case position = "position_m"
        }
    }
    struct ReviewData {
        let plan: ScannerWorkspacePresentation.Plan
        let measurements: [ScannerWorkspacePresentation.Measurement]
        let provenanceFields: [ProvenanceFieldPresentation]
        let isSynthetic: Bool
    }

    static func decode(
        _ snapshot: ScannerSceneSnapshot,
        language: ProvenancePresentationLanguage = .english
    ) -> ReviewData? {
        guard let semantic = snapshot.planAnnotation,
              semantic.sceneRevision == snapshot.revision,
              semantic.planAvailable,
              let planBytes = semantic.planJSON,
              let plan = try? JSONDecoder().decode(PlanDocument.self, from: planBytes),
              plan.sceneRevision == String(snapshot.revision),
              semantic.isSynthetic == plan.sourceIsSynthetic else { return nil }
        let annotations: [Annotation]
        if semantic.annotationLayoutAvailable,
           let data = semantic.annotationJSON,
           let document = try? JSONDecoder().decode(AnnotationDocument.self, from: data),
           document.sceneRevision == plan.sceneRevision {
            annotations = document.annotations
        } else {
            annotations = []
        }
        let segments: [(Fragment, CGPoint, CGPoint)] = plan.fragments.compactMap { fragment in
            guard fragment.sourceRevision == plan.sceneRevision,
                  let segment = fragment.segment, segment.count == 2,
                  segment[0].count == 2, segment[1].count == 2,
                  segment.flatMap({ $0 }).allSatisfy(\.isFinite) else { return nil }
            return (fragment, CGPoint(x: segment[0][0], y: segment[0][1]),
                    CGPoint(x: segment[1][0], y: segment[1][1]))
        }
        let isMeasured: (Fragment) -> Bool = {
            $0.state == "supported" && ["observed", "fitted", "user_measured"].contains($0.geometryOrigin)
        }
        let lines = segments.filter { isMeasured($0.0) }.map { [$0.1, $0.2] }
        let unknownLines = segments.filter { !isMeasured($0.0) }.map { [$0.1, $0.2] }
        let points = segments.flatMap { [$0.1, $0.2] }
        let bounds = points.dropFirst().reduce(points.first.map { CGRect(origin: $0, size: .zero) }) { partial, point in
            partial?.union(CGRect(origin: point, size: .zero))
        }
        var boundFragments: [String: Fragment] = [:]
        for (fragment, _, _) in segments {
            guard boundFragments.updateValue(fragment, forKey: fragment.id) == nil else {
                return nil
            }
        }
        var measurements: [ScannerWorkspacePresentation.Measurement] = []
        var dimensions: [ScannerWorkspacePresentation.DimensionAnnotation] = []
        for (index, annotation) in annotations.enumerated() {
            guard (plan.validity == "ready" || plan.validity == "provisional"),
                  (semantic.planAvailability == HSS_PLAN_AVAILABILITY_READY ||
                   semantic.planAvailability == HSS_PLAN_AVAILABILITY_PROVISIONAL),
                  annotation.kind == "length", annotation.sourceRevision == plan.sceneRevision,
                  annotation.anchors.count == 2,
                  let fragment = boundFragments[annotation.anchors[0].geometryID],
                  annotation.anchors[1].geometryID == fragment.id,
                  isMeasured(fragment),
                  fragment.geometryOrigin == annotation.evidenceSource,
                  let value = annotation.metricValue, value.isFinite, value > 0,
                  annotation.availability != "unavailable",
                  annotation.anchors.allSatisfy({ $0.position.count == 2 && $0.position.allSatisfy(\.isFinite) }),
                  let id = UUID(uuidString: annotation.id) else { continue }
            let evidence = evidence(for: annotation.evidenceSource)
            let start = CGPoint(x: annotation.anchors[0].position[0], y: annotation.anchors[0].position[1])
            let end = CGPoint(x: annotation.anchors[1].position[0], y: annotation.anchors[1].position[1])
            let valueText = lengthMetres(value)
            measurements.append(.init(
                id: id,
                label: localized("Boundary \(index + 1)", "حدّ \(index + 1)", language),
                value: valueText,
                evidence: evidence,
                detail: localized("Length bound to accepted revision \(plan.sceneRevision)",
                                  "طول مرتبط بالمراجعة المقبولة \(plan.sceneRevision)", language)
            ))
            dimensions.append(.init(id: id, startMetres: start, endMetres: end,
                                    label: valueText, evidence: evidence))
        }
        let areaKnown = semantic.areaAvailability == HSS_PLAN_AVAILABILITY_READY
            && plan.area.availability == "ready"
            && plan.validity == "ready"
            && semantic.areaMetresSquared != nil
            && semantic.areaMetresSquared == plan.area.areaMetresSquared
        let areaDetail: String
        if areaKnown {
            areaDetail = localized("Area from accepted semantic plan", "المساحة من المخطط الدلالي المقبول", language)
        } else if plan.height.referenceState == "floor_unknown" {
            areaDetail = localized("Floor reference unknown; section projection is provisional. Area unavailable.",
                                   "مرجع الأرضية غير معروف؛ إسقاط المقطع أولي. المساحة غير متاحة.", language)
        } else if plan.boundaryState != "closed_accepted" {
            areaDetail = localized("Boundary is open or unresolved. Area unavailable.",
                                   "الحدّ مفتوح أو غير محسوم. المساحة غير متاحة.", language)
        } else {
            areaDetail = localized("Architectural cut is provisional; finished floor area is unverified.",
                                   "المقطع المعماري أولي؛ مساحة الأرضية النهائية غير متحققة.", language)
        }
        measurements.append(.init(
            id: UUID(uuidString: "00000000-0000-4000-8000-000000000031")!,
            label: localized("Architectural cut area", "مساحة المقطع المعماري", language),
            value: areaKnown ? areaMetresSquared(semantic.areaMetresSquared!) : nil,
            evidence: .unknown,
            detail: areaDetail
        ))
        return ReviewData(
            plan: .init(polylinesMetres: lines, unknownPolylinesMetres: unknownLines,
                        dimensionAnnotations: dimensions,
                        boundsMetres: bounds?.insetBy(dx: -0.05, dy: -0.05)),
            measurements: measurements,
            provenanceFields: measurements.map {
                ProvenanceFieldPresentation(
                    id: $0.id,
                    label: ProvenanceCopy(english: "Accepted plan field", arabic: "حقل مخطط مقبول"),
                    formattedValue: $0.value.map { ProvenanceCopy(english: $0, arabic: $0) },
                    kind: provenanceKind(for: $0.evidence)
                )
            },
            isSynthetic: plan.sourceIsSynthetic
        )
    }

    private static func localized(_ english: String, _ arabic: String,
                                  _ language: ProvenancePresentationLanguage) -> String {
        language == .arabic ? arabic : english
    }
    private static func evidence(for source: String) -> ScannerWorkspacePresentation.Measurement.Evidence {
        switch source {
        case "observed": .observed
        case "fitted": .fitted
        case "user_measured": .userMeasured
        case "inferred": .inferred
        default: .unknown
        }
    }
    private static func provenanceKind(
        for evidence: ScannerWorkspacePresentation.Measurement.Evidence
    ) -> ProvenanceKind {
        switch evidence {
        case .observed: .observed
        case .fitted: .fitted
        case .userMeasured: .userMeasured
        case .inferred: .inferred
        case .unknown: .unknown
        }
    }
    private static func lengthMetres(_ value: Double) -> String {
        Measurement(value: value, unit: UnitLength.meters).formatted(
            .measurement(width: .abbreviated, usage: .asProvided,
                         numberFormatStyle: .number.precision(.fractionLength(0...2))))
    }
    private static func areaMetresSquared(_ value: Double) -> String {
        Measurement(value: value, unit: UnitArea.squareMeters).formatted(
            .measurement(width: .abbreviated, usage: .asProvided,
                         numberFormatStyle: .number.precision(.fractionLength(0...2))))
    }
}

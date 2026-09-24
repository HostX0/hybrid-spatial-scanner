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
    @State private var completedObjectArtifact: ObjectCaptureArtifact?
    @State private var currentCameraViewport: OverlayViewport?
    @State private var projectArchiveToShare: URL?

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
            },
            sceneContent: {
                if let snapshot = workspace.latestSnapshot {
                    ScannerSnapshotMeshPreview(
                        snapshot: snapshot,
                        admittedAppearanceBinding: workspace.reviewAppearanceBinding,
                        appearanceTextures: workspace.reviewAppearanceTextures
                    )
                }
            }
        )
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
        .sheet(isPresented: $isObjectCapturePresented) {
            // This is deliberately a separate RealityKit-owned workflow. It is
            // offered only after room capture has stopped and never enters the
            // workspace's metric scene or project save path.
            ObjectCaptureWorkflowView { artifact in
                completedObjectArtifact = artifact
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
        .task { workspace.reloadProjects() }
    }

    private var presentation: ScannerWorkspacePresentation {
        let selectedProject = workspace.selectedProjectID.flatMap { selectedID in
            workspace.localProjects.first(where: { $0.id == selectedID })
        }
        let reviewData = workspace.latestSnapshot.flatMap { ScannerAcceptedPlanDecoder.decode($0, language: locale.language.languageCode?.identifier == "ar" ? .arabic : .english) }
        let isReferenceScene = reviewData?.isSynthetic == true
            || workspace.latestSnapshot?.provenance.contains(.syntheticTest) == true
        return ScannerWorkspacePresentation(
            mode: mode,
            captureState: captureState,
            statusDetail: statusDetail,
            project: selectedProject.map(projectPresentation),
            projects: workspace.localProjects.map(projectPresentation),
            issues: issues,
            measurements: reviewData?.measurements ?? [],
            provenanceFields: reviewData?.provenanceFields ?? [],
            provenanceLanguage: locale.language.languageCode?.identifier == "ar" ? .arabic : .english,
            targetGuidance: targetGuidance,
            plan: reviewData?.plan,
            hasCapturedScene: reviewData != nil && !isReferenceScene,
            isReferenceScene: isReferenceScene,
            objectDetailDescription: completedObjectArtifact.map { _ in
                String(localized: "A detailed object model was saved separately on this device.")
            },
            exportStatus: exportStatus,
            projectArchiveExportStatus: projectArchiveExportStatus,
            canStartCapture: workspace.phase == .ready || workspace.phase == .browsing,
            canFinishCapture: isCaptureActive,
            canAssist: false,
            canUndo: false,
            canSave: workspace.saveState == .hasUnsavedChanges,
            canExport: workspace.canExport,
            isExporting: workspace.exportState == .exporting,
            canExportProjectArchive: workspace.canExportProjectArchive,
            isExportingProjectArchive: workspace.projectArchiveExportState == .exporting,
            canOpenLibrary: workspace.canShowLibrary,
            canCaptureObject: canCaptureObject
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
            assist: {},
            undo: {},
            save: workspace.saveSelectedProject,
            export: workspace.exportSelectedProject,
            exportProjectArchive: workspace.exportSelectedProjectArchive,
            openLibrary: workspace.showLibrary,
            openProject: workspace.openProject,
            captureObject: { isObjectCapturePresented = true },
            recover: { _ in workspace.recoverFromCurrentIssue() }
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
        switch workspace.exportState {
        case .idle:
            nil
        case .exporting:
            String(localized: "Creating PDF, SVG, DXF, OBJ, MTL, and provenance files locally.")
        case .exported:
            String(localized: "Offline export saved inside this local project.")
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

    private var issues: [ScannerWorkspacePresentation.Issue] {
        var visible: [ScannerWorkspacePresentation.Issue] = []
        if let issue = workspace.currentIssue {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: issue.id,
                    title: issue.title,
                    detail: issue.message,
                    priority: .notice,
                    recoveryTitle: recoveryTitle(for: issue.recoveryAction)
                )
            )
        }
        if case .failed(let message) = workspace.saveState, workspace.currentIssue == nil {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: UUID(),
                    title: String(localized: "Local save needs attention"),
                    detail: message,
                    priority: .important,
                    recoveryTitle: String(localized: "Try again")
                )
            )
        }
        if workspace.latestSnapshot?.provenance.contains(.syntheticTest) == true
            || workspace.latestSnapshot.flatMap({ ScannerAcceptedPlanDecoder.decode($0, language: locale.language.languageCode?.identifier == "ar" ? .arabic : .english) })?.isSynthetic == true {
            visible.append(
                ScannerWorkspacePresentation.Issue(
                    id: UUID(),
                    title: String(localized: "Reference scene"),
                    detail: String(localized: "This local reference data is not a captured room scan."),
                    priority: .notice,
                    recoveryTitle: nil
                )
            )
        }
        return visible
    }

    private func projectPresentation(_ project: ScannerProjectSummary) -> ScannerWorkspacePresentation.Project {
        ScannerWorkspacePresentation.Project(
            id: project.id,
            name: project.name,
            revision: String(project.sceneRevision),
            savedDescription: project.updatedAt.formatted(date: .abbreviated, time: .shortened),
            quality: project.state == .draft ? .draft : .ready
        )
    }

    private func recoveryTitle(for action: ScannerWorkspaceRecoveryAction?) -> String? {
        switch action {
        case .retrySave: String(localized: "Try again")
        case .reopenProject: String(localized: "Reopen project")
        case .returnToProjectList: String(localized: "View projects")
        case .stopCaptureAndSaveDraft: String(localized: "Save draft")
        case nil: nil
        }
    }

    private var canCaptureObject: Bool {
        switch workspace.phase {
        case .frozen, .saving, .openingProject:
            true
        case .browsing, .preparingProject, .ready, .startingCapture, .capturing,
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
        UIActivityViewController(activityItems: [archiveURL], applicationActivities: nil)
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
                } footer: {
                    Text("A local project is created before capture so its source data and accepted revisions stay together.")
                }
            }
            .navigationTitle("New space")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel", action: cancel)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Create", action: create)
                        .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
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

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView(frame: .zero)
        view.backgroundColor = UIColor(ScannerProductTheme.surface)
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.scene = ScannerMeshSceneBuilder.scene(
            from: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures
        )
        view.accessibilityLabel = "3D geometry from accepted snapshot"
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        view.scene = ScannerMeshSceneBuilder.scene(
            from: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures
        )
    }
}

enum ScannerMeshSceneBuilder {
    static func scene(
        from snapshot: ScannerSceneSnapshot,
        admittedAppearanceBinding: AppearanceAdmittedBinding? = nil,
        appearanceTextures: [AppearanceTextureResource] = []
    ) -> SCNScene {
        let scene = SCNScene()
        guard let geometry = geometry(
            from: snapshot,
            admittedAppearanceBinding: admittedAppearanceBinding,
            appearanceTextures: appearanceTextures
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
        appearanceTextures: [AppearanceTextureResource]
    ) -> SCNGeometry? {
        guard let displayIndices = displayTriangleIndices(from: snapshot) else { return nil }
        guard !snapshot.verticesMetres.isEmpty,
              snapshot.verticesMetres.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite }),
              !displayIndices.isEmpty,
              displayIndices.allSatisfy({ Int($0) < snapshot.verticesMetres.count }) else {
            return nil
        }
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

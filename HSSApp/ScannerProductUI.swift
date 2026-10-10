import SwiftUI
import UIKit

/// Render-only UI state. The workspace owner supplies this from accepted
/// capture/project state; this layer never estimates geometry or completion.
struct ScannerWorkspacePresentation: Equatable {
    enum Mode: Equatable {
        case readiness
        case scanning
        case review
        case library
    }

    enum CaptureState: Equatable {
        case notStarted
        case starting
        case trackingNormal
        case trackingLimited(String)
        case interrupted
        case unavailable(String)

        var title: LocalizedStringKey {
            switch self {
            case .notStarted: "Ready when you are"
            case .starting: "Starting camera"
            case .trackingNormal: "Observing space"
            case .trackingLimited: "Precise capture paused"
            case .interrupted: "Capture paused"
            case .unavailable: "Capture unavailable"
            }
        }
    }

    struct Project: Equatable, Identifiable {
        let id: UUID
        let name: String
        let revision: String?
        let savedDescription: String?
        let quality: Quality
        var canRestore: Bool = true

        enum Quality: Equatable {
            case draft
            case ready
            case needsAttention
        }
    }

    struct Issue: Equatable, Identifiable {
        enum Priority: Equatable {
            case blocking
            case important
            case optional
            case notice

            var rank: Int {
                switch self {
                case .blocking: 0
                case .important: 1
                case .optional: 2
                case .notice: 3
                }
            }
        }

        let id: UUID
        let title: String
        let detail: String
        let priority: Priority
        let recoveryTitle: String?
    }

    struct Measurement: Equatable, Identifiable {
        enum Evidence: Equatable {
            case observed
            case fitted
            case userMeasured
            case inferred
            case unknown
        }

        let id: UUID
        let label: String
        let value: String?
        let evidence: Evidence
        let detail: String?
    }

    struct Plan: Equatable {
        /// Metre-space points copied from an accepted snapshot. This is only a
        /// display payload; no screen-space edit is ever written back.
        let polylinesMetres: [[CGPoint]]
        let unknownPolylinesMetres: [[CGPoint]]
        let dimensionAnnotations: [DimensionAnnotation]
        let boundsMetres: CGRect?
    }

    struct DimensionAnnotation: Equatable, Identifiable {
        let id: UUID
        let startMetres: CGPoint
        let endMetres: CGPoint
        let label: String
        let evidence: Measurement.Evidence
    }

    /// Guidance derived from the target-tools contract. It describes selection
    /// state only; the UI never promotes a tap, overlay, or semantic label into
    /// a measured coordinate.
    struct TargetGuidance: Equatable {
        enum State: Equatable {
            case unavailable(String)
            case awaitingSelection
            case pendingNoDepth
            case staleSelection
            case confirmed(ProvenanceKind)
        }

        struct CalibratedOverlay: Equatable {
            let observedDepthPixel: SIMD2<Float>
            let calibration: OverlayDepthCalibration
            let frame: OverlayFrameIdentity
            let displayTransform: OverlayDisplayTransform
            let currentEpoch: UInt64
            let maximumFrameAge: TimeInterval
        }

        let state: State
        let title: String
        let detail: String
        let overlay: CalibratedOverlay?
        /// Actual camera-view state at presentation time. A nil or changed
        /// viewport suppresses the marker until a fresh selection is admitted.
        let currentViewport: OverlayViewport?
    }

    let mode: Mode
    let captureState: CaptureState
    let statusDetail: String?
    let project: Project?
    let projects: [Project]
    let deletedProjects: [Project]
    let isManagingLibrary: Bool
    let issues: [Issue]
    let measurements: [Measurement]
    let provenanceFields: [ProvenanceFieldPresentation]
    let provenanceLanguage: ProvenancePresentationLanguage
    let targetGuidance: TargetGuidance?
    let plan: Plan?
    let hasCapturedScene: Bool
    let isReferenceScene: Bool
    let assessment: ScannerSceneAssessment?
    let objectDetailDescription: String?
    let exportStatus: String?
    let projectArchiveExportStatus: String?
    let canStartCapture: Bool
    let canFinishCapture: Bool
    let canAssist: Bool
    let canUndo: Bool
    let canRecordMissingEvidence: Bool
    let canSave: Bool
    let canExport: Bool
    let isExporting: Bool
    let canExportProjectArchive: Bool
    let isExportingProjectArchive: Bool
    let canOpenLibrary: Bool
    let canCaptureObject: Bool
    var canConfirmPlanReference: Bool = false
}

struct ScannerWorkspaceActions {
    let startCapture: () -> Void
    let importProject: () -> Void
    let finishCapture: () -> Void
    let assist: () -> Void
    let undo: () -> Void
    let recordMissingEvidence: () -> Void
    let save: () -> Void
    let export: () -> Void
    let exportProjectArchive: () -> Void
    let openLibrary: () -> Void
    let openProject: (UUID) -> Void
    let renameProject: (UUID, String) -> Void
    let trashProject: (UUID) -> Void
    let restoreProject: (UUID) -> Void
    let permanentlyDeleteProject: (UUID) -> Void
    let captureObject: () -> Void
    let recover: (ScannerWorkspacePresentation.Issue) -> Void
    var confirmPlanReference: () -> Void = {}
}

enum ScannerProductTheme {
    static let canvas = Color(red: 0.035, green: 0.055, blue: 0.065)
    static let surface = Color(red: 0.075, green: 0.10, blue: 0.115)
    static let elevatedSurface = Color(red: 0.105, green: 0.135, blue: 0.145)
    static let accent = Color(red: 0.42, green: 0.88, blue: 0.76)
    static let warning = Color(red: 1.0, green: 0.70, blue: 0.32)
    static let critical = Color(red: 1.0, green: 0.44, blue: 0.38)
    static let muted = Color.white.opacity(0.64)
}

/// The product shell consumes only real workspace state and action closures.
/// The camera content must come from the application's one ARSession owner.
struct ScannerWorkspaceView<CameraContent: View, SceneContent: View>: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions
    @ViewBuilder let cameraContent: () -> CameraContent
    @ViewBuilder let sceneContent: () -> SceneContent

    var body: some View {
        ZStack {
            ScannerProductTheme.canvas.ignoresSafeArea()
            switch presentation.mode {
            case .readiness:
                ScannerReadinessScreen(presentation: presentation, actions: actions)
            case .scanning:
                ScannerLiveScanScreen(
                    presentation: presentation,
                    actions: actions,
                    cameraContent: cameraContent
                )
            case .review:
                ScannerReviewScreen(
                    presentation: presentation,
                    actions: actions,
                    sceneContent: sceneContent
                )
            case .library:
                ScannerLibraryScreen(presentation: presentation, actions: actions)
            }
        }
        .preferredColorScheme(.dark)
        .tint(ScannerProductTheme.accent)
    }
}

private struct ScannerReadinessScreen: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions

    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    Text("HSS")
                        .font(.headline.weight(.semibold))
                        .foregroundStyle(ScannerProductTheme.accent)
                    Text("Spatial scan")
                        .font(.largeTitle.weight(.semibold))
                }
                Spacer()
                if presentation.canOpenLibrary {
                    Button(action: actions.openLibrary) {
                        Image(systemName: "square.stack.3d.up")
                            .font(.title3)
                            .frame(width: 48, height: 48)
                    }
                    .accessibilityLabel("Open projects")
                }
            }

            Spacer()

            ScannerStatusCard(captureState: presentation.captureState, detail: presentation.statusDetail)

            if let project = presentation.project {
                ScannerProjectIdentity(project: project)
            }

            if let issue = presentation.issues.first {
                ScannerIssueCard(issue: issue, action: { actions.recover(issue) })
            }

            Button(action: actions.startCapture) {
                Label("Start scan", systemImage: "viewfinder")
                    .font(.title3.weight(.semibold))
                    .frame(maxWidth: .infinity, minHeight: 62)
            }
            .buttonStyle(ScannerPrimaryButtonStyle())
            .disabled(!presentation.canStartCapture)
            .accessibilityHint("Starts the camera only when the device is ready")
        }
        .padding(24)
    }
}

private struct ScannerLiveScanScreen<CameraContent: View>: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions
    @ViewBuilder let cameraContent: () -> CameraContent

    var body: some View {
        ZStack {
            cameraContent()
                .accessibilityHidden(true)

            if let guidance = presentation.targetGuidance {
                ScannerCalibratedTargetOverlay(guidance: guidance)
                    .allowsHitTesting(false)
            }

            LinearGradient(
                colors: [.black.opacity(0.46), .clear, .black.opacity(0.78)],
                startPoint: .top,
                endPoint: .bottom
            )
            .allowsHitTesting(false)

            VStack(spacing: 14) {
                HStack(alignment: .top) {
                    ScannerCapturePill(captureState: presentation.captureState)
                    Spacer()
                    if presentation.canOpenLibrary {
                        Button(action: actions.openLibrary) {
                            Image(systemName: "square.stack.3d.up")
                                .frame(width: 46, height: 46)
                        }
                        .buttonStyle(ScannerGlassButtonStyle())
                        .accessibilityLabel("Open projects")
                    }
                }

                if let detail = presentation.statusDetail {
                    Text(detail)
                        .font(.subheadline)
                        .foregroundStyle(.white.opacity(0.88))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(14)
                        .background(.black.opacity(0.4), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                }

                Spacer()

                if let guidance = presentation.targetGuidance {
                    ScannerTargetGuidanceCard(guidance: guidance)
                }

                if let issue = presentation.issues.first {
                    ScannerIssueCard(issue: issue, action: { actions.recover(issue) })
                }

                if presentation.canAssist || presentation.canUndo {
                    HStack(spacing: 12) {
                        if presentation.canAssist {
                            ScannerToolButton(title: "Assist", icon: "scope", enabled: true, action: actions.assist)
                        }
                        if presentation.canUndo {
                            ScannerToolButton(title: "Undo", icon: "arrow.uturn.backward", enabled: true, action: actions.undo)
                        }
                    }
                }
                if presentation.canFinishCapture {
                    Button(action: actions.finishCapture) {
                        Label("Finish", systemImage: "checkmark")
                            .font(.headline.weight(.semibold))
                            .frame(maxWidth: .infinity, minHeight: 54)
                    }
                    .buttonStyle(ScannerPrimaryButtonStyle())
                }
            }
            .padding(20)
        }
    }
}

private struct ScannerReviewScreen<SceneContent: View>: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions
    @ViewBuilder let sceneContent: () -> SceneContent

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack {
                    Text("Review")
                        .font(.largeTitle.weight(.semibold))
                    Spacer()
                    if presentation.canOpenLibrary {
                        Button(action: actions.openLibrary) { Image(systemName: "square.stack.3d.up") }
                            .buttonStyle(ScannerGlassButtonStyle())
                            .accessibilityLabel("Open projects")
                    }
                }

                if let project = presentation.project { ScannerProjectIdentity(project: project) }

                if let assessment = presentation.assessment {
                    VStack(alignment: .leading, spacing: 16) {
                        Label("Scan insight", systemImage: "waveform.path.ecg")
                            .font(.headline).foregroundStyle(ScannerProductTheme.accent)
                        ScannerAssessmentRow(label: "Captured geometry", value: assessment.geometryDescription)
                        ScannerAssessmentRow(label: "Plan reference", value: assessment.referenceDescription)
                        ScannerAssessmentRow(label: "Boundary", value: assessment.boundaryDescription)
                        Text("Visual detail is not a guarantee of measurement accuracy. Missing evidence stays unknown.")
                            .font(.caption).foregroundStyle(ScannerProductTheme.muted)
                    }
                    .padding(18)
                    .background(ScannerProductTheme.elevatedSurface, in: RoundedRectangle(cornerRadius: 22))
                }

                if !presentation.issues.isEmpty {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Needs review").font(.headline)
                        ForEach(presentation.issues) { issue in
                            ScannerIssueCard(issue: issue, action: { actions.recover(issue) })
                        }
                    }
                }

                if presentation.canConfirmPlanReference {
                    Button(action: actions.confirmPlanReference) {
                        Label("Set observed floor reference", systemImage: "square.3.layers.3d.down.right")
                    }
                    .buttonStyle(ScannerSecondaryButtonStyle())
                    .accessibilityHint("Choose retained floor support; measurement precision remains unverified")
                }

                if let plan = presentation.plan {
                    if presentation.isReferenceScene {
                        Label("Reference geometry — not a captured room", systemImage: "testtube.2")
                            .font(.subheadline.weight(.medium))
                            .foregroundStyle(ScannerProductTheme.warning)
                    }
                    ScannerPlanCanvas(plan: plan)
                        .frame(height: 270)
                        .accessibilityLabel(presentation.isReferenceScene ? "Reference plan view" : "Accepted plan view")
                }

                if presentation.hasCapturedScene {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Captured 3D")
                            .font(.headline)
                        sceneContent()
                            .frame(height: 270)
                            .clipShape(RoundedRectangle(cornerRadius: 22, style: .continuous))
                    }
                }

                if let objectDetailDescription = presentation.objectDetailDescription {
                    Label(objectDetailDescription, systemImage: "cube.transparent")
                        .font(.subheadline)
                        .foregroundStyle(ScannerProductTheme.muted)
                        .padding(14)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                }

                if !presentation.measurements.isEmpty {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Measurements")
                            .font(.headline)
                        ForEach(presentation.measurements) { measurement in
                            ScannerMeasurementRow(measurement: measurement)
                        }
                    }
                }

                if !presentation.provenanceFields.isEmpty {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Evidence and provenance")
                            .font(.headline)
                        ForEach(presentation.provenanceFields) { field in
                            ProvenanceDisclosureView(
                                field: field,
                                language: presentation.provenanceLanguage
                            )
                            .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                        }
                    }
                }

                if presentation.canRecordMissingEvidence {
                    Button(action: actions.recordMissingEvidence) {
                        Label("Record missing evidence", systemImage: "exclamationmark.bubble")
                    }
                    .buttonStyle(ScannerSecondaryButtonStyle())
                    .accessibilityHint("Records an unknown-evidence note without modifying captured geometry")
                }

                if presentation.canSave {
                    Button("Save draft", action: actions.save)
                        .buttonStyle(ScannerPrimaryButtonStyle())
                }
                Button(action: actions.export) {
                    Label(
                        presentation.isExporting ? "Exporting offline files…" : "Export offline files",
                        systemImage: "square.and.arrow.up"
                    )
                }
                .buttonStyle(ScannerSecondaryButtonStyle())
                .disabled(!presentation.canExport || presentation.isExporting)
                if let exportStatus = presentation.exportStatus {
                    Label(exportStatus, systemImage: presentation.isExporting ? "hourglass" : "externaldrive.fill")
                        .font(.subheadline)
                        .foregroundStyle(ScannerProductTheme.muted)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                Button(action: actions.exportProjectArchive) {
                    Label(
                        presentation.isExportingProjectArchive ? "Preparing HSSCAN archive…" : "Share HSSCAN archive",
                        systemImage: "archivebox"
                    )
                }
                .buttonStyle(ScannerSecondaryButtonStyle())
                .disabled(!presentation.canExportProjectArchive || presentation.isExportingProjectArchive)
                .accessibilityHint("Creates a local HSSCAN archive, then lets you choose Files or another destination.")
                if let projectArchiveExportStatus = presentation.projectArchiveExportStatus {
                    Label(projectArchiveExportStatus, systemImage: presentation.isExportingProjectArchive ? "hourglass" : "archivebox.fill")
                        .font(.subheadline)
                        .foregroundStyle(ScannerProductTheme.muted)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                if presentation.canCaptureObject {
                    Button("Capture a detailed object", action: actions.captureObject)
                        .buttonStyle(ScannerSecondaryButtonStyle())
                }
            }
            .padding(20)
        }
    }
}

private struct ScannerAssessmentRow: View {
    let label: LocalizedStringKey
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label).font(.caption).foregroundStyle(ScannerProductTheme.muted)
            Text(value).font(.subheadline.weight(.medium))
        }
        .accessibilityElement(children: .combine)
    }
}

private struct ScannerLibraryScreen: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions
    @State private var search = ""
    @State private var sortByName = false
    @State private var showingTrash = false
    @State private var showingPrivacy = false
    @State private var renaming: ScannerWorkspacePresentation.Project?
    @State private var newName = ""
    @State private var removing: ScannerWorkspacePresentation.Project?

    private var visibleProjects: [ScannerWorkspacePresentation.Project] {
        let source = showingTrash ? presentation.deletedProjects : presentation.projects
        let filtered = source.filter { search.isEmpty || $0.name.localizedStandardContains(search) }
        return sortByName ? filtered.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending } : filtered
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                HStack(alignment: .top) {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("HSS · ON DEVICE")
                            .font(.caption.weight(.bold)).tracking(2)
                            .foregroundStyle(ScannerProductTheme.accent)
                        Text(showingTrash ? String(localized: "Trash") : String(localized: "Your spaces"))
                            .font(.largeTitle.weight(.bold))
                    }
                    Spacer(minLength: 8)
                    Button { showingPrivacy = true } label: {
                        Image(systemName: "lock.shield").frame(width: 48, height: 48)
                    }
                    .buttonStyle(ScannerGlassButtonStyle())
                    .accessibilityLabel("Privacy and local data")
                }

                if !showingTrash {
                    VStack(alignment: .leading, spacing: 18) {
                        HStack(alignment: .top, spacing: 16) {
                            Image(systemName: "viewfinder")
                                .font(.system(size: 42, weight: .ultraLight))
                                .foregroundStyle(ScannerProductTheme.accent)
                                .accessibilityHidden(true)
                            VStack(alignment: .leading, spacing: 8) {
                                Text("Capture. Understand. Keep.").font(.title2.weight(.semibold))
                                Text("Room geometry, its evidence, and your files. Together on your device.")
                                    .font(.subheadline).foregroundStyle(ScannerProductTheme.muted)
                            }
                        }
                        Button(action: actions.startCapture) {
                            Label("New room scan", systemImage: "plus")
                                .frame(maxWidth: .infinity, minHeight: 52)
                        }
                        .buttonStyle(ScannerPrimaryButtonStyle())
                        .disabled(!presentation.canStartCapture)
                        Text("Depth and mesh capture requires a supported LiDAR device.")
                            .font(.caption).foregroundStyle(ScannerProductTheme.muted)
                    }
                    .padding(22)
                    .background(ScannerProductTheme.elevatedSurface, in: RoundedRectangle(cornerRadius: 26))
                }

                HStack {
                    Text(showingTrash ? String(localized: "Retained until you delete") : String(localized: "Local library")).font(.headline)
                    Spacer()
                    Button {
                        showingTrash.toggle()
                        search = ""
                    } label: {
                        Label(showingTrash ? String(localized: "Projects") : String(localized: "Trash"), systemImage: showingTrash ? "square.stack.3d.up" : "trash")
                            .font(.subheadline).frame(minHeight: 44)
                    }
                }
                if !presentation.projects.isEmpty || showingTrash {
                    HStack(spacing: 12) {
                        Image(systemName: "magnifyingglass").foregroundStyle(ScannerProductTheme.muted)
                        TextField("Search projects", text: $search)
                            .textInputAutocapitalization(.never).autocorrectionDisabled()
                        Menu {
                            Button("Recently updated") { sortByName = false }
                            Button("Name") { sortByName = true }
                        } label: {
                            Image(systemName: "arrow.up.arrow.down").frame(width: 44, height: 44)
                        }
                        .accessibilityLabel("Sort projects")
                    }
                    .padding(.leading, 16)
                    .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 16))
                }
                if presentation.isManagingLibrary {
                    ProgressView("Removing local project data…").frame(maxWidth: .infinity)
                }
                ForEach(presentation.issues) { issue in
                    ScannerIssueCard(issue: issue, action: { actions.recover(issue) })
                }
                if visibleProjects.isEmpty {
                    ContentUnavailableView(
                        search.isEmpty ? (showingTrash ? String(localized: "Trash is empty") : String(localized: "A place for every space")) : String(localized: "No matching projects"),
                        systemImage: search.isEmpty ? "square.stack.3d.up" : "magnifyingglass",
                        description: Text(showingTrash ? String(localized: "Moved projects remain on this device until you permanently delete them.") : String(localized: "Start a scan or import an HSSCAN archive. No account required."))
                    )
                } else {
                    LazyVStack(spacing: 12) {
                        ForEach(visibleProjects) { project in
                            HStack(spacing: 4) {
                                Button { if !showingTrash { actions.openProject(project.id) } } label: {
                                    ScannerProjectIdentity(project: project)
                                }
                                .buttonStyle(.plain)
                                .disabled(showingTrash)
                                Menu {
                                    if showingTrash {
                                        if project.canRestore {
                                            Button("Restore project", systemImage: "arrow.uturn.backward") { actions.restoreProject(project.id) }
                                        } else {
                                            Text("Deletion started — retry to finish")
                                        }
                                        Button("Delete permanently", systemImage: "trash", role: .destructive) { removing = project }
                                    } else {
                                        Button("Rename", systemImage: "pencil") {
                                            newName = project.name
                                            renaming = project
                                        }
                                        Button("Move to Trash", systemImage: "trash", role: .destructive) { actions.trashProject(project.id) }
                                    }
                                } label: {
                                    Image(systemName: "ellipsis").frame(width: 48, height: 48)
                                }
                                .accessibilityLabel(String(localized: "Project actions: \(project.name)"))
                            }
                            .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 20))
                        }
                    }
                }
                if !showingTrash {
                    if presentation.canCaptureObject {
                        Button(action: actions.captureObject) {
                            Label("Object library", systemImage: "cube.transparent")
                                .frame(maxWidth: .infinity, minHeight: 48)
                        }.buttonStyle(ScannerSecondaryButtonStyle())
                        .accessibilityIdentifier("object-library.open")
                    }
                    Button(action: actions.importProject) {
                        Label("Import HSSCAN archive", systemImage: "square.and.arrow.down")
                            .frame(maxWidth: .infinity, minHeight: 48)
                    }
                    .buttonStyle(ScannerSecondaryButtonStyle())
                }
            }
            .padding(24)
            .frame(maxWidth: 740)
            .frame(maxWidth: .infinity)
        }
        .disabled(presentation.isManagingLibrary)
        .alert("Rename project", isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField("Project name", text: $newName)
            Button("Save") {
                if let renaming { actions.renameProject(renaming.id, newName) }
                renaming = nil
            }.disabled(newName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || newName.count > 160)
            Button("Cancel", role: .cancel) { renaming = nil }
        }
        .alert("Delete this project permanently?", isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button("Delete permanently", role: .destructive) {
                if let removing { actions.permanentlyDeleteProject(removing.id) }
                removing = nil
            }
            Button("Cancel", role: .cancel) { removing = nil }
        } message: {
            Text("This removes this project's source observations, local exports and local HSSCAN archives. Copies you shared elsewhere and separate object captures are not removed. This cannot be undone.")
        }
        .sheet(isPresented: $showingPrivacy) {
            NavigationStack {
                List {
                    Section("On-device by design") {
                        Label("Capture, processing and exports work offline.", systemImage: "iphone")
                        Label("No account, analytics SDK or cloud reconstruction.", systemImage: "lock.shield")
                    }
                    Section("You control your projects") {
                        Text("Move to Trash to keep a recoverable project. Delete permanently from Trash to remove its local source observations and exports.")
                        Text("Sharing opens a system sheet. Only you choose a destination. Shared copies, file providers and device backups have their own retention policies.")
                    }
                    Section("Separate object captures") {
                        Text("Object Capture photos and models are stored separately from room projects. Deleting a room does not delete these sessions.")
                    }
                }
                .navigationTitle("Privacy and local data")
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { showingPrivacy = false } } }
            }
        }
    }
}

private struct ScannerStatusCard: View {
    let captureState: ScannerWorkspacePresentation.CaptureState
    let detail: String?

    var body: some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: icon)
                .font(.title2)
                .foregroundStyle(tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 4) {
                Text(captureState.title).font(.headline)
                if let detail { Text(detail).font(.subheadline).foregroundStyle(ScannerProductTheme.muted) }
            }
            Spacer(minLength: 0)
        }
        .padding(18)
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    private var icon: String {
        switch captureState {
        case .trackingNormal: "viewfinder.circle.fill"
        case .trackingLimited, .interrupted, .unavailable: "exclamationmark.triangle.fill"
        case .notStarted, .starting: "circle.dotted"
        }
    }

    private var tint: Color {
        switch captureState {
        case .trackingNormal: ScannerProductTheme.accent
        case .trackingLimited, .interrupted: ScannerProductTheme.warning
        case .unavailable: ScannerProductTheme.critical
        case .notStarted, .starting: .white
        }
    }
}

private struct ScannerCapturePill: View {
    let captureState: ScannerWorkspacePresentation.CaptureState

    var body: some View {
        Label(captureState.title, systemImage: "viewfinder")
            .font(.subheadline.weight(.semibold))
            .foregroundStyle(.white)
            .padding(.horizontal, 14)
            .frame(minHeight: 46)
            .background(.black.opacity(0.48), in: Capsule())
            .accessibilityLabel(captureState.title)
    }
}

private struct ScannerTargetGuidanceCard: View {
    let guidance: ScannerWorkspacePresentation.TargetGuidance

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Label(guidance.title, systemImage: symbol)
                    .font(.headline)
                Spacer()
                Text(status)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(tint)
            }
            Text(guidance.detail)
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.82))
        }
        .padding(16)
        .background(.black.opacity(0.58), in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .accessibilityElement(children: .combine)
    }

    private var status: LocalizedStringKey {
        switch guidance.state {
        case .unavailable: "Unavailable"
        case .awaitingSelection: "Select a target"
        case .pendingNoDepth: "Pending evidence"
        case .staleSelection: "Target changed"
        case .confirmed(let provenance):
            switch provenance {
            case .observed: "Observed"
            case .fitted: "Fitted"
            case .userMeasured: "User measured"
            case .inferred: "Inferred"
            case .unknown: "Unknown"
            }
        }
    }

    private var symbol: String {
        switch guidance.state {
        case .unavailable: "nosign"
        case .awaitingSelection: "scope"
        case .pendingNoDepth: "eye.slash"
        case .staleSelection: "arrow.triangle.2.circlepath"
        case .confirmed(let provenance):
            switch provenance {
            case .observed: "eye"
            case .fitted: "function"
            case .userMeasured: "ruler"
            case .inferred: "lightbulb"
            case .unknown: "questionmark.circle"
            }
        }
    }

    private var tint: Color {
        switch guidance.state {
        case .confirmed(.observed): ScannerProductTheme.accent
        case .confirmed(.fitted): .blue
        case .confirmed(.userMeasured): .purple
        case .confirmed(.inferred), .pendingNoDepth: ScannerProductTheme.warning
        case .unavailable, .staleSelection, .awaitingSelection, .confirmed(.unknown): ScannerProductTheme.muted
        }
    }
}

/// Draws a target only when the latest source calibration, epoch, viewport and
/// orientation agree. A rejected mapping intentionally produces no marker.
private struct ScannerCalibratedTargetOverlay: View {
    let guidance: ScannerWorkspacePresentation.TargetGuidance

    var body: some View {
        GeometryReader { proxy in
            if let point = ScannerTargetOverlayProjection.displayedPoint(
                guidance: guidance,
                renderedSize: proxy.size,
                now: ProcessInfo.processInfo.systemUptime
            ) {
                Image(systemName: "scope")
                    .font(.title2.weight(.bold))
                    .foregroundStyle(ScannerProductTheme.accent)
                    .shadow(color: .black.opacity(0.7), radius: 3)
                    .position(x: CGFloat(point.x), y: CGFloat(point.y))
                    .accessibilityHidden(true)
            }
        }
    }
}

enum ScannerTargetOverlayProjection {
    static func displayedPoint(
        guidance: ScannerWorkspacePresentation.TargetGuidance,
        renderedSize: CGSize,
        now: TimeInterval
    ) -> SIMD2<Float>? {
        guard let source = guidance.overlay,
              let viewport = guidance.currentViewport,
              renderedSize.width > 0, renderedSize.height > 0,
              viewport.width == Float(renderedSize.width),
              viewport.height == Float(renderedSize.height) else { return nil }
        let projection = CalibratedOverlayMapper.project(
            observedDepthPixel: source.observedDepthPixel,
            calibration: source.calibration,
            frame: source.frame,
            displayTransform: source.displayTransform,
            currentViewport: viewport,
            currentEpoch: source.currentEpoch,
            now: now,
            maximumFrameAge: source.maximumFrameAge
        )
        if case let .displayed(point) = projection { return point.point }
        return nil
    }
}

private struct ScannerProjectIdentity: View {
    let project: ScannerWorkspacePresentation.Project

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: "cube.transparent")
                .font(.title2)
                .foregroundStyle(ScannerProductTheme.accent)
                .frame(width: 38, height: 38)
                .background(ScannerProductTheme.accent.opacity(0.12), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            VStack(alignment: .leading, spacing: 3) {
                Text(project.name).font(.headline).lineLimit(2)
                if let savedDescription = project.savedDescription {
                    Text(savedDescription).font(.caption).foregroundStyle(ScannerProductTheme.muted)
                }
            }
            Spacer()
            ScannerQualityBadge(quality: project.quality)
        }
        .padding(16)
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

private struct ScannerQualityBadge: View {
    let quality: ScannerWorkspacePresentation.Project.Quality

    var body: some View {
        Text(label)
            .font(.caption.weight(.semibold))
            .foregroundStyle(color)
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .background(color.opacity(0.14), in: Capsule())
            .accessibilityLabel(label)
    }

    private var label: LocalizedStringKey {
        switch quality {
        case .draft: "Draft"
        case .ready: "Saved"
        case .needsAttention: "Needs review"
        }
    }

    private var color: Color {
        switch quality {
        case .draft: ScannerProductTheme.warning
        case .ready: ScannerProductTheme.accent
        case .needsAttention: ScannerProductTheme.critical
        }
    }
}

private struct ScannerIssueCard: View {
    let issue: ScannerWorkspacePresentation.Issue
    let action: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Text(issue.title).font(.headline)
                Spacer()
                Text(priorityLabel).font(.caption.weight(.semibold)).foregroundStyle(priorityColor)
            }
            Text(issue.detail).font(.subheadline).foregroundStyle(ScannerProductTheme.muted)
            if let recoveryTitle = issue.recoveryTitle {
                Button(recoveryTitle, action: action)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(ScannerProductTheme.accent)
                    .frame(minHeight: 44, alignment: .leading)
            }
        }
        .padding(16)
        .background(ScannerProductTheme.elevatedSurface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .accessibilityElement(children: .combine)
    }

    private var priorityLabel: LocalizedStringKey {
        switch issue.priority {
        case .blocking: "Blocked"
        case .important: "Important"
        case .optional: "Optional"
        case .notice: "Attention"
        }
    }

    private var priorityColor: Color {
        switch issue.priority {
        case .blocking: ScannerProductTheme.critical
        case .important: ScannerProductTheme.warning
        case .optional: ScannerProductTheme.muted
        case .notice: ScannerProductTheme.muted
        }
    }
}

private struct ScannerMeasurementRow: View {
    let measurement: ScannerWorkspacePresentation.Measurement

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 3) {
                Text(measurement.label).font(.body.weight(.medium))
                Text([evidenceDescription, measurement.detail].compactMap { $0 }.joined(separator: " · "))
                    .font(.caption)
                    .foregroundStyle(ScannerProductTheme.muted)
            }
            Spacer()
            Text(measurement.value ?? String(localized: "Unknown"))
                .font(.body.monospacedDigit().weight(.semibold))
                .foregroundStyle(measurement.value == nil ? ScannerProductTheme.warning : .white)
        }
        .padding(.horizontal, 16)
        .frame(minHeight: 58)
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityLabel("\(measurement.label), \(measurement.value ?? String(localized: "Unknown")), \(evidenceDescription)")
    }

    private var evidenceLabel: LocalizedStringKey {
        switch measurement.evidence {
        case .observed: "Observed"
        case .fitted: "Fitted"
        case .userMeasured: "Reference measurement"
        case .inferred: "Inferred"
        case .unknown: "Unknown evidence"
        }
    }

    private var evidenceDescription: String {
        switch measurement.evidence {
        case .observed: String(localized: "Observed")
        case .fitted: String(localized: "Fitted")
        case .userMeasured: String(localized: "Reference measurement")
        case .inferred: String(localized: "Inferred")
        case .unknown: String(localized: "Unknown evidence")
        }
    }
}

private struct ScannerPlanCanvas: View {
    let plan: ScannerWorkspacePresentation.Plan

    var body: some View {
        Canvas { context, size in
            guard let bounds = plan.boundsMetres, bounds.width > 0, bounds.height > 0 else { return }
            let inset: CGFloat = 24
            let scale = min((size.width - inset * 2) / bounds.width, (size.height - inset * 2) / bounds.height)
            let offset = CGPoint(
                x: (size.width - bounds.width * scale) / 2 - bounds.minX * scale,
                y: (size.height - bounds.height * scale) / 2 + bounds.maxY * scale
            )
            func draw(_ polylines: [[CGPoint]], color: Color, dash: [CGFloat] = []) {
                for points in polylines where points.count > 1 {
                    var path = Path()
                    let first = points[0]
                    path.move(to: CGPoint(x: first.x * scale + offset.x, y: offset.y - first.y * scale))
                    for point in points.dropFirst() {
                        path.addLine(to: CGPoint(x: point.x * scale + offset.x, y: offset.y - point.y * scale))
                    }
                    context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 2.5, lineCap: .round, lineJoin: .round, dash: dash))
                }
            }
            draw(plan.polylinesMetres, color: ScannerProductTheme.accent)
            draw(plan.unknownPolylinesMetres, color: ScannerProductTheme.warning, dash: [7, 5])
            for annotation in plan.dimensionAnnotations {
                let start = CGPoint(
                    x: annotation.startMetres.x * scale + offset.x,
                    y: offset.y - annotation.startMetres.y * scale
                )
                let end = CGPoint(
                    x: annotation.endMetres.x * scale + offset.x,
                    y: offset.y - annotation.endMetres.y * scale
                )
                let midpoint = CGPoint(x: (start.x + end.x) / 2, y: (start.y + end.y) / 2)
                let vector = CGPoint(x: end.x - start.x, y: end.y - start.y)
                let length = hypot(vector.x, vector.y)
                guard length > 0 else { continue }
                // Offset is presentation-only; source metre positions stay unchanged.
                let labelPosition = CGPoint(
                    x: midpoint.x - vector.y / length * 12,
                    y: midpoint.y + vector.x / length * 12
                )
                context.draw(
                    Text(annotation.label)
                        .font(.caption2.weight(.semibold))
                        .foregroundColor(annotation.evidence == .unknown ? ScannerProductTheme.warning : .white),
                    at: labelPosition
                )
            }
        }
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 22, style: .continuous))
    }
}

private struct ScannerToolButton: View {
    let title: LocalizedStringKey
    let icon: String
    let enabled: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: icon)
                .font(.subheadline.weight(.semibold))
                .frame(maxWidth: .infinity, minHeight: 52)
        }
        .buttonStyle(ScannerGlassButtonStyle())
        .disabled(!enabled)
    }
}

private struct ScannerPrimaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(ScannerProductTheme.canvas)
            .background(ScannerProductTheme.accent.opacity(configuration.isPressed ? 0.74 : 1), in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .opacity(configuration.isPressed ? 0.9 : 1)
    }
}

private struct ScannerSecondaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(ScannerProductTheme.elevatedSurface.opacity(configuration.isPressed ? 0.65 : 1), in: RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

private struct ScannerGlassButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(.white)
            .background(.black.opacity(configuration.isPressed ? 0.58 : 0.4), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .opacity(configuration.isPressed ? 0.82 : 1)
    }
}

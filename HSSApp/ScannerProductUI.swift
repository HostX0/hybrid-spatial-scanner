import SwiftUI

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

    let mode: Mode
    let captureState: CaptureState
    let statusDetail: String?
    let project: Project?
    let projects: [Project]
    let issues: [Issue]
    let measurements: [Measurement]
    let plan: Plan?
    let hasCapturedScene: Bool
    let isReferenceScene: Bool
    let objectDetailDescription: String?
    let exportStatus: String?
    let canStartCapture: Bool
    let canFinishCapture: Bool
    let canAssist: Bool
    let canUndo: Bool
    let canSave: Bool
    let canExport: Bool
    let isExporting: Bool
    let canOpenLibrary: Bool
    let canCaptureObject: Bool
}

struct ScannerWorkspaceActions {
    let startCapture: () -> Void
    let finishCapture: () -> Void
    let assist: () -> Void
    let undo: () -> Void
    let save: () -> Void
    let export: () -> Void
    let openLibrary: () -> Void
    let openProject: (UUID) -> Void
    let captureObject: () -> Void
    let recover: (ScannerWorkspacePresentation.Issue) -> Void
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

                if !presentation.issues.isEmpty {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Needs review")
                            .font(.headline)
                        ForEach(presentation.issues) { issue in
                            ScannerIssueCard(issue: issue, action: { actions.recover(issue) })
                        }
                    }
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
                if presentation.canCaptureObject {
                    Button("Capture a detailed object", action: actions.captureObject)
                        .buttonStyle(ScannerSecondaryButtonStyle())
                }
            }
            .padding(20)
        }
    }
}

private struct ScannerLibraryScreen: View {
    let presentation: ScannerWorkspacePresentation
    let actions: ScannerWorkspaceActions

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("Projects")
                .font(.largeTitle.weight(.semibold))
            if presentation.projects.isEmpty {
                ContentUnavailableView(
                    "No saved projects",
                    systemImage: "square.stack.3d.up",
                    description: Text("Projects appear here after a local save."))
            } else {
                ScrollView {
                    LazyVStack(spacing: 12) {
                        ForEach(presentation.projects) { project in
                            Button(action: { actions.openProject(project.id) }) {
                                ScannerProjectIdentity(project: project)
                            }
                            .buttonStyle(.plain)
                            .accessibilityHint("Opens \(project.name)")
                        }
                    }
                }
            }
            Spacer()
            if presentation.canStartCapture {
                Button("Start a new scan", action: actions.startCapture)
                    .buttonStyle(ScannerPrimaryButtonStyle())
            }
        }
        .padding(24)
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
            Text(measurement.value ?? "Unknown")
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

import ARKit
import SceneKit
import SwiftUI
import simd

/// Binds the product shell to the native workspace without introducing another
/// geometry, project, or ARSession owner.
@MainActor
struct ScannerWorkspaceProductHost<Model: ScannerWorkspaceModeling>: View {
    @ObservedObject var workspace: Model
    @State private var isCreatingProject = false

    var body: some View {
        ScannerWorkspaceView(
            presentation: presentation,
            actions: actions,
            cameraContent: { ScannerCameraPreview(session: workspace.captureSession) },
            sceneContent: {
                if let snapshot = workspace.latestSnapshot {
                    ScannerSnapshotMeshPreview(snapshot: snapshot)
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
        .task { workspace.reloadProjects() }
    }

    private var presentation: ScannerWorkspacePresentation {
        let selectedProject = workspace.selectedProjectID.flatMap { selectedID in
            workspace.localProjects.first(where: { $0.id == selectedID })
        }
        let snapshotPlan = workspace.latestSnapshot.flatMap(ScannerAcceptedPlanDecoder.decode)
        return ScannerWorkspacePresentation(
            mode: mode,
            captureState: captureState,
            statusDetail: statusDetail,
            project: selectedProject.map(projectPresentation),
            projects: workspace.localProjects.map(projectPresentation),
            issues: issues,
            measurements: [],
            plan: snapshotPlan,
            hasCapturedScene: workspace.latestSnapshot.map { !$0.provenance.contains(.syntheticTest) } ?? false,
            canStartCapture: workspace.phase == .ready || workspace.phase == .browsing,
            canFinishCapture: workspace.phase == .capturing,
            canAssist: false,
            canUndo: false,
            canSave: workspace.saveState == .hasUnsavedChanges,
            canOpenLibrary: false
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
            finishCapture: workspace.stopCapture,
            assist: {},
            undo: {},
            save: workspace.saveSelectedProject,
            openLibrary: {},
            openProject: workspace.openProject,
            recover: { _ in workspace.recoverFromCurrentIssue() }
        )
    }

    private var mode: ScannerWorkspacePresentation.Mode {
        switch workspace.phase {
        case .browsing: .library
        case .startingCapture, .capturing, .stoppingCapture: .scanning
        case .frozen, .saving, .openingProject: .review
        case .preparingProject, .ready, .captureUnsupported, .failed: .readiness
        }
    }

    private var captureState: ScannerWorkspacePresentation.CaptureState {
        switch workspace.phase {
        case .browsing, .preparingProject, .ready: .notStarted
        case .startingCapture: .starting
        case .capturing: .trackingNormal
        case .stoppingCapture, .frozen, .saving, .openingProject: .interrupted
        case .captureUnsupported(let reason), .failed(let reason): .unavailable(reason)
        }
    }

    private var statusDetail: String? {
        switch workspace.phase {
        case .captureUnsupported(let reason), .failed(let reason): reason
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
        if workspace.latestSnapshot?.provenance.contains(.syntheticTest) == true {
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

    func makeUIView(context: Context) -> ARSCNView {
        let view = ARSCNView(frame: .zero)
        view.automaticallyUpdatesLighting = true
        view.rendersCameraGrain = false
        if let session { view.session = session }
        return view
    }

    func updateUIView(_ view: ARSCNView, context: Context) {
        guard let session, view.session !== session else { return }
        view.session = session
    }
}

private struct ScannerSnapshotMeshPreview: UIViewRepresentable {
    let snapshot: ScannerSceneSnapshot

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView(frame: .zero)
        view.backgroundColor = UIColor(ScannerProductTheme.surface)
        view.allowsCameraControl = true
        view.autoenablesDefaultLighting = true
        view.scene = ScannerMeshSceneBuilder.scene(from: snapshot)
        view.accessibilityLabel = "3D geometry from accepted snapshot"
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        view.scene = ScannerMeshSceneBuilder.scene(from: snapshot)
    }
}

private enum ScannerMeshSceneBuilder {
    static func scene(from snapshot: ScannerSceneSnapshot) -> SCNScene {
        let scene = SCNScene()
        guard let geometry = geometry(from: snapshot) else { return scene }
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

    private static func geometry(from snapshot: ScannerSceneSnapshot) -> SCNGeometry? {
        guard !snapshot.verticesMetres.isEmpty,
              snapshot.verticesMetres.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite }),
              snapshot.triangleIndices.count.isMultiple(of: 3),
              snapshot.triangleIndices.allSatisfy({ Int($0) < snapshot.verticesMetres.count }) else {
            return nil
        }
        let vertices = snapshot.verticesMetres.map { SCNVector3(Float($0.x), Float($0.y), Float($0.z)) }
        let source = SCNGeometrySource(vertices: vertices)
        let indexData = snapshot.triangleIndices.withUnsafeBufferPointer { Data(buffer: $0) }
        let element = SCNGeometryElement(
            data: indexData,
            primitiveType: .triangles,
            primitiveCount: snapshot.triangleIndices.count / 3,
            bytesPerIndex: MemoryLayout<UInt32>.size
        )
        let geometry = SCNGeometry(sources: [source], elements: [element])
        let material = SCNMaterial()
        material.diffuse.contents = UIColor(ScannerProductTheme.accent.opacity(0.75))
        material.fillMode = .lines
        material.isDoubleSided = true
        geometry.materials = [material]
        return geometry
    }
}

private enum ScannerAcceptedPlanDecoder {
    private struct Scene: Decodable {
        let sourceIsSynthetic: Bool
        let basis: Basis
        let nodes: [Node]
        let edges: [Edge]

        enum CodingKeys: String, CodingKey {
            case sourceIsSynthetic = "source_is_synthetic"
            case basis, nodes, edges
        }
    }

    private struct Basis: Decodable {
        let floorOrigin: [Double]
        let floorU: [Double]
        let floorV: [Double]

        enum CodingKeys: String, CodingKey {
            case floorOrigin = "floor_origin_m"
            case floorU = "floor_u"
            case floorV = "floor_v"
        }
    }

    private struct Node: Decodable {
        let id: UUID
        let position: [Double]
        let provenance: Provenance

        enum CodingKeys: String, CodingKey {
            case id, position = "position_m", provenance
        }
    }

    private struct Edge: Decodable {
        let start: UUID
        let end: UUID
        let provenance: Provenance

        enum CodingKeys: String, CodingKey {
            case start = "start_node_id"
            case end = "end_node_id"
            case provenance
        }
    }

    private struct Provenance: Decodable {
        let origin: String

        enum CodingKeys: String, CodingKey { case origin = "geometry_origin" }
    }

    static func decode(_ snapshot: ScannerSceneSnapshot) -> ScannerWorkspacePresentation.Plan? {
        guard let scene = try? JSONDecoder().decode(Scene.self, from: snapshot.sceneJSON),
              !scene.sourceIsSynthetic,
              let origin = vector(scene.basis.floorOrigin),
              let u = vector(scene.basis.floorU),
              let v = vector(scene.basis.floorV),
              simd_length(u) > 0, simd_length(v) > 0 else { return nil }

        let coordinates = Dictionary(uniqueKeysWithValues: scene.nodes.compactMap { node in
            vector(node.position).map {
                (node.id, CGPoint(
                    x: CGFloat(simd_dot($0 - origin, u)),
                    y: CGFloat(simd_dot($0 - origin, v))
                ))
            }
        })
        let linePairs = scene.edges.compactMap { edge -> (CGPoint, CGPoint, Bool)? in
            guard let start = coordinates[edge.start], let end = coordinates[edge.end] else { return nil }
            return (start, end, edge.provenance.origin == "unknown" || edge.provenance.origin == "inferred")
        }
        let observed = linePairs.filter { !$0.2 }.map { [$0.0, $0.1] }
        let unknown = linePairs.filter { $0.2 }.map { [$0.0, $0.1] }
        let points = linePairs.flatMap { [$0.0, $0.1] }
        guard let first = points.first else { return nil }
        let bounds = points.dropFirst().reduce(CGRect(origin: first, size: .zero)) { partial, point in
            partial.union(CGRect(origin: point, size: .zero))
        }
        return ScannerWorkspacePresentation.Plan(
            polylinesMetres: observed,
            unknownPolylinesMetres: unknown,
            boundsMetres: bounds.insetBy(dx: -0.05, dy: -0.05)
        )
    }

    private static func vector(_ values: [Double]) -> SIMD3<Double>? {
        guard values.count == 3, values.allSatisfy(\.isFinite) else { return nil }
        return SIMD3(values[0], values[1], values[2])
    }
}

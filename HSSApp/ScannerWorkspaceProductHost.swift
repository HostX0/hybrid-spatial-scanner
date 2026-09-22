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
    @State private var isObjectCapturePresented = false
    @State private var completedObjectArtifact: ObjectCaptureArtifact?

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
        .sheet(isPresented: $isObjectCapturePresented) {
            // This is deliberately a separate RealityKit-owned workflow. It is
            // offered only after room capture has stopped and never enters the
            // workspace's metric scene or project save path.
            ObjectCaptureWorkflowView { artifact in
                completedObjectArtifact = artifact
            }
        }
        .task { workspace.reloadProjects() }
    }

    private var presentation: ScannerWorkspacePresentation {
        let selectedProject = workspace.selectedProjectID.flatMap { selectedID in
            workspace.localProjects.first(where: { $0.id == selectedID })
        }
        let reviewData = workspace.latestSnapshot.flatMap(ScannerAcceptedPlanDecoder.decode)
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
            plan: reviewData?.plan,
            hasCapturedScene: reviewData != nil && !isReferenceScene,
            isReferenceScene: isReferenceScene,
            objectDetailDescription: completedObjectArtifact.map { _ in
                String(localized: "A detailed object model was saved separately on this device.")
            },
            canStartCapture: workspace.phase == .ready || workspace.phase == .browsing,
            canFinishCapture: isCaptureActive,
            canAssist: false,
            canUndo: false,
            canSave: workspace.saveState == .hasUnsavedChanges,
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
            finishCapture: workspace.stopCapture,
            assist: {},
            undo: {},
            save: workspace.saveSelectedProject,
            openLibrary: workspace.showLibrary,
            openProject: workspace.openProject,
            captureObject: { isObjectCapturePresented = true },
            recover: { _ in workspace.recoverFromCurrentIssue() }
        )
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
            || workspace.latestSnapshot.flatMap(ScannerAcceptedPlanDecoder.decode)?.isSynthetic == true {
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
        let regions: [Region]

        enum CodingKeys: String, CodingKey {
            case sourceIsSynthetic = "source_is_synthetic"
            case basis, nodes, edges, regions
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
        let id: UUID
        let start: UUID
        let end: UUID
        let provenance: Provenance

        enum CodingKeys: String, CodingKey {
            case id, start = "start_node_id"
            case end = "end_node_id"
            case provenance
        }
    }

    private struct Region: Decodable {
        let id: UUID
        let areaMetresSquared: Double?
        let areaPolicy: String
        let view: String
        let cutHeightMetres: Double
        let provenance: Provenance

        enum CodingKeys: String, CodingKey {
            case id
            case areaMetresSquared = "area_m2"
            case areaPolicy = "area_policy"
            case view
            case cutHeightMetres = "cut_height_m"
            case provenance
        }
    }

    private struct Provenance: Decodable {
        let origin: String

        enum CodingKeys: String, CodingKey { case origin = "geometry_origin" }
    }

    struct ReviewData {
        let plan: ScannerWorkspacePresentation.Plan
        let measurements: [ScannerWorkspacePresentation.Measurement]
        let isSynthetic: Bool
    }

    static func decode(_ snapshot: ScannerSceneSnapshot) -> ReviewData? {
        guard let scene = try? JSONDecoder().decode(Scene.self, from: snapshot.sceneJSON),
              let origin = vector(scene.basis.floorOrigin),
              let u = vector(scene.basis.floorU),
              let v = vector(scene.basis.floorV),
              approximatelyUnit(u), approximatelyUnit(v),
              abs(simd_dot(u, v)) < 0.001 else { return nil }

        let coordinates = Dictionary(uniqueKeysWithValues: scene.nodes.compactMap { node in
            vector(node.position).map {
                (node.id, CGPoint(
                    x: CGFloat(simd_dot($0 - origin, u)),
                    y: CGFloat(simd_dot($0 - origin, v))
                ))
            }
        })
        let linePairs = scene.edges.compactMap { edge -> (UUID, CGPoint, CGPoint, ScannerWorkspacePresentation.Measurement.Evidence)? in
            guard let start = coordinates[edge.start], let end = coordinates[edge.end] else { return nil }
            return (edge.id, start, end, evidence(for: edge.provenance.origin))
        }
        let observed = linePairs.filter { $0.3 != .unknown && $0.3 != .inferred }.map { [$0.1, $0.2] }
        let unknown = linePairs.filter { $0.3 == .unknown || $0.3 == .inferred }.map { [$0.1, $0.2] }
        let points = linePairs.flatMap { [$0.1, $0.2] }
        guard let first = points.first else { return nil }
        let bounds = points.dropFirst().reduce(CGRect(origin: first, size: .zero)) { partial, point in
            partial.union(CGRect(origin: point, size: .zero))
        }
        let dimensions = linePairs.enumerated().compactMap { index, edge -> ScannerWorkspacePresentation.Measurement? in
            let length = hypot(edge.2.x - edge.1.x, edge.2.y - edge.1.y)
            guard length.isFinite, length > 0 else { return nil }
            return ScannerWorkspacePresentation.Measurement(
                id: edge.0,
                label: "\(String(localized: "Boundary")) \(index + 1)",
                value: lengthMetres(length),
                evidence: edge.3,
                detail: nil
            )
        }
        let annotations = linePairs.compactMap { edge -> ScannerWorkspacePresentation.DimensionAnnotation? in
            let length = hypot(edge.2.x - edge.1.x, edge.2.y - edge.1.y)
            guard length.isFinite, length > 0 else { return nil }
            return ScannerWorkspacePresentation.DimensionAnnotation(
                id: edge.0,
                startMetres: edge.1,
                endMetres: edge.2,
                label: lengthMetres(length),
                evidence: edge.3
            )
        }
        let areaMeasurements = scene.regions.map { region in
            let isKnown = region.areaMetresSquared.map { $0.isFinite && $0 >= 0 } ?? false
            return ScannerWorkspacePresentation.Measurement(
                id: region.id,
                label: region.view == "architectural_cut"
                    ? String(localized: "Architectural cut area")
                    : String(localized: "Floor footprint area"),
                value: isKnown ? areaMetresSquared(region.areaMetresSquared!) : nil,
                evidence: isKnown ? evidence(for: region.provenance.origin) : .unknown,
                detail: regionDetail(region)
            )
        }
        return ReviewData(
            plan: ScannerWorkspacePresentation.Plan(
                polylinesMetres: observed,
                unknownPolylinesMetres: unknown,
                dimensionAnnotations: annotations,
                boundsMetres: bounds.insetBy(dx: -0.05, dy: -0.05)
            ),
            measurements: dimensions + areaMeasurements,
            isSynthetic: scene.sourceIsSynthetic
        )
    }

    private static func vector(_ values: [Double]) -> SIMD3<Double>? {
        guard values.count == 3, values.allSatisfy(\.isFinite) else { return nil }
        return SIMD3(values[0], values[1], values[2])
    }

    private static func approximatelyUnit(_ vector: SIMD3<Double>) -> Bool {
        abs(simd_length(vector) - 1) < 0.001
    }

    private static func evidence(for origin: String) -> ScannerWorkspacePresentation.Measurement.Evidence {
        switch origin {
        case "observed": .observed
        case "fitted": .fitted
        case "user_measured": .userMeasured
        case "inferred": .inferred
        default: .unknown
        }
    }

    private static func lengthMetres(_ value: CGFloat) -> String {
        let measurement = Measurement(value: Double(value), unit: UnitLength.meters)
        return measurement.formatted(
            .measurement(
                width: .abbreviated,
                usage: .asProvided,
                numberFormatStyle: .number.precision(.fractionLength(0...2))
            )
        )
    }

    private static func areaMetresSquared(_ value: Double) -> String {
        let measurement = Measurement(value: value, unit: UnitArea.squareMeters)
        return measurement.formatted(
            .measurement(
                width: .abbreviated,
                usage: .asProvided,
                numberFormatStyle: .number.precision(.fractionLength(0...2))
            )
        )
    }

    private static func regionDetail(_ region: Region) -> String {
        let policy = region.areaPolicy == "net_fixed_obstructions"
            ? String(localized: "Net fixed obstructions")
            : String(localized: "Finished internal face")
        let cut = lengthMetres(CGFloat(region.cutHeightMetres))
        return "\(policy) · \(String(localized: "Cut")) \(cut)"
    }
}

import ARKit
import Combine
import Foundation
import HSSContracts
import UIKit
import simd

/// Maps one capture session's source clock into the durable project's ordered
/// time axis. The first event anchors to the durable cursor, not device uptime.
/// Only within-session event intervals advance it; delivery inversions are
/// clamped. This is an ordering key, not physical elapsed time across sessions.
struct ProjectCaptureClock {
    private static let maximumDeliverySkew: TimeInterval = 1
    private(set) var lastTime: TimeInterval
    private var sourceOrigin: TimeInterval?
    private var projectOrigin: TimeInterval?

    init(lastDurableTime: TimeInterval) {
        lastTime = lastDurableTime
    }

    mutating func projectTime(for sourceTime: TimeInterval) -> TimeInterval? {
        guard sourceTime.isFinite, sourceTime >= 0,
              lastTime.isFinite, lastTime >= 0 else { return nil }
        let origin = sourceOrigin ?? sourceTime
        let durableOrigin = projectOrigin ?? lastTime
        let shifted = durableOrigin + (sourceTime - origin)
        guard shifted.isFinite,
              shifted >= lastTime - Self.maximumDeliverySkew else { return nil }
        let mapped = max(lastTime, shifted)
        sourceOrigin = origin
        projectOrigin = durableOrigin
        lastTime = mapped
        return mapped
    }
}

/// An idle review control uses the accepted snapshot's coordinate context,
/// not the presence of an active camera session. It never registers a new epoch
/// or invents a relationship to an old world's arbitrary origin.
enum ScannerRetainedPlanEpoch {
    private struct Document: Decodable {
        let project_id: UUID
        let scene_revision: String
        let units: String
        let epochs: [Epoch]
    }
    private struct Epoch: Decodable {
        let capture_id: UUID
        let session_epoch: UInt64
        let transform_revision: String
        let relationship_state: String
        let T_project_world: [Double]?
    }

    static func resolve(snapshot: ScannerSceneSnapshot, projectID: UUID, captureID: UUID) -> UInt64? {
        guard let document = try? JSONDecoder().decode(Document.self, from: snapshot.sceneJSON),
              document.project_id == projectID, document.units == "m",
              document.scene_revision == String(snapshot.revision), document.epochs.count <= 32,
              document.epochs.allSatisfy({ epoch in
                  epoch.session_epoch > 0 && epoch.session_epoch <= UInt64(UInt32.max)
                      && UInt64(epoch.transform_revision).map { $0 > 0 && String($0) == epoch.transform_revision } == true
                      && ["accepted", "unresolved"].contains(epoch.relationship_state)
                      && (epoch.relationship_state != "accepted" || (epoch.T_project_world?.count == 16 && epoch.T_project_world?.allSatisfy(\.isFinite) == true))
              }) else { return nil }
        return document.epochs.filter { $0.capture_id == captureID && $0.relationship_state == "accepted" }
            .map(\.session_epoch).max()
    }
}

@MainActor
final class ScannerWorkspaceModel: ScannerWorkspaceModeling {
    typealias OfflineExportOperation = @Sendable (
        ScannerSceneSnapshot, UUID, URL, ScannerTexturedMesh?
    ) async throws -> URL
    typealias ArchiveExportOperation = @Sendable (String, String) async throws -> Void

    @Published private(set) var localProjects: [ScannerProjectSummary] = []
    @Published private(set) var deletedProjects: [ScannerProjectSummary] = []
    @Published private(set) var isManagingLibrary = false
    @Published private(set) var phase: ScannerWorkspacePhase = .browsing {
        didSet { if phase != .frozen { invalidateInFlightExports() } }
    }
    @Published private(set) var latestSnapshot: ScannerSceneSnapshot? {
        didSet {
            if latestSnapshot?.revision != oldValue?.revision
                || latestSnapshot?.isFrozen != oldValue?.isFrozen {
                invalidateInFlightExports()
            }
            refreshOptionalAppearance()
        }
    }
    @Published private(set) var selectedProjectID: UUID? {
        didSet {
            if selectedProjectID != oldValue {
                invalidateInFlightExports()
                commandHistory = nil
                includeSourceImagesInExport = false
            }
        }
    }
    @Published var selectedProjectName: String = "" {
        didSet {
            guard !isAssigningSelectedName,
                  selectedProjectID != nil,
                  selectedProjectName != oldValue else { return }
            saveState = .hasUnsavedChanges
        }
    }
    @Published private(set) var currentIssue: ScannerWorkspaceIssue?
    @Published private(set) var saveState: ScannerWorkspaceSaveState = .noProject
    @Published private(set) var exportState: ScannerExportState = .idle {
        didSet { if exportState != .exporting { offlineExportRequest = nil } }
    }
    @Published private(set) var projectArchiveExportState: ScannerProjectArchiveExportState = .idle {
        didSet { if projectArchiveExportState != .exporting { archiveExportRequest = nil } }
    }
    @Published private(set) var captureAccounting: ScannerCaptureAccounting = .zero
    @Published private(set) var targetSelectionState: NativeTargetSelectionState = .awaitingSelection
    @Published private(set) var commandHistory: ScannerCommandHistory?
    @Published private(set) var captureAdvice: ScannerCaptureAdvice?
    @Published private(set) var reviewTexturedMesh: ScannerTexturedMesh?
    @Published private(set) var appearanceMessage: String?
    @Published var includeSourceImagesInExport = false

    var editableBoundary: [ScannerBoundaryEdge] {
        guard canRecordMissingEvidence, let snapshot = latestSnapshot, let id = selectedProjectID else { return [] }
        return ScannerBoundaryEdge.decode(snapshot: snapshot, projectID: id)
    }

    var captureSession: ARSession? { captureCoordinator.captureSession }
    private var retainedPlanEpoch: UInt64? {
        guard let snapshot = latestSnapshot, let entry = selectedCatalogEntry else { return nil }
        return ScannerRetainedPlanEpoch.resolve(snapshot: snapshot, projectID: entry.id, captureID: entry.captureID)
    }
    var canConfirmPlanReference: Bool {
        phase == .frozen
            && runtime != nil
            && selectedProjectID != nil
            && latestSnapshot?.verticesMetres.isEmpty == false
            && retainedPlanEpoch != nil
            && !captureCoordinator.isSessionRunning
            && !drainScheduled
            && !pendingCommandJournal
            && !frozenCommandInFlight
            && !reviewSnapshotRefreshRequired
    }
    var canShowLibrary: Bool {
        !captureCoordinator.isSessionRunning
            && !drainScheduled
            && !pendingCommandJournal
            && !frozenCommandInFlight
            && (saveState == .clean || isSavedState)
            && phase != .saving
            && phase != .openingProject
    }
    var canExport: Bool {
        ScannerExportEligibility.isEligible(
            snapshot: latestSnapshot,
            project: selectedCatalogEntry?.summary,
            saveState: saveState,
            phase: phase
        ) && !pendingCommandJournal && !frozenCommandInFlight && exportState != .exporting
    }
    var canExportProjectArchive: Bool {
        selectedProjectID != nil
            && selectedCatalogEntry != nil
            && !pendingCommandJournal
            && !frozenCommandInFlight
            && !reviewSnapshotRefreshRequired
            && phase == .frozen
            && (saveState == .clean || isSavedState)
            && projectArchiveExportState != .exporting
    }
    var canRecordMissingEvidence: Bool {
        runtime != nil
            && selectedProjectID != nil
            && latestSnapshot?.isFrozen == true
            && latestSnapshot?.verticesMetres.isEmpty == false
            && !captureCoordinator.isSessionRunning
            && !drainScheduled
            && !pendingCommandJournal
            && !frozenCommandInFlight
            && !reviewSnapshotRefreshRequired
            && phase == .frozen
            && saveState != .saving
    }

    private enum PendingNativeEvent: Sendable {
        case epoch(CaptureEpochControl, monotonicTime: TimeInterval)
        case frame(NormalizedSceneDepthFrame)
        case mesh(OwnedMeshAnchorEvent)
        case planReference(matrix: [Double], cutHeightMetres: Double, epoch: UInt64, monotonicTime: TimeInterval)

        var isFrame: Bool {
            if case .frame = self { true } else { false }
        }

        var frameIdentity: SceneDepthFrameIdentity? {
            if case .frame(let frame) = self { frame.identity } else { nil }
        }
    }

    private struct SequencedNativeEvent: Sendable {
        let sequence: UInt64
        let event: PendingNativeEvent
    }

    private struct WorkspaceCatalog: Codable {
        var projects: [WorkspaceCatalogEntry]
    }

    private struct WorkspaceCatalogEntry: Codable, Equatable {
        let id: UUID
        var captureID: UUID
        var name: String
        var updatedAt: Date
        var sceneRevision: UInt64
        var state: ScannerProjectState
        var sourceIsSynthetic: Bool
        /// Additive catalog field: absent in previous catalogs means active.
        var deletedAt: Date? = nil
        /// Durable intent precedes destructive work. A partially purged entry
        /// must never be restored and misrepresented as an intact project.
        var purgeStarted: Bool? = nil

        var summary: ScannerProjectSummary {
            ScannerProjectSummary(
                id: id, name: name, updatedAt: updatedAt,
                sceneRevision: sceneRevision, state: state, canRestore: purgeStarted != true
            )
        }
    }

    private let trustedRoot: URL
    private let catalogURL: URL
    private let sourceIsSynthetic: Bool
    private let appearanceStore: ScannerAppearanceStore
    private let offlineExportOperation: OfflineExportOperation
    private let archiveExportOperation: ArchiveExportOperation
    /// Job identity is intentionally ephemeral. Reopening the same project at
    /// the same revision is a new UI context, not ownership of an older job.
    private struct ExportRequest: Equatable {
        let id = UUID()
        let projectID: UUID
        let sceneRevision: UInt64
    }
    private var offlineExportRequest: ExportRequest?
    private var archiveExportRequest: ExportRequest?
    private var optionalAppearanceEnabled = true
    private var appearanceResourcesAvailable = true
    private var appearanceSourceIsVerified = true
    private var appearanceGeneration: UInt64 = 0
    private let resourceMonitor = ScannerIOSResourceMonitor()
    private var appearanceRuntimeToken = UUID()
    private var runtime: HSSProjectRuntime? {
        didSet {
            let abandoned = appearanceRuntimeToken
            appearanceRuntimeToken = UUID()
            Task { [appearanceStore] in await appearanceStore.discardPending(runtimeToken: abandoned) }
        }
    }
    private var pendingCommandJournal = false
    private var frozenCommandInFlight = false
    /// A command journaled successfully, but the review snapshot could not be
    /// refreshed. Saving or reopening can recover the view; new corrections
    /// remain blocked so stale UI state cannot drive another operation.
    private var reviewSnapshotRefreshRequired = false
    private var selectedCatalogEntry: WorkspaceCatalogEntry?
    private var pendingEvents: [SequencedNativeEvent] = []
    private var nextEventSequence: UInt64 = 1
    private var lastDurableEventTime: TimeInterval = 0
    private var captureClock: ProjectCaptureClock?
    private var drainScheduled = false
    private var freezeRequested = false
    private var saveRequested = false
    private var saveAfterFreeze = false
    private var resourceSamplingInProgress = false
    private var resourcePauseInProgress = false
    private var resourcePauseLatched = false
    private var resourceCheckpointSucceeded = false
    private var resourceMetricIntakeBlocked = false
    private var resourceConservingMessage: String?
    private var isAssigningSelectedName = false
    private var observedProvenance: Set<ScannerSceneProvenance> = []
    private var didReportDepthKeyframeCapacity = false
    private var queuedTargetSourceFrames: [SceneDepthFrameIdentity] = []
    private var acceptedTargetSourceFrames: [SceneDepthFrameIdentity] = []
    private var pendingTargetCandidate: NativeTargetCandidate?
    private var acceptedEpochProjectTransforms: [UInt64: [Double]] = [:]
    private var acceptedEpochTransformRevisions: [UInt64: UInt64] = [:]
    private var cancellables: Set<AnyCancellable> = []
    private let keyframeSelector = SceneDepthKeyframeSelector()
    private let captureCoach = ScannerCaptureCoach()
    private var captureAdviceObservedAt: TimeInterval = 0
    private let maximumPendingEvents = 512
    private let maximumPendingFrames = 2

    private lazy var captureCoordinator: ARSessionCoordinator = {
        ARSessionCoordinator(
            normalizedFrameSink: { [weak self] frame in self?.receive(frame) },
            depthRejectionSink: { [weak self] reason in self?.receiveDepthRejection(reason) },
            meshAnchorSink: { [weak self] event in self?.receive(event) },
            epochControlSink: { [weak self] control in self?.receive(control) }
        )
    }()

    init(
        trustedRoot: URL? = nil,
        sourceIsSynthetic: Bool = false,
        offlineExportOperation: OfflineExportOperation? = nil,
        archiveExportOperation: ArchiveExportOperation? = nil
    ) {
        let documentsRoot = FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask
        ).first!
        var defaultRoot = documentsRoot.appendingPathComponent("HSSProjects", isDirectory: true)
#if DEBUG && targetEnvironment(simulator)
        // Each explicit UI-test launch owns a new namespace. Never clear or
        // borrow the installed app's existing library to reset a test fixture.
        if let isolated = Self.uiTestingRoot(documentsRoot: documentsRoot,
            environment: ProcessInfo.processInfo.environment) {
            defaultRoot = isolated
        }
#endif
        let root = trustedRoot ?? defaultRoot
        self.trustedRoot = root
        self.catalogURL = root.appendingPathComponent("workspace-catalog.json", isDirectory: false)
        self.sourceIsSynthetic = sourceIsSynthetic
        self.appearanceStore = ScannerAppearanceStore(trustedRoot: root)
        // Default production work still runs off the UI executor. Injectable
        // operations let native tests hold an old completion across navigation
        // without depending on file size, disk timing, or an actual failure.
        self.offlineExportOperation = offlineExportOperation ?? { snapshot, projectID, root, appearance in
            try await Task.detached(priority: .userInitiated) {
                try ScannerOfflineExporter.export(snapshot: snapshot, projectID: projectID,
                    trustedRoot: root, appearance: appearance)
            }.value
        }
        self.archiveExportOperation = archiveExportOperation ?? { projectPath, destinationPath in
            try await HSSProjectArchiveExport.writeArchive(
                projectPath: projectPath, destinationPath: destinationPath)
        }

        Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()
            .sink { [weak self] _ in
                guard let self, captureAdvice != nil,
                      ProcessInfo.processInfo.systemUptime - captureAdviceObservedAt > 1.5 else { return }
                captureAdvice = nil
            }.store(in: &cancellables)

        captureCoordinator.$phase
            .removeDuplicates()
            .sink { [weak self] capturePhase in self?.capturePhaseChanged(capturePhase) }
            .store(in: &cancellables)
        NotificationCenter.default.publisher(for: UIApplication.didEnterBackgroundNotification)
            .sink { [weak self] _ in
                Task { @MainActor in self?.applicationEnteredBackground() }
            }
            .store(in: &cancellables)
        NotificationCenter.default.publisher(for: ProcessInfo.thermalStateDidChangeNotification)
            .sink { [weak self] _ in
                Task { @MainActor in self?.sampleResourcesDuringCapture() }
            }
            .store(in: &cancellables)
        NotificationCenter.default.publisher(for: UIApplication.didReceiveMemoryWarningNotification)
            .sink { [weak self] _ in
                Task { @MainActor in
                    self?.resourceMonitor.noteMemoryWarning()
                    self?.sampleResourcesDuringCapture()
                }
            }
            .store(in: &cancellables)
        Timer.publish(every: 5, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                Task { @MainActor in self?.sampleResourcesDuringCapture() }
            }
            .store(in: &cancellables)
    }

    func reloadProjects() {
        do {
            try FileManager.default.createDirectory(at: trustedRoot, withIntermediateDirectories: true)
            let catalog = try loadCatalog()
            refreshLibrary(catalog)
            if selectedProjectID == nil { phase = .browsing }
        } catch {
            fail(
                title: "Project library unavailable",
                message: error.localizedDescription,
                recovery: .returnToProjectList
            )
        }
    }

#if DEBUG
#if targetEnvironment(simulator)
    static func uiTestingRoot(documentsRoot: URL, environment: [String: String]) -> URL? {
        guard let runID = environment["HSS_UI_TEST_RUN_ID"],
              let id = UUID(uuidString: runID),
              runID.lowercased() == id.uuidString.lowercased() else { return nil }
        return documentsRoot.appendingPathComponent("HSSUITestProjects", isDirectory: true)
            .appendingPathComponent(id.uuidString, isDirectory: true)
    }
#endif

    /// Explicit native UI-test setup only. Release has no launch-time seeding
    /// path. Refuses a nonempty library and uses actual source/checkpoint APIs.
    func seedReferenceRoomForUITesting() async throws {
        guard ProcessInfo.processInfo.environment["HSS_UI_REFERENCE_ROOM"] == "1" else { return }
        try FileManager.default.createDirectory(at: trustedRoot, withIntermediateDirectories: true)
        guard try loadCatalog().projects.isEmpty,
              try FileManager.default.contentsOfDirectory(atPath: trustedRoot.path).isEmpty else { return }
        let projectID = UUID(uuidString: "01000000-0000-5000-8000-000000000000")!
        let captureID = UUID(uuidString: "02000000-0000-5000-8000-000000000000")!
        let runtime = try HSSProjectRuntime(projectID: projectID, captureID: captureID,
            sourceIsSynthetic: true, trustedRoot: trustedRoot, openMode: .create)
        let matrix: [Float] = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
        try await runtime.setEpoch(CaptureEpochControl(sessionEpoch: 1, relationshipState: .accepted,
            projectFromWorld: matrix.map(Double.init), transformRevision: 1), eventSequence: 1, monotonicTime: 0)
        let vertices: [SIMD3<Float>] = [SIMD3(0,0,0), SIMD3(4,0,0), SIMD3(4,0,-5), SIMD3(0,0,-5),
            SIMD3(0,2.8,0), SIMD3(4,2.8,0), SIMD3(4,2.8,-5), SIMD3(0,2.8,-5)]
        let triangles: [UInt32] = [0,1,5, 0,5,4, 1,2,6, 1,6,5, 2,3,7, 2,7,6, 3,0,4, 3,4,7]
        let mesh = OwnedMeshAnchorEvent(kind: .add,
            anchorID: UUID(uuidString: "03000000-0000-5000-8000-000000000000")!,
            anchorRevision: 1, generation: 1, sessionEpoch: 1, sequence: 2, monotonicTimestamp: 0.1,
            worldFromAnchor: matrix, payload: MeshAnchorPayload(vertices: vertices, normals: [],
                triangleIndices: triangles, classifications: [], localBoundsMin: SIMD3(0,0,-5), localBoundsMax: SIMD3(4,2.8,0)))
        try await runtime.submitMeshEvent(mesh, eventSequence: 2, projectTime: 0.1)
        try await runtime.setPlanReference(projectFromFloor: matrix.map(Double.init), cutHeightMetres: 1.2,
            sessionEpoch: 1, eventSequence: 3, monotonicTime: 0.2)
        let snapshot = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        guard snapshot.verticesMetres.count == 8,
              ScannerBoundaryEdge.decode(snapshot: snapshot, projectID: projectID).count == 8 else {
            throw HSSBridgeError.invalidValue("The synthetic reference room did not expose its eight supported cut segments.")
        }
        _ = try await runtime.checkpoint()
        try publishCatalogEntry(WorkspaceCatalogEntry(id: projectID, captureID: captureID,
            name: "Reference room", updatedAt: Date(), sceneRevision: snapshot.revision,
            state: .frozen, sourceIsSynthetic: true))
        reloadProjects()
    }
#endif

    func createProject(named name: String) {
        guard !isManagingLibrary, !captureCoordinator.isSessionRunning, !drainScheduled else { return }
        let displayName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !displayName.isEmpty else {
            fail(title: "A space name is required", message: "Enter a name before creating the local project.", recovery: nil)
            return
        }
        phase = .preparingProject
        currentIssue = nil
        let projectID = UUID()
        let captureID = UUID()

        Task { [weak self] in
            guard let self else { return }
            do {
                try FileManager.default.createDirectory(at: trustedRoot, withIntermediateDirectories: true)
                let candidate = try HSSProjectRuntime(
                    projectID: projectID,
                    captureID: captureID,
                    sourceIsSynthetic: sourceIsSynthetic,
                    trustedRoot: trustedRoot,
                    openMode: .create
                )
                // Store creation already persists empty project metadata. The
                // first source checkpoint is valid only after source events.
                guard captureCoordinator.prepareForProject(lastCommittedEpoch: 0, hasRetainedSource: false) else {
                    throw HSSBridgeError.invalidValue("Capture epoch identity is unavailable for this project.")
                }
                let entry = WorkspaceCatalogEntry(
                    id: projectID, captureID: captureID, name: displayName,
                    updatedAt: Date(), sceneRevision: 0, state: .draft,
                    sourceIsSynthetic: sourceIsSynthetic
                )
                try publishCatalogEntry(entry)
                setOptionalAppearanceSourceVerified(true)
                runtime = candidate
                selectedCatalogEntry = entry
                selectedProjectID = projectID
                assignSelectedName(displayName)
                latestSnapshot = try await candidate.snapshot(
                    provenance: sourceIsSynthetic ? [.syntheticTest] : [],
                    frozen: false
                )
                observedProvenance = sourceIsSynthetic ? [.syntheticTest] : []
                nextEventSequence = 1
                lastDurableEventTime = 0
                pendingCommandJournal = false
                frozenCommandInFlight = false
                reviewSnapshotRefreshRequired = false
                acceptedEpochProjectTransforms.removeAll()
                keyframeSelector.reset()
                captureAccounting = .zero
                didReportDepthKeyframeCapacity = false
                resetTargetSelection()
                saveState = .saved(entry.updatedAt)
                exportState = .idle
                projectArchiveExportState = .idle
                reloadProjects()
                phase = .ready
            } catch {
                fail(title: "Project could not be created", message: describe(error), recovery: .returnToProjectList)
            }
        }
    }

    func importProjectArchive(at url: URL) {
        guard !isManagingLibrary, !captureCoordinator.isSessionRunning, !drainScheduled else { return }
        guard url.isFileURL else {
            fail(title: "Project import failed", message: "Choose a local HSSCAN archive.", recovery: .returnToProjectList)
            return
        }
        phase = .openingProject
        currentIssue = nil
        let sourcePath = url.path(percentEncoded: false)
        let hasSecurityScope = url.startAccessingSecurityScopedResource()

        Task { [weak self] in
            defer {
                if hasSecurityScope { url.stopAccessingSecurityScopedResource() }
            }
            guard let self else { return }
            let stageRoot = trustedRoot.appendingPathComponent(".hss-import-\(UUID().uuidString)", isDirectory: true)
            do {
                try FileManager.default.createDirectory(at: trustedRoot, withIntermediateDirectories: true)
                let payload = stageRoot.appendingPathComponent("payload", isDirectory: true)
                try await HSSProjectImportBackgroundWork.extractArchive(
                    sourcePath: sourcePath,
                    stagingPath: payload.path(percentEncoded: false)
                )
                let metadata = try HSSImportedProjectMetadata.parse(Data(contentsOf: payload.appendingPathComponent("project.json")))
                let projectDirectory = stageRoot.appendingPathComponent(metadata.projectID.hssDirectoryName, isDirectory: true)
                let finalDirectory = trustedRoot.appendingPathComponent(metadata.projectID.hssDirectoryName, isDirectory: true)
                guard !FileManager.default.fileExists(atPath: finalDirectory.path),
                      !(try loadCatalog()).projects.contains(where: { $0.id == metadata.projectID }) else {
                    throw HSSProjectImportError.projectAlreadyExists
                }
                try FileManager.default.moveItem(at: payload, to: projectDirectory)

                // Validate every committed source/manifest and replay into an
                // isolated engine before any staged bytes enter the library.
                var candidate: HSSProjectRuntime? = try HSSProjectRuntime(
                    projectID: metadata.projectID,
                    captureID: UUID(),
                    sourceIsSynthetic: metadata.sourceIsSynthetic,
                    trustedRoot: stageRoot,
                    openMode: .openExisting
                )
                let stagedReplay = try await candidate!.replayCommittedSource()
                try await candidate!.drain()
                candidate = nil
                let provenance: Set<ScannerSceneProvenance> = metadata.sourceIsSynthetic
                    ? [.syntheticTest, .importedReplay] : [.importedReplay]
                let entry = WorkspaceCatalogEntry(
                    id: metadata.projectID, captureID: stagedReplay.captureID,
                    name: "Imported \(metadata.projectID.uuidString.prefix(8))", updatedAt: Date(),
                    sceneRevision: 0,
                    state: .frozen, sourceIsSynthetic: metadata.sourceIsSynthetic
                )
                let adopted = try await HSSProjectImportAdoption.adopt(
                    stagedProject: projectDirectory, destination: finalDirectory,
                    validate: {
                        let runtime = try HSSProjectRuntime(
                            projectID: metadata.projectID,
                            captureID: stagedReplay.captureID,
                            sourceIsSynthetic: metadata.sourceIsSynthetic,
                            trustedRoot: trustedRoot,
                            openMode: .openExisting
                        )
                        let replay = try await runtime.replayCommittedSource()
                        try await runtime.drain()
                        let snapshot = try await runtime.snapshot(provenance: provenance, frozen: true)
                        guard replay.captureID == stagedReplay.captureID,
                              replay.lastEventSequence == stagedReplay.lastEventSequence,
                              replay.lastEventSequence < UInt64.max - 1,
                              captureCoordinator.prepareForProject(
                                lastCommittedEpoch: replay.highestSessionEpoch,
                                hasRetainedSource: replay.durableEventCount > 0
                              ) else {
                            throw HSSBridgeError.invalidValue("Imported source changed or its capture identity is exhausted.")
                        }
                        return (runtime, replay, snapshot)
                    },
                    publish: { adopted in
                        var published = entry
                        published.captureID = adopted.1.captureID
                        published.sceneRevision = adopted.2.revision
                        try publishCatalogEntry(published)
                    }
                )
                try? FileManager.default.removeItem(at: stageRoot)
                var adoptedEntry = entry
                adoptedEntry.captureID = adopted.1.captureID
                adoptedEntry.sceneRevision = adopted.2.revision
                self.runtime = adopted.0
                setOptionalAppearanceSourceVerified(true)
                selectedCatalogEntry = adoptedEntry
                selectedProjectID = entry.id
                assignSelectedName(adoptedEntry.name)
                latestSnapshot = adopted.2
                observedProvenance = provenance
                nextEventSequence = adopted.1.lastEventSequence + 1
                refreshOptionalAppearance()
                lastDurableEventTime = adopted.1.lastMonotonicTime
                pendingCommandJournal = false
                frozenCommandInFlight = false
                reviewSnapshotRefreshRequired = false
                acceptedEpochProjectTransforms.removeAll()
                resetTargetSelection()
                saveState = .clean
                exportState = .idle
                projectArchiveExportState = .idle
                phase = .frozen
            } catch {
                try? FileManager.default.removeItem(at: stageRoot)
                fail(title: "Project import failed", message: describe(error), recovery: .returnToProjectList)
            }
        }
    }

    func startCapture() {
        guard runtime != nil, selectedProjectID != nil, !captureCoordinator.isSessionRunning,
              !pendingCommandJournal, !frozenCommandInFlight,
              !reviewSnapshotRefreshRequired else { return }
        guard !resourcePauseLatched else {
            showResourcePauseIssue(checkpointSucceeded: resourceCheckpointSucceeded)
            return
        }
        currentIssue = nil
        exportState = .idle
        projectArchiveExportState = .idle
        phase = .startingCapture
        captureCoach.reset()
        captureAdvice = nil
        keyframeSelector.reset()
        captureAccounting = .zero
        didReportDepthKeyframeCapacity = false
        resetTargetSelection()
        if captureCoordinator.canRequestCameraPermission {
            Task { [weak self] in
                guard let self else { return }
                await captureCoordinator.requestCameraPermission()
                guard captureCoordinator.canStartSession else {
                    fail(
                        title: "Camera access is unavailable",
                        message: captureCoordinator.phase.summary,
                        recovery: nil
                    )
                    return
                }
                beginCaptureSession()
            }
        } else {
            beginCaptureSession()
        }
    }

    func stopCapture() {
        guard captureCoordinator.isSessionRunning else {
            if latestSnapshot != nil { phase = .frozen }
            return
        }
        phase = .stoppingCapture
        freezeRequested = true
        captureCoordinator.stopSession()
    }

    func saveSelectedProject() {
        guard runtime != nil, selectedProjectID != nil else { return }
        if pendingCommandJournal {
            retryPendingCommandJournal()
            return
        }
        guard !frozenCommandInFlight else { return }
        if resourcePauseInProgress {
            saveRequested = true
            return
        }
        if captureCoordinator.isSessionRunning {
            saveAfterFreeze = true
            stopCapture()
            return
        }
        if drainScheduled {
            saveRequested = true
            return
        }
        performSave()
    }

    func exportSelectedProject() {
        // An accidental second invocation must not invalidate the active job
        // by replacing its progress with an ineligibility error.
        guard exportState != .exporting else { return }
        guard canExport,
              let snapshot = latestSnapshot,
              let projectID = selectedProjectID else {
            exportState = .failed(
                String(localized: "Only a saved, frozen accepted revision with measured plan and mesh geometry can be exported.")
            )
            return
        }
        let request = ExportRequest(projectID: projectID, sceneRevision: snapshot.revision)
        offlineExportRequest = request
        exportState = .exporting
        let root = trustedRoot
        let appearance = includeSourceImagesInExport ? reviewTexturedMesh : nil
        let operation = offlineExportOperation
        Task { [weak self] in
            do {
                let directory = try await operation(snapshot, projectID, root, appearance)
                guard let self, ownsOfflineExport(request) else { return }
                exportState = .exported(directory: directory, at: Date())
            } catch {
                guard let self, ownsOfflineExport(request) else { return }
                exportState = .failed(error.localizedDescription)
            }
        }
    }

    func exportSelectedProjectArchive() {
        guard projectArchiveExportState != .exporting else { return }
        guard canExportProjectArchive,
              let projectID = selectedProjectID,
              let entry = selectedCatalogEntry else {
            projectArchiveExportState = .failed(
                String(localized: "Only a saved local project can be archived.")
            )
            return
        }
        let request = ExportRequest(projectID: projectID, sceneRevision: entry.sceneRevision)
        archiveExportRequest = request
        projectArchiveExportState = .exporting
        let projectDirectory = trustedRoot.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        let archiveDirectory = trustedRoot.appendingPathComponent("archives", isDirectory: true)
        let archive = archiveDirectory.appendingPathComponent(
            ScannerProjectArchiveExportName.make(
                projectID: projectID,
                sceneRevision: entry.sceneRevision
            ),
            isDirectory: false
        )
        let operation = archiveExportOperation
        Task { [weak self] in
            do {
                try await operation(projectDirectory.path(percentEncoded: false),
                    archive.path(percentEncoded: false))
                guard let self, ownsArchiveExport(request) else { return }
                projectArchiveExportState = .exported(archive)
            } catch {
                guard let self, ownsArchiveExport(request) else { return }
                projectArchiveExportState = .failed(describe(error))
            }
        }
    }

    private func ownsOfflineExport(_ request: ExportRequest) -> Bool {
        offlineExportRequest == request && exportState == .exporting
            && selectedProjectID == request.projectID
            && latestSnapshot?.revision == request.sceneRevision && phase == .frozen
    }

    private func ownsArchiveExport(_ request: ExportRequest) -> Bool {
        archiveExportRequest == request && projectArchiveExportState == .exporting
            && selectedProjectID == request.projectID
            && selectedCatalogEntry?.sceneRevision == request.sceneRevision
            && latestSnapshot?.revision == request.sceneRevision && phase == .frozen
    }

    private func invalidateInFlightExports() {
        offlineExportRequest = nil
        archiveExportRequest = nil
        if exportState == .exporting { exportState = .idle }
        if projectArchiveExportState == .exporting { projectArchiveExportState = .idle }
        // A completed immutable bundle/archive is not deleted. Navigation and
        // existing explicit state resets decide whether to show that result.
    }

    func openProject(id: UUID) {
        guard !isManagingLibrary, !captureCoordinator.isSessionRunning, !drainScheduled,
              !pendingCommandJournal, !frozenCommandInFlight else { return }
        guard let entry = (try? loadCatalog().projects.first(where: { $0.id == id && $0.deletedAt == nil })) ?? nil else {
            fail(title: "Project is unavailable", message: "The selected project is not present in the local catalog.", recovery: .returnToProjectList)
            return
        }
        phase = .openingProject
        currentIssue = nil

        Task { [weak self] in
            guard let self else { return }
            do {
                let candidate = try HSSProjectRuntime(
                    projectID: entry.id,
                    captureID: entry.captureID,
                    sourceIsSynthetic: entry.sourceIsSynthetic,
                    trustedRoot: trustedRoot,
                    openMode: .openExisting
                )
                let replay = try await candidate.replayCommittedSource()
                try await candidate.drain()
                setOptionalAppearanceSourceVerified(false)
                do {
                    try await appearanceStore.reconcile(projectID: entry.id, lastDurableEventSequence: replay.lastEventSequence)
                    setOptionalAppearanceSourceVerified(true)
                } catch {
                    appearanceMessage = String(localized: "Measured geometry is available. Source images could not be verified for this revision.")
                }
                let provenance: Set<ScannerSceneProvenance> = entry.sourceIsSynthetic
                    ? [.syntheticTest, .importedReplay]
                    : [.importedReplay]
                let snapshot = try await candidate.snapshot(provenance: provenance, frozen: true)
                guard replay.lastEventSequence < UInt64.max - 1,
                      captureCoordinator.prepareForProject(
                        lastCommittedEpoch: replay.highestSessionEpoch,
                        hasRetainedSource: replay.durableEventCount > 0
                      ) else {
                    throw HSSBridgeError.invalidValue("Recovered capture identity is exhausted.")
                }
                var reconciledEntry = entry
                reconciledEntry.captureID = replay.captureID
                reconciledEntry.sceneRevision = snapshot.revision
                if reconciledEntry != entry { try publishCatalogEntry(reconciledEntry) }
                runtime = candidate
                selectedCatalogEntry = reconciledEntry
                selectedProjectID = entry.id
                assignSelectedName(entry.name)
                latestSnapshot = snapshot
                commandHistory = try? await candidate.reviewCommandHistory()
                observedProvenance = provenance
                nextEventSequence = replay.lastEventSequence + 1
                refreshOptionalAppearance()
                lastDurableEventTime = replay.lastMonotonicTime
                pendingCommandJournal = false
                frozenCommandInFlight = false
                reviewSnapshotRefreshRequired = false
                acceptedEpochProjectTransforms.removeAll()
                resetTargetSelection()
                saveState = .clean
                exportState = .idle
                projectArchiveExportState = .idle
                phase = .frozen
            } catch {
                fail(title: "Project could not be reopened", message: describe(error), recovery: .reopenProject)
            }
        }
    }

    func showLibrary() {
        guard canShowLibrary else { return }
        runtime = nil
        pendingCommandJournal = false
        frozenCommandInFlight = false
        reviewSnapshotRefreshRequired = false
        selectedCatalogEntry = nil
        selectedProjectID = nil
        assignSelectedName("")
        latestSnapshot = nil
        observedProvenance = []
        saveState = .noProject
        exportState = .idle
        projectArchiveExportState = .idle
        currentIssue = nil
        resetTargetSelection()
        phase = .browsing
        reloadProjects()
    }

    /// Library mutations never operate on an open runtime or pending journal.
    private var canManageLibrary: Bool {
        phase == .browsing && runtime == nil && selectedProjectID == nil
            && !isManagingLibrary && !drainScheduled && !pendingCommandJournal
            && !frozenCommandInFlight && !captureCoordinator.isSessionRunning
    }

    func renameProject(id: UUID, name: String) {
        guard canManageLibrary else { return }
        let cleanName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanName.isEmpty, cleanName.count <= 160 else {
            libraryFailure(String(localized: "Use a project name between 1 and 160 characters."))
            return
        }
        mutateLibrary(id: id) { entry in
            guard entry.deletedAt == nil else { return }
            entry.name = cleanName
            // Renaming is metadata, not a reconstructed geometry revision.
            entry.updatedAt = Date()
        }
    }

    func moveProjectToTrash(id: UUID) {
        guard canManageLibrary else { return }
        mutateLibrary(id: id) { $0.deletedAt = Date() }
    }

    func restoreProject(id: UUID) {
        guard canManageLibrary else { return }
        mutateLibrary(id: id) { entry in
            guard entry.purgeStarted != true else { return }
            entry.deletedAt = nil
        }
    }

    func permanentlyDeleteProject(id: UUID) {
        guard canManageLibrary else { return }
        do {
            var catalog = try loadCatalog()
            guard let index = catalog.projects.firstIndex(where: { $0.id == id }),
                  catalog.projects[index].deletedAt != nil else { return }
            catalog.projects[index].purgeStarted = true
            try writeCatalog(catalog)
        } catch {
            libraryFailure(describe(error))
            return
        }
        isManagingLibrary = true
        let root = trustedRoot
        Task { [weak self] in
            guard let self else { return }
            defer { isManagingLibrary = false }
            do {
                // Large source/photo trees are removed off the UI executor.
                try await Task.detached(priority: .utility) {
                    try HSSLocalProjectRemoval.remove(projectID: id, trustedRoot: root)
                }.value
                // Purge first, catalog last: a failed purge stays visible and
                // retryable in Trash. Repeated purge is deliberately idempotent.
                var catalog = try loadCatalog()
                catalog.projects.removeAll { $0.id == id && $0.deletedAt != nil }
                try writeCatalog(catalog)
                currentIssue = nil
            } catch {
                libraryFailure(describe(error))
            }
        }
    }

    private func mutateLibrary(id: UUID, change: (inout WorkspaceCatalogEntry) -> Void) {
        do {
            var catalog = try loadCatalog()
            guard let index = catalog.projects.firstIndex(where: { $0.id == id }) else { return }
            change(&catalog.projects[index])
            try writeCatalog(catalog)
            currentIssue = nil
        } catch { libraryFailure(describe(error)) }
    }

    private func libraryFailure(_ message: String) {
        currentIssue = ScannerWorkspaceIssue(
            id: UUID(), title: String(localized: "Library change failed"),
            message: message, recoveryAction: nil
        )
    }

    func selectTarget(_ tap: NativeTargetTap) {
        guard phase == .capturing else {
            setPendingTarget(.captureUnavailable)
            return
        }
        guard let source = captureCoordinator.targetSelectionSource(viewport: tap.viewport) else {
            setPendingTarget(.missingCurrentDepthFrame)
            return
        }
        let normalizedFrame = source.normalizedFrame
        guard let currentEpoch = captureCoordinator.epochRelocalization.currentEpoch,
              let projectFromWorld = acceptedEpochProjectTransforms[currentEpoch] else {
            setPendingTarget(.missingAcceptedEpochTransform)
            return
        }
        switch NativeTargetSelectionAdapter.prepare(
            tap: tap,
            normalizedFrame: normalizedFrame,
            overlayCalibration: source.calibration,
            overlayFrame: source.overlayFrame,
            displayTransform: source.displayTransform,
            currentEpoch: currentEpoch,
            projectFromWorld: projectFromWorld,
            now: ProcessInfo.processInfo.systemUptime,
            maximumFrameAge: 0.25
        ) {
        case .failure(let reason):
            setPendingTarget(reason)
        case .success(let candidate):
            pendingTargetCandidate = candidate
            if acceptedTargetSourceFrames.contains(candidate.sourceFrame),
               let revision = latestSnapshot?.revision,
               let selection = NativeTargetSelectionAdapter.confirm(
                candidate,
                acceptedSourceFrame: candidate.sourceFrame,
                currentSceneRevision: revision
               ) {
                pendingTargetCandidate = nil
                targetSelectionState = .confirmed(selection)
                return
            }
            if !queuedTargetSourceFrames.contains(candidate.sourceFrame) {
                guard keyframeSelector.retainForTarget(normalizedFrame) else {
                    setPendingTarget(.sourceCapacityReached)
                    return
                }
                captureAccounting = keyframeSelector.accounting
                enqueueTargetFrame(normalizedFrame)
            }
            targetSelectionState = .pending(.awaitingSourceAcceptance)
        }
    }

    func confirmPlanReference(projectFromFloor: simd_double4x4, cutHeightMetres: Double) {
        guard canConfirmPlanReference,
              cutHeightMetres.isFinite, cutHeightMetres > 0, cutHeightMetres <= 100,
              let epoch = retainedPlanEpoch else { return }
        let matrix = [
            projectFromFloor.columns.0.x, projectFromFloor.columns.0.y, projectFromFloor.columns.0.z, projectFromFloor.columns.0.w,
            projectFromFloor.columns.1.x, projectFromFloor.columns.1.y, projectFromFloor.columns.1.z, projectFromFloor.columns.1.w,
            projectFromFloor.columns.2.x, projectFromFloor.columns.2.y, projectFromFloor.columns.2.z, projectFromFloor.columns.2.w,
            projectFromFloor.columns.3.x, projectFromFloor.columns.3.y, projectFromFloor.columns.3.z, projectFromFloor.columns.3.w,
        ]
        // Preserve the review-only lifecycle after the queued control drains.
        // An intermediate mutable snapshot must not remain the frozen review.
        // Reopened projects have no live capture clock; start an ordered review
        // control clock at the verified durable cursor, never at arbitrary zero.
        captureClock = ProjectCaptureClock(lastDurableTime: lastDurableEventTime)
        freezeRequested = true
        enqueue(.planReference(
            matrix: matrix, cutHeightMetres: cutHeightMetres,
            epoch: epoch, monotonicTime: ProcessInfo.processInfo.systemUptime
        ))
    }

    /// This review-only command does not claim that the live-camera target
    /// selection maps to a durable core entity. It records a zero-target
    /// unknown-evidence note and leaves raw captured geometry unchanged.
    func recordMissingEvidence(reason: String) {
        beginReviewCommand(reason: reason, edit: nil)
    }

    func applyBoundaryEdit(_ edit: ScannerBoundaryEdit, expectedRevision: UInt64) {
        guard latestSnapshot?.revision == expectedRevision, canRecordMissingEvidence else { return }
        let edges = editableBoundary
        let valid: Bool
        switch edit {
        case .protect(let id), .split(let id, _): valid = edges.contains { $0.id == id }
        case .lock(let id, let position, let confirmed):
            valid = confirmed && edges.contains { ($0.startID == id && $0.start == position) || ($0.endID == id && $0.end == position) }
        case .dimension(let a, let b, _, _, _, _):
            valid = edges.contains { ($0.startID == a && $0.endID == b) || ($0.startID == b && $0.endID == a) }
        case .undo(let id): valid = commandHistory?.scene_revision == String(expectedRevision) && commandHistory?.undoID == id
        case .redo(let id): valid = commandHistory?.scene_revision == String(expectedRevision) && commandHistory?.redoID == id
        }
        guard valid else {
            currentIssue = ScannerWorkspaceIssue(id: UUID(), title: String(localized: "Boundary selection changed"),
                message: String(localized: "Select a boundary in the current scan before applying a correction. The accepted shape is unchanged."), recoveryAction: nil)
            return
        }
        beginReviewCommand(reason: "", edit: edit)
    }

    private func beginReviewCommand(reason: String, edit: ScannerBoundaryEdit?) {
        guard canRecordMissingEvidence, let runtime else { return }
        let trimmedReason = reason.trimmingCharacters(in: .whitespacesAndNewlines)
        guard edit != nil || !trimmedReason.isEmpty else {
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(),
                title: "Missing-evidence note needs a reason",
                message: "Describe what remains unobserved before recording it.",
                recoveryAction: nil
            )
            return
        }
        guard nextEventSequence < UInt64.max else {
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(),
                title: "Project event identity exhausted",
                message: "Start a new local project before recording another review note.",
                recoveryAction: .returnToProjectList
            )
            return
        }
        let sequence = nextEventSequence
        // Review-only control ordering is defined by event sequence. Do not
        // append device boot time or imply a physical capture interval here.
        let eventTime = lastDurableEventTime
        frozenCommandInFlight = true
        Task { [weak self] in
            guard let self else { return }
            do {
                let outcome: HSSFrozenUnknownCommandOutcome
                if let edit, let revision = latestSnapshot?.revision {
                    outcome = try await runtime.applyFrozenBoundaryCorrection(edit, expectedRevision: revision,
                        eventSequence: sequence, monotonicTime: eventTime)
                } else {
                    outcome = try await runtime.recordFrozenMissingEvidence(reason: trimmedReason,
                        eventSequence: sequence, monotonicTime: eventTime)
                }
                switch outcome {
                case .journaled(let application):
                    await publishJournaledReviewCommand(application, runtime: runtime)
                case .journalRetryRequired(let application, let failure):
                    guard application.eventSequence == sequence else {
                        throw HSSBridgeError.invalidValue(
                            "The pending command returned a different event sequence."
                        )
                    }
                    frozenCommandInFlight = false
                    pendingCommandJournal = true
                    saveState = .failed("The review result is not yet journaled: \(failure)")
                    currentIssue = ScannerWorkspaceIssue(
                        id: UUID(),
                        title: String(localized: "Review result needs a journal retry"),
                        message: String(localized: "The review result is not saved yet. Retry its exact local journal before saving, leaving, or starting capture."),
                        recoveryAction: .retryCommandJournal
                    )
                case .resultRecoveryRequired(let failure):
                    frozenCommandInFlight = false
                    pendingCommandJournal = true
                    saveState = .failed("The review command needs exact-result recovery: \(failure)")
                    currentIssue = ScannerWorkspaceIssue(
                        id: UUID(),
                        title: String(localized: "Review result needs recovery"),
                        message: String(localized: "The exact review result must be recovered before it can be saved. Retry the same local command before saving, leaving, or starting capture."),
                        recoveryAction: .retryCommandJournal
                    )
                }
            } catch {
                frozenCommandInFlight = false
                currentIssue = ScannerWorkspaceIssue(
                    id: UUID(),
                    title: edit == nil ? String(localized: "Missing evidence was not recorded") : String(localized: "Correction was not applied"),
                    message: describe(error),
                    recoveryAction: nil
                )
            }
        }
    }

    private func retryPendingCommandJournal() {
        guard pendingCommandJournal, !frozenCommandInFlight, let runtime else { return }
        frozenCommandInFlight = true
        Task { [weak self] in
            guard let self else { return }
            do {
                let application = try await runtime.retryPendingCommandJournal()
                await publishJournaledReviewCommand(application, runtime: runtime)
            } catch {
                frozenCommandInFlight = false
                currentIssue = ScannerWorkspaceIssue(
                    id: UUID(),
                    title: String(localized: "Review result still needs a journal retry"),
                    message: describe(error),
                    recoveryAction: .retryCommandJournal
                )
            }
        }
    }

    private func publishJournaledReviewCommand(
        _ application: HSSFrozenUnknownCommandApplication,
        runtime: HSSProjectRuntime
    ) async {
        let sequenceMatchedReservation = application.eventSequence == nextEventSequence
            && application.eventSequence < UInt64.max
        // The native store already owns this exact command when this method is
        // reached. Commit the main-actor cursor before attempting a fallible
        // snapshot refresh, so a refresh error cannot reuse its sequence or
        // falsely present the project as clean.
        nextEventSequence = application.eventSequence + 1
        lastDurableEventTime = max(lastDurableEventTime, application.monotonicTime)
        pendingCommandJournal = false
        frozenCommandInFlight = false
        saveState = .hasUnsavedChanges
        exportState = .idle
        projectArchiveExportState = .idle
        phase = .frozen
        guard sequenceMatchedReservation else {
            reviewSnapshotRefreshRequired = true
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(),
                title: "Review state needs recovery",
                message: String(localized: "The review result did not match the reserved event order. Save or reopen the project before another review action."),
                recoveryAction: .retrySave
            )
            return
        }
        do {
            let snapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: true)
            guard snapshot.revision == application.afterSceneRevision else {
                throw HSSBridgeError.invalidValue(
                    "The journaled review command snapshot does not match its accepted revision."
                )
            }
            latestSnapshot = snapshot
            commandHistory = try? await runtime.reviewCommandHistory()
            if application.disposition != .applied {
                currentIssue = ScannerWorkspaceIssue(id: UUID(), title: String(localized: "Correction was not applied"),
                    message: application.disposition == .conflict
                        ? String(localized: "This correction conflicts with an earlier measurement or position lock. The last accepted shape is kept. Review the selected boundary and your proposed value.")
                        : String(localized: "This correction is no longer compatible with the current scan. The last accepted shape is kept. Select the boundary again and review your input."), recoveryAction: nil)
            } else {
                let command = try JSONSerialization.jsonObject(with: application.commandJSON) as? [String: Any]
                let isMissingEvidence = command?["type"] as? String == "mark_unknown"
                currentIssue = ScannerWorkspaceIssue(id: UUID(),
                    title: isMissingEvidence ? String(localized: "Missing evidence recorded") : String(localized: "Correction applied"),
                    message: isMissingEvidence
                        ? String(localized: "Applied locally. Save this draft to make the review note durable.")
                        : String(localized: "The accepted correction is visible in your scan. Save this draft to keep the change."), recoveryAction: nil)
            }
            reviewSnapshotRefreshRequired = false
            refreshTargetRevisionBinding()
        } catch {
            reviewSnapshotRefreshRequired = true
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(),
                title: String(localized: "Review result needs a snapshot refresh"),
                message: String(localized: "The review result is recorded locally, but its refreshed view is unavailable. Save or reopen the project before another review action."),
                recoveryAction: .retrySave
            )
            return
        }
    }

    func recoverFromCurrentIssue() {
        guard let action = currentIssue?.recoveryAction else { return }
        switch action {
        case .retrySave:
            saveSelectedProject()
        case .retryCommandJournal:
            retryPendingCommandJournal()
        case .reopenProject:
            if let selectedProjectID { openProject(id: selectedProjectID) }
        case .returnToProjectList:
            if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
            pendingEvents.removeAll()
            drainScheduled = false
            saveState = .clean
            showLibrary()
        case .stopCaptureAndSaveDraft:
            saveAfterFreeze = true
            stopCapture()
        }
    }

    private var isSavedState: Bool {
        if case .saved = saveState { true } else { false }
    }

    private func beginCaptureSession() {
        guard let runtime, !resourceSamplingInProgress else { return }
        resourceSamplingInProgress = true
        let reading = resourceMonitor.read(storageDirectory: trustedRoot)
        Task { [weak self] in
            guard let self else { return }
            defer { resourceSamplingInProgress = false }
            do {
                let decision = try await runtime.observeResource(reading)
                updateOptionalAppearanceResourcePolicy(decision)
                if decision.requestCheckpoint || !decision.acceptMetricObservations {
                    await handleResourcePause(decision, runtime: runtime)
                    return
                }
                guard !resourcePauseLatched,
                      selectedProjectID == runtime.projectID,
                      phase == .startingCapture else { return }
                captureClock = ProjectCaptureClock(lastDurableTime: lastDurableEventTime)
                captureCoordinator.startSession()
                switch captureCoordinator.phase {
                case .fallback(let reason), .blocked(let reason):
                    captureCoordinator.stopSession()
                    phase = .captureUnsupported(reason)
                case .failed(let reason):
                    phase = .failed(reason)
                default:
                    break
                }
            } catch {
                resourceMetricIntakeBlocked = true
                resourcePauseLatched = true
                await checkpointAfterResourceInputFailure(runtime, error: error)
            }
        }
    }

    private func sampleResourcesDuringCapture() {
        guard let runtime, captureCoordinator.isSessionRunning,
              !resourceSamplingInProgress, !resourcePauseLatched else { return }
        resourceSamplingInProgress = true
        let reading = resourceMonitor.read(storageDirectory: trustedRoot)
        if reading.hasUnsafeMeasuredPressure {
            // Stop fresh metric intake before yielding to the serial native
            // actor. The ARSession stop barrier still delivers protected
            // lifecycle events and lets previously queued source drain.
            resourceMetricIntakeBlocked = true
            captureCoordinator.stopSession()
        }
        Task { [weak self] in
            guard let self else { return }
            defer { resourceSamplingInProgress = false }
            do {
                if reading.hasUnsafeMeasuredPressure {
                    try await awaitCaptureAndQueueDrain()
                }
                let decision = try await runtime.observeResource(reading)
                updateOptionalAppearanceResourcePolicy(decision)
                if decision.requestCheckpoint || !decision.acceptMetricObservations {
                    await handleResourcePause(decision, runtime: runtime)
                } else if decision.mode == UInt32(HSS_RESOURCE_MODE_CONSERVING) {
                    let message = resourceMessage(for: decision.reason)
                    resourceConservingMessage = message
                    phase = .captureLimited(message)
                    currentIssue = ScannerWorkspaceIssue(
                        id: UUID(),
                        title: String(localized: "Resource pressure limits capture"),
                        message: message,
                        recoveryAction: .stopCaptureAndSaveDraft
                    )
                } else {
                    resourceConservingMessage = nil
                    if captureCoordinator.isSessionRunning { phase = .capturing }
                }
            } catch {
                resourceMetricIntakeBlocked = true
                resourcePauseLatched = true
                if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
                await checkpointAfterResourceInputFailure(runtime, error: error)
            }
        }
    }

    private func awaitCaptureAndQueueDrain() async throws {
        let deadline = ProcessInfo.processInfo.systemUptime + 15
        while captureCoordinator.isSessionRunning || drainScheduled || !pendingEvents.isEmpty {
            guard ProcessInfo.processInfo.systemUptime < deadline else {
                throw HSSBridgeError.invalidValue("Capture stop or queued source drain timed out.")
            }
            try await Task.sleep(for: .milliseconds(10))
        }
    }

    private func handleResourcePause(
        _ decision: ScannerResourceDecision,
        runtime: HSSProjectRuntime
    ) async {
        resourceMetricIntakeBlocked = true
        resourcePauseLatched = true
        resourcePauseInProgress = true
        resourceConservingMessage = nil
        if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
        phase = .captureLimited(resourceMessage(for: decision.reason))
        do {
            try await awaitCaptureAndQueueDrain()
            guard decision.requestCheckpoint else {
                throw HSSBridgeError.invalidValue("Metric capture stopped without a core checkpoint request.")
            }
            let outcome = try await runtime.completeResourceCheckpoint()
            if let checkpoint = outcome.checkpoint {
                try await publishResourceCheckpoint(checkpoint, runtime: runtime)
                resourceCheckpointSucceeded = true
                showResourcePauseIssue(checkpointSucceeded: true)
            } else {
                resourceCheckpointSucceeded = false
                saveState = .failed(outcome.failure ?? String(localized: "The local checkpoint failed."))
                showResourcePauseIssue(checkpointSucceeded: false)
            }
        } catch {
            resourceCheckpointSucceeded = false
            saveState = .failed(describe(error))
            showResourceInputFailure(error)
        }
        resourcePauseInProgress = false
        saveRequested = false
    }

    private func publishResourceCheckpoint(
        _ checkpoint: HSSProjectCheckpoint,
        runtime: HSSProjectRuntime
    ) async throws {
        guard var entry = selectedCatalogEntry else { throw HSSBridgeError.closed }
        let snapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: true)
        // Store durable revision counts checkpoints; scene revision counts
        // accepted edits. The verified manifest is independent of scene count.
        guard checkpoint.durableRevision > 0, checkpoint.durableEventCount > 0 else {
            throw HSSBridgeError.invalidValue("The store did not report a durable source manifest.")
        }
        await commitOptionalAppearance(projectID: entry.id, checkpoint: checkpoint)
        entry.updatedAt = Date()
        entry.sceneRevision = snapshot.revision
        entry.state = .frozen
        try publishCatalogEntry(entry)
        selectedCatalogEntry = entry
        latestSnapshot = snapshot
        saveState = .saved(entry.updatedAt)
        phase = .frozen
        reloadProjects()
    }

    private func checkpointAfterResourceInputFailure(
        _ runtime: HSSProjectRuntime,
        error: Error
    ) async {
        resourcePauseInProgress = true
        do {
            try await awaitCaptureAndQueueDrain()
            try await runtime.drain()
            let checkpoint = try await runtime.checkpoint()
            try await publishResourceCheckpoint(checkpoint, runtime: runtime)
            resourceCheckpointSucceeded = true
        } catch {
            resourceCheckpointSucceeded = false
            saveState = .failed(describe(error))
        }
        resourcePauseInProgress = false
        showResourceInputFailure(error)
    }

    private func resourceMessage(for reason: UInt32) -> String {
        switch reason {
        case UInt32(HSS_RESOURCE_REASON_THERMAL_CRITICAL):
            String(localized: "Device heat stopped metric capture. Retained source is being saved locally.")
        case UInt32(HSS_RESOURCE_REASON_STORAGE_STOP):
            String(localized: "Free storage is too low for safe capture. Retained source is being saved locally.")
        case UInt32(HSS_RESOURCE_REASON_THERMAL_FAIR), UInt32(HSS_RESOURCE_REASON_THERMAL_SERIOUS):
            String(localized: "Device heat is limiting optional preview work. Metric validation remains active.")
        case UInt32(HSS_RESOURCE_REASON_STORAGE_WARNING):
            String(localized: "Local storage is low. Optional preview work is reduced while metric validation remains active.")
        case UInt32(HSS_RESOURCE_REASON_MEMORY_WARNING):
            String(localized: "iOS reported a memory warning. Optional preview work is reduced; memory recovery cannot be verified.")
        default:
            String(localized: "Resource monitoring paused metric capture to protect retained source.")
        }
    }

    private func showResourcePauseIssue(checkpointSucceeded: Bool) {
        phase = .frozen
        currentIssue = ScannerWorkspaceIssue(
            id: UUID(),
            title: String(localized: "Metric capture remains paused"),
            message: checkpointSucceeded
                ? String(localized: "Accepted source was saved locally. Resource recovery cannot be fully verified on this device; review this project and return to the library. Start a new capture only after resolving device heat or storage pressure.")
                : String(localized: "The local checkpoint failed. Metric capture remains paused; retry saving this project."),
            recoveryAction: checkpointSucceeded ? .returnToProjectList : .retrySave
        )
    }

    private func showResourceInputFailure(_ error: Error) {
        phase = .failed(String(localized: "Resource monitoring could not verify safe metric capture."))
        currentIssue = ScannerWorkspaceIssue(
            id: UUID(),
            title: String(localized: "Resource monitoring paused capture"),
            message: "\(String(localized: "Retained source remains local. Retry saving, then review the project.")) \(describe(error))",
            recoveryAction: .retrySave
        )
    }

    private func capturePhaseChanged(_ capturePhase: CaptureSessionPhase) {
        // A frame-bound recommendation cannot survive loss of normal
        // tracking, a stopped session or an unresolved coordinate epoch.
        if capturePhase != .captureReady { captureAdvice = nil }
        if resourcePauseLatched { return }
        switch capturePhase {
        case .captureReady:
            if let resourceConservingMessage {
                phase = .captureLimited(resourceConservingMessage)
            } else {
                phase = .capturing
                currentIssue = nil
            }
        case .limited(let reason):
            phase = .captureLimited(reason)
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(), title: "Precise capture paused", message: reason, recoveryAction: nil
            )
        case .interrupted:
            phase = .captureInterrupted
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(), title: "Capture interrupted",
                message: "Source received before the interruption remains local. Tracking must recover before more metric evidence is accepted.",
                recoveryAction: .stopCaptureAndSaveDraft
            )
        case .fallback(let reason):
            if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
            phase = .captureUnsupported(reason)
        case .blocked(let reason):
            phase = .captureUnsupported(reason)
        case .failed(let reason):
            phase = .failed(reason)
            currentIssue = ScannerWorkspaceIssue(
                id: UUID(), title: "Capture failed", message: reason,
                recoveryAction: .stopCaptureAndSaveDraft
            )
        case .awaitingNormalTracking, .starting, .requestingPermission:
            phase = .startingCapture
        case .stopped:
            if phase == .stoppingCapture, !drainScheduled { scheduleFreeze() }
        case .idle:
            break
        }
    }

    private func receive(_ control: CaptureEpochControl) {
        if control.relationshipState == .accepted {
            acceptedEpochProjectTransforms[control.sessionEpoch] = control.projectFromWorld
            acceptedEpochTransformRevisions[control.sessionEpoch] = control.transformRevision
        } else {
            acceptedEpochProjectTransforms.removeValue(forKey: control.sessionEpoch)
            acceptedEpochTransformRevisions.removeValue(forKey: control.sessionEpoch)
        }
        enqueue(.epoch(control, monotonicTime: ProcessInfo.processInfo.systemUptime))
    }

    private func receive(_ frame: NormalizedSceneDepthFrame) {
        guard !resourceMetricIntakeBlocked else { return }
        captureAdviceObservedAt = ProcessInfo.processInfo.systemUptime
        captureAdvice = captureCoach.observe(frame: frame, resourcePressure: resourceConservingMessage == nil ? 0 : 1)
        let retained = keyframeSelector.shouldRetain(frame)
        captureAccounting = keyframeSelector.accounting
        guard retained else {
            if keyframeSelector.hasReachedCapacity, !didReportDepthKeyframeCapacity {
                didReportDepthKeyframeCapacity = true
                currentIssue = ScannerWorkspaceIssue(
                    id: UUID(),
                    title: "Depth keyframe limit reached",
                    message: "Calibrated keyframes already retained remain available. ARKit mesh capture can continue, but additional depth frames are not counted as independent evidence in this project.",
                    recoveryAction: .stopCaptureAndSaveDraft
                )
            }
            return
        }
        enqueueTargetFrame(frame)
    }

    private func receiveDepthRejection(_ reason: SceneDepthFrameRejection) {
        captureAdvice = nil
        guard reason == .noValidDepthSamples, !resourceMetricIntakeBlocked,
              captureCoordinator.phase == .captureReady else { return }
        captureAdviceObservedAt = ProcessInfo.processInfo.systemUptime
        captureAdvice = captureCoach.observeMissingDepth(resourcePressure: resourceConservingMessage == nil ? 0 : 1)
    }

    private func enqueueTargetFrame(_ frame: NormalizedSceneDepthFrame) {
        guard !resourceMetricIntakeBlocked else { return }
        queuedTargetSourceFrames.append(frame.identity)
        if queuedTargetSourceFrames.count > 16 { queuedTargetSourceFrames.removeFirst() }
        enqueue(.frame(frame))
    }

    private func receive(_ event: OwnedMeshAnchorEvent) {
        if resourceMetricIntakeBlocked && event.kind != .remove { return }
        enqueue(.mesh(event))
    }

    private func enqueue(_ event: PendingNativeEvent) {
        guard runtime != nil else { return }
        if event.isFrame {
            let pendingFrameIndices = pendingEvents.indices.filter { pendingEvents[$0].event.isFrame }
            if pendingFrameIndices.count >= maximumPendingFrames {
                let pinned = pendingTargetCandidate?.sourceFrame
                if let removable = pendingFrameIndices.first(where: {
                    pendingEvents[$0].event.frameIdentity != pinned
                }) {
                    if let removedIdentity = pendingEvents[removable].event.frameIdentity {
                        queuedTargetSourceFrames.removeAll { $0 == removedIdentity }
                    }
                    pendingEvents.remove(at: removable)
                } else if event.frameIdentity != pinned {
                    if let skippedIdentity = event.frameIdentity {
                        queuedTargetSourceFrames.removeAll { $0 == skippedIdentity }
                    }
                    return
                }
            }
        }
        guard pendingEvents.count < maximumPendingEvents else {
            if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
            fail(
                title: "Capture paused to protect source data",
                message: "The bounded native input queue reached capacity. Retained structural events were not silently discarded.",
                recovery: .stopCaptureAndSaveDraft
            )
            return
        }
        let sequence = nextEventSequence
        guard sequence != UInt64.max else {
            fail(title: "Capture identity exhausted", message: "Start a new local project before capturing more evidence.", recovery: .stopCaptureAndSaveDraft)
            return
        }
        nextEventSequence += 1
        pendingEvents.append(SequencedNativeEvent(sequence: sequence, event: event))
        saveState = .hasUnsavedChanges
        scheduleDrain()
    }

    private func scheduleDrain() {
        guard !drainScheduled else { return }
        drainScheduled = true
        Task { [weak self] in await self?.drainPendingEvents() }
    }

    private func drainPendingEvents() async {
        let appearanceToken = appearanceRuntimeToken
        guard let runtime else {
            drainScheduled = false
            return
        }
        do {
            var acceptedFrames: [SceneDepthFrameIdentity] = []
            while !pendingEvents.isEmpty {
                let next = pendingEvents.removeFirst()
                let sourceTime: TimeInterval
                switch next.event {
                case .epoch(_, let time), .planReference(_, _, _, let time): sourceTime = time
                case .frame(let frame): sourceTime = frame.identity.monotonicTimestamp
                case .mesh(let event): sourceTime = event.monotonicTimestamp
                }
                guard var clock = captureClock,
                      let projectTime = clock.projectTime(for: sourceTime) else {
                    throw HSSBridgeError.invalidValue("Capture source time could not be mapped to the project log.")
                }
                switch next.event {
                case .epoch(let control, _):
                    try await runtime.setEpoch(control, eventSequence: next.sequence, monotonicTime: projectTime)
                case .frame(let frame):
                    try await runtime.submitFrame(frame, eventSequence: next.sequence, projectTime: projectTime)
                    if optionalAppearanceEnabled, appearanceSourceIsVerified, frame.appearancePhoto != nil,
                       let revision = acceptedEpochTransformRevisions[frame.identity.sessionEpoch],
                       let projectID = selectedProjectID {
                        let acceptedRevision = try await runtime.stats().scene_revision
                        let sourceCaptureID = await runtime.captureID
                        do {
                            try await appearanceStore.retain(frame: frame, projectID: projectID, captureID: sourceCaptureID,
                                runtimeToken: appearanceToken,
                                eventSequence: next.sequence, acceptedSceneRevision: acceptedRevision,
                                transformRevision: revision, projectTime: projectTime)
                        } catch {
                            appearanceMessage = String(localized: "Some source images could not be retained. Measured geometry is unchanged.")
                        }
                    }
                    observedProvenance.insert(.capturedSceneDepth)
                    acceptedFrames.append(frame.identity)
                case .mesh(let event):
                    try await runtime.submitMeshEvent(event, eventSequence: next.sequence, projectTime: projectTime)
                    observedProvenance.insert(.capturedARKitMesh)
                case .planReference(let matrix, let cutHeightMetres, let epoch, _):
                    try await runtime.setPlanReference(
                        projectFromFloor: matrix, cutHeightMetres: cutHeightMetres,
                        sessionEpoch: epoch, eventSequence: next.sequence,
                        monotonicTime: projectTime
                    )
                }
                captureClock = clock
                lastDurableEventTime = projectTime
            }
            latestSnapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: false)
            for identity in acceptedFrames {
                queuedTargetSourceFrames.removeAll { $0 == identity }
                acceptedTargetSourceFrames.append(identity)
            }
            if acceptedTargetSourceFrames.count > 16 {
                acceptedTargetSourceFrames.removeFirst(acceptedTargetSourceFrames.count - 16)
            }
            resolvePendingTargetIfPossible()
            refreshTargetRevisionBinding()
            drainScheduled = false
            if freezeRequested && !captureCoordinator.isSessionRunning {
                scheduleFreeze()
            } else if saveRequested {
                saveRequested = false
                performSave()
            }
        } catch {
            pendingEvents.removeAll()
            drainScheduled = false
            if captureCoordinator.isSessionRunning { captureCoordinator.stopSession() }
            fail(
                title: "Native pipeline paused",
                message: describe(error),
                recovery: .stopCaptureAndSaveDraft
            )
        }
    }

    private func scheduleFreeze() {
        guard !resourcePauseInProgress else { return }
        freezeRequested = false
        Task { [weak self] in
            guard let self, let runtime else { return }
            do {
                try await runtime.drain()
                latestSnapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: true)
                refreshTargetRevisionBinding()
                phase = .frozen
                if saveAfterFreeze || saveRequested {
                    saveAfterFreeze = false
                    saveRequested = false
                    performSave()
                }
            } catch {
                fail(title: "Capture could not be frozen", message: describe(error), recovery: .stopCaptureAndSaveDraft)
            }
        }
    }

    private func performSave() {
        guard let runtime, var entry = selectedCatalogEntry else { return }
        phase = .saving
        saveState = .saving
        Task { [weak self] in
            guard let self else { return }
            do {
                let checkpoint = try await runtime.checkpoint()
                await commitOptionalAppearance(projectID: entry.id, checkpoint: checkpoint)
                let snapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: true)
                entry.name = selectedProjectName.trimmingCharacters(in: .whitespacesAndNewlines)
                if entry.name.isEmpty { entry.name = "Untitled Scan" }
                entry.updatedAt = Date()
                entry.sceneRevision = snapshot.revision
                entry.state = .frozen
                try publishCatalogEntry(entry)
                selectedCatalogEntry = entry
                assignSelectedName(entry.name)
                latestSnapshot = snapshot
                reviewSnapshotRefreshRequired = false
                refreshTargetRevisionBinding()
                saveState = .saved(entry.updatedAt)
                exportState = .idle
                projectArchiveExportState = .idle
                currentIssue = checkpoint.durableRevision == 0 && checkpoint.durableEventCount > 0
                    ? ScannerWorkspaceIssue(
                        id: UUID(), title: "Save verification failed",
                        message: "The project store did not report a durable revision.", recoveryAction: .retrySave
                    )
                    : nil
                reloadProjects()
                phase = .frozen
                if resourcePauseLatched {
                    resourceCheckpointSucceeded = true
                    showResourcePauseIssue(checkpointSucceeded: true)
                }
            } catch {
                saveState = .failed(describe(error))
                fail(title: "Project was not saved", message: describe(error), recovery: .retrySave)
            }
        }
    }

    private func applicationEnteredBackground() {
        guard selectedProjectID != nil else { return }
        if captureCoordinator.isSessionRunning {
            saveAfterFreeze = true
            stopCapture()
        } else if saveState == .hasUnsavedChanges {
            saveSelectedProject()
        }
    }

    private func loadCatalog() throws -> WorkspaceCatalog {
        guard FileManager.default.fileExists(atPath: catalogURL.path) else {
            return WorkspaceCatalog(projects: [])
        }
        let data = try Data(contentsOf: catalogURL, options: [.mappedIfSafe])
        return try JSONDecoder().decode(WorkspaceCatalog.self, from: data)
    }

    private func publishCatalogEntry(_ entry: WorkspaceCatalogEntry) throws {
        var catalog = try loadCatalog()
        if let index = catalog.projects.firstIndex(where: { $0.id == entry.id }) {
            catalog.projects[index] = entry
        } else {
            catalog.projects.append(entry)
        }
        try writeCatalog(catalog)
    }

    private func writeCatalog(_ catalog: WorkspaceCatalog) throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        try encoder.encode(catalog).write(to: catalogURL, options: [.atomic])
        refreshLibrary(catalog)
    }

    private func refreshLibrary(_ catalog: WorkspaceCatalog) {
        localProjects = catalog.projects.filter { $0.deletedAt == nil }.map(\.summary)
            .sorted { $0.updatedAt > $1.updatedAt }
        deletedProjects = catalog.projects.filter { $0.deletedAt != nil }.map(\.summary)
            .sorted { $0.updatedAt > $1.updatedAt }
    }

    private func assignSelectedName(_ name: String) {
        isAssigningSelectedName = true
        selectedProjectName = name
        isAssigningSelectedName = false
    }

    private func fail(
        title: String,
        message: String,
        recovery: ScannerWorkspaceRecoveryAction?
    ) {
        currentIssue = ScannerWorkspaceIssue(
            id: UUID(), title: title, message: message, recoveryAction: recovery
        )
        phase = .failed(message)
    }

    private func describe(_ error: Error) -> String {
        if let bridge = error as? HSSBridgeError {
            switch bridge {
            case .native(let status): return "The native pipeline returned status \(status)."
            case .closed: return "The local project runtime is closed."
            case .invalidValue(let message): return message
            }
        }
        return error.localizedDescription
    }

    private func setPendingTarget(_ reason: NativeTargetPendingReason) {
        pendingTargetCandidate = nil
        targetSelectionState = .pending(reason)
    }

    private func resolvePendingTargetIfPossible() {
        guard let candidate = pendingTargetCandidate,
              acceptedTargetSourceFrames.contains(candidate.sourceFrame),
              let revision = latestSnapshot?.revision,
              let selection = NativeTargetSelectionAdapter.confirm(
                candidate,
                acceptedSourceFrame: candidate.sourceFrame,
                currentSceneRevision: revision
              ) else { return }
        pendingTargetCandidate = nil
        targetSelectionState = .confirmed(selection)
    }

    private func refreshTargetRevisionBinding() {
        guard let revision = latestSnapshot?.revision else { return }
        if case .confirmed(let selection) = targetSelectionState,
           selection.sceneRevision != revision {
            targetSelectionState = .stale(selection)
        }
    }

    private func resetTargetSelection() {
        queuedTargetSourceFrames.removeAll(keepingCapacity: true)
        acceptedTargetSourceFrames.removeAll(keepingCapacity: true)
        pendingTargetCandidate = nil
        acceptedEpochProjectTransforms.removeAll(keepingCapacity: true)
        acceptedEpochTransformRevisions.removeAll(keepingCapacity: true)
        targetSelectionState = .awaitingSelection
    }

    private func updateOptionalAppearanceResourcePolicy(_ decision: ScannerResourceDecision) {
        appearanceResourcesAvailable = !decision.suspendOptionalAppearance && !decision.pauseCapture
        optionalAppearanceEnabled = appearanceSourceIsVerified && appearanceResourcesAvailable
        captureCoordinator.setOptionalAppearanceEnabled(optionalAppearanceEnabled)
        if !optionalAppearanceEnabled {
            appearanceGeneration &+= 1
            reviewTexturedMesh = nil
            captureAdvice = nil
            appearanceMessage = appearanceSourceIsVerified
                ? String(localized: "Appearance is paused to protect capture resources. Measured geometry remains available.")
                : String(localized: "Measured geometry is available. Source images could not be verified for this revision.")
        }
    }

    /// Integrity quarantine belongs to one opened source, while resource
    /// pressure belongs to the device. Neither may silently clear the other.
    private func setOptionalAppearanceSourceVerified(_ verified: Bool) {
        appearanceSourceIsVerified = verified
        optionalAppearanceEnabled = verified && appearanceResourcesAvailable
        appearanceGeneration &+= 1
        reviewTexturedMesh = nil
        captureCoordinator.setOptionalAppearanceEnabled(optionalAppearanceEnabled)
        if optionalAppearanceEnabled { appearanceMessage = nil }
    }

    private func commitOptionalAppearance(projectID: UUID, checkpoint: HSSProjectCheckpoint) async {
        guard checkpoint.durableRevision > 0, appearanceSourceIsVerified else { return }
        do {
            try await appearanceStore.commit(projectID: projectID, runtimeToken: appearanceRuntimeToken,
                                             lastDurableEventSequence: checkpoint.lastEventSequence)
            appearanceGeneration &+= 1
            reviewTexturedMesh = nil
        }
        catch { appearanceMessage = String(localized: "Some source images could not be retained. Measured geometry is unchanged.") }
    }

    private func refreshOptionalAppearance() {
        guard let snapshot = latestSnapshot, let projectID = selectedProjectID,
              snapshot.isFrozen, optionalAppearanceEnabled, appearanceSourceIsVerified else {
            appearanceGeneration &+= 1
            reviewTexturedMesh = nil
            return
        }
        if reviewTexturedMesh?.projectID == projectID, reviewTexturedMesh?.matches(snapshot) == true { return }
        appearanceGeneration &+= 1
        let generation = appearanceGeneration
        let maximumSequence = nextEventSequence > 0 ? nextEventSequence - 1 : 0
        reviewTexturedMesh = nil
        appearanceMessage = String(localized: "Checking source images for the textured twin…")
        Task { [weak self] in
            guard let self else { return }
            do {
                let layer = try await appearanceStore.project(snapshot: snapshot, projectID: projectID,
                                                              maximumAcceptedEventSequence: maximumSequence)
                guard generation == appearanceGeneration, selectedProjectID == projectID,
                      latestSnapshot?.revision == snapshot.revision, optionalAppearanceEnabled else { return }
                reviewTexturedMesh = layer
                appearanceMessage = layer?.faces.isEmpty == false
                    ? String(localized: "Color comes from verified source views. Hidden or unsupported areas stay neutral; measurements are unchanged.")
                    : String(localized: "Measured geometry is available. No supported source image is available for this revision.")
            } catch {
                guard generation == appearanceGeneration, selectedProjectID == projectID else { return }
                appearanceMessage = String(localized: "Measured geometry is available. Source images could not be verified for this revision.")
            }
        }
    }
}

/// Deterministic capture-side selection. It retains the first valid frame and
/// later frames only after useful camera translation, rotation, or newly
/// visible coarse depth support. Repeated stationary frames are not counted as
/// independent measurement evidence.
final class SceneDepthKeyframeSelector {
    private let minimumTranslationMetres: Float
    private let minimumRotationRadians: Float
    private let minimumNewSupportFraction: Float
    private let maximumRetainedFrames: Int
    private var lastPose: [Float]?
    private var lastSupport: [Bool]?
    private(set) var retainedFrameCount = 0
    private(set) var discardedCorrelatedFrameCount = 0
    private(set) var discardedCapacityFrameCount = 0

    var hasReachedCapacity: Bool { retainedFrameCount >= maximumRetainedFrames }
    var accounting: ScannerCaptureAccounting {
        ScannerCaptureAccounting(
            retainedDepthKeyframes: UInt64(retainedFrameCount),
            discardedCorrelatedDepthFrames: UInt64(discardedCorrelatedFrameCount),
            discardedCapacityDepthFrames: UInt64(discardedCapacityFrameCount)
        )
    }

    init(
        minimumTranslationMetres: Float = 0.15,
        minimumRotationRadians: Float = 10 * .pi / 180,
        minimumNewSupportFraction: Float = 0.10,
        maximumRetainedFrames: Int = 256
    ) {
        precondition(minimumTranslationMetres > 0)
        precondition(minimumRotationRadians > 0)
        precondition(minimumNewSupportFraction > 0 && minimumNewSupportFraction <= 1)
        precondition(maximumRetainedFrames > 0)
        self.minimumTranslationMetres = minimumTranslationMetres
        self.minimumRotationRadians = minimumRotationRadians
        self.minimumNewSupportFraction = minimumNewSupportFraction
        self.maximumRetainedFrames = maximumRetainedFrames
    }

    func reset() {
        lastPose = nil
        lastSupport = nil
        retainedFrameCount = 0
        discardedCorrelatedFrameCount = 0
        discardedCapacityFrameCount = 0
    }

    func shouldRetain(_ frame: NormalizedSceneDepthFrame) -> Bool {
        guard !hasReachedCapacity else {
            discardedCapacityFrameCount += 1
            return false
        }
        let support = coarseSupport(frame)
        guard let previousPose = lastPose, let previousSupport = lastSupport else {
            accept(frame, support: support)
            return true
        }
        let translated = translationDistance(previousPose, frame.worldFromCamera) >= minimumTranslationMetres
        let rotated = rotationAngle(previousPose, frame.worldFromCamera) >= minimumRotationRadians
        let newSupportCount = zip(previousSupport, support).reduce(into: 0) { count, pair in
            if !pair.0 && pair.1 { count += 1 }
        }
        let supportFraction = Float(newSupportCount) / Float(max(1, support.count))
        guard translated || rotated || supportFraction >= minimumNewSupportFraction else {
            discardedCorrelatedFrameCount += 1
            return false
        }
        accept(frame, support: support)
        return true
    }

    /// A user target may promote the latest correlated frame into retained
    /// source evidence. Capacity is never bypassed and a previously counted
    /// correlated discard is corrected when the same latest frame is kept.
    func retainForTarget(_ frame: NormalizedSceneDepthFrame) -> Bool {
        guard !hasReachedCapacity else { return false }
        if discardedCorrelatedFrameCount > 0 { discardedCorrelatedFrameCount -= 1 }
        accept(frame, support: coarseSupport(frame))
        return true
    }

    private func accept(_ frame: NormalizedSceneDepthFrame, support: [Bool]) {
        lastPose = frame.worldFromCamera
        lastSupport = support
        retainedFrameCount += 1
    }

    private func coarseSupport(_ frame: NormalizedSceneDepthFrame) -> [Bool] {
        let binsX = 8
        let binsY = 6
        let width = frame.intrinsics.depthWidth
        let height = frame.intrinsics.depthHeight
        var result = [Bool](repeating: false, count: binsX * binsY)
        guard width > 0, height > 0 else { return result }
        for y in 0..<height {
            let binY = min(binsY - 1, y * binsY / height)
            for x in 0..<width where frame.metricFusionMask[y * width + x] != 0 {
                let binX = min(binsX - 1, x * binsX / width)
                result[binY * binsX + binX] = true
            }
        }
        return result
    }

    private func translationDistance(_ lhs: [Float], _ rhs: [Float]) -> Float {
        guard lhs.count == 16, rhs.count == 16 else { return .infinity }
        return simd_distance(
            SIMD3(lhs[12], lhs[13], lhs[14]),
            SIMD3(rhs[12], rhs[13], rhs[14])
        )
    }

    private func rotationAngle(_ lhs: [Float], _ rhs: [Float]) -> Float {
        guard lhs.count == 16, rhs.count == 16 else { return .infinity }
        let left = simd_float3x3(columns: (
            SIMD3(lhs[0], lhs[1], lhs[2]),
            SIMD3(lhs[4], lhs[5], lhs[6]),
            SIMD3(lhs[8], lhs[9], lhs[10])
        ))
        let right = simd_float3x3(columns: (
            SIMD3(rhs[0], rhs[1], rhs[2]),
            SIMD3(rhs[4], rhs[5], rhs[6]),
            SIMD3(rhs[8], rhs[9], rhs[10])
        ))
        let relative = left.transpose * right
        let cosine = max(-1, min(1, (relative[0, 0] + relative[1, 1] + relative[2, 2] - 1) / 2))
        return acos(cosine)
    }
}

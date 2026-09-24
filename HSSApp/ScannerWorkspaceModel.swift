import ARKit
import Combine
import Foundation
import HSSContracts
import UIKit
import simd

@MainActor
final class ScannerWorkspaceModel: ScannerWorkspaceModeling {
    @Published private(set) var localProjects: [ScannerProjectSummary] = []
    @Published private(set) var phase: ScannerWorkspacePhase = .browsing
    @Published private(set) var latestSnapshot: ScannerSceneSnapshot?
    @Published private(set) var selectedProjectID: UUID?
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
    @Published private(set) var exportState: ScannerExportState = .idle
    @Published private(set) var projectArchiveExportState: ScannerProjectArchiveExportState = .idle
    @Published private(set) var captureAccounting: ScannerCaptureAccounting = .zero
    @Published private(set) var targetSelectionState: NativeTargetSelectionState = .awaitingSelection

    var captureSession: ARSession? { captureCoordinator.captureSession }
    var canConfirmPlanReference: Bool {
        selectedProjectID != nil
            && latestSnapshot?.verticesMetres.isEmpty == false
            && captureCoordinator.epochRelocalization.currentEpoch != nil
            && !drainScheduled
            && phase != .capturing
            && phase != .captureInterrupted
            && phase != .stoppingCapture
            && phase != .saving
    }
    var canShowLibrary: Bool {
        !captureCoordinator.isSessionRunning
            && !drainScheduled
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
        ) && exportState != .exporting
    }
    var canExportProjectArchive: Bool {
        selectedProjectID != nil
            && selectedCatalogEntry != nil
            && phase == .frozen
            && (saveState == .clean || isSavedState)
            && projectArchiveExportState != .exporting
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

        var summary: ScannerProjectSummary {
            ScannerProjectSummary(
                id: id, name: name, updatedAt: updatedAt,
                sceneRevision: sceneRevision, state: state
            )
        }
    }

    private let trustedRoot: URL
    private let catalogURL: URL
    private let sourceIsSynthetic: Bool
    private let resourceMonitor = ScannerIOSResourceMonitor()
    private var runtime: HSSProjectRuntime?
    private var selectedCatalogEntry: WorkspaceCatalogEntry?
    private var pendingEvents: [SequencedNativeEvent] = []
    private var nextEventSequence: UInt64 = 1
    private var lastDurableEventTime: TimeInterval = 0
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
    private var cancellables: Set<AnyCancellable> = []
    private let keyframeSelector = SceneDepthKeyframeSelector()
    private let maximumPendingEvents = 512
    private let maximumPendingFrames = 2

    private lazy var captureCoordinator: ARSessionCoordinator = {
        ARSessionCoordinator(
            normalizedFrameSink: { [weak self] frame in self?.receive(frame) },
            meshAnchorSink: { [weak self] event in self?.receive(event) },
            epochControlSink: { [weak self] control in self?.receive(control) }
        )
    }()

    init(
        trustedRoot: URL? = nil,
        sourceIsSynthetic: Bool = false
    ) {
        let root = trustedRoot ?? FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask
        ).first!.appendingPathComponent("HSSProjects", isDirectory: true)
        self.trustedRoot = root
        self.catalogURL = root.appendingPathComponent("workspace-catalog.json", isDirectory: false)
        self.sourceIsSynthetic = sourceIsSynthetic

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
            localProjects = catalog.projects.map(\.summary).sorted { $0.updatedAt > $1.updatedAt }
            if selectedProjectID == nil { phase = .browsing }
        } catch {
            fail(
                title: "Project library unavailable",
                message: error.localizedDescription,
                recovery: .returnToProjectList
            )
        }
    }

    func createProject(named name: String) {
        guard !captureCoordinator.isSessionRunning, !drainScheduled else { return }
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
        guard !captureCoordinator.isSessionRunning, !drainScheduled else { return }
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
                selectedCatalogEntry = adoptedEntry
                selectedProjectID = entry.id
                assignSelectedName(adoptedEntry.name)
                latestSnapshot = adopted.2
                observedProvenance = provenance
                nextEventSequence = adopted.1.lastEventSequence + 1
                lastDurableEventTime = adopted.1.lastMonotonicTime
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
        guard runtime != nil, selectedProjectID != nil, !captureCoordinator.isSessionRunning else { return }
        guard ProcessInfo.processInfo.systemUptime >= lastDurableEventTime else {
            fail(
                title: String(localized: "Capture time is not yet monotonic"),
                message: String(localized: "The retained source time is ahead of this device clock. Start a new local project or retry after the monotonic clock catches up."),
                recovery: .returnToProjectList
            )
            return
        }
        guard !resourcePauseLatched else {
            showResourcePauseIssue(checkpointSucceeded: resourceCheckpointSucceeded)
            return
        }
        currentIssue = nil
        exportState = .idle
        projectArchiveExportState = .idle
        phase = .startingCapture
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
        guard canExport,
              let snapshot = latestSnapshot,
              let projectID = selectedProjectID else {
            exportState = .failed(
                String(localized: "Only a saved, frozen accepted revision with measured plan and mesh geometry can be exported.")
            )
            return
        }
        exportState = .exporting
        let root = trustedRoot
        Task { [weak self] in
            do {
                let directory = try await Task.detached(priority: .userInitiated) {
                    try ScannerOfflineExporter.export(
                        snapshot: snapshot,
                        projectID: projectID,
                        trustedRoot: root
                    )
                }.value
                guard let self,
                      selectedProjectID == projectID,
                      latestSnapshot?.revision == snapshot.revision else { return }
                exportState = .exported(directory: directory, at: Date())
            } catch {
                guard let self else { return }
                exportState = .failed(error.localizedDescription)
            }
        }
    }

    func exportSelectedProjectArchive() {
        guard canExportProjectArchive,
              let projectID = selectedProjectID,
              let entry = selectedCatalogEntry else {
            projectArchiveExportState = .failed(
                String(localized: "Only a saved local project can be archived.")
            )
            return
        }
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
        Task { [weak self] in
            do {
                try await HSSProjectArchiveExport.writeArchive(
                    projectPath: projectDirectory.path(percentEncoded: false),
                    destinationPath: archive.path(percentEncoded: false)
                )
                guard let self,
                      selectedProjectID == projectID,
                      selectedCatalogEntry?.sceneRevision == entry.sceneRevision else { return }
                projectArchiveExportState = .exported(archive)
            } catch {
                guard let self else { return }
                projectArchiveExportState = .failed(describe(error))
            }
        }
    }

    func openProject(id: UUID) {
        guard !captureCoordinator.isSessionRunning, !drainScheduled else { return }
        guard let entry = (try? loadCatalog().projects.first(where: { $0.id == id })) ?? nil else {
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
                observedProvenance = provenance
                nextEventSequence = replay.lastEventSequence + 1
                lastDurableEventTime = replay.lastMonotonicTime
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
              cutHeightMetres.isFinite, cutHeightMetres > 0,
              let epoch = captureCoordinator.epochRelocalization.currentEpoch else { return }
        let matrix = [
            projectFromFloor.columns.0.x, projectFromFloor.columns.0.y, projectFromFloor.columns.0.z, projectFromFloor.columns.0.w,
            projectFromFloor.columns.1.x, projectFromFloor.columns.1.y, projectFromFloor.columns.1.z, projectFromFloor.columns.1.w,
            projectFromFloor.columns.2.x, projectFromFloor.columns.2.y, projectFromFloor.columns.2.z, projectFromFloor.columns.2.w,
            projectFromFloor.columns.3.x, projectFromFloor.columns.3.y, projectFromFloor.columns.3.z, projectFromFloor.columns.3.w,
        ]
        enqueue(.planReference(
            matrix: matrix, cutHeightMetres: cutHeightMetres,
            epoch: epoch, monotonicTime: ProcessInfo.processInfo.systemUptime
        ))
    }

    func recoverFromCurrentIssue() {
        guard let action = currentIssue?.recoveryAction else { return }
        switch action {
        case .retrySave:
            saveSelectedProject()
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
                if decision.requestCheckpoint || !decision.acceptMetricObservations {
                    await handleResourcePause(decision, runtime: runtime)
                    return
                }
                guard !resourcePauseLatched,
                      selectedProjectID == runtime.projectID,
                      phase == .startingCapture else { return }
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
        } else {
            acceptedEpochProjectTransforms.removeValue(forKey: control.sessionEpoch)
        }
        enqueue(.epoch(control, monotonicTime: ProcessInfo.processInfo.systemUptime))
    }

    private func receive(_ frame: NormalizedSceneDepthFrame) {
        guard !resourceMetricIntakeBlocked else { return }
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
        guard let runtime else {
            drainScheduled = false
            return
        }
        do {
            var acceptedFrames: [SceneDepthFrameIdentity] = []
            while !pendingEvents.isEmpty {
                let next = pendingEvents.removeFirst()
                switch next.event {
                case .epoch(let control, let monotonicTime):
                    try await runtime.setEpoch(control, eventSequence: next.sequence, monotonicTime: monotonicTime)
                case .frame(let frame):
                    try await runtime.submitFrame(frame, eventSequence: next.sequence)
                    observedProvenance.insert(.capturedSceneDepth)
                    acceptedFrames.append(frame.identity)
                case .mesh(let event):
                    try await runtime.submitMeshEvent(event, eventSequence: next.sequence)
                    observedProvenance.insert(.capturedARKitMesh)
                case .planReference(let matrix, let cutHeightMetres, let epoch, let monotonicTime):
                    try await runtime.setPlanReference(
                        projectFromFloor: matrix, cutHeightMetres: cutHeightMetres,
                        sessionEpoch: epoch, eventSequence: next.sequence,
                        monotonicTime: monotonicTime
                    )
                }
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
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        try encoder.encode(catalog).write(to: catalogURL, options: [.atomic])
        localProjects = catalog.projects.map(\.summary).sorted { $0.updatedAt > $1.updatedAt }
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
        targetSelectionState = .awaitingSelection
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

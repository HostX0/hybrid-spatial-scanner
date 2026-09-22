import ARKit
import Combine
import Foundation
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
    @Published private(set) var captureAccounting: ScannerCaptureAccounting = .zero

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

    private enum PendingNativeEvent: Sendable {
        case epoch(CaptureEpochControl, monotonicTime: TimeInterval)
        case frame(NormalizedSceneDepthFrame)
        case mesh(OwnedMeshAnchorEvent)
        case planReference(matrix: [Double], cutHeightMetres: Double, epoch: UInt64, monotonicTime: TimeInterval)

        var isFrame: Bool {
            if case .frame = self { true } else { false }
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
    private var runtime: HSSProjectRuntime?
    private var selectedCatalogEntry: WorkspaceCatalogEntry?
    private var pendingEvents: [SequencedNativeEvent] = []
    private var nextEventSequence: UInt64 = 1
    private var drainScheduled = false
    private var freezeRequested = false
    private var saveRequested = false
    private var saveAfterFreeze = false
    private var isAssigningSelectedName = false
    private var observedProvenance: Set<ScannerSceneProvenance> = []
    private var didReportDepthKeyframeCapacity = false
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
                _ = try await candidate.checkpoint()
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
                keyframeSelector.reset()
                captureAccounting = .zero
                didReportDepthKeyframeCapacity = false
                saveState = .saved(entry.updatedAt)
                exportState = .idle
                reloadProjects()
                phase = .ready
            } catch {
                fail(title: "Project could not be created", message: describe(error), recovery: .returnToProjectList)
            }
        }
    }

    func startCapture() {
        guard runtime != nil, selectedProjectID != nil, !captureCoordinator.isSessionRunning else { return }
        currentIssue = nil
        exportState = .idle
        phase = .startingCapture
        keyframeSelector.reset()
        captureAccounting = .zero
        didReportDepthKeyframeCapacity = false
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
                _ = try await candidate.replayCommittedSource()
                try await candidate.drain()
                let provenance: Set<ScannerSceneProvenance> = entry.sourceIsSynthetic
                    ? [.syntheticTest, .importedReplay]
                    : [.importedReplay]
                let snapshot = try await candidate.snapshot(provenance: provenance, frozen: true)
                runtime = candidate
                selectedCatalogEntry = entry
                selectedProjectID = entry.id
                assignSelectedName(entry.name)
                latestSnapshot = snapshot
                observedProvenance = provenance
                nextEventSequence = max(1, snapshot.revision + 1)
                saveState = .clean
                exportState = .idle
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
        currentIssue = nil
        phase = .browsing
        reloadProjects()
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
    }

    private func capturePhaseChanged(_ capturePhase: CaptureSessionPhase) {
        switch capturePhase {
        case .captureReady:
            phase = .capturing
            currentIssue = nil
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
        enqueue(.epoch(control, monotonicTime: ProcessInfo.processInfo.systemUptime))
    }

    private func receive(_ frame: NormalizedSceneDepthFrame) {
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
        enqueue(.frame(frame))
    }

    private func receive(_ event: OwnedMeshAnchorEvent) {
        enqueue(.mesh(event))
    }

    private func enqueue(_ event: PendingNativeEvent) {
        guard runtime != nil else { return }
        if event.isFrame {
            let pendingFrameIndices = pendingEvents.indices.filter { pendingEvents[$0].event.isFrame }
            if pendingFrameIndices.count >= maximumPendingFrames, let oldest = pendingFrameIndices.first {
                pendingEvents.remove(at: oldest)
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
            while !pendingEvents.isEmpty {
                let next = pendingEvents.removeFirst()
                switch next.event {
                case .epoch(let control, let monotonicTime):
                    try await runtime.setEpoch(control, eventSequence: next.sequence, monotonicTime: monotonicTime)
                case .frame(let frame):
                    try await runtime.submitFrame(frame, eventSequence: next.sequence)
                    observedProvenance.insert(.capturedSceneDepth)
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
        freezeRequested = false
        Task { [weak self] in
            guard let self, let runtime else { return }
            do {
                try await runtime.drain()
                latestSnapshot = try await runtime.snapshot(provenance: observedProvenance, frozen: true)
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
                saveState = .saved(entry.updatedAt)
                exportState = .idle
                currentIssue = checkpoint.durableRevision == 0 && checkpoint.durableEventCount > 0
                    ? ScannerWorkspaceIssue(
                        id: UUID(), title: "Save verification failed",
                        message: "The project store did not report a durable revision.", recoveryAction: .retrySave
                    )
                    : nil
                reloadProjects()
                phase = .frozen
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

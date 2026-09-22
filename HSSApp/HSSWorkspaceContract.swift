import ARKit
import Combine
import Foundation
import simd

enum ScannerWorkspacePhase: Equatable, Sendable {
    case browsing
    case preparingProject
    case ready
    case startingCapture
    case capturing
    case captureLimited(String)
    case captureInterrupted
    case stoppingCapture
    case frozen
    case saving
    case openingProject
    case captureUnsupported(String)
    case failed(String)
}

enum ScannerProjectState: String, Codable, Equatable, Sendable {
    case draft
    case frozen
}

struct ScannerProjectSummary: Identifiable, Codable, Equatable, Hashable, Sendable {
    let id: UUID
    let name: String
    let updatedAt: Date
    let sceneRevision: UInt64
    let state: ScannerProjectState
}

enum ScannerSceneProvenance: String, Codable, Equatable, Sendable {
    case capturedARKitMesh
    case capturedSceneDepth
    case importedReplay
    case syntheticTest
}

/// Immutable renderer/review input. Canonical positions are metres in the
/// right-handed project frame; triangleIndices contains packed triples.
struct ScannerSceneSnapshot: Equatable, Sendable {
    let revision: UInt64
    let verticesMetres: [SIMD3<Double>]
    let triangleIndices: [UInt32]
    let sceneJSON: Data
    let provenance: Set<ScannerSceneProvenance>
    let isFrozen: Bool
}

enum ScannerWorkspaceSaveState: Equatable, Sendable {
    case noProject
    case clean
    case hasUnsavedChanges
    case saving
    case saved(Date)
    case failed(String)
}

enum ScannerWorkspaceRecoveryAction: Equatable, Sendable {
    case retrySave
    case reopenProject
    case returnToProjectList
    case stopCaptureAndSaveDraft
}

struct ScannerWorkspaceIssue: Identifiable, Equatable, Sendable {
    let id: UUID
    let title: String
    let message: String
    let recoveryAction: ScannerWorkspaceRecoveryAction?
}

/// Shared UI/native seam. `ScannerWorkspaceModel` is the production concrete
/// implementation and publishes each property below from the main actor.
/// `captureSession` is the same single ARSession owned by the coordinator.
@MainActor
protocol ScannerWorkspaceModeling: ObservableObject {
    var localProjects: [ScannerProjectSummary] { get }
    var phase: ScannerWorkspacePhase { get }
    var latestSnapshot: ScannerSceneSnapshot? { get }
    var selectedProjectID: UUID? { get }
    var selectedProjectName: String { get set }
    var currentIssue: ScannerWorkspaceIssue? { get }
    var saveState: ScannerWorkspaceSaveState { get }
    var captureSession: ARSession? { get }
    var canConfirmPlanReference: Bool { get }
    var canShowLibrary: Bool { get }

    func reloadProjects()
    func createProject(named name: String)
    func startCapture()
    func stopCapture()
    func saveSelectedProject()
    func openProject(id: UUID)
    func showLibrary()
    /// Applies only a caller-confirmed observed floor reference. It never uses
    /// the arbitrary AR world origin as an automatic physical-floor claim.
    func confirmPlanReference(projectFromFloor: simd_double4x4, cutHeightMetres: Double)
    func recoverFromCurrentIssue()
}

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
    /// Raw accepted captured-mesh topology. Exporters consume this array and
    /// never rewrite it from the derived dynamic presentation mask.
    let triangleIndices: [UInt32]
    /// One byte per raw triangle when supplied by the C facade. A value of
    /// zero hides that triangle in the derived review view only.
    let reviewTriangleInclusion: [UInt8]
    let dynamicMaskSummary: ScannerDynamicMaskSummary?
    let sceneJSON: Data
    let provenance: Set<ScannerSceneProvenance>
    let isFrozen: Bool

    init(
        revision: UInt64,
        verticesMetres: [SIMD3<Double>],
        triangleIndices: [UInt32],
        reviewTriangleInclusion: [UInt8] = [],
        dynamicMaskSummary: ScannerDynamicMaskSummary? = nil,
        sceneJSON: Data,
        provenance: Set<ScannerSceneProvenance>,
        isFrozen: Bool
    ) {
        self.revision = revision
        self.verticesMetres = verticesMetres
        self.triangleIndices = triangleIndices
        self.reviewTriangleInclusion = reviewTriangleInclusion
        self.dynamicMaskSummary = dynamicMaskSummary
        self.sceneJSON = sceneJSON
        self.provenance = provenance
        self.isFrozen = isFrozen
    }
}

struct ScannerDynamicMaskSummary: Equatable, Sendable {
    let sceneRevision: UInt64
    let triangleCount: UInt64
    let includedTriangleCount: UInt64
    let excludedTriangleCount: UInt64
    let appliedExclusionCount: UInt64
    let staleExclusionCount: UInt64
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

struct ScannerCaptureAccounting: Equatable, Sendable {
    let retainedDepthKeyframes: UInt64
    let discardedCorrelatedDepthFrames: UInt64
    let discardedCapacityDepthFrames: UInt64

    static let zero = ScannerCaptureAccounting(
        retainedDepthKeyframes: 0,
        discardedCorrelatedDepthFrames: 0,
        discardedCapacityDepthFrames: 0
    )
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
    var exportState: ScannerExportState { get }
    var captureAccounting: ScannerCaptureAccounting { get }
    var captureSession: ARSession? { get }
    var targetSelectionState: NativeTargetSelectionState { get }
    var reviewAppearanceBinding: AppearanceAdmittedBinding? { get }
    var reviewAppearanceTextures: [AppearanceTextureResource] { get }
    var canConfirmPlanReference: Bool { get }
    var canShowLibrary: Bool { get }
    var canExport: Bool { get }

    func reloadProjects()
    func createProject(named name: String)
    func startCapture()
    func stopCapture()
    func saveSelectedProject()
    func exportSelectedProject()
    func openProject(id: UUID)
    func showLibrary()
    func selectTarget(_ tap: NativeTargetTap)
    /// Applies only a caller-confirmed observed floor reference. It never uses
    /// the arbitrary AR world origin as an automatic physical-floor claim.
    func confirmPlanReference(projectFromFloor: simd_double4x4, cutHeightMetres: Double)
    func recoverFromCurrentIssue()
}

extension ScannerWorkspaceModeling {
    var reviewAppearanceBinding: AppearanceAdmittedBinding? { nil }
    var reviewAppearanceTextures: [AppearanceTextureResource] { [] }
}

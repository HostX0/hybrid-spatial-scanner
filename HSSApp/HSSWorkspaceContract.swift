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
    var canRestore: Bool = true
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
    /// Value-owned B-031/B-032 view from the same accepted core revision.
    let planAnnotation: ScannerPlanAnnotationSnapshot?
    let provenance: Set<ScannerSceneProvenance>
    let isFrozen: Bool

    init(
        revision: UInt64,
        verticesMetres: [SIMD3<Double>],
        triangleIndices: [UInt32],
        reviewTriangleInclusion: [UInt8] = [],
        dynamicMaskSummary: ScannerDynamicMaskSummary? = nil,
        sceneJSON: Data,
        planAnnotation: ScannerPlanAnnotationSnapshot? = nil,
        provenance: Set<ScannerSceneProvenance>,
        isFrozen: Bool
    ) {
        self.revision = revision
        self.verticesMetres = verticesMetres
        self.triangleIndices = triangleIndices
        self.reviewTriangleInclusion = reviewTriangleInclusion
        self.dynamicMaskSummary = dynamicMaskSummary
        self.sceneJSON = sceneJSON
        self.planAnnotation = planAnnotation
        self.provenance = provenance
        self.isFrozen = isFrozen
    }
}

struct ScannerPlanAnnotationSnapshot: Equatable, Sendable {
    let sceneRevision: UInt64
    let planAvailable: Bool
    let annotationLayoutAvailable: Bool
    let referenceState: UInt32
    let boundaryState: UInt32
    let planAvailability: UInt32
    let areaAvailability: UInt32
    let areaMetresSquared: Double?
    let cutHeightMetres: Double
    let isSynthetic: Bool
    let planJSON: Data?
    let annotationJSON: Data?
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
    case retryCommandJournal
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
    var deletedProjects: [ScannerProjectSummary] { get }
    var isManagingLibrary: Bool { get }
    var phase: ScannerWorkspacePhase { get }
    var latestSnapshot: ScannerSceneSnapshot? { get }
    var selectedProjectID: UUID? { get }
    var selectedProjectName: String { get set }
    var currentIssue: ScannerWorkspaceIssue? { get }
    var saveState: ScannerWorkspaceSaveState { get }
    var exportState: ScannerExportState { get }
    var projectArchiveExportState: ScannerProjectArchiveExportState { get }
    var captureAccounting: ScannerCaptureAccounting { get }
    var captureAdvice: ScannerCaptureAdvice? { get }
    var captureSession: ARSession? { get }
    var targetSelectionState: NativeTargetSelectionState { get }
    var reviewAppearanceBinding: AppearanceAdmittedBinding? { get }
    var reviewAppearanceTextures: [AppearanceTextureResource] { get }
    var reviewTexturedMesh: ScannerTexturedMesh? { get }
    var appearanceMessage: String? { get }
    var includeSourceImagesInExport: Bool { get set }
    var canConfirmPlanReference: Bool { get }
    var canShowLibrary: Bool { get }
    var canExport: Bool { get }
    var canExportProjectArchive: Bool { get }
    /// A review-only, zero-target uncertainty marker. It never activates
    /// target editing or undo from the live camera surface.
    var canRecordMissingEvidence: Bool { get }
    var editableBoundary: [ScannerBoundaryEdge] { get }
    var commandHistory: ScannerCommandHistory? { get }

    func reloadProjects()
    func renameProject(id: UUID, name: String)
    func moveProjectToTrash(id: UUID)
    func restoreProject(id: UUID)
    func permanentlyDeleteProject(id: UUID)
    func createProject(named name: String)
    func importProjectArchive(at url: URL)
    func startCapture()
    func stopCapture()
    func saveSelectedProject()
    func exportSelectedProject()
    func exportSelectedProjectArchive()
    func openProject(id: UUID)
    func showLibrary()
    func selectTarget(_ tap: NativeTargetTap)
    /// Applies only a caller-confirmed observed floor reference. It never uses
    /// the arbitrary AR world origin as an automatic physical-floor claim.
    func confirmPlanReference(projectFromFloor: simd_double4x4, cutHeightMetres: Double)
    func recordMissingEvidence(reason: String)
    func applyBoundaryEdit(_ edit: ScannerBoundaryEdit, expectedRevision: UInt64)
    func recoverFromCurrentIssue()
}

extension ScannerWorkspaceModeling {
    var reviewTexturedMesh: ScannerTexturedMesh? { nil }
    var appearanceMessage: String? { nil }
    var includeSourceImagesInExport: Bool { get { false } set {} }
    var captureAdvice: ScannerCaptureAdvice? { nil }
    var reviewAppearanceBinding: AppearanceAdmittedBinding? { nil }
    var reviewAppearanceTextures: [AppearanceTextureResource] { [] }
    var editableBoundary: [ScannerBoundaryEdge] { [] }
    var commandHistory: ScannerCommandHistory? { nil }
    func applyBoundaryEdit(_ edit: ScannerBoundaryEdit, expectedRevision: UInt64) {}
}

/// Only current explicit graph identities are selectable. A camera ray or raw
/// mesh vertex is not promoted to a boundary node. Native command semantics
/// revalidate this copied, revision-bound input before mutating accepted state.
struct ScannerBoundaryEdge: Identifiable, Equatable, Sendable {
    let id: UUID
    let startID: UUID
    let endID: UUID
    let start: SIMD3<Double>
    let end: SIMD3<Double>
    let protectedTopology: Bool
    var lengthMetres: Double { simd_length(end - start) }

    static func decode(snapshot: ScannerSceneSnapshot, projectID: UUID) -> [Self] {
        struct Provenance: Decodable { let geometry_origin: String; let support_ids: [UUID] }
        struct Node: Decodable { let id: UUID; let position_m: [Double] }
        struct Edge: Decodable {
            let id: UUID; let start_node_id: UUID; let end_node_id: UUID
            let protected_topology: Bool; let provenance: Provenance
        }
        struct Document: Decodable {
            let schema_version: String; let project_id: UUID; let scene_revision: String
            let units: String; let nodes: [Node]; let edges: [Edge]
        }
        guard snapshot.sceneJSON.count <= 16 * 1024 * 1024,
              let d = try? JSONDecoder().decode(Document.self, from: snapshot.sceneJSON),
              d.schema_version == "1.1", d.project_id == projectID, d.units == "m",
              d.scene_revision == String(snapshot.revision), d.nodes.count <= 4096, d.edges.count <= 2048,
              Set(d.nodes.map(\.id)).count == d.nodes.count, Set(d.edges.map(\.id)).count == d.edges.count,
              d.nodes.allSatisfy({ $0.position_m.count == 3 && $0.position_m.allSatisfy { $0.isFinite && abs($0) <= 10_000 } }) else { return [] }
        let nodes = Dictionary(uniqueKeysWithValues: d.nodes.map { ($0.id, SIMD3($0.position_m[0], $0.position_m[1], $0.position_m[2])) })
        return d.edges.compactMap { edge in
            guard edge.start_node_id != edge.end_node_id,
                  let a = nodes[edge.start_node_id], let b = nodes[edge.end_node_id],
                  simd_length(b - a) > 0, !edge.provenance.support_ids.isEmpty,
                  ["observed", "fitted", "user_measured"].contains(edge.provenance.geometry_origin) else { return nil }
            return Self(id: edge.id, startID: edge.start_node_id, endID: edge.end_node_id,
                        start: a, end: b, protectedTopology: edge.protected_topology)
        }
    }
}

enum ScannerBoundaryEdit: Equatable, Sendable {
    enum Source: String, CaseIterable, Sendable { case tape, laser, referenceSurvey = "reference_survey", guess }
    enum Mode: String, CaseIterable, Sendable { case exactModel = "exact_model", softMeasurement = "soft_measurement" }
    case protect(edge: UUID)
    case split(edge: UUID, parameter: Double)
    case lock(node: UUID, position: SIMD3<Double>, confirmed: Bool)
    case dimension(a: UUID, b: UUID, metres: Double, source: Source, uncertainty: Double?, mode: Mode)
    case undo(original: UUID)
    case redo(original: UUID)

    func encode(projectID: UUID, revision: UInt64, sequence: UInt64) throws -> Data {
        let type: String, targets: [UUID], payload: [String: Any]
        switch self {
        case .protect(let edge):
            type = "protect_feature"; targets = [edge]; payload = ["preserve_topology": true, "lock_position": false]
        case .split(let edge, let parameter):
            guard parameter.isFinite, parameter > 0, parameter < 1 else { throw HSSBridgeError.invalidValue("Split position must be inside the selected edge.") }
            type = "split_edge"; targets = [edge]; payload = ["edge_parameter": parameter]
        case .lock(let node, let position, let confirmed):
            guard confirmed, [position.x, position.y, position.z].allSatisfy({ $0.isFinite && abs($0) <= 10_000 }) else { throw HSSBridgeError.invalidValue("Lock Position requires explicit confirmation of a finite current position.") }
            type = "lock_position"; targets = [node]; payload = ["position_m": [position.x, position.y, position.z], "explicit_confirmation": true]
        case .dimension(let a, let b, let metres, let source, let uncertainty, let mode):
            guard a != b, metres.isFinite, metres > 0, metres <= 10_000,
                  uncertainty.map({ $0.isFinite && $0 >= 0 && $0 <= 10_000 }) ?? true else { throw HSSBridgeError.invalidValue("A reference dimension needs distinct endpoints and a valid positive metre value.") }
            type = "set_dimension"; targets = [a, b]
            payload = ["endpoint_a": a.uuidString.lowercased(), "endpoint_b": b.uuidString.lowercased(), "value": metres, "unit": "m", "source": source.rawValue, "uncertainty_m": uncertainty.map { $0 as Any } ?? NSNull(), "mode": mode.rawValue, "weight": 1.0]
        case .undo(let original): type = "undo"; targets = []; payload = ["original_command_id": original.uuidString.lowercased()]
        case .redo(let original): type = "redo"; targets = []; payload = ["original_command_id": original.uuidString.lowercased()]
        }
        return try JSONSerialization.data(withJSONObject: [
            "schema_version": "1.1", "command_id": UUID().uuidString.lowercased(), "project_id": projectID.uuidString.lowercased(),
            "expected_scene_revision": String(revision), "event_sequence": String(sequence),
            "targets": targets.map { $0.uuidString.lowercased() }, "user_intent": "explicit frozen boundary review", "type": type, "payload": payload
        ], options: [.sortedKeys])
    }
}

enum ScannerMeasurementEntry {
    /// Read a complete decimal entry in the user's locale. Grouping, unit
    /// suffixes and partial parses are rejected; canonical storage is metres.
    static func metres(_ text: String, locale: Locale) -> Double? {
        let entry = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !entry.isEmpty, entry.count <= 32 else { return nil }
        let separator = locale.decimalSeparator ?? "."
        var normalized = "", hasSeparator = false, digits = 0
        for character in entry {
            if String(character) == separator || character == "." {
                guard !hasSeparator else { return nil }
                hasSeparator = true
                normalized.append(".")
            } else {
                guard character.unicodeScalars.count == 1,
                      let scalar = character.unicodeScalars.first,
                      CharacterSet.decimalDigits.contains(scalar),
                      let digit = character.wholeNumberValue else { return nil }
                normalized.append(String(digit))
                digits += 1
            }
        }
        guard digits > 0, let value = Double(normalized), value.isFinite,
              value >= 0, value <= 10_000 else { return nil }
        return value
    }
}

struct ScannerCommandHistory: Equatable, Sendable, Decodable {
    struct Record: Equatable, Sendable, Decodable {
        let command_id: UUID; let type: String; let original_command_id: UUID?
        let before_scene_revision: String; let after_scene_revision: String
    }
    let schema_version: String; let project_id: UUID; let scene_revision: String; let records: [Record]
    static func decode(_ data: Data, projectID: UUID, revision: UInt64) -> Self? {
        guard data.count <= 16 * 1024 * 1024, let value = try? JSONDecoder().decode(Self.self, from: data),
              value.schema_version == "1.1", value.project_id == projectID, value.scene_revision == String(revision), value.records.count <= 65_536,
              Set(value.records.map(\.command_id)).count == value.records.count,
              value.records.allSatisfy({ record in
                  guard let a = UInt64(record.before_scene_revision), let b = UInt64(record.after_scene_revision) else { return false }
                  return String(a) == record.before_scene_revision && String(b) == record.after_scene_revision
                      && a <= b && b <= revision && b - a <= 1
              }) else { return nil }
        return value
    }
    private var applied: [Record] { records.filter { $0.before_scene_revision != $0.after_scene_revision } }
    var undoID: UUID? {
        guard applied.last?.after_scene_revision == scene_revision else { return nil }
        // Source/control publications do not appear in command history. Stop
        // at a revision gap: a later undo must not expose a pre-refinement edit.
        var cursor = scene_revision
        var suffix: [Record] = []
        for record in applied.reversed() {
            guard record.after_scene_revision == cursor else { break }
            suffix.append(record)
            cursor = record.before_scene_revision
        }
        var undone = Set<UUID>()
        for record in suffix.reversed() {
            if let id = record.original_command_id {
                if record.type == "undo" { undone.insert(id) }
                if record.type == "redo" { undone.remove(id) }
            }
        }
        return suffix.first { ["protect_feature", "split_edge", "set_dimension", "lock_position", "mark_unknown"].contains($0.type) && !undone.contains($0.command_id) }?.command_id
    }
    var redoID: UUID? {
        guard let last = applied.last, last.type == "undo", last.after_scene_revision == scene_revision else { return nil }
        return last.original_command_id
    }
}

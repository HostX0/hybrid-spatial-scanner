import Foundation

/// Opaque identity for an immutable metric snapshot. The token lets a renderer
/// correlate presentation with a fixed topology without copying metric data
/// into the optional appearance path.
struct AppearanceMetricSnapshotIdentity: Equatable, Sendable {
    let topologyToken: String
    let sceneRevision: UInt64

    init?(topologyToken: String, sceneRevision: UInt64) {
        guard AppearancePresentationAdapter.isIdentifier(topologyToken), sceneRevision > 0 else {
            return nil
        }
        self.topologyToken = topologyToken
        self.sceneRevision = sceneRevision
    }
}

/// Opaque renderer target derived from the accepted metric snapshot. It has no
/// positions, indices, dimensions, transforms, or mutable geometry state.
struct AppearanceRenderTarget: Equatable, Sendable {
    let entityID: String
    let metricSnapshot: AppearanceMetricSnapshotIdentity

    init?(entityID: String, metricSnapshot: AppearanceMetricSnapshotIdentity) {
        guard AppearancePresentationAdapter.isIdentifier(entityID) else { return nil }
        self.entityID = entityID
        self.metricSnapshot = metricSnapshot
    }
}

/// Swift value representation of the B-036 native binding. This is the full
/// rendering boundary: a local asset association and visual quality only.
struct AppearanceNativeBinding: Equatable, Sendable {
    let entityID: String
    let measuredSceneRevision: UInt64
    let keyframeID: String
    let localAssetID: String
    let visualConfidence: Double
}

/// Mirrors the status relevant to native B-036 update results.
enum AppearanceNativeUpdateStatus: Equatable, Sendable {
    case applied
    case unknownCalibration
    case rejectedCalibration
    case unknownTransform
    case rejectedTransform
    case unknownVisibility
    case rejectedVisibility
    case staleSceneRevision
    case invalidInput
    case invalidOptions
    case resourceLimit
    case internalFailure
}

/// A copied native result. `metricGeometryChanged` must remain false, matching
/// the B-036 contract, before the result can enter the renderer.
struct AppearanceNativeUpdateResult: Equatable, Sendable {
    let status: AppearanceNativeUpdateStatus
    let metricGeometryChanged: Bool
    /// Native B-036 uses this only when a failed candidate retained a prior
    /// admitted binding. That prior binding remains safe to present.
    let retainedExistingBinding: Bool
    let activeBinding: AppearanceNativeBinding?
}

/// Marker type which prevents an unadmitted native result from reaching the
/// presentation adapter. A prior admitted binding may remain present after a
/// rejected update only when native B-036 explicitly marked it as retained.
struct AppearanceAdmittedBinding: Equatable, Sendable {
    let nativeBinding: AppearanceNativeBinding

    init?(nativeResult: AppearanceNativeUpdateResult) {
        guard nativeResult.status == .applied || nativeResult.retainedExistingBinding,
              !nativeResult.metricGeometryChanged,
              let binding = nativeResult.activeBinding,
              AppearancePresentationAdapter.isValid(binding) else {
            return nil
        }
        nativeBinding = binding
    }
}

/// The render runtime resolves local asset IDs into opaque texture handles. No
/// file path, image bytes, image decoding, or geometry is accepted here.
struct AppearanceTextureResource: Equatable, Sendable {
    let localAssetID: String
    let rendererTextureID: String

    init?(localAssetID: String, rendererTextureID: String) {
        guard AppearancePresentationAdapter.isIdentifier(localAssetID),
              AppearancePresentationAdapter.isIdentifier(rendererTextureID) else {
            return nil
        }
        self.localAssetID = localAssetID
        self.rendererTextureID = rendererTextureID
    }
}

enum AppearanceRenderMaterial: Equatable, Sendable {
    /// Used when no local texture is available. Neutral is presentation-only
    /// and does not imply a missing metric surface has been reconstructed.
    case neutral
    case textured(rendererTextureID: String)
}

/// The renderer receives the exact immutable metric identity supplied by the
/// caller. The adapter never owns or mutates the underlying metric snapshot.
struct AppearanceRenderPresentation: Equatable, Sendable {
    let target: AppearanceRenderTarget
    let material: AppearanceRenderMaterial
    /// This is appearance quality only, never a geometry accuracy measure.
    let visualConfidence: Double?
}

enum AppearancePresentationRejection: Equatable, Sendable {
    case mismatchedTarget
    case staleSceneRevision
    case malformedBinding
}

enum AppearancePresentationResult: Equatable, Sendable {
    case present(AppearanceRenderPresentation)
    /// Suppression is fail-closed: no binding is presented for this target.
    case suppressed(AppearancePresentationRejection)
}

/// Pure native-result-to-render-presentation adapter. It intentionally has no
/// ARKit, Metal, Core bridge, file, or mutable snapshot dependency.
enum AppearancePresentationAdapter {
    static func present(
        target: AppearanceRenderTarget,
        admittedBinding: AppearanceAdmittedBinding?,
        textures: [AppearanceTextureResource]
    ) -> AppearancePresentationResult {
        guard let admittedBinding else {
            return .present(neutralPresentation(for: target))
        }

        let binding = admittedBinding.nativeBinding
        guard isValid(binding) else { return .suppressed(.malformedBinding) }
        guard binding.entityID == target.entityID else { return .suppressed(.mismatchedTarget) }
        guard binding.measuredSceneRevision == target.metricSnapshot.sceneRevision else {
            return .suppressed(.staleSceneRevision)
        }

        let texture = textures.first {
            $0.localAssetID == binding.localAssetID
        }
        guard let texture else {
            return .present(neutralPresentation(for: target, visualConfidence: binding.visualConfidence))
        }
        return .present(
            AppearanceRenderPresentation(
                target: target,
                material: .textured(rendererTextureID: texture.rendererTextureID),
                visualConfidence: binding.visualConfidence
            )
        )
    }

    private static func neutralPresentation(
        for target: AppearanceRenderTarget,
        visualConfidence: Double? = nil
    ) -> AppearanceRenderPresentation {
        AppearanceRenderPresentation(
            target: target,
            material: .neutral,
            visualConfidence: visualConfidence
        )
    }

    fileprivate static func isIdentifier(_ value: String) -> Bool {
        guard !value.isEmpty, value.utf8.count <= 128 else { return false }
        return value.utf8.allSatisfy { byte in
            (48...57).contains(byte)
                || (65...90).contains(byte)
                || (97...122).contains(byte)
                || byte == 45 || byte == 46 || byte == 95
        }
    }

    fileprivate static func isValid(_ binding: AppearanceNativeBinding) -> Bool {
        isIdentifier(binding.entityID)
            && binding.measuredSceneRevision > 0
            && isIdentifier(binding.keyframeID)
            && isIdentifier(binding.localAssetID)
            && binding.visualConfidence.isFinite
            && (0...1).contains(binding.visualConfidence)
    }
}

#if APPEARANCE_STANDALONE
import Foundation

@main
struct AppearancePresentationAdapterStandaloneTests {
    static func main() {
        let snapshot = AppearanceMetricSnapshotIdentity(topologyToken: "topology-r7", sceneRevision: 7)!
        let target = AppearanceRenderTarget(entityID: "wall-a", metricSnapshot: snapshot)!
        let binding = AppearanceNativeBinding(
            entityID: "wall-a",
            measuredSceneRevision: 7,
            keyframeID: "frame-12",
            localAssetID: "asset-wall-a",
            visualConfidence: 0.82
        )
        let admitted = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied,
            metricGeometryChanged: false,
            retainedExistingBinding: false,
            activeBinding: binding
        ))!
        let texture = AppearanceTextureResource(localAssetID: "asset-wall-a", rendererTextureID: "metal-wall-a")!

        let textured = AppearancePresentationAdapter.present(
            target: target,
            admittedBinding: admitted,
            textures: [texture]
        )
        guard case .present(let texturedPresentation) = textured,
              texturedPresentation.target.metricSnapshot == snapshot,
              texturedPresentation.material == .textured(rendererTextureID: "metal-wall-a"),
              texturedPresentation.visualConfidence == 0.82 else {
            fatalError("admitted same-revision binding must preserve the immutable metric identity")
        }

        let neutral = AppearancePresentationAdapter.present(
            target: target,
            admittedBinding: admitted,
            textures: []
        )
        guard case .present(let neutralPresentation) = neutral,
              neutralPresentation.target.metricSnapshot == snapshot,
              neutralPresentation.material == .neutral else {
            fatalError("missing texture must use a neutral material without changing metric identity")
        }

        let stale = AppearanceNativeBinding(
            entityID: "wall-a",
            measuredSceneRevision: 6,
            keyframeID: "frame-11",
            localAssetID: "asset-wall-a",
            visualConfidence: 0.8
        )
        let staleAdmission = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied,
            metricGeometryChanged: false,
            retainedExistingBinding: false,
            activeBinding: stale
        ))!
        guard AppearancePresentationAdapter.present(
            target: target,
            admittedBinding: staleAdmission,
            textures: [texture]
        ) == .suppressed(.staleSceneRevision) else {
            fatalError("a stale binding must fail closed")
        }

        let wrongTarget = AppearanceNativeBinding(
            entityID: "wall-b",
            measuredSceneRevision: 7,
            keyframeID: "frame-13",
            localAssetID: "asset-wall-a",
            visualConfidence: 0.8
        )
        let wrongTargetAdmission = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied,
            metricGeometryChanged: false,
            retainedExistingBinding: false,
            activeBinding: wrongTarget
        ))!
        guard AppearancePresentationAdapter.present(
            target: target,
            admittedBinding: wrongTargetAdmission,
            textures: [texture]
        ) == .suppressed(.mismatchedTarget) else {
            fatalError("a mismatched binding must fail closed")
        }

        let retainedAdmission = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .staleSceneRevision,
            metricGeometryChanged: false,
            retainedExistingBinding: true,
            activeBinding: binding
        ))
        guard retainedAdmission != nil,
              AppearancePresentationAdapter.present(
                target: target,
                admittedBinding: retainedAdmission,
                textures: [texture]
              ) == textured else {
            fatalError("an explicitly retained same-revision binding must remain presentable")
        }

        guard AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .unknownVisibility,
            metricGeometryChanged: false,
            retainedExistingBinding: false,
            activeBinding: binding
        )) == nil,
        AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied,
            metricGeometryChanged: true,
            retainedExistingBinding: false,
            activeBinding: binding
        )) == nil else {
            fatalError("only native applied non-metric results may enter presentation")
        }

        print("Appearance presentation adapter standalone tests passed")
    }
}
#else
import XCTest
@testable import HybridSpatialScanner

final class AppearancePresentationAdapterTests: XCTestCase {
    func testAdmittedBindingPreservesMetricIdentityAndFailsClosed() {
        let snapshot = AppearanceMetricSnapshotIdentity(topologyToken: "topology-r7", sceneRevision: 7)!
        let target = AppearanceRenderTarget(entityID: "wall-a", metricSnapshot: snapshot)!
        let binding = AppearanceNativeBinding(entityID: "wall-a", measuredSceneRevision: 7,
                                             keyframeID: "frame-12", localAssetID: "asset-wall-a",
                                             visualConfidence: 0.82)
        let admitted = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied, metricGeometryChanged: false,
            retainedExistingBinding: false, activeBinding: binding
        ))!
        let texture = AppearanceTextureResource(localAssetID: "asset-wall-a", rendererTextureID: "metal-wall-a")!

        XCTAssertEqual(
            AppearancePresentationAdapter.present(target: target, admittedBinding: admitted, textures: [texture]),
            .present(AppearanceRenderPresentation(target: target,
                                                  material: .textured(rendererTextureID: "metal-wall-a"),
                                                  visualConfidence: 0.82))
        )
        XCTAssertEqual(
            AppearancePresentationAdapter.present(target: target, admittedBinding: admitted, textures: []),
            .present(AppearanceRenderPresentation(target: target, material: .neutral, visualConfidence: 0.82))
        )

        let stale = AppearanceNativeBinding(entityID: "wall-a", measuredSceneRevision: 6,
                                            keyframeID: "frame-11", localAssetID: "asset-wall-a",
                                            visualConfidence: 0.8)
        let staleAdmission = AppearanceAdmittedBinding(nativeResult: AppearanceNativeUpdateResult(
            status: .applied, metricGeometryChanged: false,
            retainedExistingBinding: false, activeBinding: stale
        ))!
        XCTAssertEqual(
            AppearancePresentationAdapter.present(target: target, admittedBinding: staleAdmission, textures: [texture]),
            .suppressed(.staleSceneRevision)
        )
    }
}
#endif

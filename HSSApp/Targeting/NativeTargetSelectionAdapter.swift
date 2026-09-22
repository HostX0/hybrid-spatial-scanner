import Foundation
import simd

struct NativeTargetTap: Equatable, Sendable {
    let screenPoint: SIMD2<Float>
    let viewport: OverlayViewport
}

enum NativeTargetProvenance: Equatable, Sendable {
    case observed
    case fitted
}

enum NativeTargetPendingReason: Error, Equatable, Sendable {
    case captureUnavailable
    case missingCurrentDepthFrame
    case frameMismatch
    case invalidCalibration
    case invalidViewport
    case staleEpoch
    case staleFrame
    case viewportChanged
    case outsideDepthSupport
    case lowConfidenceDepth
    case missingAcceptedEpochTransform
    case awaitingSourceAcceptance
    case sourceCapacityReached
}

struct NativeTargetCandidate: Equatable, Sendable {
    let sourceFrame: SceneDepthFrameIdentity
    let observedDepthPixel: SIMD2<Float>
    let projectPositionMetres: SIMD3<Double>
    let provenance: NativeTargetProvenance
    let supportID: String
    let calibration: OverlayDepthCalibration
    let overlayFrame: OverlayFrameIdentity
    let displayTransform: OverlayDisplayTransform
    let viewport: OverlayViewport
}

struct NativeTargetSelection: Equatable, Sendable {
    let id: String
    let sceneRevision: UInt64
    let sourceFrame: SceneDepthFrameIdentity
    let projectPositionMetres: SIMD3<Double>
    let provenance: NativeTargetProvenance
    let supportIDs: [String]
    let observedDepthPixel: SIMD2<Float>
    let calibration: OverlayDepthCalibration
    let overlayFrame: OverlayFrameIdentity
    let displayTransform: OverlayDisplayTransform
    let viewport: OverlayViewport
}

enum NativeTargetSelectionState: Equatable, Sendable {
    case awaitingSelection
    case pending(NativeTargetPendingReason)
    case confirmed(NativeTargetSelection)
    case stale(NativeTargetSelection)
}

/// Calibrated tap-to-depth selection. It creates a candidate only from the
/// exact normalized frame/display transform pair supplied by the single
/// capture session. A separate confirmation step binds that candidate to the
/// immutable scene revision that accepted the source frame.
enum NativeTargetSelectionAdapter {
    static func prepare(
        tap: NativeTargetTap,
        normalizedFrame: NormalizedSceneDepthFrame,
        overlayCalibration: OverlayDepthCalibration,
        overlayFrame: OverlayFrameIdentity,
        displayTransform: OverlayDisplayTransform,
        currentEpoch: UInt64,
        projectFromWorld: [Double],
        now: TimeInterval,
        maximumFrameAge: TimeInterval
    ) -> Result<NativeTargetCandidate, NativeTargetPendingReason> {
        guard normalizedFrame.identity.sessionEpoch == overlayFrame.epoch,
              normalizedFrame.identity.sequence == overlayFrame.sequence,
              normalizedFrame.identity.monotonicTimestamp == overlayFrame.timestamp,
              displayTransform.frame == overlayFrame else {
            return .failure(.frameMismatch)
        }
        guard overlayFrame.epoch == currentEpoch else { return .failure(.staleEpoch) }
        guard now.isFinite, maximumFrameAge.isFinite, maximumFrameAge >= 0,
              overlayFrame.timestamp.isFinite, now >= overlayFrame.timestamp,
              now - overlayFrame.timestamp <= maximumFrameAge else {
            return .failure(.staleFrame)
        }
        guard validViewport(tap.viewport), validViewport(displayTransform.viewport) else {
            return .failure(.invalidViewport)
        }
        guard tap.viewport == displayTransform.viewport else { return .failure(.viewportChanged) }
        guard calibrationMatches(normalizedFrame.intrinsics, overlayCalibration) else {
            return .failure(.invalidCalibration)
        }
        guard let normalizedImagePoint = normalizedCapturedImagePoint(
            screenPoint: tap.screenPoint,
            viewport: tap.viewport,
            displayTransform: displayTransform
        ) else {
            return .failure(.outsideDepthSupport)
        }
        let imagePoint = SIMD2(
            normalizedImagePoint.x * Float(overlayCalibration.imageWidth),
            normalizedImagePoint.y * Float(overlayCalibration.imageHeight)
        )
        guard imagePoint.x >= overlayCalibration.depthCropX,
              imagePoint.y >= overlayCalibration.depthCropY,
              imagePoint.x < overlayCalibration.depthCropX + overlayCalibration.depthCropWidth,
              imagePoint.y < overlayCalibration.depthCropY + overlayCalibration.depthCropHeight else {
            return .failure(.outsideDepthSupport)
        }

        let depthX = Int(floor(
            (imagePoint.x - overlayCalibration.depthCropX)
                * Float(overlayCalibration.depthWidth) / overlayCalibration.depthCropWidth
        ))
        let depthY = Int(floor(
            (imagePoint.y - overlayCalibration.depthCropY)
                * Float(overlayCalibration.depthHeight) / overlayCalibration.depthCropHeight
        ))
        guard depthX >= 0, depthY >= 0,
              depthX < normalizedFrame.intrinsics.depthWidth,
              depthY < normalizedFrame.intrinsics.depthHeight else {
            return .failure(.outsideDepthSupport)
        }
        let index = depthY * normalizedFrame.intrinsics.depthWidth + depthX
        guard normalizedFrame.depthMeters.indices.contains(index),
              normalizedFrame.validDepthMask.indices.contains(index),
              normalizedFrame.metricFusionMask.indices.contains(index),
              normalizedFrame.validDepthMask[index] != 0 else {
            return .failure(.outsideDepthSupport)
        }
        guard normalizedFrame.metricFusionMask[index] != 0 else {
            return .failure(.lowConfidenceDepth)
        }
        let depth = normalizedFrame.depthMeters[index]
        guard depth.isFinite, depth > 0,
              let projectPoint = unproject(
                depth: depth,
                pixel: SIMD2(Float(depthX), Float(depthY)),
                intrinsics: normalizedFrame.intrinsics,
                worldFromCamera: normalizedFrame.worldFromCamera,
                projectFromWorld: projectFromWorld
              ) else {
            return .failure(.invalidCalibration)
        }

        let supportID = "depth-\(overlayFrame.epoch)-\(overlayFrame.sequence)"
        return .success(NativeTargetCandidate(
            sourceFrame: normalizedFrame.identity,
            observedDepthPixel: SIMD2(Float(depthX), Float(depthY)),
            projectPositionMetres: projectPoint,
            provenance: .observed,
            supportID: supportID,
            calibration: overlayCalibration,
            overlayFrame: overlayFrame,
            displayTransform: displayTransform,
            viewport: tap.viewport
        ))
    }

    static func confirm(
        _ candidate: NativeTargetCandidate,
        acceptedSourceFrame: SceneDepthFrameIdentity,
        currentSceneRevision: UInt64
    ) -> NativeTargetSelection? {
        guard candidate.sourceFrame == acceptedSourceFrame, currentSceneRevision > 0,
              candidate.provenance == .observed || candidate.provenance == .fitted else {
            return nil
        }
        return NativeTargetSelection(
            id: "target-\(candidate.overlayFrame.epoch)-\(candidate.overlayFrame.sequence)-\(Int(candidate.observedDepthPixel.x))-\(Int(candidate.observedDepthPixel.y))",
            sceneRevision: currentSceneRevision,
            sourceFrame: candidate.sourceFrame,
            projectPositionMetres: candidate.projectPositionMetres,
            provenance: candidate.provenance,
            supportIDs: [candidate.supportID],
            observedDepthPixel: candidate.observedDepthPixel,
            calibration: candidate.calibration,
            overlayFrame: candidate.overlayFrame,
            displayTransform: candidate.displayTransform,
            viewport: candidate.viewport
        )
    }

    private static func normalizedCapturedImagePoint(
        screenPoint: SIMD2<Float>,
        viewport: OverlayViewport,
        displayTransform: OverlayDisplayTransform
    ) -> SIMD2<Float>? {
        guard screenPoint.x.isFinite, screenPoint.y.isFinite,
              screenPoint.x >= 0, screenPoint.y >= 0,
              screenPoint.x < viewport.width, screenPoint.y < viewport.height else {
            return nil
        }
        let determinant = displayTransform.a * displayTransform.d
            - displayTransform.b * displayTransform.c
        guard determinant.isFinite, abs(determinant) > 0.000_001 else { return nil }
        let viewportPoint = SIMD2(
            screenPoint.x / viewport.width - displayTransform.tx,
            screenPoint.y / viewport.height - displayTransform.ty
        )
        let imageNormalized = SIMD2(
            (displayTransform.d * viewportPoint.x - displayTransform.c * viewportPoint.y) / determinant,
            (-displayTransform.b * viewportPoint.x + displayTransform.a * viewportPoint.y) / determinant
        )
        guard imageNormalized.x.isFinite, imageNormalized.y.isFinite,
              imageNormalized.x >= 0, imageNormalized.y >= 0,
              imageNormalized.x < 1, imageNormalized.y < 1 else { return nil }
        return imageNormalized
    }

    private static func calibrationMatches(
        _ normalized: ScaledSceneDepthIntrinsics,
        _ overlay: OverlayDepthCalibration
    ) -> Bool {
        guard overlay.imageWidth == normalized.calibrationImageWidth,
              overlay.imageHeight == normalized.calibrationImageHeight,
              overlay.depthWidth == normalized.depthWidth,
              overlay.depthHeight == normalized.depthHeight,
              overlay.depthCropX == normalized.depthCropX,
              overlay.depthCropY == normalized.depthCropY,
              overlay.depthCropWidth == normalized.depthCropWidth,
              overlay.depthCropHeight == normalized.depthCropHeight else { return false }
        let xScale = Float(overlay.depthWidth) / overlay.depthCropWidth
        let yScale = Float(overlay.depthHeight) / overlay.depthCropHeight
        let values = [overlay.fx, overlay.fy, overlay.cx, overlay.cy]
        guard values.allSatisfy(\.isFinite), overlay.fx > 0, overlay.fy > 0 else { return false }
        return approximatelyEqual(normalized.fx, overlay.fx * xScale)
            && approximatelyEqual(normalized.fy, overlay.fy * yScale)
            && approximatelyEqual(normalized.cx, (overlay.cx - overlay.depthCropX) * xScale)
            && approximatelyEqual(normalized.cy, (overlay.cy - overlay.depthCropY) * yScale)
    }

    private static func unproject(
        depth: Float,
        pixel: SIMD2<Float>,
        intrinsics: ScaledSceneDepthIntrinsics,
        worldFromCamera: [Float],
        projectFromWorld: [Double]
    ) -> SIMD3<Double>? {
        guard intrinsics.fx.isFinite, intrinsics.fy.isFinite,
              intrinsics.fx > 0, intrinsics.fy > 0,
              validRigidTransform(worldFromCamera.map(Double.init)),
              validRigidTransform(projectFromWorld) else { return nil }
        let optical = SIMD4<Double>(
            Double((pixel.x - intrinsics.cx) * depth / intrinsics.fx),
            Double(-(pixel.y - intrinsics.cy) * depth / intrinsics.fy),
            Double(-depth),
            1
        )
        let world = multiply(worldFromCamera.map(Double.init), optical)
        let project = multiply(projectFromWorld, world)
        guard project.x.isFinite, project.y.isFinite, project.z.isFinite,
              abs(project.w - 1) <= 0.001 else { return nil }
        return SIMD3(project.x, project.y, project.z)
    }

    private static func multiply(_ matrix: [Double], _ vector: SIMD4<Double>) -> SIMD4<Double> {
        SIMD4(
            matrix[0] * vector.x + matrix[4] * vector.y + matrix[8] * vector.z + matrix[12] * vector.w,
            matrix[1] * vector.x + matrix[5] * vector.y + matrix[9] * vector.z + matrix[13] * vector.w,
            matrix[2] * vector.x + matrix[6] * vector.y + matrix[10] * vector.z + matrix[14] * vector.w,
            matrix[3] * vector.x + matrix[7] * vector.y + matrix[11] * vector.z + matrix[15] * vector.w
        )
    }

    private static func validRigidTransform(_ values: [Double]) -> Bool {
        guard values.count == 16, values.allSatisfy(\.isFinite) else { return false }
        let tolerance = 0.001
        guard abs(values[3]) <= tolerance, abs(values[7]) <= tolerance,
              abs(values[11]) <= tolerance, abs(values[15] - 1) <= tolerance else { return false }
        let x = SIMD3(values[0], values[1], values[2])
        let y = SIMD3(values[4], values[5], values[6])
        let z = SIMD3(values[8], values[9], values[10])
        return abs(simd_length(x) - 1) <= tolerance
            && abs(simd_length(y) - 1) <= tolerance
            && abs(simd_length(z) - 1) <= tolerance
            && abs(simd_dot(x, y)) <= tolerance
            && abs(simd_dot(x, z)) <= tolerance
            && abs(simd_dot(y, z)) <= tolerance
            && simd_dot(simd_cross(x, y), z) > 1 - tolerance
    }

    private static func validViewport(_ viewport: OverlayViewport) -> Bool {
        viewport.width.isFinite && viewport.height.isFinite
            && viewport.width > 0 && viewport.height > 0
    }

    private static func approximatelyEqual(_ lhs: Float, _ rhs: Float) -> Bool {
        abs(lhs - rhs) <= max(0.000_1, max(abs(lhs), abs(rhs)) * 0.000_01)
    }
}

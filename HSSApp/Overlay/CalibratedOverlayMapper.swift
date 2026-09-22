import Foundation

/// The display orientation used when ARKit supplied its display transform.
/// The depth calibration itself always remains in unrotated captured-image
/// pixels; this value prevents a transform obtained for one presentation from
/// being reused after a rotation.
enum OverlayPresentationOrientation: String, CaseIterable, Equatable, Sendable {
    case portrait
    case portraitUpsideDown
    case landscapeLeft
    case landscapeRight
}

struct OverlayViewport: Equatable, Sendable {
    let width: Float
    let height: Float
    let orientation: OverlayPresentationOrientation
    /// Increment when the renderer replaces its viewport, including rotation.
    let generation: UInt64
}

struct OverlayFrameIdentity: Equatable, Sendable {
    let epoch: UInt64
    let sequence: UInt64
    let timestamp: TimeInterval
}

/// Calibration for the depth raster. These values deliberately describe the
/// captured image and depth crop, never UIKit points or a display viewport.
struct OverlayDepthCalibration: Equatable, Sendable {
    let imageWidth: Int
    let imageHeight: Int
    let depthWidth: Int
    let depthHeight: Int
    let fx: Float
    let fy: Float
    let cx: Float
    let cy: Float
    let depthCropX: Float
    let depthCropY: Float
    let depthCropWidth: Float
    let depthCropHeight: Float
}

/// A value-copy of `ARFrame.displayTransform(for:viewportSize:)`. It maps
/// normalized captured-image coordinates to normalized viewport coordinates.
struct OverlayDisplayTransform: Equatable, Sendable {
    let frame: OverlayFrameIdentity
    let viewport: OverlayViewport
    let a: Float
    let b: Float
    let c: Float
    let d: Float
    let tx: Float
    let ty: Float

    func applying(to normalizedImagePoint: SIMD2<Float>) -> SIMD2<Float> {
        SIMD2(
            a * normalizedImagePoint.x + c * normalizedImagePoint.y + tx,
            b * normalizedImagePoint.x + d * normalizedImagePoint.y + ty
        )
    }
}

struct OverlayScreenPoint: Equatable, Sendable {
    let point: SIMD2<Float>
    /// The image point is retained for diagnostics and hit provenance; it is
    /// not a new project-space point.
    let normalizedCapturedImagePoint: SIMD2<Float>
}

enum CalibratedOverlayRejection: String, Equatable, Sendable {
    case invalidCalibration
    case invalidViewport
    case invalidDisplayTransform
    case invalidFrameTimestamp
    case staleEpoch
    case staleFrame
    case transformFrameMismatch
    case viewportChanged
    case orientationChanged
    case invalidDepthPixel
    case outsideCapturedImage
    case outsideViewport
}

enum CalibratedOverlayProjection: Equatable, Sendable {
    case displayed(OverlayScreenPoint)
    /// A hidden result must not be substituted with a screen-space estimate.
    case hidden(CalibratedOverlayRejection)
}

/// Pure calibrated-image-to-screen mapping for overlays backed by an observed
/// depth pixel. It does not unproject depth, raycast, fit a plane, or create
/// any geometry. The caller must keep a pending target pending when this
/// mapper returns `.hidden`.
enum CalibratedOverlayMapper {
    static func project(
        observedDepthPixel: SIMD2<Float>,
        calibration: OverlayDepthCalibration,
        frame: OverlayFrameIdentity,
        displayTransform: OverlayDisplayTransform,
        currentViewport: OverlayViewport,
        currentEpoch: UInt64,
        now: TimeInterval,
        maximumFrameAge: TimeInterval
    ) -> CalibratedOverlayProjection {
        guard isValid(calibration) else { return .hidden(.invalidCalibration) }
        guard isValid(currentViewport), isValid(displayTransform.viewport) else {
            return .hidden(.invalidViewport)
        }
        guard isValid(displayTransform) else { return .hidden(.invalidDisplayTransform) }
        guard frame.timestamp.isFinite, now.isFinite, maximumFrameAge.isFinite,
              maximumFrameAge >= 0 else {
            return .hidden(.invalidFrameTimestamp)
        }
        guard frame.epoch == currentEpoch else { return .hidden(.staleEpoch) }
        guard displayTransform.frame == frame else { return .hidden(.transformFrameMismatch) }
        guard displayTransform.viewport.generation == currentViewport.generation,
              displayTransform.viewport.width == currentViewport.width,
              displayTransform.viewport.height == currentViewport.height else {
            return .hidden(.viewportChanged)
        }
        guard displayTransform.viewport.orientation == currentViewport.orientation else {
            return .hidden(.orientationChanged)
        }
        guard now >= frame.timestamp, now - frame.timestamp <= maximumFrameAge else {
            return .hidden(.staleFrame)
        }
        guard observedDepthPixel.x.isFinite, observedDepthPixel.y.isFinite,
              observedDepthPixel.x >= 0, observedDepthPixel.y >= 0,
              observedDepthPixel.x < Float(calibration.depthWidth),
              observedDepthPixel.y < Float(calibration.depthHeight) else {
            return .hidden(.invalidDepthPixel)
        }

        // Pixel centres map through the declared depth crop into captured-image
        // pixels. Orientation is represented only by the matching transform.
        let imagePoint = SIMD2(
            calibration.depthCropX + (observedDepthPixel.x + 0.5) * calibration.depthCropWidth / Float(calibration.depthWidth),
            calibration.depthCropY + (observedDepthPixel.y + 0.5) * calibration.depthCropHeight / Float(calibration.depthHeight)
        )
        guard imagePoint.x >= 0, imagePoint.y >= 0,
              imagePoint.x < Float(calibration.imageWidth),
              imagePoint.y < Float(calibration.imageHeight) else {
            return .hidden(.outsideCapturedImage)
        }
        let normalizedImagePoint = SIMD2(
            imagePoint.x / Float(calibration.imageWidth),
            imagePoint.y / Float(calibration.imageHeight)
        )
        let normalizedViewportPoint = displayTransform.applying(to: normalizedImagePoint)
        guard normalizedViewportPoint.x.isFinite, normalizedViewportPoint.y.isFinite,
              normalizedViewportPoint.x >= 0, normalizedViewportPoint.x <= 1,
              normalizedViewportPoint.y >= 0, normalizedViewportPoint.y <= 1 else {
            return .hidden(.outsideViewport)
        }
        return .displayed(OverlayScreenPoint(
            point: SIMD2(normalizedViewportPoint.x * currentViewport.width,
                         normalizedViewportPoint.y * currentViewport.height),
            normalizedCapturedImagePoint: normalizedImagePoint
        ))
    }

    private static func isValid(_ calibration: OverlayDepthCalibration) -> Bool {
        let values = [calibration.fx, calibration.fy, calibration.cx, calibration.cy,
                      calibration.depthCropX, calibration.depthCropY,
                      calibration.depthCropWidth, calibration.depthCropHeight]
        return calibration.imageWidth > 0 && calibration.imageHeight > 0 &&
            calibration.depthWidth > 0 && calibration.depthHeight > 0 &&
            values.allSatisfy(\.isFinite) && calibration.fx > 0 && calibration.fy > 0 &&
            calibration.depthCropX >= 0 && calibration.depthCropY >= 0 &&
            calibration.depthCropWidth > 0 && calibration.depthCropHeight > 0 &&
            calibration.depthCropX + calibration.depthCropWidth <= Float(calibration.imageWidth) &&
            calibration.depthCropY + calibration.depthCropHeight <= Float(calibration.imageHeight) &&
            calibration.cx >= 0 && calibration.cx < Float(calibration.imageWidth) &&
            calibration.cy >= 0 && calibration.cy < Float(calibration.imageHeight)
    }

    private static func isValid(_ viewport: OverlayViewport) -> Bool {
        viewport.width.isFinite && viewport.height.isFinite && viewport.width > 0 && viewport.height > 0
    }

    private static func isValid(_ transform: OverlayDisplayTransform) -> Bool {
        let values = [transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty]
        let determinant = transform.a * transform.d - transform.b * transform.c
        return values.allSatisfy(\.isFinite) && determinant.isFinite && abs(determinant) > 0.000_001
    }
}

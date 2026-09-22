#if canImport(ARKit) && canImport(CoreVideo) && canImport(UIKit)
@preconcurrency import ARKit
import CoreVideo
import UIKit

/// Platform boundary for B-028. It snapshots `ARFrame` calibration and the
/// display transform together; callers must discard the snapshot on a new
/// viewport generation or orientation and request another one from ARKit.
enum ARFrameCalibratedOverlayAdapter {
    static func makeSnapshot(
        frame: ARFrame,
        sessionEpoch: UInt64,
        sequence: UInt64,
        viewport: OverlayViewport
    ) -> (calibration: OverlayDepthCalibration, frame: OverlayFrameIdentity, displayTransform: OverlayDisplayTransform)? {
        guard let sceneDepth = frame.sceneDepth,
              let interfaceOrientation = uiOrientation(for: viewport.orientation) else {
            return nil
        }
        let imageResolution = frame.camera.imageResolution
        let intrinsics = frame.camera.intrinsics
        let identity = OverlayFrameIdentity(epoch: sessionEpoch, sequence: sequence, timestamp: frame.timestamp)
        let transform = frame.displayTransform(
            for: interfaceOrientation,
            viewportSize: CGSize(width: CGFloat(viewport.width), height: CGFloat(viewport.height))
        )
        return (
            OverlayDepthCalibration(
                imageWidth: Int(imageResolution.width), imageHeight: Int(imageResolution.height),
                depthWidth: CVPixelBufferGetWidth(sceneDepth.depthMap),
                depthHeight: CVPixelBufferGetHeight(sceneDepth.depthMap),
                fx: intrinsics[0, 0], fy: intrinsics[1, 1],
                cx: intrinsics[2, 0], cy: intrinsics[2, 1],
                depthCropX: 0, depthCropY: 0,
                depthCropWidth: Float(imageResolution.width), depthCropHeight: Float(imageResolution.height)
            ),
            identity,
            OverlayDisplayTransform(
                frame: identity, viewport: viewport,
                a: Float(transform.a), b: Float(transform.b), c: Float(transform.c), d: Float(transform.d),
                tx: Float(transform.tx), ty: Float(transform.ty)
            )
        )
    }

    private static func uiOrientation(for orientation: OverlayPresentationOrientation) -> UIInterfaceOrientation? {
        switch orientation {
        case .portrait: .portrait
        case .portraitUpsideDown: .portraitUpsideDown
        case .landscapeLeft: .landscapeLeft
        case .landscapeRight: .landscapeRight
        }
    }
}
#endif

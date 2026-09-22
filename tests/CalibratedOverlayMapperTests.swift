#if OVERLAY_STANDALONE
import Foundation

@main
struct CalibratedOverlayMapperStandaloneTests {
    static func main() {
        let frame = OverlayFrameIdentity(epoch: 4, sequence: 12, timestamp: 10)
        let portrait = OverlayViewport(width: 200, height: 400, orientation: .portrait, generation: 3)
        let calibration = fixtureCalibration()
        let identity = OverlayDisplayTransform(frame: frame, viewport: portrait,
                                               a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0)
        let shown = CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(1.5, 0), calibration: calibration,
                                                     frame: frame, displayTransform: identity,
                                                     currentViewport: portrait, currentEpoch: 4,
                                                     now: 10.02, maximumFrameAge: 0.1)
        guard case .displayed(let point) = shown,
              abs(point.point.x - 100) < 0.001, abs(point.point.y - 100) < 0.001 else {
            fatalError("calibrated pixel mapping failed")
        }
        let rotated = OverlayViewport(width: 400, height: 200, orientation: .landscapeLeft, generation: 4)
        guard CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(1.5, 0), calibration: calibration,
                                              frame: frame, displayTransform: identity,
                                              currentViewport: rotated, currentEpoch: 4,
                                              now: 10.02, maximumFrameAge: 0.1) == .hidden(.viewportChanged) else {
            fatalError("rotation must invalidate the old transform")
        }
        guard CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(1.5, 0), calibration: calibration,
                                              frame: frame, displayTransform: identity,
                                              currentViewport: portrait, currentEpoch: 5,
                                              now: 10.02, maximumFrameAge: 0.1) == .hidden(.staleEpoch) else {
            fatalError("cross-epoch overlay must remain hidden")
        }
        print("Calibrated overlay standalone tests passed")
    }

    static func fixtureCalibration() -> OverlayDepthCalibration {
        OverlayDepthCalibration(imageWidth: 8, imageHeight: 4, depthWidth: 4, depthHeight: 2,
                                fx: 6, fy: 5, cx: 4, cy: 2,
                                depthCropX: 0, depthCropY: 0, depthCropWidth: 8, depthCropHeight: 4)
    }
}
#else
import XCTest
@testable import HybridSpatialScanner

final class CalibratedOverlayMapperTests: XCTestCase {
    func testMapsDepthPixelThroughCropAndCurrentDisplayTransform() {
        let frame = OverlayFrameIdentity(epoch: 4, sequence: 12, timestamp: 10)
        let viewport = OverlayViewport(width: 200, height: 400, orientation: .portrait, generation: 3)
        let transform = OverlayDisplayTransform(frame: frame, viewport: viewport,
                                                a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0)
        let result = CalibratedOverlayMapper.project(
            observedDepthPixel: SIMD2(1.5, 0), calibration: fixtureCalibration(), frame: frame,
            displayTransform: transform, currentViewport: viewport, currentEpoch: 4,
            now: 10.02, maximumFrameAge: 0.1
        )
        guard case .displayed(let point) = result else { return XCTFail("Expected overlay") }
        XCTAssertEqual(point.normalizedCapturedImagePoint.x, 0.5, accuracy: 0.0001)
        XCTAssertEqual(point.normalizedCapturedImagePoint.y, 0.25, accuracy: 0.0001)
        XCTAssertEqual(point.point.x, 100, accuracy: 0.0001)
        XCTAssertEqual(point.point.y, 100, accuracy: 0.0001)
    }

    func testCurrentRotationRequiresANewTransformAndViewport() {
        let frame = OverlayFrameIdentity(epoch: 4, sequence: 12, timestamp: 10)
        let portrait = OverlayViewport(width: 200, height: 400, orientation: .portrait, generation: 3)
        let landscape = OverlayViewport(width: 400, height: 200, orientation: .landscapeLeft, generation: 4)
        let oldTransform = OverlayDisplayTransform(frame: frame, viewport: portrait,
                                                   a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0)
        XCTAssertEqual(
            CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(0, 0), calibration: fixtureCalibration(),
                                             frame: frame, displayTransform: oldTransform, currentViewport: landscape,
                                             currentEpoch: 4, now: 10.02, maximumFrameAge: 0.1),
            .hidden(.viewportChanged)
        )
    }

    func testRejectsStaleEpochAndInvalidCalibrationWithoutSynthesizingAPoint() {
        let frame = OverlayFrameIdentity(epoch: 4, sequence: 12, timestamp: 10)
        let viewport = OverlayViewport(width: 200, height: 400, orientation: .portrait, generation: 3)
        let transform = OverlayDisplayTransform(frame: frame, viewport: viewport,
                                                a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0)
        XCTAssertEqual(
            CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(0, 0), calibration: fixtureCalibration(),
                                             frame: frame, displayTransform: transform, currentViewport: viewport,
                                             currentEpoch: 5, now: 10.02, maximumFrameAge: 0.1),
            .hidden(.staleEpoch)
        )
        var invalid = fixtureCalibration()
        invalid = OverlayDepthCalibration(imageWidth: invalid.imageWidth, imageHeight: invalid.imageHeight,
                                           depthWidth: invalid.depthWidth, depthHeight: invalid.depthHeight,
                                           fx: 0, fy: invalid.fy, cx: invalid.cx, cy: invalid.cy,
                                           depthCropX: invalid.depthCropX, depthCropY: invalid.depthCropY,
                                           depthCropWidth: invalid.depthCropWidth, depthCropHeight: invalid.depthCropHeight)
        XCTAssertEqual(
            CalibratedOverlayMapper.project(observedDepthPixel: SIMD2(0, 0), calibration: invalid,
                                             frame: frame, displayTransform: transform, currentViewport: viewport,
                                             currentEpoch: 4, now: 10.02, maximumFrameAge: 0.1),
            .hidden(.invalidCalibration)
        )
    }

    private func fixtureCalibration() -> OverlayDepthCalibration {
        OverlayDepthCalibration(imageWidth: 8, imageHeight: 4, depthWidth: 4, depthHeight: 2,
                                fx: 6, fy: 5, cx: 4, cy: 2,
                                depthCropX: 0, depthCropY: 0, depthCropWidth: 8, depthCropHeight: 4)
    }
}
#endif

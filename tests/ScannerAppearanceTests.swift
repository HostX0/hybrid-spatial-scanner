import XCTest
import UIKit
import CoreImage
import CoreVideo
import ImageIO
import simd
@testable import HybridSpatialScanner

/// Synthetic native integration fixtures, not real-camera/metrology evidence.
/// Native raster tests use the production encoder, not a second implementation.
final class ScannerAppearanceTests: XCTestCase {
    @MainActor
    func testSourceCheckpointPrecedesAppearanceAndPreservesExactGeometry() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        let directory = f.root.appendingPathComponent(f.projectID.hssDirectoryName).appendingPathComponent("appearance")
        XCTAssertFalse(FileManager.default.fileExists(atPath: directory.path), "Pending images are not durable evidence.")
        let absent = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        XCTAssertNil(absent)
        let checkpoint = try await f.runtime.checkpoint()
        XCTAssertEqual(checkpoint.durableEventCount, 4)
        XCTAssertEqual(checkpoint.lastEventSequence, 12, "Intake sequence gaps must not be replaced by the event count.")
        try await f.store.commit(projectID: f.projectID, runtimeToken: f.token, lastDurableEventSequence: checkpoint.lastEventSequence)
        let projected = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        let layer = try XCTUnwrap(projected)
        XCTAssertEqual(layer.faces.count, 1)
        XCTAssertEqual(layer.images.count, 1)
        XCTAssertTrue(layer.matches(f.snapshot))
        let sourceAfter = try await f.runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(sourceAfter, f.snapshot)
        let manifest = try XCTUnwrap(JSONSerialization.jsonObject(with: layer.manifest) as? [String: Any])
        XCTAssertEqual(manifest["source_is_synthetic"] as? Bool, true)
        XCTAssertTrue(String(data: layer.obj, encoding: .utf8)!.contains("mtllib scene.mtl\n"))
        let reopened = ScannerAppearanceStore(trustedRoot: f.root)
        let durableLayer = try await reopened.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        XCTAssertEqual(durableLayer?.faces, layer.faces)
        XCTAssertEqual(durableLayer?.images, layer.images)
        let sidecar = try String(contentsOf: directory.appendingPathComponent("manifest.json"), encoding: .utf8)
        XCTAssertFalse(sidecar.contains("monotonicTimestamp"))
        XCTAssertFalse(sidecar.contains("EXIF"))
        XCTAssertFalse(sidecar.contains(f.root.path))
    }

    @MainActor
    func testRuntimeReplacementRefusesOldPendingImagesEvenForSameProject() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        _ = try await f.runtime.checkpoint()
        try await f.store.commit(projectID: f.projectID, runtimeToken: UUID(), lastDurableEventSequence: 12)
        let layer = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        XCTAssertNil(layer, "An old failed/replaced runtime cannot rebind its pending source image.")
    }

    @MainActor
    func testPreviousManifestRecoveryRemovesHigherAppearanceBindings() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        _ = try await f.runtime.checkpoint()
        try await f.store.commit(projectID: f.projectID, runtimeToken: f.token, lastDurableEventSequence: 12)
        try await f.store.reconcile(projectID: f.projectID, lastDurableEventSequence: 2)
        let layer = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        XCTAssertNil(layer, "A reused event sequence does not revive abandoned optional evidence.")
        let folder = f.root.appendingPathComponent(f.projectID.hssDirectoryName).appendingPathComponent("appearance")
        XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: folder.path), ["manifest.json"])
    }

    @MainActor
    func testCorruptImageCannotDecorateOtherwiseValidSavedMetricSnapshot() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        _ = try await f.runtime.checkpoint()
        try await f.store.commit(projectID: f.projectID, runtimeToken: f.token, lastDurableEventSequence: 12)
        let folder = f.root.appendingPathComponent(f.projectID.hssDirectoryName).appendingPathComponent("appearance")
        let image = try XCTUnwrap(FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil).first { $0.pathExtension == "jpg" })
        try Data("corrupt image".utf8).write(to: image)
        let layer = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        XCTAssertNil(layer)
        let after = try await f.runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        XCTAssertEqual(after, f.snapshot)
    }

    @MainActor
    func testTexturedRendererKeepsExactPositionsAndRespectsDerivedMask() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        _ = try await f.runtime.checkpoint()
        try await f.store.commit(projectID: f.projectID, runtimeToken: f.token, lastDurableEventSequence: 12)
        let projected = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        let layer = try XCTUnwrap(projected)
        let scene = ScannerMeshSceneBuilder.scene(from: f.snapshot, texturedMesh: layer)
        let geometry = try XCTUnwrap(scene.rootNode.childNodes.first?.geometry)
        XCTAssertEqual(geometry.sources(for: .vertex).first?.vectorCount, 3)
        XCTAssertEqual(geometry.sources(for: .texcoord).first?.vectorCount, 3)
        XCTAssertEqual(geometry.elements.map(\.primitiveCount).reduce(0,+), 1)
        let hidden = ScannerSceneSnapshot(revision: f.snapshot.revision, verticesMetres: f.snapshot.verticesMetres,
            triangleIndices: f.snapshot.triangleIndices, reviewTriangleInclusion: [0], sceneJSON: f.snapshot.sceneJSON,
            provenance: [.syntheticTest], isFrozen: true)
        let masked = ScannerMeshSceneBuilder.scene(from: hidden, texturedMesh: layer)
        XCTAssertNil(masked.rootNode.childNodes.first?.geometry)
        XCTAssertEqual(f.snapshot.triangleIndices, [0,1,2])
    }

    func testColorIntrinsicsPreservePixelCentersAtHalfAndQuarterScale() {
        let k = ScaledSceneDepthIntrinsics(fx: 6,fy: 7,cx: 5.25,cy: 2.5,
            calibrationImageWidth: 16,calibrationImageHeight: 12,depthCropX: 0,depthCropY: 0,
            depthCropWidth: 16,depthCropHeight: 12,depthWidth: 8,depthHeight: 6)
        for scale in [0.5, 0.25] {
            let color = ScannerAppearanceStore.colorIntrinsics(depth: k, imageWidth: Int(16 * scale), imageHeight: Int(12 * scale))
            XCTAssertEqual(color[0], Double(k.fx) * 2 * scale, accuracy: 1e-12)
            XCTAssertEqual(color[2] + 0.5, (Double(k.cx) * 2 + 0.5) * scale, accuracy: 1e-12)
            XCTAssertEqual((color[2] + 0.5) / (16 * scale), (Double(k.cx) * 2 + 0.5) / 16, accuracy: 1e-12)
            XCTAssertEqual(color[3] + 0.5, (Double(k.cy) * 2 + 0.5) * scale, accuracy: 1e-12)
        }
    }

    func testProductionJPEGKeepsSensorRasterAndUVPixelCentersAtHalfAndQuarterScale() throws {
        let context = CIContext(options: [.cacheIntermediates: false])
        let colors: [[UInt8]] = [[240,20,20], [20,240,20], [20,20,240], [240,240,20]]
        for width in [1920, 3840] {
            let height = width / 2
            var buffer: CVPixelBuffer?
            XCTAssertEqual(CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA,
                [kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary, &buffer), kCVReturnSuccess)
            let pixelBuffer = try XCTUnwrap(buffer)
            XCTAssertEqual(CVPixelBufferLockBaseAddress(pixelBuffer, []), kCVReturnSuccess)
            do {
                defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, []) }
                let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(pixelBuffer)).assumingMemoryBound(to: UInt8.self)
                let stride = CVPixelBufferGetBytesPerRow(pixelBuffer)
                for y in 0..<height {
                    for x in 0..<width {
                        let c = colors[(y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0)]
                        let p = base.advanced(by: y * stride + x * 4)
                        p[0] = c[2]; p[1] = c[1]; p[2] = c[0]; p[3] = 255
                    }
                }
            }
            let photo = try XCTUnwrap(ScannerAppearancePhotoSampler.encodeSensorImage(pixelBuffer,
                width: width, height: height, context: context))
            XCTAssertEqual(photo.width, 960); XCTAssertEqual(photo.height, 480)
            XCTAssertTrue(ScannerAppearanceStore.validJPEG(photo.jpeg, width: 960, height: 480))
            let source = try XCTUnwrap(CGImageSourceCreateWithData(photo.jpeg as CFData, nil))
            let properties = try XCTUnwrap(CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any])
            XCTAssertNil(properties[kCGImagePropertyGPSDictionary])
            XCTAssertEqual((properties[kCGImagePropertyOrientation] as? NSNumber)?.intValue ?? 1, 1)
            let decoded = try XCTUnwrap(CIImage(data: photo.jpeg))
            let depthWidth = width / 8, depthHeight = height / 8
            let k = ScaledSceneDepthIntrinsics(fx: Float(width) / 16, fy: Float(height) / 16,
                cx: Float(depthWidth) * 0.3, cy: Float(depthHeight) * 0.4,
                calibrationImageWidth: width, calibrationImageHeight: height, depthCropX: 0, depthCropY: 0,
                depthCropWidth: Float(width), depthCropHeight: Float(height), depthWidth: depthWidth, depthHeight: depthHeight)
            let colorK = ScannerAppearanceStore.colorIntrinsics(depth: k, imageWidth: photo.width, imageHeight: photo.height)
            for (index, fraction) in [(0, SIMD2<Double>(0.25, 0.25)), (1, SIMD2(0.75, 0.25)),
                                      (2, SIMD2(0.25, 0.75)), (3, SIMD2(0.75, 0.75))] {
                // Off-axis camera rays with a known sensor-raster target. UV v
                // uses the OBJ/SceneKit bottom-left convention, CI uses y-up.
                let sensorX = fraction.x * Double(width) - 0.5
                let sensorY = fraction.y * Double(height) - 0.5
                let rayX = (sensorX - Double(k.cx) * 8) / (Double(k.fx) * 8)
                let rayY = (sensorY - Double(k.cy) * 8) / (Double(k.fy) * 8)
                let u = (colorK[0] * rayX + colorK[2] + 0.5) / Double(photo.width)
                let v = 1 - (colorK[1] * rayY + colorK[3] + 0.5) / Double(photo.height)
                XCTAssertEqual(u, fraction.x, accuracy: 1e-12)
                XCTAssertEqual(v, 1 - fraction.y, accuracy: 1e-12)
                var rgba = [UInt8](repeating: 0, count: 4)
                rgba.withUnsafeMutableBytes { bytes in
                    context.render(decoded, toBitmap: bytes.baseAddress!, rowBytes: 4,
                        bounds: CGRect(x: Int(u * Double(photo.width)), y: Int(v * Double(photo.height)), width: 1, height: 1),
                        format: .RGBA8, colorSpace: CGColorSpace(name: CGColorSpace.sRGB))
                }
                for channel in 0..<3 {
                    XCTAssertEqual(Double(rgba[channel]), Double(colors[index][channel]), accuracy: 12,
                        "The production JPEG must preserve the asymmetric sensor quadrants at its actual UV positions.")
                }
            }
        }
    }

    @MainActor
    func testExplicitImageOptInDoesNotAcceptTraversalOrUnknownBundleFiles() throws {
        var files = Dictionary(uniqueKeysWithValues: ScannerOfflineArtifactSet.requiredNames.map { ($0, Data("fixture".utf8)) })
        files["appearance.provenance.json"] = Data("{}".utf8)
        files["textures/../outside.jpg"] = Data([1])
        XCTAssertThrowsError(try ScannerOfflineArtifactSet(files: files))
        files.removeValue(forKey: "textures/../outside.jpg")
        files["textures/\(UUID().uuidString.lowercased()).jpg"] = jpeg()
        XCTAssertNoThrow(try ScannerOfflineArtifactSet(files: files))
        XCTAssertFalse(ScannerAppearanceStore.validJPEG(Data("PNG pretending to be JPEG".utf8), width: 8, height: 6))
        XCTAssertFalse(ScannerAppearanceStore.validJPEG(jpeg(), width: 4096, height: 4096))
    }

    @MainActor
    func testNewUVLayerInvalidatesRendererEvenWhenMetricAndImageIDsAreUnchanged() async throws {
        let f = try await fixture()
        defer { try? FileManager.default.removeItem(at: f.root) }
        _ = try await f.runtime.checkpoint()
        try await f.store.commit(projectID: f.projectID, runtimeToken: f.token, lastDurableEventSequence: 12)
        let projected = try await f.store.project(snapshot: f.snapshot, projectID: f.projectID, maximumAcceptedEventSequence: 12)
        let layer = try XCTUnwrap(projected)
        let face = try XCTUnwrap(layer.faces.first)
        let revised = ScannerTexturedFace(faceIndex: face.faceIndex, assetID: face.assetID,
                                          uv: face.uv.map { [1 - $0[0], $0[1]] })
        let replacement = ScannerTexturedMesh(projectID: layer.projectID, sceneRevision: layer.sceneRevision,
            topologySHA256: layer.topologySHA256, faces: [revised], images: layer.images,
            obj: layer.obj, mtl: layer.mtl, manifest: layer.manifest)
        XCTAssertEqual(layer.images.keys.sorted(), replacement.images.keys.sorted())
        XCTAssertEqual(layer.faces.count, replacement.faces.count)
        XCTAssertNotEqual(layer.faces, replacement.faces)
        XCTAssertNotEqual(layer.presentationID, replacement.presentationID,
                          "The renderer cache must not retain old UVs at an unchanged metric revision.")
        let original = ScannerMeshSceneBuilder.scene(from: f.snapshot, texturedMesh: layer)
        let updated = ScannerMeshSceneBuilder.scene(from: f.snapshot, texturedMesh: replacement)
        let a = try XCTUnwrap(original.rootNode.childNodes.first?.geometry)
        let b = try XCTUnwrap(updated.rootNode.childNodes.first?.geometry)
        XCTAssertEqual(a.sources(for: .vertex).first?.data, b.sources(for: .vertex).first?.data)
        XCTAssertNotEqual(a.sources(for: .texcoord).first?.data, b.sources(for: .texcoord).first?.data)
    }

    @MainActor
    private func fixture() async throws -> (root: URL, projectID: UUID, token: UUID, runtime: HSSProjectRuntime,
                                            store: ScannerAppearanceStore, snapshot: ScannerSceneSnapshot) {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("hss-appearance-fixture-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: false)
        let projectID = UUID(), captureID = UUID(), token = UUID()
        let runtime = try HSSProjectRuntime(projectID: projectID, captureID: captureID, sourceIsSynthetic: true, trustedRoot: root, openMode: .create)
        let identity: [Double] = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
        try await runtime.setEpoch(CaptureEpochControl(sessionEpoch: 1, relationshipState: .unresolved, projectFromWorld: identity, transformRevision: 1), eventSequence: 1, monotonicTime: 0)
        try await runtime.setEpoch(CaptureEpochControl(sessionEpoch: 1, relationshipState: .accepted, projectFromWorld: identity, transformRevision: 2), eventSequence: 2, monotonicTime: 0)
        let source = frame(photo: ScannerAppearancePhoto(jpeg: jpeg(), width: 8, height: 6))
        try await runtime.submitFrame(source, eventSequence: 10, projectTime: 1)
        let revision = try await runtime.stats().scene_revision
        let store = ScannerAppearanceStore(trustedRoot: root)
        try await store.retain(frame: source, projectID: projectID, captureID: captureID, runtimeToken: token,
            eventSequence: 10, acceptedSceneRevision: revision, transformRevision: 2, projectTime: 1)
        let mesh = OwnedMeshAnchorEvent(kind: .add, anchorID: UUID(), anchorRevision: 1, generation: 1,
            sessionEpoch: 1, sequence: 12, monotonicTimestamp: 2, worldFromAnchor: identity.map(Float.init),
            payload: MeshAnchorPayload(vertices: [SIMD3(-0.375,0.125,-1), SIMD3(0.375,0.125,-1), SIMD3(0,-0.375,-1)],
                normals: [], triangleIndices: [0,1,2], classifications: [],
                localBoundsMin: SIMD3(-0.375,-0.375,-1), localBoundsMax: SIMD3(0.375,0.125,-1)))
        try await runtime.submitMeshEvent(mesh, eventSequence: 12, projectTime: 2)
        let snapshot = try await runtime.snapshot(provenance: [.syntheticTest], frozen: true)
        return (root,projectID,token,runtime,store,snapshot)
    }

    @MainActor
    private func jpeg() -> Data {
        let format = UIGraphicsImageRendererFormat(); format.scale = 1
        return UIGraphicsImageRenderer(size: CGSize(width: 8,height: 6), format: format).jpegData(withCompressionQuality: 0.95) { context in
            UIColor.orange.setFill(); context.fill(CGRect(x: 0,y: 0,width: 8,height: 6))
            UIColor.blue.setFill(); context.fill(CGRect(x: 0,y: 0,width: 3,height: 2))
        }
    }

    private func frame(photo: ScannerAppearancePhoto?) -> NormalizedSceneDepthFrame {
        let layout = PixelBufferLayout(width: 8,height: 6,pixelFormat: 0,planes: [PixelPlaneLayout(width: 8,height: 6,bytesPerRow: 32)])
        return NormalizedSceneDepthFrame(identity: SceneDepthFrameIdentity(sessionEpoch: 1,sequence: 1,monotonicTimestamp: 1),
            intrinsics: ScaledSceneDepthIntrinsics(fx: 4,fy: 4,cx: 3.5,cy: 2.5,calibrationImageWidth: 8,calibrationImageHeight: 6,
                depthCropX: 0,depthCropY: 0,depthCropWidth: 8,depthCropHeight: 6,depthWidth: 8,depthHeight: 6),
            worldFromCamera: [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],rgbLayout: layout,depthLayout: layout,confidenceLayout: nil,
            depthMeters: [Float](repeating: 1,count: 48),confidence: [UInt8](repeating: 2,count: 48),
            validDepthMask: [UInt8](repeating: 1,count: 48),metricFusionMask: [UInt8](repeating: 1,count: 48),
            provisionalPreviewMask: [UInt8](repeating: 0,count: 48),appearancePhoto: photo)
    }
}

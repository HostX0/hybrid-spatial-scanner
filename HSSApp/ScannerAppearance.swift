import ARKit
import CoreImage
import CoreVideo
import CryptoKit
import Darwin
import Foundation
import ImageIO
import simd

struct ScannerAppearancePhoto: Sendable {
    let jpeg: Data
    let width: Int
    let height: Int
}

/// Runs on the existing delegate path. Optional work is bounded, sampled by
/// pose/support novelty and suspendable independently of metric ingestion.
final class ScannerAppearancePhotoSampler: @unchecked Sendable {
    private let lock = NSLock()
    private let context = CIContext(options: [.cacheIntermediates: false])
    private let selector = SceneDepthKeyframeSelector(maximumRetainedFrames: 24)
    private var enabled = true
    private var lastTime: TimeInterval?

    func reset() { lock.lock(); defer { lock.unlock() }; selector.reset(); lastTime = nil }
    func setEnabled(_ value: Bool) { lock.lock(); defer { lock.unlock() }; enabled = value }

    func photo(arFrame: ARFrame, normalized: NormalizedSceneDepthFrame) -> ScannerAppearancePhoto? {
        lock.lock(); defer { lock.unlock() }
        guard enabled, !selector.hasReachedCapacity,
              lastTime.map({ arFrame.timestamp - $0 >= 0.8 }) ?? true,
              normalized.rgbLayout.width == normalized.intrinsics.calibrationImageWidth,
              normalized.rgbLayout.height == normalized.intrinsics.calibrationImageHeight,
              selector.shouldRetain(normalized) else { return nil }
        lastTime = arFrame.timestamp
        return Self.encodeSensorImage(arFrame.capturedImage, width: normalized.rgbLayout.width,
                                      height: normalized.rgbLayout.height, context: context)
    }

    /// The same bounded encoder is exercised by native raster tests. Sampling
    /// still happens under the delegate-path lock; only owned JPEG bytes escape.
    static func encodeSensorImage(_ buffer: CVPixelBuffer, width w: Int, height h: Int,
                                  context: CIContext) -> ScannerAppearancePhoto? {
        guard w > 0, h > 0, w <= 4096, h <= 4096, w.isMultiple(of: 2), h.isMultiple(of: 2),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        var scale: CGFloat = 1
        while CGFloat(max(w, h)) * scale > 960 { scale *= 0.5 }
        guard (CGFloat(w) * scale).rounded() == CGFloat(w) * scale,
              (CGFloat(h) * scale).rounded() == CGFloat(h) * scale else { return nil }
        let image = CIImage(cvPixelBuffer: buffer)
            .transformed(by: CGAffineTransform(scaleX: scale, y: scale))
            .settingProperties([:])
        // Sensor orientation is preserved. No borrowed image buffer escapes,
        // and no original EXIF/location metadata is copied into the JPEG.
        guard let bytes = context.jpegRepresentation(of: image, colorSpace: space),
              !bytes.isEmpty, bytes.count <= 4 * 1024 * 1024 else { return nil }
        return ScannerAppearancePhoto(jpeg: bytes, width: Int(CGFloat(w) * scale), height: Int(CGFloat(h) * scale))
    }
}

struct ScannerTexturedFace: Codable, Equatable, Sendable {
    let faceIndex: Int
    let assetID: String
    let uv: [[Double]]
}

struct ScannerTexturedMesh: Sendable {
    // One cheap presentation-generation identity per immutable derived layer.
    // Never persisted/exported and never used as a metric scene revision.
    let presentationID = UUID()
    let projectID: UUID
    let sceneRevision: UInt64
    let topologySHA256: String
    let faces: [ScannerTexturedFace]
    let images: [String: Data]
    let obj: Data
    let mtl: Data
    let manifest: Data

    func matches(_ snapshot: ScannerSceneSnapshot) -> Bool {
        guard sceneRevision == snapshot.revision, topologySHA256 == Self.fingerprint(snapshot),
              let document = try? JSONSerialization.jsonObject(with: snapshot.sceneJSON) as? [String: Any],
              document["project_id"] as? String == projectID.uuidString.lowercased() else { return false }
        return true
    }
    static func fingerprint(_ snapshot: ScannerSceneSnapshot) -> String {
        var hash = SHA256()
        let values = snapshot.verticesMetres.flatMap { [$0.x, $0.y, $0.z] }
        values.withUnsafeBufferPointer { hash.update(data: Data(buffer: $0)) }
        snapshot.triangleIndices.withUnsafeBufferPointer { hash.update(data: Data(buffer: $0)) }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
}

/// Appearance sidecar v1. Core source/replay schemas remain unchanged. Only
/// actually accepted source events enter this bounded, manifest-last local
/// store; a corrupt/missing sidecar is optional and never damages metric data.
actor ScannerAppearanceStore {
    private struct Record: Codable, Sendable {
        let assetID: String
        let captureID: UUID
        let epoch: UInt64
        let frameSequence: UInt64
        let sourceEventSequence: UInt64
        let acceptedSceneRevision: UInt64
        let transformRevision: UInt64
        let projectTime: TimeInterval
        let worldFromCamera: [Double]
        let depthK: [Double]
        let colorK: [Double]
        let depthWidth: Int
        let depthHeight: Int
        let colorWidth: Int
        let colorHeight: Int
        let jpegSHA256: String
        let depthSHA256: String
    }
    private struct Manifest: Codable { let version: Int; let projectID: UUID; var records: [Record] }
    private struct Pending: Sendable { let record: Record; let jpeg: Data; let depth: Data }
    private var pendingProjectID: UUID?
    private var pendingRuntimeToken: UUID?
    private var pending: [Pending] = []
    private struct Scene: Decodable { let project_id: UUID; let scene_revision: String; let source_is_synthetic: Bool; let epochs: [Epoch] }
    private struct Epoch: Decodable {
        let capture_id: UUID
        let session_epoch: UInt64
        let transform_revision: String
        let relationship_state: String
        let T_project_world: [Double]?
    }
    private let trustedRoot: URL
    init(trustedRoot: URL) { self.trustedRoot = trustedRoot }

    func discardPending(runtimeToken: UUID) {
        guard pendingRuntimeToken == runtimeToken else { return }
        pending.removeAll(); pendingRuntimeToken = nil; pendingProjectID = nil
    }

    func retain(frame: NormalizedSceneDepthFrame, projectID: UUID, captureID: UUID, runtimeToken: UUID,
                eventSequence: UInt64, acceptedSceneRevision: UInt64, transformRevision: UInt64,
                projectTime: TimeInterval) throws {
        guard let photo = frame.appearancePhoto, transformRevision > 0,
              eventSequence > 0, acceptedSceneRevision > 0,
              projectTime.isFinite, projectTime >= 0,
              frame.depthLayout.width > 0, frame.depthLayout.width <= 1024,
              frame.depthLayout.height > 0, frame.depthLayout.height <= 1024,
              frame.intrinsics.depthWidth == frame.depthLayout.width,
              frame.intrinsics.depthHeight == frame.depthLayout.height,
              frame.intrinsics.calibrationImageWidth > 0, frame.intrinsics.calibrationImageHeight > 0,
              frame.depthMeters.count == frame.depthLayout.width * frame.depthLayout.height,
              frame.metricFusionMask.count == frame.depthMeters.count,
              frame.depthMeters.count <= 1_048_576,
              frame.worldFromCamera.count == 16, frame.worldFromCamera.allSatisfy(\.isFinite),
              Self.validJPEG(photo.jpeg, width: photo.width, height: photo.height) else { return }
        if pendingProjectID != projectID || pendingRuntimeToken != runtimeToken {
            pending.removeAll(); pendingProjectID = projectID; pendingRuntimeToken = runtimeToken
        }
        let directory = try root(projectID, create: false)
        let manifest = try directory.map { try loadManifest($0, projectID: projectID) }
            ?? Manifest(version: 1, projectID: projectID, records: [])
        guard manifest.records.count + pending.count < 24,
              !pending.contains(where: { $0.record.captureID == captureID && $0.record.sourceEventSequence == eventSequence }),
              !manifest.records.contains(where: { $0.captureID == captureID && $0.sourceEventSequence == eventSequence }) else { return }
        let id = UUID().uuidString.lowercased()
        let visibilityDepth = frame.depthMeters.enumerated().map { frame.metricFusionMask[$0.offset] == 1 ? $0.element : 0 }
        let depth = visibilityDepth.withUnsafeBufferPointer { Data(buffer: $0) }
        let k = frame.intrinsics
        let record = Record(assetID: id, captureID: captureID, epoch: frame.identity.sessionEpoch,
                            frameSequence: frame.identity.sequence, sourceEventSequence: eventSequence,
                            acceptedSceneRevision: acceptedSceneRevision, transformRevision: transformRevision,
                            projectTime: projectTime, worldFromCamera: frame.worldFromCamera.map(Double.init),
                            depthK: [Double(k.fx), Double(k.fy), Double(k.cx), Double(k.cy)],
                            colorK: Self.colorIntrinsics(depth: k, imageWidth: photo.width, imageHeight: photo.height),
                            depthWidth: k.depthWidth, depthHeight: k.depthHeight,
                            colorWidth: photo.width, colorHeight: photo.height,
                            jpegSHA256: digest(photo.jpeg), depthSHA256: digest(depth))
        let reserved = pending.reduce(0) { $0 + $1.jpeg.count + $1.depth.count }
        guard reserved + photo.jpeg.count + depth.count <= 32 * 1024 * 1024 else { return }
        // No durable appearance evidence exists before the canonical source
        // checkpoint. A crash cannot rebind an orphan photo to a reused event.
        pending.append(Pending(record: record, jpeg: photo.jpeg, depth: depth))
    }

    /// Call only after the core source store reports a durable checkpoint.
    /// The cursor supplies the last durable sequence (not an event count;
    /// bounded capture intake can discard frames and leave sequence gaps).
    func commit(projectID: UUID, runtimeToken: UUID, lastDurableEventSequence: UInt64) throws {
        guard pendingProjectID == projectID, pendingRuntimeToken == runtimeToken else {
            pending.removeAll(); pendingProjectID = projectID; pendingRuntimeToken = runtimeToken
            return
        }
        guard !pending.isEmpty,
              let directory = try root(projectID, create: true) else { return }
        var manifest = try loadManifest(directory, projectID: projectID)
        let committed = pending.filter { $0.record.sourceEventSequence <= lastDurableEventSequence }
        guard !committed.isEmpty else { return }
        for item in committed {
            if manifest.records.contains(where: { $0.assetID == item.record.assetID }) { continue }
            guard manifest.records.count < 24 else { break }
            try publish(item.jpeg, at: directory.appendingPathComponent(item.record.assetID + ".jpg"))
            try publish(item.depth, at: directory.appendingPathComponent(item.record.assetID + ".depth"))
            manifest.records.append(item.record)
        }
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let bytes = try encoder.encode(manifest)
        guard bytes.count <= 64 * 1024 else { throw HSSBridgeError.invalidValue("Appearance manifest bound exceeded.") }
        try publish(bytes, at: directory.appendingPathComponent("manifest.json"))
        guard try loadManifest(directory, projectID: projectID).records.map(\.assetID) == manifest.records.map(\.assetID) else {
            throw HSSBridgeError.invalidValue("Appearance manifest readback failed.")
        }
        pending.removeAll { $0.record.sourceEventSequence <= lastDurableEventSequence }
    }

    /// Previous-manifest recovery can lower the durable cursor. Remove higher
    /// sidecar bindings before any event identity can be reused on resume.
    func reconcile(projectID: UUID, lastDurableEventSequence: UInt64) throws {
        pending.removeAll(); pendingRuntimeToken = nil; pendingProjectID = projectID
        guard let directory = try root(projectID, create: false) else { return }
        var manifest = try loadManifest(directory, projectID: projectID)
        let abandoned = manifest.records.filter { $0.sourceEventSequence > lastDurableEventSequence }
        guard !abandoned.isEmpty else { return }
        manifest.records.removeAll { $0.sourceEventSequence > lastDurableEventSequence }
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        try publish(encoder.encode(manifest), at: directory.appendingPathComponent("manifest.json"))
        for record in abandoned where UUID(uuidString: record.assetID)?.uuidString.lowercased() == record.assetID {
            for suffix in [".jpg", ".depth"] {
                let path = directory.appendingPathComponent(record.assetID + suffix)
                if let values = try? path.resourceValues(forKeys: [.isRegularFileKey,.isSymbolicLinkKey]),
                   values.isRegularFile == true, values.isSymbolicLink != true { try? FileManager.default.removeItem(at: path) }
            }
        }
    }

    func project(snapshot: ScannerSceneSnapshot, projectID: UUID, maximumAcceptedEventSequence: UInt64) throws -> ScannerTexturedMesh? {
        guard snapshot.isFrozen, snapshot.revision > 0, snapshot.sceneJSON.count <= 16 * 1024 * 1024,
              !snapshot.verticesMetres.isEmpty, !snapshot.triangleIndices.isEmpty else { return nil }
        let scene = try JSONDecoder().decode(Scene.self, from: snapshot.sceneJSON)
        guard scene.project_id == projectID, scene.scene_revision == String(snapshot.revision), scene.epochs.count <= 32 else { return nil }
        guard let directory = try root(projectID, create: false) else { return nil }
        let manifest = try loadManifest(directory, projectID: projectID)
        // One verified epoch cohort at a time. Cross-epoch camera visibility
        // is not inferred from an arbitrary old world or nearest transform.
        let candidates = manifest.records.filter { record in
            record.sourceEventSequence <= maximumAcceptedEventSequence && record.acceptedSceneRevision <= snapshot.revision &&
                scene.epochs.contains { epoch in
                    epoch.capture_id == record.captureID && epoch.session_epoch == record.epoch &&
                        epoch.relationship_state == "accepted" && epoch.transform_revision == String(record.transformRevision)
                }
        }
        guard let first = candidates.first,
              let epoch = scene.epochs.first(where: { $0.capture_id == first.captureID && $0.session_epoch == first.epoch }),
              let transform = epoch.T_project_world, let projectFromWorld = Self.matrix(transform) else { return nil }
        var packets: [[String: Any]] = []
        var images: [String: Data] = [:]
        for record in candidates where record.captureID == first.captureID && record.epoch == first.epoch {
            guard UUID(uuidString: record.assetID)?.uuidString.lowercased() == record.assetID,
                  record.depthK.count == 4, record.colorK.count == 4,
                  (record.depthK + record.colorK).allSatisfy(\.isFinite),
                  record.depthWidth > 0, record.depthHeight > 0,
                  record.depthWidth <= 1024, record.depthHeight <= 1024,
                  let worldFromCamera = Self.matrix(record.worldFromCamera) else { continue }
            let jpeg = try read(directory.appendingPathComponent(record.assetID + ".jpg"), maximum: 4 * 1024 * 1024)
            let depth = try read(directory.appendingPathComponent(record.assetID + ".depth"), maximum: 4 * 1024 * 1024)
            guard digest(jpeg) == record.jpegSHA256, digest(depth) == record.depthSHA256,
                  Self.validJPEG(jpeg, width: record.colorWidth, height: record.colorHeight),
                  depth.count == record.depthWidth * record.depthHeight * MemoryLayout<Float>.size else { continue }
            let pose = projectFromWorld * worldFromCamera
            let values = (0..<4).flatMap { c in (0..<4).map { r in pose[c,r] } }
            let poseData = values.withUnsafeBufferPointer { Data(buffer: $0) }
            images[record.assetID] = jpeg
            packets.append(["keyframeID": "frame-\(record.sourceEventSequence)", "assetID": record.assetID,
                            "sha256": record.jpegSHA256, "jpeg": jpeg, "depth": depth, "pose": poseData,
                            "colorWidth": record.colorWidth, "colorHeight": record.colorHeight,
                            "depthWidth": record.depthWidth, "depthHeight": record.depthHeight,
                            "fx": record.colorK[0], "fy": record.colorK[1], "cx": record.colorK[2], "cy": record.colorK[3],
                            "dfx": record.depthK[0], "dfy": record.depthK[1], "dcx": record.depthK[2], "dcy": record.depthK[3]])
        }
        guard !packets.isEmpty else { return nil }
        let xyz = snapshot.verticesMetres.flatMap { [$0.x, $0.y, $0.z] }
        let vertices = xyz.withUnsafeBufferPointer { Data(buffer: $0) }
        let indices = snapshot.triangleIndices.withUnsafeBufferPointer { Data(buffer: $0) }
        let output = try HSSAppearanceBridge.build(projectID: projectID.uuidString.lowercased(), sceneRevision: snapshot.revision,
                                                   sourceIsSynthetic: scene.source_is_synthetic,
                                                   transformRevision: first.transformRevision, verticesXYZ: vertices,
                                                   triangleIndices: indices, views: packets)
        let faces = try JSONDecoder().decode([ScannerTexturedFace].self, from: output.faceJSON)
        guard faces.count <= 16_384, Set(faces.map(\.faceIndex)).count == faces.count,
              faces.allSatisfy({ face in face.faceIndex >= 0 && face.faceIndex < snapshot.triangleIndices.count / 3 &&
                  images[face.assetID] != nil && face.uv.count == 3 && face.uv.allSatisfy { $0.count == 2 && $0.allSatisfy { $0.isFinite && (0...1).contains($0) } } }) else { return nil }
        let used = Set(output.usedAssetIDs)
        return ScannerTexturedMesh(projectID: projectID, sceneRevision: snapshot.revision,
                                   topologySHA256: ScannerTexturedMesh.fingerprint(snapshot), faces: faces,
                                   images: images.filter { used.contains($0.key) }, obj: output.objData,
                                   mtl: output.mtlData, manifest: output.manifestData)
    }

    private static func matrix(_ values: [Double]) -> simd_double4x4? {
        guard values.count == 16, values.allSatisfy(\.isFinite) else { return nil }
        return simd_double4x4(columns: (SIMD4(values[0],values[1],values[2],values[3]), SIMD4(values[4],values[5],values[6],values[7]),
                                      SIMD4(values[8],values[9],values[10],values[11]), SIMD4(values[12],values[13],values[14],values[15])))
    }
    private func root(_ projectID: UUID, create: Bool) throws -> URL? {
        let project = trustedRoot.appendingPathComponent(projectID.hssDirectoryName, isDirectory: true)
        guard try project.resourceValues(forKeys: [.isDirectoryKey,.isSymbolicLinkKey]).isDirectory == true,
              try project.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw HSSBridgeError.invalidValue("Trusted appearance project directory unavailable.")
        }
        let directory = project.appendingPathComponent("appearance", isDirectory: true)
        if !create && !FileManager.default.fileExists(atPath: directory.path) { return nil }
        if create && !FileManager.default.fileExists(atPath: directory.path) {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false,
                attributes: [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication])
        }
        guard try directory.resourceValues(forKeys: [.isDirectoryKey,.isSymbolicLinkKey]).isDirectory == true,
              try directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw HSSBridgeError.invalidValue("Trusted appearance sidecar directory unavailable.")
        }
        return directory
    }
    private func loadManifest(_ directory: URL, projectID: UUID) throws -> Manifest {
        let path = directory.appendingPathComponent("manifest.json")
        guard FileManager.default.fileExists(atPath: path.path) else { return Manifest(version: 1, projectID: projectID, records: []) }
        let manifest = try JSONDecoder().decode(Manifest.self, from: read(path, maximum: 64 * 1024))
        guard manifest.version == 1, manifest.projectID == projectID, manifest.records.count <= 24,
              Set(manifest.records.map(\.assetID)).count == manifest.records.count else {
            throw HSSBridgeError.invalidValue("Unsupported or malformed appearance sidecar.")
        }
        return manifest
    }
    private func read(_ path: URL, maximum: Int) throws -> Data {
        let values = try path.resourceValues(forKeys: [.isRegularFileKey,.isSymbolicLinkKey,.fileSizeKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true,
              let bytes = values.fileSize, bytes > 0, bytes <= maximum else {
            throw HSSBridgeError.invalidValue("Appearance asset exceeds its safe file boundary.")
        }
        let data = try Data(contentsOf: path)
        guard data.count == bytes else { throw HSSBridgeError.invalidValue("Appearance asset changed during read.") }
        return data
    }
    private func publish(_ bytes: Data, at path: URL) throws {
        if FileManager.default.fileExists(atPath: path.path) {
            if try path.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true {
                throw HSSBridgeError.invalidValue("Appearance destination link refused.")
            }
        }
        try bytes.write(to: path, options: [.atomic,.completeFileProtectionUntilFirstUserAuthentication])
        let file = try FileHandle(forWritingTo: path); try file.synchronize(); try file.close()
        let directory = open(path.deletingLastPathComponent().path, O_RDONLY)
        guard directory >= 0 else { throw HSSBridgeError.invalidValue("Appearance directory flush unavailable.") }
        defer { close(directory) }
        guard fsync(directory) == 0 else { throw HSSBridgeError.invalidValue("Appearance directory flush failed.") }
    }
    static func validJPEG(_ bytes: Data, width: Int, height: Int) -> Bool {
        guard let size = jpegDimensions(bytes) else { return false }
        return size.width == width && size.height == height
    }
    /// Pixel centers are indexed from zero; CoreImage's scale acts on pixel
    /// boundaries. Preserve their offset instead of moving the principal point
    /// by half an output pixel. This changes optional UVs, never depth K.
    static func colorIntrinsics(depth k: ScaledSceneDepthIntrinsics, imageWidth: Int, imageHeight: Int) -> [Double] {
        let x = Double(imageWidth) / Double(k.calibrationImageWidth)
        let y = Double(imageHeight) / Double(k.calibrationImageHeight)
        let cropX = Double(k.depthCropWidth) / Double(k.depthWidth)
        let cropY = Double(k.depthCropHeight) / Double(k.depthHeight)
        return [Double(k.fx) * cropX * x, Double(k.fy) * cropY * y,
                (Double(k.cx) * cropX + Double(k.depthCropX) + 0.5) * x - 0.5,
                (Double(k.cy) * cropY + Double(k.depthCropY) + 0.5) * y - 0.5]
    }
    static func jpegDimensions(_ bytes: Data) -> (width: Int, height: Int)? {
        guard !bytes.isEmpty, bytes.count <= 4 * 1024 * 1024,
              let source = CGImageSourceCreateWithData(bytes as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              CGImageSourceGetCount(source) == 1,
              CGImageSourceGetType(source) as String? == "public.jpeg",
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
              let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
              width > 0, height > 0, width <= 960, height <= 960,
              ((properties[kCGImagePropertyOrientation] as? NSNumber)?.intValue ?? 1) == 1,
              properties[kCGImagePropertyGPSDictionary] == nil else { return nil }
        return (width, height)
    }
    private func digest(_ bytes: Data) -> String { SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined() }
}

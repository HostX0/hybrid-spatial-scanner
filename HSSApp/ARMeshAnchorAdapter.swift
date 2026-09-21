#if canImport(ARKit) && canImport(Metal)
@preconcurrency import ARKit
import Foundation
import Metal
import simd

/// Copies ARMeshAnchor buffers while the delegate owns them. The output has no
/// ARKit or Metal references and is therefore safe to hand to the lifecycle
/// store/core after this method returns.
enum ARMeshAnchorLifecycleAdapter {
    static func makeInput(
        anchor: ARMeshAnchor,
        kind: MeshAnchorEventKind,
        sessionEpoch: UInt64,
        sequence: UInt64,
        monotonicTimestamp: TimeInterval
    ) -> MeshAnchorEventInput {
        let transform = columnMajorArray(anchor.transform)
        guard kind != .remove else {
            return MeshAnchorEventInput(kind: kind, anchorID: anchor.identifier, sessionEpoch: sessionEpoch,
                                        sequence: sequence, monotonicTimestamp: monotonicTimestamp,
                                        worldFromAnchor: transform, payload: nil)
        }
        let payload = copiedPayload(from: anchor.geometry) ?? invalidPayload()
        return MeshAnchorEventInput(kind: kind, anchorID: anchor.identifier, sessionEpoch: sessionEpoch,
                                    sequence: sequence, monotonicTimestamp: monotonicTimestamp,
                                    worldFromAnchor: transform, payload: payload)
    }

    private static func copiedPayload(from geometry: ARMeshGeometry) -> MeshAnchorPayload? {
        guard geometry.vertices.format == .float3, geometry.vertices.componentsPerVector == 3,
              geometry.normals.format == .float3, geometry.normals.componentsPerVector == 3,
              geometry.vertices.count > 0, geometry.vertices.stride >= MemoryLayout<SIMD3<Float>>.stride,
              geometry.normals.count == geometry.vertices.count,
              geometry.normals.stride >= MemoryLayout<SIMD3<Float>>.stride,
              geometry.faces.indexCountPerPrimitive == 3,
              geometry.faces.bytesPerIndex == MemoryLayout<UInt16>.stride || geometry.faces.bytesPerIndex == MemoryLayout<UInt32>.stride
        else { return nil }

        let vertices = copyFloat3Source(geometry.vertices)
        let normals = copyFloat3Source(geometry.normals)
        let triangleCount = geometry.faces.count
        guard vertices.count == geometry.vertices.count, normals.count == vertices.count,
              let indices = copyTriangleIndices(geometry.faces, triangleCount: triangleCount),
              let classifications = copyClassifications(geometry.classification, triangleCount: triangleCount)
        else { return nil }
        guard let first = vertices.first else { return nil }
        let bounds = vertices.dropFirst().reduce((first, first)) { current, point in
            (simd_min(current.0, point), simd_max(current.1, point))
        }
        return MeshAnchorPayload(vertices: vertices, normals: normals, triangleIndices: indices,
                                 classifications: classifications, localBoundsMin: bounds.0, localBoundsMax: bounds.1)
    }

    private static func copyFloat3Source(_ source: ARGeometrySource) -> [SIMD3<Float>] {
        (0..<source.count).map { index in
            let address = source.buffer.contents().advanced(by: source.offset + index * source.stride)
            var result = SIMD3<Float>(repeating: 0)
            withUnsafeMutableBytes(of: &result) { destination in
                destination.copyMemory(from: UnsafeRawBufferPointer(start: address, count: MemoryLayout<SIMD3<Float>>.stride))
            }
            return result
        }
    }

    private static func copyTriangleIndices(_ faces: ARGeometryElement, triangleCount: Int) -> [UInt32]? {
        let count = triangleCount * 3
        guard triangleCount >= 0, count / 3 == triangleCount else { return nil }
        return (0..<count).map { index in
            let address = faces.buffer.contents().advanced(by: index * faces.bytesPerIndex)
            if faces.bytesPerIndex == MemoryLayout<UInt16>.stride {
                var value: UInt16 = 0
                withUnsafeMutableBytes(of: &value) { $0.copyMemory(from: UnsafeRawBufferPointer(start: address, count: 2)) }
                return UInt32(value)
            }
            var value: UInt32 = 0
            withUnsafeMutableBytes(of: &value) { $0.copyMemory(from: UnsafeRawBufferPointer(start: address, count: 4)) }
            return value
        }
    }

    private static func copyClassifications(_ source: ARGeometrySource?, triangleCount: Int) -> [UInt8]? {
        guard let source else { return [] }
        guard source.count == triangleCount, source.componentsPerVector == 1, source.stride >= 1 else { return nil }
        return (0..<triangleCount).map { index in
            source.buffer.contents().advanced(by: source.offset + index * source.stride).load(as: UInt8.self)
        }
    }

    private static func columnMajorArray(_ matrix: simd_float4x4) -> [Float] {
        [matrix.columns.0.x, matrix.columns.0.y, matrix.columns.0.z, matrix.columns.0.w,
         matrix.columns.1.x, matrix.columns.1.y, matrix.columns.1.z, matrix.columns.1.w,
         matrix.columns.2.x, matrix.columns.2.y, matrix.columns.2.z, matrix.columns.2.w,
         matrix.columns.3.x, matrix.columns.3.y, matrix.columns.3.z, matrix.columns.3.w]
    }

    private static func invalidPayload() -> MeshAnchorPayload {
        MeshAnchorPayload(vertices: [], normals: [], triangleIndices: [], classifications: [],
                          localBoundsMin: .zero, localBoundsMax: .zero)
    }
}
#endif

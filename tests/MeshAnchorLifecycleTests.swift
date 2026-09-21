import XCTest
@testable import HybridSpatialScanner

final class MeshAnchorLifecycleTests: XCTestCase {
    func testAddUpdateRemoveReplacesWholePayloadAndClearsLiveAnchor() {
        let store = MeshAnchorLifecycleStore(maximumVertexCount: 16, maximumTriangleCount: 16)
        let id = UUID()

        let added = acceptedRevision(store.apply(event(.add, id: id, sequence: 1, payload: triangle(x: 0))))
        XCTAssertEqual(added.revision, 1)
        XCTAssertEqual(added.generation, 1)
        XCTAssertEqual(added.payload.vertices.count, 3)

        let updated = acceptedRevision(store.apply(event(.update, id: id, sequence: 2, payload: triangle(x: 10))))
        XCTAssertEqual(updated.revision, 2)
        XCTAssertEqual(updated.generation, 1)
        XCTAssertEqual(updated.payload.vertices.map(\.x), [10, 11, 10], "Update must replace rather than append or spatially merge payloads.")

        XCTAssertNil(acceptedRevisionOrNil(store.apply(event(.remove, id: id, sequence: 3))))
        XCTAssertNil(store.liveRevision(for: id))
        assertRejected(.removeWithoutLiveAnchor, store.apply(event(.remove, id: id, sequence: 4)))
    }

    func testReaddedIdentityGetsNewGenerationAndDoesNotRetainPriorPayload() {
        let store = MeshAnchorLifecycleStore(maximumVertexCount: 16, maximumTriangleCount: 16)
        let id = UUID()
        _ = acceptedRevision(store.apply(event(.add, id: id, sequence: 1, payload: triangle(x: 0))))
        _ = acceptedRevisionOrNil(store.apply(event(.remove, id: id, sequence: 2)))
        let readded = acceptedRevision(store.apply(event(.add, id: id, sequence: 3, payload: triangle(x: 20))))

        XCTAssertEqual(readded.revision, 1)
        XCTAssertEqual(readded.generation, 2)
        XCTAssertEqual(readded.payload.vertices.map(\.x), [20, 21, 20])
    }

    func testEpochSequenceAndGeometryValidationRejectWithoutReplacingLiveRevision() {
        let store = MeshAnchorLifecycleStore(maximumVertexCount: 3, maximumTriangleCount: 1)
        let id = UUID()
        let accepted = acceptedRevision(store.apply(event(.add, id: id, epoch: 9, sequence: 7, payload: triangle(x: 0))))

        assertRejected(.nonMonotonicSequence, store.apply(event(.update, id: id, epoch: 9, sequence: 7, payload: triangle(x: 5))))
        var malformed = triangle(x: 4)
        malformed = MeshAnchorPayload(vertices: malformed.vertices, normals: malformed.normals,
                                      triangleIndices: [0, 1, 9], classifications: malformed.classifications,
                                      localBoundsMin: malformed.localBoundsMin, localBoundsMax: malformed.localBoundsMax)
        assertRejected(.invalidIndices, store.apply(event(.update, id: id, epoch: 10, sequence: 1, payload: malformed)))
        assertRejected(.invalidTransform, store.apply(event(.update, id: id, epoch: 10, sequence: 2,
                                                           transform: Array(repeating: 0, count: 16), payload: triangle(x: 6))))

        XCTAssertEqual(store.liveRevision(for: id), accepted)
        let counters = store.countersSnapshot()
        XCTAssertEqual(counters.acceptedAdds, 1)
        XCTAssertEqual(counters.acceptedUpdates, 0)
        XCTAssertEqual(counters.rejectedEvents, 3)
        XCTAssertEqual(counters.rejectionCount(for: .invalidIndices), 1)
    }

    func testOwnedCopySurvivesSyntheticSourceMutation() {
        let store = MeshAnchorLifecycleStore(maximumVertexCount: 16, maximumTriangleCount: 16)
        let id = UUID()
        var inputPayload = triangle(x: 0)
        let revision = acceptedRevision(store.apply(event(.add, id: id, sequence: 1, payload: inputPayload)))
        inputPayload.vertices[0].x = 99

        XCTAssertEqual(revision.payload.vertices[0].x, 0)
        XCTAssertEqual(store.liveRevision(for: id)?.payload.vertices[0].x, 0)
    }

    private func event(
        _ kind: MeshAnchorEventKind,
        id: UUID,
        epoch: UInt64 = 4,
        sequence: UInt64,
        transform: [Float] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
        payload: MeshAnchorPayload? = nil
    ) -> MeshAnchorEventInput {
        MeshAnchorEventInput(kind: kind, anchorID: id, sessionEpoch: epoch, sequence: sequence,
                              monotonicTimestamp: 2.5, worldFromAnchor: transform, payload: payload)
    }

    private func triangle(x: Float) -> MeshAnchorPayload {
        MeshAnchorPayload(
            vertices: [SIMD3(x, 0, 0), SIMD3(x + 1, 0, 0), SIMD3(x, 1, 0)],
            normals: [SIMD3(0, 0, 1), SIMD3(0, 0, 1), SIMD3(0, 0, 1)],
            triangleIndices: [0, 1, 2], classifications: [2],
            localBoundsMin: SIMD3(x, 0, 0), localBoundsMax: SIMD3(x + 1, 1, 0)
        )
    }

    private func acceptedRevision(_ result: MeshAnchorLifecycleResult, file: StaticString = #filePath, line: UInt = #line) -> OwnedMeshAnchorRevision {
        guard case .accepted(let revision?, _) = result else {
            XCTFail("Expected an accepted live revision", file: file, line: line)
            fatalError("unreachable")
        }
        return revision
    }

    private func acceptedRevisionOrNil(_ result: MeshAnchorLifecycleResult, file: StaticString = #filePath, line: UInt = #line) -> OwnedMeshAnchorRevision? {
        guard case .accepted(let revision, _) = result else {
            XCTFail("Expected accepted result", file: file, line: line)
            return nil
        }
        return revision
    }

    private func assertRejected(_ reason: MeshAnchorLifecycleRejection, _ result: MeshAnchorLifecycleResult, file: StaticString = #filePath, line: UInt = #line) {
        guard case .rejected(let actual, _) = result else { return XCTFail("Expected rejection", file: file, line: line) }
        XCTAssertEqual(actual, reason, file: file, line: line)
    }
}

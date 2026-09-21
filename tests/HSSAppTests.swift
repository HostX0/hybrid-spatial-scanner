import XCTest
@testable import HybridSpatialScanner

final class HSSAppTests: XCTestCase {
    func testFoundationShellLoads() {
        XCTAssertNotNil(ReadinessView())
    }
}

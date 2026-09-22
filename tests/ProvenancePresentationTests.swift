import XCTest
@testable import HybridSpatialScanner

final class ProvenancePresentationTests: XCTestCase {
    func testEveryEvidenceKindHasEnglishAndArabicCopy() {
        for kind in ProvenanceKind.allCases {
            XCTAssertFalse(kind.title.value(for: .english).isEmpty)
            XCTAssertFalse(kind.title.value(for: .arabic).isEmpty)
            XCTAssertFalse(kind.explanation.value(for: .english).isEmpty)
            XCTAssertFalse(kind.explanation.value(for: .arabic).isEmpty)
        }
    }

    func testDefaultAccuracyDisclosureIsNotMeasured() {
        let field = ProvenanceFieldPresentation(
            label: ProvenanceCopy(english: "Wall length", arabic: "طول الجدار"),
            formattedValue: ProvenanceCopy(english: "4.20 m", arabic: "٤٫٢٠ م"),
            kind: .fitted
        )

        XCTAssertEqual(field.accuracy, .notMeasured)
    }

    func testUserMeasurementRequiresSourceAndUncertaintyDisclosures() {
        let incomplete = ProvenanceFieldPresentation(
            label: ProvenanceCopy(english: "Door width", arabic: "عرض الباب"),
            kind: .userMeasured
        )
        let complete = ProvenanceFieldPresentation(
            label: ProvenanceCopy(english: "Door width", arabic: "عرض الباب"),
            kind: .userMeasured,
            sourceDescription: ProvenanceCopy(english: "Laser meter", arabic: "مقياس ليزر"),
            uncertainty: .stated(ProvenanceCopy(english: "±2 mm", arabic: "±٢ مم"))
        )

        XCTAssertTrue(incomplete.needsUserMeasurementDisclosure)
        XCTAssertFalse(complete.needsUserMeasurementDisclosure)
    }
}

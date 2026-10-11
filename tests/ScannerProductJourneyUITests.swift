import XCTest

final class ScannerProductJourneyUITests: XCTestCase {
    @MainActor
    func testEnglishLibraryAccessibilityAudit() throws {
        try auditLibraries(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicLibraryAccessibilityAudit() throws {
        try auditLibraries(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func auditLibraries(language: String, locale: String) throws {
        let app = launch(language: language, locale: locale)
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 10))
        attach(app, name: language + " project library accessibility")
        // Do not suppress reported issues: these are real current-screen audits,
        // not a blanket VoiceOver or physical capture acceptance.
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
        app.buttons["privacy.open"].tap()
        XCTAssertTrue(app.buttons["privacy.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " privacy disclosure accessibility")
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
        app.buttons["privacy.done"].tap()
        openObjectLibrary(app)
        attach(app, name: language + " object library accessibility")
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
    }

    @MainActor
    func testEnglishObjectLibraryAndCaptureIntroductionReturnJourney() {
        let app = launch(language: "en", locale: "en_US")
        openObjectLibrary(app)
        app.buttons["object-library.new-capture"].tap()
        let unsupported = app.staticTexts["Detailed object capture unavailable"]
        let start = app.buttons["Start Object Capture"]
        XCTAssertTrue(unsupported.waitForExistence(timeout: 5) || start.exists)
        app.buttons["object-capture.close"].tap()
        XCTAssertTrue(app.buttons["object-library.done"].waitForExistence(timeout: 5))
        attach(app, name: "English object library")
        app.buttons["object-library.done"].tap()
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 5))
        app.terminate()
    }

    @MainActor
    func testArabicObjectLibraryRoundTrip() {
        let app = launch(language: "ar", locale: "ar_IQ")
        openObjectLibrary(app)
        XCTAssertTrue(app.navigationBars["مكتبة المجسمات"].exists)
        XCTAssertTrue(app.buttons["object-library.new-capture"].isHittable)
        attach(app, name: "Arabic RTL object library")
        app.buttons["object-library.done"].tap()
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 5))
        app.terminate()
    }

    @MainActor
    func testLandscapeObjectLibraryControlsRemainReachable() {
        let app = launch(language: "en", locale: "en_US")
        XCUIDevice.shared.orientation = .landscapeLeft
        defer { XCUIDevice.shared.orientation = .portrait; app.terminate() }
        openObjectLibrary(app)
        XCTAssertGreaterThan(app.frame.width, app.frame.height)
        XCTAssertTrue(app.buttons["object-library.new-capture"].isHittable)
        XCTAssertTrue(app.buttons["object-library.done"].isHittable)
        attach(app, name: "Landscape object library")
        app.buttons["object-library.done"].tap()
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 5))
    }

    @MainActor
    func testEnglishBoundaryCorrectionSplitUndoRedoJourney() throws {
        try boundaryCorrectionJourney(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicBoundaryCorrectionSplitUndoRedoJourney() throws {
        try boundaryCorrectionJourney(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func boundaryCorrectionJourney(language: String, locale: String) throws {
        let app = launch(language: language, locale: locale, referenceRoom: true)
        defer { app.terminate() }
        let project = app.buttons["project.open.01000000-0000-5000-8000-000000000000"]
        // Debug-only setup checkpoints an explicitly synthetic room using the
        // real engine/store after XCTest's fresh-container installation.
        XCTAssertTrue(project.waitForExistence(timeout: 10))
        reveal(project, in: app)
        project.tap()
        let review = app.buttons["boundary-review.open"]
        XCTAssertTrue(review.waitForExistence(timeout: 10))
        reveal(review, in: app)
        review.tap()
        XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " captured boundary selection")
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
        let edges = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge."))
        XCTAssertEqual(edges.count, 8)
        let firstEdge = edges.element(boundBy: 0)
        reveal(firstEdge, in: app)
        firstEdge.tap()
        let split = app.buttons["boundary-review.split"]
        reveal(split, in: app)
        XCTAssertTrue(split.isEnabled)
        attach(app, name: language + " boundary refinement controls")
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
        split.tap()
        XCTAssertTrue(app.buttons["boundary-review.undo"].waitForExistence(timeout: 5))
        XCTAssertEqual(edges.count, 9)
        let undo = app.buttons["boundary-review.undo"]
        reveal(undo, in: app)
        undo.tap()
        XCTAssertTrue(app.buttons["boundary-review.redo"].waitForExistence(timeout: 5))
        XCTAssertEqual(edges.count, 8)
        let redo = app.buttons["boundary-review.redo"]
        reveal(redo, in: app)
        redo.tap()
        XCTAssertTrue(app.buttons["boundary-review.undo"].waitForExistence(timeout: 5))
        XCTAssertEqual(edges.count, 9)
        attach(app, name: language + " accepted split and correction history")
        // Restore the reference shape before background autosave or the next
        // journey; original journal records still remain append-only.
        reveal(undo, in: app)
        undo.tap()
        XCTAssertTrue(app.buttons["boundary-review.redo"].waitForExistence(timeout: 5))
        XCTAssertEqual(edges.count, 8)
        app.buttons["boundary-review.done"].tap()
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for _ in 0..<14 {
            if element.isHittable { return }
            if element.exists && element.frame.midY < app.frame.minY + 120 { app.swipeDown() }
            else { app.swipeUp() }
        }
        XCTAssertTrue(element.isHittable)
    }

    @MainActor
    private func launch(language: String, locale: String, referenceRoom: Bool = false) -> XCUIApplication {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(\(language))", "-AppleLocale", locale]
        if referenceRoom { app.launchEnvironment["HSS_UI_REFERENCE_ROOM"] = "1" }
        app.launch()
        return app
    }

    @MainActor
    private func openObjectLibrary(_ app: XCUIApplication) {
        let button = app.buttons["object-library.open"]
        XCTAssertTrue(button.waitForExistence(timeout: 10))
        if !button.isHittable { app.swipeUp() }
        button.tap()
        XCTAssertTrue(app.buttons["object-library.done"].waitForExistence(timeout: 10))
    }

    @MainActor
    private func attach(_ app: XCUIApplication, name: String) {
        let attachment = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}

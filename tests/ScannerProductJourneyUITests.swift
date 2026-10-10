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
    private func launch(language: String, locale: String) -> XCUIApplication {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(\(language))", "-AppleLocale", locale]
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

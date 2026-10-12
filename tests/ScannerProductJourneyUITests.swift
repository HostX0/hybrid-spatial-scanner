import XCTest

/// Optional artifact workflow, not a synthetic capture or an accuracy gate.
/// Run explicitly with Release and a preserved, owned reference project already
/// installed. The shipping app has no debug seeding enabled. Nothing is uploaded.
final class ScannerReleaseStoreScreenshotUITests: XCTestCase {
    @MainActor
    func testEnglishReleaseStoreScreenshots() throws {
        try screenshots(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicReleaseStoreScreenshots() throws {
        try screenshots(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func screenshots(language: String, locale: String) throws {
        #if DEBUG
        throw XCTSkip("Store artifacts must use the Release application, not debug setup.")
        #else
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(\(language))", "-AppleLocale", locale,
            "-UIPreferredContentSizeCategoryName", "UICTContentSizeCategoryL"]
        app.launch()
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["privacy.open"].waitForExistence(timeout: 10))
        let project = app.buttons["project.open.01000000-0000-5000-8000-000000000000"]
        reveal(project, in: app)
        XCTAssertTrue(project.exists, "Stage only the owned, explicitly synthetic example before this workflow.")

        // Rename through the real, persisted product action. Provenance remains
        // synthetic, and all public frames must disclose the reference example.
        let actionsPrefix = language == "ar" ? "إجراءات المشروع:" : "Project actions:"
        let actions = app.buttons.matching(NSPredicate(format: "label BEGINSWITH %@", actionsPrefix)).firstMatch
        reveal(actions, in: app)
        actions.tap()
        app.buttons[language == "ar" ? "إعادة تسمية" : "Rename"].tap()
        let field = app.alerts.textFields.firstMatch
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        field.tap()
        // The default rename text is replaced via ordinary editing keys.
        let existing = field.value as? String ?? ""
        field.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: existing.count))
        field.typeText(language == "ar" ? "مثال مرجعي" : "Example room")
        app.alerts.buttons[language == "ar" ? "حفظ" : "Save"].tap()
        XCTAssertTrue(project.waitForExistence(timeout: 5))
        app.swipeDown()
        attach(app, name: language + "-01-library")

        reveal(project, in: app)
        project.tap()
        let correction = app.buttons["boundary-review.open"]
        XCTAssertTrue(correction.waitForExistence(timeout: 10))
        reveal(correction, in: app)
        // This viewport includes the actual plan and correction entry point.
        // Public frames must also carry the explicit reference disclosure.
        attach(app, name: language + "-02-review")
        correction.tap()
        XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
        let edge = app.collectionViews.firstMatch.buttons.matching(
            NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge.")).firstMatch
        XCTAssertTrue(edge.waitForExistence(timeout: 5))
        edge.tap()
        attach(app, name: language + "-03-boundaries")
        app.buttons["boundary-review.done"].tap()
        let library = app.buttons[language == "ar" ? "فتح المشاريع" : "Open projects"]
        reveal(library, in: app)
        library.tap()
        XCTAssertTrue(app.buttons["privacy.open"].waitForExistence(timeout: 5))
        app.buttons["privacy.open"].tap()
        XCTAssertTrue(app.buttons["privacy.done"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["privacy.heading"].exists)
        attach(app, name: language + "-04-privacy")
        app.buttons["privacy.done"].tap()
        app.buttons["support.open"].tap()
        XCTAssertTrue(app.buttons["support.done"].waitForExistence(timeout: 5))
        attach(app, name: language + "-05-help")
        #endif
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for _ in 0..<12 {
            if element.exists && element.isHittable { return }
            let down = element.exists && element.frame.midY < app.frame.minY + 120
            if down { app.swipeDown() } else { app.swipeUp() }
        }
        XCTAssertTrue(element.isHittable)
    }

    @MainActor
    private func attach(_ app: XCUIApplication, name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }
}

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
    func testEnglishPrivacyAndOfflineSupportRoundTrip() {
        privacyAndSupportJourney(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicPrivacyAndOfflineSupportRoundTrip() {
        privacyAndSupportJourney(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    func testEnglishLargestTextPrivacyHeadingAndDismissal() {
        largestTextPrivacy(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicLargestTextPrivacyHeadingAndDismissal() {
        largestTextPrivacy(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func largestTextPrivacy(language: String, locale: String) {
        let app = launch(language: language, locale: locale,
            contentSize: "UICTContentSizeCategoryAccessibilityXXXL")
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["privacy.open"].waitForExistence(timeout: 10))
        app.buttons["privacy.open"].tap()
        XCTAssertTrue(app.buttons["privacy.done"].waitForExistence(timeout: 5))
        let heading = app.staticTexts["privacy.heading"]
        XCTAssertTrue(heading.exists)
        XCTAssertGreaterThan(heading.frame.height, 45)
        XCTAssertLessThanOrEqual(heading.frame.width, app.frame.width)
        XCTAssertTrue(app.buttons["privacy.done"].isHittable)
        attach(app, name: language + " largest-text multiline privacy heading")
        app.buttons["privacy.done"].tap()
        XCTAssertTrue(app.buttons["support.open"].waitForExistence(timeout: 5))
    }

    @MainActor
    private func privacyAndSupportJourney(language: String, locale: String) {
        let app = launch(language: language, locale: locale)
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["privacy.open"].waitForExistence(timeout: 10))
        app.buttons["privacy.open"].tap()
        XCTAssertTrue(app.buttons["privacy.done"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["privacy.heading"].exists)
        attach(app, name: language + " offline privacy disclosure")
        app.buttons["privacy.done"].tap()
        app.buttons["support.open"].tap()
        XCTAssertTrue(app.buttons["support.done"].waitForExistence(timeout: 5))
        let email = app.descendants(matching: .any).matching(identifier: "support.email").firstMatch
        reveal(email, in: app)
        XCTAssertTrue(email.exists)
        let phone = app.descendants(matching: .any).matching(identifier: "support.phone").firstMatch
        reveal(phone, in: app)
        XCTAssertTrue(phone.exists)
        attach(app, name: language + " approved support destinations")
        // Inspect reachability only. No call, email, browser or capture upload.
        app.buttons["support.done"].tap()
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 5))
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
        app.buttons["support.open"].tap()
        XCTAssertTrue(app.buttons["support.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " offline help accessibility")
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType])
        let email = app.descendants(matching: .any).matching(identifier: "support.email").firstMatch
        reveal(email, in: app)
        let phone = app.descendants(matching: .any).matching(identifier: "support.phone").firstMatch
        reveal(phone, in: app)
        XCTAssertTrue(email.exists)
        XCTAssertTrue(phone.exists)
        // Presence only: do not send an email, place a call or upload a capture.
        attach(app, name: language + " approved publisher support")
        app.buttons["support.done"].tap()
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

    // Keep accessibility failures visible in independent gates. They must not
    // prevent the separate interaction tests from exercising split/undo/redo.
    @MainActor
    func testEnglishBoundaryCorrectionAccessibilityAudit() throws {
        try boundaryCorrectionJourney(language: "en", locale: "en_US", audits: true)
    }

    @MainActor
    func testArabicBoundaryCorrectionAccessibilityAudit() throws {
        try boundaryCorrectionJourney(language: "ar", locale: "ar_IQ", audits: true)
    }

    @MainActor
    func testEnglishLargestTextLibraryAndBoundaryControls() {
        largestTextJourney(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicLargestTextLibraryAndBoundaryControls() {
        largestTextJourney(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func largestTextJourney(language: String, locale: String) {
        let app = launch(language: language, locale: locale, referenceRoom: true,
            contentSize: "UICTContentSizeCategoryAccessibilityXXXL")
        defer { app.terminate() }
        let project = app.buttons["project.open.01000000-0000-5000-8000-000000000000"]
        // Largest text pushes the lazy project row below the initial viewport.
        reveal(project, in: app)
        XCTAssertTrue(project.waitForExistence(timeout: 10))
        let hierarchy = XCTAttachment(string: app.debugDescription)
        hierarchy.name = language + " largest-text library accessibility hierarchy"
        hierarchy.lifetime = .keepAlways
        add(hierarchy)
        attach(app, name: language + " largest-text project library")
        let name = app.staticTexts["Reference room"].firstMatch
        XCTAssertTrue(name.exists)
        // At the normal category this headline is about 20 points high. Check
        // actual rendered growth, not merely a semantic font declaration.
        XCTAssertGreaterThan(name.frame.height, 35)
        project.tap()
        let review = app.buttons["boundary-review.open"]
        reveal(review, in: app)
        review.tap()
        XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " largest-text boundary introduction")
        let form = app.collectionViews.firstMatch
        let edge = form.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge.")).firstMatch
        reveal(edge, in: app)
        XCTAssertTrue(edge.isEnabled)
        edge.tap()
        let split = form.buttons["boundary-review.split"]
        reveal(split, in: app)
        XCTAssertTrue(split.isEnabled)
        attach(app, name: language + " largest-text correction controls")
        app.buttons["boundary-review.done"].tap()
    }

    @MainActor
    private func boundaryCorrectionJourney(language: String, locale: String, audits: Bool = false) throws {
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
        // Scope to the presented Form: the underlying workspace intentionally
        // has its own undo action with the same semantic identifier.
        let form = app.collectionViews.firstMatch
        XCTAssertTrue(form.exists)
        attach(app, name: language + " captured boundary selection")
        if audits { try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType]) }
        let edges = form.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge."))
        // Form realizes only visible rows. Exact split/undo/redo topology is
        // asserted by the native frozen-boundary runtime test; this journey
        // verifies selection and the real supported-history acknowledgements.
        XCTAssertGreaterThan(edges.count, 0)
        let firstEdge = edges.element(boundBy: 0)
        reveal(firstEdge, in: app)
        XCTAssertTrue(firstEdge.isEnabled)
        firstEdge.tap()
        let hierarchy = XCTAttachment(string: app.debugDescription)
        hierarchy.name = language + " selected boundary accessibility hierarchy"
        hierarchy.lifetime = .keepAlways
        add(hierarchy)
        attach(app, name: language + " selected boundary controls")
        let split = form.buttons["boundary-review.split"]
        reveal(split, in: app)
        XCTAssertTrue(split.isEnabled)
        attach(app, name: language + " boundary refinement controls")
        if audits { try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType]) }
        split.tap()
        XCTAssertTrue(form.buttons["boundary-review.undo"].waitForExistence(timeout: 5))
        let undo = form.buttons["boundary-review.undo"]
        reveal(undo, in: app)
        undo.tap()
        XCTAssertTrue(form.buttons["boundary-review.redo"].waitForExistence(timeout: 5))
        let redo = form.buttons["boundary-review.redo"]
        reveal(redo, in: app)
        redo.tap()
        XCTAssertTrue(form.buttons["boundary-review.undo"].waitForExistence(timeout: 5))
        attach(app, name: language + " accepted split and correction history")
        // Restore the reference shape before background autosave or the next
        // journey; original journal records still remain append-only.
        reveal(undo, in: app)
        undo.tap()
        XCTAssertTrue(form.buttons["boundary-review.redo"].waitForExistence(timeout: 5))
        app.buttons["boundary-review.done"].tap()
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for _ in 0..<14 {
            if element.exists && element.isHittable { return }
            let moveDown = element.exists && element.frame.midY < app.frame.minY + 120
            if app.buttons["boundary-review.done"].exists {
                // A center swipe pans the interactive SceneKit preview instead
                // of scrolling the Form. Use its trailing gutter, outside the
                // preview, and keep the real UI selection/acknowledgement checks.
                let form = app.collectionViews.firstMatch
                let lower = form.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.9))
                let upper = form.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.25))
                if moveDown { upper.press(forDuration: 0.01, thenDragTo: lower) }
                else { lower.press(forDuration: 0.01, thenDragTo: upper) }
            } else if app.scrollViews.firstMatch.exists {
                let scroll = app.scrollViews.firstMatch
                let lower = scroll.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.9))
                let upper = scroll.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.25))
                if moveDown { upper.press(forDuration: 0.01, thenDragTo: lower) }
                else { lower.press(forDuration: 0.01, thenDragTo: upper) }
            } else if moveDown { app.swipeDown() }
            else { app.swipeUp() }
        }
        XCTAssertTrue(element.isHittable)
    }

    @MainActor
    private func launch(language: String, locale: String, referenceRoom: Bool = false, contentSize: String? = nil) -> XCUIApplication {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(\(language))", "-AppleLocale", locale]
        if let contentSize { app.launchArguments += ["-UIPreferredContentSizeCategoryName", contentSize] }
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

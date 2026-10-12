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
    func testEnglishLargestTextRoomReadinessReturnJourney() throws {
        try largestTextReadinessReturn(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicLargestTextRoomReadinessReturnJourney() throws {
        try largestTextReadinessReturn(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    func testEnglishOfflineExportShareSheetCancellationJourney() throws {
        try offlineExportShareSheetCancellation(language: "en", locale: "en_US")
    }

    @MainActor
    func testArabicOfflineExportShareSheetCancellationJourney() throws {
        try offlineExportShareSheetCancellation(language: "ar", locale: "ar_IQ")
    }

    @MainActor
    private func offlineExportShareSheetCancellation(language: String, locale: String) throws {
        #if !DEBUG || !targetEnvironment(simulator)
        throw XCTSkip("This share-sheet journey uses only the explicit debug reference project on the owned simulator.")
        #else
        let app = launch(language: language, locale: locale, referenceRoom: true)
        defer { app.terminate() }
        let project = app.buttons["project.open.01000000-0000-5000-8000-000000000000"]
        reveal(project, in: app)
        XCTAssertTrue(project.exists)
        project.tap()
        let export = app.buttons["review.export"]
        reveal(export, in: app)
        XCTAssertTrue(export.isEnabled)
        export.tap()
        // The retained native failure hierarchy exposes the system container,
        // not the identifier on UIActivityViewController's wrapping root view.
        let sheet = app.otherElements["ActivityListView"]
        let appeared = sheet.waitForExistence(timeout: 15)
        attach(app, name: language + " offline export native share sheet")
        let hierarchy = XCTAttachment(string: app.debugDescription)
        hierarchy.name = language + " offline export share-sheet hierarchy"
        hierarchy.lifetime = .keepAlways
        add(hierarchy)
        // Preserve actual screenshot/hierarchy even if presentation fails.
        XCTAssertTrue(appeared)
        let caption = sheet.otherElements["LP.CaptionBar.BottomCaption"]
        XCTAssertTrue(caption.exists)
        XCTAssertTrue(caption.label.localizedCaseInsensitiveContains("ZIP"))
        let title = sheet.otherElements["LP.CaptionBar.TopCaption"]
        XCTAssertTrue(title.exists)
        XCTAssertTrue(title.label.hasPrefix("HSS-export-r"))
        // Only the observed native close control is used. Never select Files,
        // a person, a service or any outbound activity.
        let close = sheet.buttons["header.closeButton"]
        XCTAssertTrue(close.isHittable)
        close.tap()
        XCTAssertTrue(sheet.waitForNonExistence(timeout: 5))
        reveal(export, in: app)
        XCTAssertTrue(export.isEnabled)
        attach(app, name: language + " offline export retry available after cancellation")
        #endif
    }

    @MainActor
    private func largestTextReadinessReturn(language: String, locale: String) throws {
        #if !targetEnvironment(simulator)
        throw XCTSkip("This readiness-return journey is restricted to the owned simulator and never starts capture.")
        #else
        let app = launch(language: language, locale: locale,
            contentSize: "UICTContentSizeCategoryAccessibilityXXXL")
        defer { app.terminate() }
        let newProject = app.buttons["library.create"]
        reveal(newProject, in: app)
        XCTAssertTrue(newProject.isEnabled)
        newProject.tap()
        let input = app.textFields["project-name.input"]
        XCTAssertTrue(input.waitForExistence(timeout: 5))
        input.tap()
        let existing = input.value as? String ?? ""
        input.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: existing.count))
        input.typeText("Readiness size fixture")
        let create = app.buttons["project-name.create"]
        XCTAssertTrue(create.isHittable)
        create.tap()
        let start = app.buttons["readiness.start"]
        XCTAssertTrue(start.waitForExistence(timeout: 10))
        reveal(start, in: app)
        XCTAssertTrue(start.isEnabled)
        let createdName = app.staticTexts.matching(NSPredicate(format: "identifier BEGINSWITH %@ AND label == %@",
            "project.name.", "Readiness size fixture")).firstMatch
        reveal(createdName, in: app)
        XCTAssertTrue(createdName.exists)
        let createdNameIdentifier = createdName.identifier
        attach(app, name: language + " largest-text empty-project readiness")
        // Reachability only: never tap Start, request camera permission or make
        // any assertion about the sensor/unsupported-capture lifecycle.
        let library = app.buttons["readiness.open-library"]
        reveal(library, in: app)
        XCTAssertTrue(library.isEnabled)
        attach(app, name: language + " largest-text readiness return control")
        library.tap()
        XCTAssertTrue(app.buttons["privacy.open"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["library.create"].exists)
        let persistedName = app.staticTexts[createdNameIdentifier]
        reveal(persistedName, in: app)
        XCTAssertEqual(persistedName.label, "Readiness size fixture")
        let createdProjectID = String(createdNameIdentifier.dropFirst("project.name.".count))
        let persistedProject = app.buttons["project.open." + createdProjectID]
        XCTAssertTrue(persistedProject.exists)
        XCTAssertTrue(persistedProject.label.contains(language == "ar" ? "مسودة" : "Draft"))
        attach(app, name: language + " largest-text returned local library")
        #endif
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
        // Audit findings remain failures, but must not prevent observations on
        // the independently reachable privacy, help and object-library views.
        continueAfterFailure = true
        defer { app.terminate() }
        XCTAssertTrue(app.buttons["object-library.open"].waitForExistence(timeout: 10))
        attach(app, name: language + " project library accessibility")
        // Do not suppress reported issues: these are real current-screen audits,
        // not a blanket VoiceOver or physical capture acceptance.
        try observedAudit(app, name: language + " project library")
        app.buttons["privacy.open"].tap()
        XCTAssertTrue(app.buttons["privacy.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " privacy disclosure accessibility")
        try observedAudit(app, name: language + " privacy disclosure")
        app.buttons["privacy.done"].tap()
        app.buttons["support.open"].tap()
        XCTAssertTrue(app.buttons["support.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " offline help accessibility")
        try observedAudit(app, name: language + " offline help")
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
        try observedAudit(app, name: language + " object library")
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
        let normal = measureProjectAndBoundary(language: language, locale: locale,
            contentSize: "UICTContentSizeCategoryL", exerciseCorrection: false)
        let largest = measureProjectAndBoundary(language: language, locale: locale,
            contentSize: "UICTContentSizeCategoryAccessibilityXXXL", exerciseCorrection: true)
        // Paired measurements of the exact same named reference project replace
        // the old >35pt check, which a normal two-line name already satisfied.
        let metrics = XCTAttachment(string: """
            Language: \(language)
            Project name: \(normal.projectName)
            Normal category: UICTContentSizeCategoryL
            Largest category: UICTContentSizeCategoryAccessibilityXXXL
            Name height: \(normal.nameHeight) -> \(largest.nameHeight)
            Saved-date height: \(normal.savedDateHeight) -> \(largest.savedDateHeight)
            Captured-boundaries heading height: \(normal.boundaryHeaderHeight) -> \(largest.boundaryHeaderHeight)
            These are actual XCUI element bounds, not an exposed UIFont point-size measurement.
            """)
        metrics.name = language + " paired normal and largest-text element bounds"
        metrics.lifetime = .keepAlways
        add(metrics)
        XCTAssertEqual(largest.projectName, normal.projectName)
        XCTAssertGreaterThan(largest.nameHeight, normal.nameHeight * 1.5)
        XCTAssertGreaterThan(largest.savedDateHeight, normal.savedDateHeight * 1.5)
        XCTAssertGreaterThan(largest.boundaryHeaderHeight, normal.boundaryHeaderHeight * 1.5)
    }

    private struct ProjectAndBoundaryMetrics {
        let projectName: String
        let nameHeight: CGFloat
        let savedDateHeight: CGFloat
        let boundaryHeaderHeight: CGFloat
    }

    @MainActor
    private func measureProjectAndBoundary(language: String, locale: String,
        contentSize: String, exerciseCorrection: Bool) -> ProjectAndBoundaryMetrics {
        let app = launch(language: language, locale: locale, referenceRoom: true,
            contentSize: contentSize)
        defer { app.terminate() }
        let projectID = "01000000-0000-5000-8000-000000000000"
        let project = app.buttons["project.open." + projectID]
        // Largest text pushes the lazy project row below the initial viewport.
        reveal(project, in: app)
        XCTAssertTrue(project.waitForExistence(timeout: 10))
        let hierarchy = XCTAttachment(string: app.debugDescription)
        hierarchy.name = language + " " + contentSize + " library accessibility hierarchy"
        hierarchy.lifetime = .keepAlways
        add(hierarchy)
        attach(app, name: language + " " + contentSize + " project library")
        let name = app.staticTexts["project.name." + projectID]
        let savedDate = app.staticTexts["project.saved-date." + projectID]
        reveal(name, in: app)
        XCTAssertTrue(name.exists)
        XCTAssertFalse(name.label.isEmpty)
        let projectName = name.label
        let nameHeight = name.frame.height
        reveal(savedDate, in: app)
        XCTAssertTrue(savedDate.exists)
        XCTAssertFalse(savedDate.label.isEmpty)
        let savedDateHeight = savedDate.frame.height
        XCTAssertGreaterThan(nameHeight, 0)
        XCTAssertGreaterThan(savedDateHeight, 0)
        XCTAssertLessThanOrEqual(name.frame.width, app.frame.width)
        XCTAssertLessThanOrEqual(savedDate.frame.width, app.frame.width)
        reveal(project, in: app)
        project.tap()
        let review = app.buttons["boundary-review.open"]
        reveal(review, in: app)
        review.tap()
        XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
        attach(app, name: language + " " + contentSize + " boundary introduction")
        let header = app.staticTexts["boundary-review.captured-header"]
        reveal(header, in: app)
        XCTAssertTrue(header.exists)
        XCTAssertFalse(header.label.isEmpty)
        let boundaryHeaderHeight = header.frame.height
        XCTAssertGreaterThan(boundaryHeaderHeight, 0)
        XCTAssertLessThanOrEqual(header.frame.width, app.frame.width)
        let metrics = ProjectAndBoundaryMetrics(projectName: projectName,
            nameHeight: nameHeight, savedDateHeight: savedDateHeight,
            boundaryHeaderHeight: boundaryHeaderHeight)
        if !exerciseCorrection {
            app.buttons["boundary-review.done"].tap()
            return metrics
        }
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
        return metrics
    }

    @MainActor
    private func boundaryCorrectionJourney(language: String, locale: String, audits: Bool = false) throws {
        let app = launch(language: language, locale: locale, referenceRoom: true)
        if audits { continueAfterFailure = true }
        defer { app.terminate() }
        let project = app.buttons["project.open.01000000-0000-5000-8000-000000000000"]
        // Debug-only setup checkpoints an explicitly synthetic room using the
        // real engine/store after XCTest's fresh-container installation.
        reveal(project, in: app)
        XCTAssertTrue(project.waitForExistence(timeout: 10))
        project.tap()
        let review = app.buttons["boundary-review.open"]
        XCTAssertTrue(review.waitForExistence(timeout: 10))
        reveal(review, in: app)
        review.tap()
        XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
        // Scope to the presented Form: the underlying workspace intentionally
        // has its own undo action with the same semantic identifier.
        var form = app.collectionViews.firstMatch
        XCTAssertTrue(form.exists)
        // Modal focus excludes the known-present covered workspace action,
        // without hiding the editor's header, boundary rows or Done control.
        XCTAssertFalse(review.exists)
        let capturedHeader = app.staticTexts["boundary-review.captured-header"]
        reveal(capturedHeader, in: app)
        XCTAssertTrue(capturedHeader.exists)
        let edges = form.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge."))
        XCTAssertTrue(edges.firstMatch.exists)
        XCTAssertTrue(app.buttons["boundary-review.done"].exists)
        attach(app, name: language + " captured boundary selection")
        if audits { try observedAudit(app, name: language + " captured boundary selection") }
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
        if !audits {
            // Both inputs are untouched after selecting the edge: no measured
            // length was entered and point A was not confirmed. Check the real
            // actionable explanations, not geometry acceptance or accuracy.
            let alertTitle = language == "ar" ? "راجع التصحيح" : "Check correction"
            let okTitle = language == "ar" ? "موافق" : "OK"
            let incompleteInputs = [
                ("boundary-review.set-dimension",
                    language == "ar" ? "أدخل طولاً مقاساً أكبر من صفر بالمتر." : "Enter a positive measured length in metres.",
                    "empty reference dimension"),
                ("boundary-review.lock-position",
                    language == "ar" ? "أكّد الموضع الحالي للنقطة A قبل تثبيته." : "Confirm the current position of point A before locking it.",
                    "unconfirmed point position")
            ]
            for (identifier, instruction, observationName) in incompleteInputs {
                let action = form.buttons[identifier]
                reveal(action, in: app)
                XCTAssertTrue(action.isEnabled)
                action.tap()
                let alert = app.alerts[alertTitle]
                XCTAssertTrue(alert.waitForExistence(timeout: 5))
                XCTAssertTrue(alert.staticTexts[instruction].exists)
                attach(app, name: language + " " + observationName + " native validation alert")
                let ok = alert.buttons[okTitle]
                XCTAssertTrue(ok.isHittable)
                ok.tap()
                XCTAssertTrue(alert.waitForNonExistence(timeout: 5))
            }
            // Exercise ordinary numeric editing and the source binding without
            // applying a dimension, locking a point or accepting any geometry.
            let dimension = form.textFields["boundary-review.dimension-value"]
            reveal(dimension, in: app)
            dimension.tap()
            dimension.typeText("2.0")
            XCTAssertEqual(dimension.value as? String, "2.0")
            let keyboardDone = app.buttons["boundary-review.keyboard-done"]
            XCTAssertTrue(keyboardDone.waitForExistence(timeout: 5))
            keyboardDone.tap()
            let source = form.buttons["boundary-review.source"]
            reveal(source, in: app)
            source.tap()
            let laserTitle = language == "ar" ? "قياس ليزري" : "Laser measure"
            let laser = app.buttons[laserTitle]
            XCTAssertTrue(laser.waitForExistence(timeout: 5))
            laser.tap()
            XCTAssertEqual(source.value as? String, laserTitle)
            XCTAssertEqual(dimension.value as? String, "2.0")
            let uncertainty = form.textFields["boundary-review.uncertainty-value"]
            reveal(uncertainty, in: app)
            uncertainty.tap()
            uncertainty.typeText("0.02")
            XCTAssertEqual(uncertainty.value as? String, "0.02")
            XCTAssertTrue(keyboardDone.waitForExistence(timeout: 5))
            keyboardDone.tap()
            XCTAssertEqual(uncertainty.value as? String, "0.02")
            attach(app, name: language + " numeric correction inputs and laser source binding")
            reveal(source, in: app)
            source.tap()
            let tapeTitle = language == "ar" ? "شريط قياس" : "Tape measure"
            app.buttons[tapeTitle].tap()
            XCTAssertEqual(source.value as? String, tapeTitle)
        }
        var split = form.buttons["boundary-review.split"]
        reveal(split, in: app)
        XCTAssertTrue(split.isEnabled)
        attach(app, name: language + " boundary refinement controls")
        if audits {
            try observedAudit(app, name: language + " selected boundary refinement")
            // Retain every strict finding, then reset the auditor's viewport
            // through the real close/open flow before exercising correction.
            app.buttons["boundary-review.done"].tap()
            XCTAssertTrue(review.waitForExistence(timeout: 5))
            reveal(review, in: app)
            XCTAssertTrue(review.isHittable)
            review.tap()
            XCTAssertTrue(app.buttons["boundary-review.done"].waitForExistence(timeout: 5))
            form = app.collectionViews.firstMatch
            XCTAssertTrue(form.exists)
            XCTAssertFalse(review.exists)
            reveal(capturedHeader, in: app)
            XCTAssertTrue(capturedHeader.exists)
            let reopenedEdge = form.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "boundary-review.edge.")).firstMatch
            reveal(reopenedEdge, in: app)
            XCTAssertTrue(reopenedEdge.exists)
            XCTAssertTrue(app.buttons["boundary-review.done"].exists)
            XCTAssertTrue(reopenedEdge.isEnabled)
            reopenedEdge.tap()
            split = form.buttons["boundary-review.split"]
            reveal(split, in: app)
            XCTAssertTrue(split.isEnabled)
        }
        split.tap()
        let undo = form.buttons["boundary-review.undo"]
        reveal(undo, in: app)
        XCTAssertTrue(undo.waitForExistence(timeout: 5))
        XCTAssertTrue(undo.isEnabled)
        undo.tap()
        let redo = form.buttons["boundary-review.redo"]
        reveal(redo, in: app)
        XCTAssertTrue(redo.waitForExistence(timeout: 5))
        XCTAssertTrue(redo.isEnabled)
        redo.tap()
        reveal(undo, in: app)
        XCTAssertTrue(undo.waitForExistence(timeout: 5))
        XCTAssertTrue(undo.isEnabled)
        attach(app, name: language + " accepted split and correction history")
        // Restore the reference shape before background autosave or the next
        // journey; original journal records still remain append-only.
        undo.tap()
        reveal(redo, in: app)
        XCTAssertTrue(redo.waitForExistence(timeout: 5))
        XCTAssertTrue(redo.isEnabled)
        app.buttons["boundary-review.done"].tap()
        XCTAssertTrue(review.waitForExistence(timeout: 5))
        reveal(review, in: app)
        XCTAssertTrue(review.isHittable)
    }

    @MainActor
    private func observedAudit(_ app: XCUIApplication, name: String) throws {
        var issues: [XCUIAccessibilityAuditIssue] = []
        defer {
            // Observe after the auditor has finished, including when it throws.
            // Snapshot queries inside its callback can interfere with the audit.
            for issue in issues {
                let metadata = XCTAttachment(string: """
                    Screen: \(name)
                    Audit type: \(issue.auditType.rawValue)
                    Description: \(issue.compactDescription)
                    Detail: \(issue.detailedDescription)
                    """)
                metadata.name = name + " unsuppressed accessibility issue metadata"
                metadata.lifetime = .keepAlways
                add(metadata)
                var observation = "Post-audit associated element observation."
                if let element = issue.element {
                    let exists = element.exists
                    observation += "\nElement exists: \(exists)"
                    if exists {
                        observation += """

                            Identifier: \(element.identifier)
                            Label: \(element.label)
                            Value: \(String(describing: element.value))
                            Element type: \(element.elementType.rawValue)
                            Frame: \(element.frame)
                            Hittable: \(element.isHittable)
                            Element hierarchy: \(element.debugDescription)
                            """
                    }
                } else {
                    observation += "\nNo associated XCUI element was supplied."
                }
                let attachment = XCTAttachment(string: observation)
                attachment.name = name + " post-audit accessibility element observation"
                attachment.lifetime = .keepAlways
                add(attachment)
            }
        }
        try app.performAccessibilityAudit(for: [.contrast, .hitRegion, .textClipped, .dynamicType]) { issue in
            issues.append(issue)
            // SDK contract: true filters out the issue. Always false means
            // XCTest still records it and fails the test; this only observes.
            return false
        }
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        func validFrame(_ frame: CGRect) -> Bool {
            !frame.isNull && !frame.isEmpty &&
                [frame.minX, frame.minY, frame.maxX, frame.maxY, frame.width, frame.height].allSatisfy { $0.isFinite }
        }
        var diagnostic = "No visibility observation recorded."
        // Inspect once more after the fourteenth gesture; never add an
        // unbounded retry or accept a genuinely oversized control.
        for attempt in 0...14 {
            let appFrame = app.frame
            let form = app.collectionViews.firstMatch
            let isBoundaryReview = app.buttons["boundary-review.done"].exists && form.exists
            let scroll = app.scrollViews.firstMatch
            let hasScroll = !isBoundaryReview && scroll.exists
            let containerFrame = isBoundaryReview ? form.frame : (hasScroll ? scroll.frame : appFrame)
            let baseViewport = containerFrame.intersection(appFrame)
            let navigationFrames = app.navigationBars.allElementsBoundByIndex.map { $0.frame }
                .filter { validFrame($0) && $0.intersects(baseViewport) }
            // A Form can extend behind the navigation bar even when XCTest
            // reports its partially covered row as hittable. With no bar,
            // reserve a conservative status-area inset from the app's top.
            let topBoundary = navigationFrames.map { $0.maxY }.max() ?? (appFrame.minY + 60)
            let viewportTop = max(baseViewport.minY, topBoundary) + 4
            let viewport = CGRect(x: baseViewport.minX, y: viewportTop,
                width: baseViewport.width, height: max(0, baseViewport.maxY - viewportTop))
            let exists = element.exists
            let frame = exists ? element.frame : CGRect.null
            let hittable = exists && element.isHittable
            let fullyVisible = validFrame(frame) && validFrame(viewport) && viewport.contains(frame)
            diagnostic = """
                Visibility observation after \(attempt) of at most 14 gestures.
                Target exists: \(exists)
                Target hittable: \(hittable)
                Target frame: \(frame)
                Target fully inside viewport: \(fullyVisible)
                App frame: \(appFrame)
                Container: \(isBoundaryReview ? "boundary Form" : (hasScroll ? "ScrollView" : "application"))
                Container frame: \(containerFrame)
                Navigation bar frames: \(navigationFrames)
                Visible viewport: \(viewport)
                """
            if exists { diagnostic += "\nTarget identifier: \(element.identifier)\nTarget label: \(element.label)" }
            if hittable && fullyVisible { return }
            if attempt == 14 { break }
            let moveDown = exists && validFrame(frame) && frame.minY < viewport.minY
            guard validFrame(containerFrame), validFrame(viewport) else {
                diagnostic += "\nCannot issue a finite gesture inside the recorded container/viewport."
                break
            }
            let clippedDistance = exists && validFrame(frame)
                ? max(0, moveDown ? viewport.minY - frame.minY : frame.maxY - viewport.maxY) : 0
            let maximumDrag = min(containerFrame.height, viewport.height) * 0.35
            // A fixed large swipe oscillated around a 414pt card that fit the
            // 810pt viewport but needed only 40pt of movement. Correct the
            // actual clipped distance; use the bounded search step only while
            // a lazy target is absent or has no vertical clipping measurement.
            let requestedDrag = clippedDistance > 0 ? max(16, clippedDistance + 8) : maximumDrag
            let dragDistance = min(maximumDrag, requestedDrag)
            diagnostic += "\nClipped distance: \(clippedDistance)\nNext bounded drag: \(dragDistance)"
            let scrollingElement: XCUIElement = isBoundaryReview ? form : (hasScroll ? scroll : app)
            // Stay in the trailing gutter: a center gesture pans the SceneKit
            // preview. Slow motion and a held endpoint avoid a fling past the
            // measured correction; this is a real UI gesture, not state setup.
            let lower = scrollingElement.coordinate(withNormalizedOffset: CGVector(dx: 0.98, dy: 0.8))
            let upper = scrollingElement.coordinate(withNormalizedOffset:
                CGVector(dx: 0.98, dy: 0.8 - dragDistance / containerFrame.height))
            if moveDown {
                upper.press(forDuration: 0.01, thenDragTo: lower, withVelocity: .slow, thenHoldForDuration: 0.1)
            } else {
                lower.press(forDuration: 0.01, thenDragTo: upper, withVelocity: .slow, thenHoldForDuration: 0.1)
            }
        }
        let attachment = XCTAttachment(string: diagnostic)
        attachment.name = "Unreachable target frame and visible viewport"
        attachment.lifetime = .keepAlways
        add(attachment)
        attach(app, name: "Unreachable target visible viewport")
        XCTFail("Target could not be fully revealed inside the unobscured viewport within the 14-gesture budget.")
    }

    @MainActor
    private func launch(language: String, locale: String, referenceRoom: Bool = false, contentSize: String? = nil) -> XCUIApplication {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-AppleLanguages", "(\(language))", "-AppleLocale", locale,
            "-UIPreferredContentSizeCategoryName", contentSize ?? "UICTContentSizeCategoryL"]
        #if DEBUG && targetEnvironment(simulator)
        app.launchEnvironment["HSS_UI_TEST_RUN_ID"] = UUID().uuidString
        #endif
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

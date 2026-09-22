import Foundation

/// Language selection supplied by the eventual application localization owner.
/// This slice deliberately owns its temporary English/Arabic copy so it does
/// not add keys to the shared localization tables before integration.
enum ProvenancePresentationLanguage: Equatable, Sendable {
    case english
    case arabic
}

/// A small, explicit bilingual value. Integrators can replace this boundary
/// with localized resources without changing the evidence contract.
struct ProvenanceCopy: Equatable, Sendable {
    let english: String
    let arabic: String

    func value(for language: ProvenancePresentationLanguage) -> String {
        switch language {
        case .english: english
        case .arabic: arabic
        }
    }
}

/// The origin of a displayed field. This describes how the value entered the
/// project; it deliberately does not imply dimensional accuracy.
enum ProvenanceKind: CaseIterable, Equatable, Sendable {
    /// Directly supported by retained capture evidence.
    case observed
    /// Derived by a model fit from supporting evidence.
    case fitted
    /// Entered from a physical measurement by the operator.
    case userMeasured
    /// A declared estimate or semantic guess.
    case inferred
    /// No adequate source is known for the field.
    case unknown

    var title: ProvenanceCopy {
        switch self {
        case .observed: ProvenanceCopy(english: "Observed", arabic: "مرصود")
        case .fitted: ProvenanceCopy(english: "Fitted", arabic: "مُلائَم")
        case .userMeasured: ProvenanceCopy(english: "User measured", arabic: "مقاس بواسطة المستخدم")
        case .inferred: ProvenanceCopy(english: "Inferred", arabic: "مستنتج")
        case .unknown: ProvenanceCopy(english: "Unknown", arabic: "غير معروف")
        }
    }

    var explanation: ProvenanceCopy {
        switch self {
        case .observed:
            ProvenanceCopy(english: "Supported by captured evidence.", arabic: "مدعوم بأدلة ملتقطة.")
        case .fitted:
            ProvenanceCopy(english: "Derived by fitting a model to supporting evidence.", arabic: "مشتق من ملاءمة نموذج لأدلة داعمة.")
        case .userMeasured:
            ProvenanceCopy(english: "Entered from a physical measurement; retain its source and uncertainty.", arabic: "مُدخل من قياس مادي؛ احتفظ بمصدره وعدم يقينه.")
        case .inferred:
            ProvenanceCopy(english: "An estimate or guess, not direct evidence.", arabic: "تقدير أو تخمين، وليس دليلاً مباشراً.")
        case .unknown:
            ProvenanceCopy(english: "No adequate source has been recorded.", arabic: "لم يُسجَّل مصدر كافٍ.")
        }
    }
}

/// Uncertainty is kept distinct from both source and validation. An absent
/// uncertainty is displayed as absent; it is never replaced with a default.
enum ProvenanceUncertainty: Equatable, Sendable {
    case notStated
    case stated(ProvenanceCopy)
}

/// Evidence needed before a reference-comparison result can be displayed.
/// The producer, not this view, is responsible for supplying a real
/// independent reference and its uncertainty.
struct IndependentReferenceComparison: Equatable, Sendable {
    let referenceDescription: ProvenanceCopy
    let resultDescription: ProvenanceCopy
    let referenceUncertainty: ProvenanceUncertainty
}

/// Accuracy is intentionally not a free-form status string. Until an
/// independent comparison is supplied, the only representable state is
/// `notMeasured`, which prevents an estimate from being labeled as measured.
enum ProvenanceAccuracyDisclosure: Equatable, Sendable {
    case notMeasured
    case independentlyCompared(IndependentReferenceComparison)
}

/// A reusable, field-level payload for plan, camera, inspector, and export
/// review. Values are already formatted by the caller, preserving canonical
/// units and avoiding UI-side numerical interpretation.
struct ProvenanceFieldPresentation: Identifiable, Equatable, Sendable {
    let id: UUID
    let label: ProvenanceCopy
    let formattedValue: ProvenanceCopy?
    let kind: ProvenanceKind
    let sourceDescription: ProvenanceCopy?
    let uncertainty: ProvenanceUncertainty
    let accuracy: ProvenanceAccuracyDisclosure

    init(
        id: UUID = UUID(),
        label: ProvenanceCopy,
        formattedValue: ProvenanceCopy? = nil,
        kind: ProvenanceKind,
        sourceDescription: ProvenanceCopy? = nil,
        uncertainty: ProvenanceUncertainty = .notStated,
        accuracy: ProvenanceAccuracyDisclosure = .notMeasured
    ) {
        self.id = id
        self.label = label
        self.formattedValue = formattedValue
        self.kind = kind
        self.sourceDescription = sourceDescription
        self.uncertainty = uncertainty
        self.accuracy = accuracy
    }

    /// A user measurement without both origin and uncertainty remains visible
    /// but is incomplete. The caller should request the missing disclosures.
    var needsUserMeasurementDisclosure: Bool {
        guard kind == .userMeasured else { return false }
        guard sourceDescription != nil else { return true }
        if case .notStated = uncertainty { return true }
        return false
    }
}

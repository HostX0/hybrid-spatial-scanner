import SwiftUI

/// A field-level provenance card for live guidance and correction review.
/// It is render-only: no measurement, fitting, or validation occurs here.
struct ProvenanceDisclosureView: View {
    let field: ProvenanceFieldPresentation
    let language: ProvenancePresentationLanguage

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Text(field.label.value(for: language)).font(.headline)
                Spacer()
                provenanceBadge
            }
            if let value = field.formattedValue {
                Text(value.value(for: language)).font(.title3.monospacedDigit())
            }
            Text(field.kind.explanation.value(for: language))
                .font(.subheadline)
                .foregroundStyle(.secondary)
            sourceAndUncertainty
            accuracyDisclosure
            if field.needsUserMeasurementDisclosure {
                Label {
                    Text(copy("Add the measurement source and uncertainty.", "أضف مصدر القياس وعدم اليقين."))
                } icon: {
                    Image(systemName: "exclamationmark.triangle")
                }
                .font(.footnote)
                .foregroundStyle(.orange)
            }
        }
        .padding(12)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(accessibilitySummary)
    }

    private var provenanceBadge: some View {
        Label(field.kind.title.value(for: language), systemImage: symbolName)
            .font(.caption.weight(.semibold))
            .padding(.horizontal, 8)
            .padding(.vertical, 5)
            .background(badgeColor.opacity(0.18), in: Capsule())
            .foregroundStyle(badgeColor)
    }

    @ViewBuilder private var sourceAndUncertainty: some View {
        if let source = field.sourceDescription {
            disclosureRow(title: copy("Source", "المصدر"), value: source.value(for: language))
        }
        switch field.uncertainty {
        case .notStated:
            disclosureRow(title: copy("Uncertainty", "عدم اليقين"), value: copy("Not stated", "غير مذكور"))
        case let .stated(value):
            disclosureRow(title: copy("Uncertainty", "عدم اليقين"), value: value.value(for: language))
        }
    }

    @ViewBuilder private var accuracyDisclosure: some View {
        switch field.accuracy {
        case .notMeasured:
            disclosureRow(title: copy("Accuracy", "الدقة"), value: copy("Not measured against an independent reference", "لم تُقَس مقابل مرجع مستقل"))
        case let .independentlyCompared(comparison):
            disclosureRow(title: copy("Reference comparison", "مقارنة مرجعية"), value: comparison.resultDescription.value(for: language))
            disclosureRow(title: copy("Reference", "المرجع"), value: comparison.referenceDescription.value(for: language))
            switch comparison.referenceUncertainty {
            case .notStated:
                disclosureRow(title: copy("Reference uncertainty", "عدم يقين المرجع"), value: copy("Not stated", "غير مذكور"))
            case let .stated(value):
                disclosureRow(title: copy("Reference uncertainty", "عدم يقين المرجع"), value: value.value(for: language))
            }
        }
    }

    private func disclosureRow(title: String, value: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Text(title).foregroundStyle(.secondary)
            Spacer(minLength: 12)
            Text(value).multilineTextAlignment(.trailing)
        }
        .font(.footnote)
    }

    private var symbolName: String {
        switch field.kind {
        case .observed: "eye"
        case .fitted: "function"
        case .userMeasured: "ruler"
        case .inferred: "lightbulb"
        case .unknown: "questionmark.circle"
        }
    }

    private var badgeColor: Color {
        switch field.kind {
        case .observed: .green
        case .fitted: .blue
        case .userMeasured: .purple
        case .inferred: .orange
        case .unknown: .secondary
        }
    }

    private var accessibilitySummary: String {
        [field.label.value(for: language), field.kind.title.value(for: language), field.kind.explanation.value(for: language)].joined(separator: ". ")
    }

    private func copy(_ english: String, _ arabic: String) -> String {
        ProvenanceCopy(english: english, arabic: arabic).value(for: language)
    }
}

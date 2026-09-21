import SwiftUI

/// A deliberately honest starting point for the native shell. Capture,
/// permissions, depth ingestion, and generated geometry are introduced by
/// their dedicated work packets after physical-device validation.
struct ReadinessView: View {
    var body: some View {
        ContentUnavailableView {
            Label("Scanner foundation ready", systemImage: "square.stack.3d.up")
        } description: {
            Text("The native app and portable SpatialCore target are linked. Capture is not implemented yet.")
        }
        .accessibilityElement(children: .combine)
    }
}

#Preview {
    ReadinessView()
}

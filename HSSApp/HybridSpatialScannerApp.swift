import SwiftUI

@main
struct HybridSpatialScannerApp: App {
    @StateObject private var workspace = ScannerWorkspaceModel()

    var body: some Scene {
        WindowGroup {
            ScannerWorkspaceProductHost(workspace: workspace)
#if DEBUG
                .task { try? await workspace.seedReferenceRoomForUITesting() }
#endif
        }
    }
}

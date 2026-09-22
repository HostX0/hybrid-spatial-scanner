import ARKit
import AVFoundation
import Foundation
import SwiftUI

/// The permission state is inspected only. This gate never prompts for access.
enum CameraPermissionState: Equatable {
    case authorized, denied, restricted, notDetermined
}

/// `notEstablished` is the honest value until the explicit capture flow starts
/// the owned ARSession and receives a tracking callback.
enum InitialTrackingState: Equatable {
    case normal, limited, unavailable, notEstablished
}

struct CaptureReadiness: Equatable {
    let cameraPermission: CameraPermissionState
    let supportsRequiredARFeatures: Bool
    let hasUsableStorage: Bool
    let initialTracking: InitialTrackingState

    var blockedConditions: [String] {
        var conditions: [String] = []
        switch cameraPermission {
        case .authorized: break
        case .denied: conditions.append("Camera access is denied. Capture is unavailable until it is allowed in Settings.")
        case .restricted: conditions.append("Camera access is restricted by this device. Capture is unavailable.")
        case .notDetermined: conditions.append("Camera access has not been granted. Capture is unavailable until a later capture flow requests it.")
        }
        if !supportsRequiredARFeatures {
            conditions.append("This device does not support ARKit scene depth and mesh reconstruction required for capture.")
        }
        if !hasUsableStorage {
            conditions.append("Usable local storage is unavailable. Capture cannot begin because source observations cannot be retained.")
        }
        switch initialTracking {
        case .normal: break
        case .limited: conditions.append("AR tracking is limited. Capture remains unavailable until normal tracking is established.")
        case .unavailable: conditions.append("AR tracking is unavailable. Capture is unavailable.")
        case .notEstablished: conditions.append("Initial AR tracking has not been established. Capture remains unavailable until a later session reports normal tracking.")
        }
        return conditions
    }

    var isReadyForCapture: Bool { blockedConditions.isEmpty }
}

protocol CaptureReadinessProviding {
    func currentReadiness() -> CaptureReadiness
}

protocol CapturePlatformInspecting {
    func cameraPermission() -> CameraPermissionState
    func supportsRequiredARFeatures() -> Bool
    func hasUsableStorage() -> Bool
    func initialTrackingState() -> InitialTrackingState
}

struct CaptureReadinessProvider: CaptureReadinessProviding {
    let platform: any CapturePlatformInspecting

    init(platform: any CapturePlatformInspecting = SystemCapturePlatform()) {
        self.platform = platform
    }

    func currentReadiness() -> CaptureReadiness {
        CaptureReadiness(cameraPermission: platform.cameraPermission(), supportsRequiredARFeatures: platform.supportsRequiredARFeatures(), hasUsableStorage: platform.hasUsableStorage(), initialTracking: platform.initialTrackingState())
    }
}

struct SystemCapturePlatform: CapturePlatformInspecting {
    func cameraPermission() -> CameraPermissionState {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized: .authorized
        case .denied: .denied
        case .restricted: .restricted
        case .notDetermined: .notDetermined
        @unknown default: .restricted
        }
    }

    func supportsRequiredARFeatures() -> Bool {
        guard ARWorldTrackingConfiguration.isSupported, #available(iOS 13.4, *) else { return false }
        return ARWorldTrackingConfiguration.supportsFrameSemantics(.sceneDepth)
            && ARWorldTrackingConfiguration.supportsSceneReconstruction(.mesh)
    }

    func hasUsableStorage() -> Bool {
        guard let directory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return false }
        do {
            let values = try directory.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
            return (values.volumeAvailableCapacityForImportantUsage ?? 0) > 0
        } catch { return false }
    }

    func initialTrackingState() -> InitialTrackingState {
        // Do not initialize ARSession here. B-004 owns session lifecycle.
        .notEstablished
    }
}

/// Readiness and explicit lifecycle controls for the one owned ARKit session.
/// Merely presenting this view never requests camera permission or starts ARKit.
struct ReadinessView: View {
    @StateObject private var coordinator: ARSessionCoordinator

    @MainActor
    init(coordinator: ARSessionCoordinator = ARSessionCoordinator()) {
        _coordinator = StateObject(wrappedValue: coordinator)
    }

    var body: some View {
        ZStack {
            ScannerProductTheme.canvas.ignoresSafeArea()
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("HSS")
                            .font(.headline.weight(.semibold))
                            .foregroundStyle(ScannerProductTheme.accent)
                        Text("Prepare to scan")
                            .font(.largeTitle.weight(.semibold))
                            .foregroundStyle(.white)
                    }

                    readinessCard

                    if !readinessMessages.isEmpty {
                        VStack(alignment: .leading, spacing: 10) {
                            Text("Before you begin")
                                .font(.headline)
                                .foregroundStyle(.white)
                            ForEach(readinessMessages, id: \.self) { condition in
                                Label(condition, systemImage: "exclamationmark.triangle.fill")
                                    .font(.subheadline)
                                    .foregroundStyle(.white.opacity(0.88))
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .padding(14)
                                    .background(ScannerProductTheme.elevatedSurface, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                                    .accessibilityElement(children: .combine)
                            }
                        }
                    }

                    actions

                    if let report = coordinator.effectiveConfiguration {
                        configurationSummary(report)
                    }
                }
                .padding(24)
            }
        }
        .foregroundStyle(.white)
        .navigationBarTitleDisplayMode(.inline)
    }

    private var readinessCard: some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: coordinator.readiness.isReadyForCapture ? "checkmark.circle.fill" : "circle.dotted")
                .font(.title2)
                .foregroundStyle(coordinator.readiness.isReadyForCapture ? ScannerProductTheme.accent : ScannerProductTheme.warning)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 5) {
                Text(coordinator.readiness.isReadyForCapture ? "Ready for capture" : "Checking capture requirements")
                    .font(.headline)
                Text(readinessSummary)
                    .font(.subheadline)
                    .foregroundStyle(ScannerProductTheme.muted)
            }
            Spacer(minLength: 0)
        }
        .padding(18)
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    @ViewBuilder
    private var actions: some View {
        if coordinator.canRequestCameraPermission {
            Button {
                Task { await coordinator.requestCameraPermission() }
            } label: {
                Label("Allow camera", systemImage: "camera.fill")
                    .font(.headline.weight(.semibold))
                    .frame(maxWidth: .infinity, minHeight: 56)
            }
            .buttonStyle(.borderedProminent)
            .tint(ScannerProductTheme.accent)
            .accessibilityHint("Requests access to the camera for an on-device scan")
        }
        if coordinator.canStartSession && !coordinator.isSessionRunning {
            Button {
                coordinator.startSession()
            } label: {
                Label("Start camera", systemImage: "viewfinder")
                    .font(.headline.weight(.semibold))
                    .frame(maxWidth: .infinity, minHeight: 56)
            }
            .buttonStyle(.borderedProminent)
            .tint(ScannerProductTheme.accent)
            .accessibilityHint("Starts the single capture session")
        }
        if coordinator.isSessionRunning {
            Button("Stop camera", role: .destructive) {
                coordinator.stopSession()
            }
            .frame(maxWidth: .infinity, minHeight: 48)
        }
    }

    private func configurationSummary(_ report: EffectiveCaptureConfigurationReport) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Capture mode")
                .font(.headline)
            Label(
                report.chosenDepthSemantic == .sceneDepth && report.chosenMeshOption == .mesh
                    ? "Depth and mesh capture are active."
                    : "Basic AR capture is active; some capture data is unavailable.",
                systemImage: report.chosenDepthSemantic == .sceneDepth && report.chosenMeshOption == .mesh
                    ? "cube.transparent"
                    : "exclamationmark.triangle"
            )
            .font(.subheadline)
            .foregroundStyle(ScannerProductTheme.muted)
        }
        .padding(16)
        .background(ScannerProductTheme.surface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
    }

    private var readinessSummary: LocalizedStringKey {
        if coordinator.readiness.isReadyForCapture {
            return coordinator.isSessionRunning ? "The camera is ready for a metric scan." : "Start the camera when you are in the space."
        }
        return "Resolve the items below before capture can begin."
    }

    private var readinessMessages: [String] {
        var messages: [String] = []
        switch coordinator.readiness.cameraPermission {
        case .authorized: break
        case .denied:
            messages.append(String(localized: "Allow camera access in Settings to scan this space."))
        case .restricted:
            messages.append(String(localized: "Camera access is restricted on this iPhone."))
        case .notDetermined:
            messages.append(String(localized: "Allow camera access to begin an on-device scan."))
        }
        if !coordinator.readiness.supportsRequiredARFeatures {
            messages.append(String(localized: "This iPhone does not support the depth and mesh capture required for scanning."))
        }
        if !coordinator.readiness.hasUsableStorage {
            messages.append(String(localized: "Free local storage before starting a scan."))
        }
        switch coordinator.readiness.initialTracking {
        case .normal: break
        case .limited:
            messages.append(String(localized: "Move slowly and keep textured surfaces in view until tracking recovers."))
        case .unavailable:
            messages.append(String(localized: "Tracking is unavailable. Start again in a well-lit space."))
        case .notEstablished:
            messages.append(String(localized: "Start the camera to establish tracking."))
        }
        return messages
    }
}

#Preview { ReadinessView() }

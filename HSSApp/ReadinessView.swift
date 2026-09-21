import ARKit
import AVFoundation
import Foundation
import SwiftUI

/// The permission state is inspected only. This gate never prompts for access.
enum CameraPermissionState: Equatable {
    case authorized, denied, restricted, notDetermined
}

/// B-003 has no ARSession. `notEstablished` is consequently the honest system
/// value until the later capture packet owns and observes a session.
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

/// A capability gate only: this view does not request access or initialize ARKit.
struct ReadinessView: View {
    private let readiness: CaptureReadiness

    init(provider: any CaptureReadinessProviding = CaptureReadinessProvider()) {
        readiness = provider.currentReadiness()
    }

    var body: some View {
        List {
            Section {
                Label(readiness.isReadyForCapture ? "Capture requirements are ready" : "Capture is unavailable", systemImage: readiness.isReadyForCapture ? "checkmark.circle" : "exclamationmark.triangle")
            } footer: {
                Text("This screen checks readiness only. It does not request permission, initialize an AR session, start capture, or create geometry.")
            }
            if !readiness.isReadyForCapture {
                Section("Blocked conditions") {
                    ForEach(readiness.blockedConditions, id: \.self) { condition in Text(condition) }
                }
            }
        }
        .navigationTitle("Capture readiness")
    }
}

#Preview { ReadinessView() }

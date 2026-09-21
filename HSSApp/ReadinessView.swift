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
        List {
            Section {
                Label(coordinator.phase.summary, systemImage: coordinator.readiness.isReadyForCapture ? "checkmark.circle" : "exclamationmark.triangle")
            } footer: {
                Text("Camera access and ARKit start only after the corresponding button is selected. RoomPlan is not enabled until shared-session behavior is physically validated.")
            }
            if !coordinator.readiness.isReadyForCapture {
                Section("Blocked conditions") {
                    ForEach(coordinator.readiness.blockedConditions, id: \.self) { condition in Text(condition) }
                }
            }
            Section("Actions") {
                if coordinator.canRequestCameraPermission {
                    Button("Allow Camera") {
                        Task { await coordinator.requestCameraPermission() }
                    }
                }
                if coordinator.canStartSession && !coordinator.isSessionRunning {
                    Button("Start ARKit Session") { coordinator.startSession() }
                }
                if coordinator.isSessionRunning {
                    Button("Stop Session", role: .destructive) { coordinator.stopSession() }
                }
            }
            if let report = coordinator.effectiveConfiguration {
                Section("Effective configuration") {
                    LabeledContent("Requested depth", value: report.requestedDepthSemantic.rawValue)
                    LabeledContent("Supported depth", value: report.sceneDepthSupported ? "sceneDepth" : "none")
                    LabeledContent("Chosen depth", value: report.chosenDepthSemantic.rawValue)
                    LabeledContent("Requested mesh", value: report.requestedMeshOption.rawValue)
                    LabeledContent("Supported mesh", value: report.meshReconstructionSupported ? "mesh" : "none")
                    LabeledContent("Chosen mesh", value: report.chosenMeshOption.rawValue)
                    LabeledContent("RoomPlan", value: "Not enabled; physical shared-session validation pending")
                    if let reason = report.fallbackReason {
                        Text("Fallback: \(reason)")
                    }
                }
            }
        }
        .navigationTitle("Capture readiness")
    }
}

#Preview { ReadinessView() }

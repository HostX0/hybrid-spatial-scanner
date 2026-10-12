import Foundation
import UIKit

struct ScannerCaptureAdvice: Equatable, Sendable {
    let code: UInt32
    let qualityRevision: UInt64
    let sourceFrame: SceneDepthFrameIdentity?
    let validDepthFraction: Double

    var title: String {
        switch code {
        case 1: String(localized: "Steady the camera")
        case 2: String(localized: "Protect the scan")
        case 4: String(localized: "Find a clearer view")
        case 9: String(localized: "Processing captured detail")
        default: String(localized: "Review capture support")
        }
    }
    var detail: String {
        switch code {
        case 1: String(localized: "Move slowly and keep visible detail in view. Precise overlays wait for tracking to recover.")
        case 2: String(localized: "Capture is conserving resources. Retained source stays local; stop and save if the pause continues.")
        case 4: String(localized: "Depth support is limited in this view. Aim at visible surfaces from another angle; shiny or covered areas can remain unknown.")
        case 9: String(localized: "Keep the camera steady while the accepted source is processed.")
        default: String(localized: "Guidance uses observed support, not a promised accuracy score.")
        }
    }
    var symbol: String { code == 2 ? "exclamationmark.shield" : code == 1 ? "hand.raised" : "viewfinder" }
}

/// One MainActor-owned instance of the portable measured-quality policy. Its
/// clock/revision are ephemeral presentation evidence, never exported source.
@MainActor
final class ScannerCaptureCoach {
    private let ownerAddress: UInt?
    private var owner: OpaquePointer? { ownerAddress.flatMap(OpaquePointer.init(bitPattern:)) }
    private var revision: UInt64 = 0

    init() {
        var profile = HSSGuidanceTuning()
        profile.abi_version = HSS_ABI_VERSION
        profile.struct_size = UInt32(MemoryLayout<HSSGuidanceTuning>.size)
        profile.debounce_s = 0.8
        profile.minimum_display_s = 2
        profile.repeated_haptic_s = 8
        profile.low_enter = 0.4
        profile.low_exit = 0.5
        profile.high_enter = 0.7
        profile.high_exit = 0.6
        var created: OpaquePointer?
        ownerAddress = hss_guidance_create(&profile, &created) == HSS_OK ? created.map { UInt(bitPattern: $0) } : nil
    }

    deinit { hss_guidance_destroy(ownerAddress.flatMap(OpaquePointer.init(bitPattern:))) }

    func reset() {
        hss_guidance_reset(owner)
        revision = 0
    }

    func observe(frame: NormalizedSceneDepthFrame, resourcePressure: UInt32) -> ScannerCaptureAdvice? {
        let age = ProcessInfo.processInfo.systemUptime - frame.identity.monotonicTimestamp
        guard !frame.validDepthMask.isEmpty, age.isFinite, age >= 0, age <= 1.5 else { return nil }
        let fraction = Double(frame.validDepthMask.filter { $0 == 1 }.count) / Double(frame.validDepthMask.count)
        return evaluate(fraction: fraction, sourceFrame: frame.identity, resourcePressure: resourcePressure)
    }

    func observeMissingDepth(resourcePressure: UInt32) -> ScannerCaptureAdvice? {
        // Only the normalizer's noValidDepthSamples outcome takes this path.
        // It is evidence of absence, never an accepted metric frame.
        evaluate(fraction: 0, sourceFrame: nil, resourcePressure: resourcePressure)
    }

    private func evaluate(fraction: Double, sourceFrame: SceneDepthFrameIdentity?, resourcePressure: UInt32) -> ScannerCaptureAdvice? {
        guard let owner, revision < UInt64.max - 1 else { return nil }
        revision += 1
        var quality = HSSGuidanceQuality()
        quality.abi_version = HSS_ABI_VERSION
        quality.struct_size = UInt32(MemoryLayout<HSSGuidanceQuality>.size)
        quality.quality_revision = revision
        quality.monotonic_time_s = ProcessInfo.processInfo.systemUptime
        quality.tracking = 0 // Normalized metric frames require normal tracking.
        quality.resources = resourcePressure
        quality.measured_facts = UInt32(HSS_GUIDANCE_DEPTH_MEASURED)
        quality.valid_depth_fraction = fraction
        var presentation = HSSGuidancePresentation()
        guard hss_guidance_evaluate(owner, &quality, &presentation) == HSS_OK,
              presentation.has_issue == 1,
              presentation.evidence_revision == revision,
              presentation.expires_before_quality_revision > revision else { return nil }
        // Resource/tracking workflow already owns pauses/precise-overlay
        // suppression; this presentation path never starts/stops the ARSession.
        return ScannerCaptureAdvice(code: presentation.code, qualityRevision: revision,
                                    sourceFrame: sourceFrame, validDepthFraction: quality.valid_depth_fraction)
    }
}

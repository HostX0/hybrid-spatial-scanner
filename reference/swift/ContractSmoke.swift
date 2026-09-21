// Host type-check only. No ARKit/RoomPlan/iOS behavior is exercised here.
import HSSContracts
func makeConfig() -> HSSConfig {
    var config = HSSConfig()
    config.abi_version = HSS_ABI_VERSION
    config.struct_size = UInt32(MemoryLayout<HSSConfig>.size)
    config.structure_voxel_m = 0.02
    config.detail_voxel_m = 0.01
    config.truncation_m = 0.04
    config.max_working_bytes = 256 * 1024 * 1024
    config.max_pending_frames = 2
    config.deterministic_seed = 101
    return config
}
// Production wrapper: one serial executor; all buffers copied synchronously
// by submit calls. Do not claim pointer-owning wrappers Sendable unchecked.

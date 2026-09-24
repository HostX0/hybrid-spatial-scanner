# B-041 native resource-policy adoption evidence

Date: 2026-09-24

Branch: `hss/i-resource-native`

The iOS owner submits monotonic, ordered thermal, memory-warning, and
important-usage free-storage readings to the additive core ABI. Missing sensor
values are explicitly unmeasured. iOS does not expose a reliable nominal
memory-pressure reading here; absence of a warning is never mapped to nominal.
Unsafe measured heat or storage blocks new metric observations immediately,
stops AR capture, drains queued protected source, then performs the existing
durable store checkpoint before acknowledging success or failure to the core.
The UI shows a localized pause and a save/review recovery route. It does not
auto-resume while complete recovery cannot be proved. Existing source is not
evicted.

Focused `xcodebuild test` on the named iPhone 17 Pro simulator
`2F6A107C-24BF-43F4-B091-6DE5271B48EE`, with isolated derived data
`/tmp/hss-i-resource-native-derived` and code signing disabled, passed 4/4
resource tests. They cover a real source checkpoint after an unsafe sample,
unmeasured memory recovery remaining paused, warning status without invented
nominal memory, prior verified checkpoint reuse at an unchanged revision, and
acknowledgement only after a failed checkpoint attempt. The same run also
passed the empty-draft regression (5/5 total); that small fix is a separate
follow-up commit.

Physical iPhone thermal transitions, actual memory warnings, low-storage,
background/protected-data behavior, and ARSession stop-barrier ordering remain
device verification work. Simulator tests prove bridge and persistence paths,
not real sensor timing.

## Checkpoint-domain correction

The store's durable revision counts committed manifests, while engine scene
revision counts accepted source changes. Native resource handling no longer
compares them. A no-pending checkpoint now revalidates the on-disk manifest
and committed source stream and confirms they match the store's in-memory
accepted sequence/revision. Metadata-only projects still return
`HSS_NOT_READY`; no native cache can turn that into success.

Focused ASan/UBSan host `ctest` in
`/tmp/hss-i-resource-native-host-fix`: `hss_project_store` and
`hss_resource_runtime_integration` both passed (2/2). The store test covers
two accepted events in one durable manifest, idempotent no-pending validation,
reopen validation, and empty-draft rejection. Focused named iPhone 17 Pro
simulator `xcodebuild test` then passed the three affected resource tests
(3/3), including two events/one manifest followed by a pressure checkpoint,
and metadata-only checkpoint failure acknowledgement.

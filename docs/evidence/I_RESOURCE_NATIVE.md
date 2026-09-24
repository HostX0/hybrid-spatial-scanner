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

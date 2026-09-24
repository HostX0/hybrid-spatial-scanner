# Durable replay cursor and native resume evidence

Date: 2026-09-24. Branch: `hss/i-replay-sequence`. Baseline: `5122673`.

The additive `HSSProjectResumeCursor` revalidates the committed source stream
after replay. It reports distinct last event sequence, accepted scene revision,
manifest revision, capture ID, highest epoch and monotonic time. Native reopen
and import bind those values to the replayed engine. They reuse the verified
capture ID, seed the next event above the retained sequence, reserve a fresh
epoch above prior epochs, and leave a resumed world origin unresolved. Imported
catalog entries use the accepted snapshot revision, including when a valid
previous manifest was recovered. A monotonic clock behind retained event time
blocks resumed capture with a localized route.

Focused AppleClang ASan/UBSan host `hss_project_store` passed 1/1 from
`/tmp/hss-i-replay-sequence-host`. Its conflict-audit fixture has last event
sequence 8, accepted scene revision 7 and manifest revision 2; the cursor
reports each separately and append at sequence 9 succeeds. The named iPhone
17 Pro simulator passed 3/3 focused tests in
`/tmp/hss-i-replay-sequence-derived`: imported archive catalog/reopen and
append, fresh unresolved epoch after resume, and existing synthetic replay.
After strengthening cursor state comparison, the host store test and native
import regression passed again (1/1 each). The import test also checks that a
measured frozen revision is eligible against scene revision 4 and rejected
against manifest revision 2.

No physical ARSession or restart test ran. Resumed epoch relationship evidence
and device clock behavior remain device-dependent; the adapter does not infer
an identity transform between capture sessions.

# B-035 native observation feasibility gate

Date: 2026-09-24. Baseline: `8aa4adb04ff23458a38c900c476feb642dd8e377`.

## Decision

Do not construct `dynamic_evidence_verifier::Observation` from the current
native capture, or issue a `HSSDynamicEvidenceBatch` for real capture. Keep a
pending `exclude_dynamic_region` command as a no-effect audit marker. This is
an evidence gap, not a negative result about ARKit's physical performance.

`HSSFrame` retains source identity, sequence, epoch, pose, intrinsics, depth,
and ordinal confidence. `HSSMeshEvent` retains anchor identity, revision,
transform, vertices, and triangles. The project store retains accepted source
events and the runtime provenance mirror retains active mesh revisions and
accepted epoch controls. These are enough to locate *candidate* source points
and reject stale mesh sources. They do not establish that points in different
frames/mesh revisions are the same physical moving target, nor supply a
calibrated absolute 3D error bound including depth, pose, intrinsics, epoch
transform, and mesh estimation. Ordinal depth confidence, fit residuals,
stationary repeats, and a mesh derived from the same capture are not such a
bound or independent confirmations. A mesh update also replaces its previous
live revision; it must not silently count as a separate independent sensor.

The portable verifier requires an exact retained source, shared geometric
correspondence, independent pose/group identities, an accepted versioned
project transform, a bounded source-local point, absolute position uncertainty
at most 0.02 m, correspondence residual at most 0.01 m, and discontinuity
clearance at least 0.03 m plus uncertainty. Its motion gate additionally
requires three observations and two uncertainty-adjusted displacements of at
least 0.05 m. Those numeric thresholds are policy hypotheses, not measured
iPhone accuracy. The current C ABI evidence record carries only the *result*
of verification; passing authored fields to it would be circular.

## Required iPhone trial before implementing the resolver

1. Freeze device/iOS/app/core build, effective one-session ARKit configuration,
   depth stream (`sceneDepth`), intrinsics/crop, orientation, range, material,
   lighting, tracking, epoch-transform revision, and retained source hashes.
   Use independently surveyed 3D targets with recorded instrument calibration
   and reference uncertainty, including off-axis and discontinuity-near points.
2. Record synchronized, unmodified depth frames and mesh lifecycle events with
   exact capture/epoch/sequence, pose, timestamp, and target identity. Measure
   3D point error in project metres across independently reserved tuning and
   held-out runs, including systematic pose/epoch error and source correlation.
   Derive a conservative absolute radial error budget for a *declared operating
   domain*. A finite empirical percentile alone cannot be labeled an absolute
   bound. If the budget cannot justify 0.02 m, retain no-effect masking under
   the current verifier policy; any policy change needs its own review.
3. Use an independently tracked moving target with known 3D trajectory, plus
   static and occluded controls, to validate source-point correspondence,
   matching residual, discontinuity clearance, frame independence, timing,
   and target-relative displacement. Include person, curtain, and screen
   cases only where the reference trajectory/visibility is measurable. Record
   false masks and missed motion separately. Confirm that relocalization,
   anchor replacement/removal, save/reopen, and replay cannot reuse stale
   evidence. Reject any case with unknown association or uncertainty.

Only after those measurements support the bounds should a bounded native
resolver read exact retained sources and accepted epoch state, derive rather
than accept verification fields, call the portable verifier, and submit its
result to the existing B-035 mask path. No ABI, engine, store, or masking code
was changed by this assessment. No iPhone or simulator trial was run.

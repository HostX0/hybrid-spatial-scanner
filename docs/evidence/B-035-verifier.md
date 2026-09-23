# B-035 independently evidenced dynamic-object verifier

Date: 2026-09-24

Branch: `hss/dynamic-evidence-verifier`

Baseline: `ab747c36ea0cd46260895268984276f9853fb847`

## Delivered contract

`hss::dynamic_evidence_verifier::verify` is a portable C++20 gate between
retained calibrated observations and the existing
`dynamic_provenance::VerifiedDynamicEvidence` record. It accepts all input by
const reference and returns no replacement geometry. The raw captured mesh and
source observations remain immutable.

The default policy requires at least three observations and at least two
motion confirmations relative to the exact target source. Each observation
must:

- name the same UUID project, capture, session epoch and correspondence;
- fall inside the pending command's inclusive sequence interval and have
  strictly increasing sequence and monotonic time after canonical sorting;
- name a unique exact retained source, accepted calibrated pose and explicit
  independence group;
- carry normal tracking, accepted calibration, accepted pose, an accepted
  versioned epoch relationship, and accepted retained-source state;
- contain a finite source-local geometric point and a validated column-major
  rigid transform into canonical project metres;
- use calibrated depth or calibrated mesh support whose kind matches the exact
  captured source; semantic-only input is rejected;
- carry a finite positive absolute metric uncertainty no greater than 0.02 m,
  a correspondence residual no greater than 0.01 m, and discontinuity
  clearance of at least 0.03 m plus its uncertainty.

The target source must occur exactly once. Its uncertainty-bounded project
point must lie inside its retained source bounds, and every observation's
uncertainty-bounded project point must lie inside the command bounds. The
verifier computes project points itself from source-local points and rigid
transforms. All observations must use the same accepted epoch-transform
revision. It does not accept a caller-authored project point.

Motion is accepted only when at least two independent observations have a
target-relative displacement lower bound of 0.05 m after subtracting both
absolute position uncertainties and both correspondence residuals. The
time-sorted track must fit a linear
endpoint interpolation with no uncertainty-adjusted residual above 0.02 m and
must span no more than 10 seconds. This deliberately narrow model may reject
real acceleration or direction changes; it prevents a discontinuous or
mismatched track from being called verified. All defaults are explicit policy
hypotheses, not measured sensor accuracy.

Success produces one `kTrackedMultiFrameMotion` evidence record linked to the
request's exact evidence ID, exclusion command, project, retained target
source and target-source bounds. Diagnostics retain the observation count,
motion-confirmation count, minimum uncertainty-adjusted displacement, maximum
uncertainty-adjusted model residual and time span. Every failed gate returns a
specific status with no evidence and no partial diagnostics.

## Negative and deterministic coverage

The focused host test exercises a three-frame constant-velocity track and
checks exact output linkage, diagnostics, observation immutability and
input-order independence. It separately rejects:

- one frame, duplicate pose identity and duplicate independence group;
- semantic-only support and a depth/mesh source-kind mismatch;
- limited tracking, unknown calibration, a non-rigid pose, an unversioned or
  mixed-revision epoch relationship and an unretained source;
- uncertainty above threshold and support whose uncertainty reaches an
  occlusion/discontinuity edge;
- the wrong project, capture, epoch or command sequence, and a target source
  absent from the retained observations;
- mixed correspondence identity, excessive geometric match residual, a track
  that violates the motion model, and displacement below the uncertainty-
  adjusted motion threshold.

## AppleClang sanitizer evidence

The final focused run used AppleClang 21.0.0.21000334 and CMake 3.31.6 with
AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```sh
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B /tmp/hss-dynamic-evidence-asan \
  -DHSS_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build /tmp/hss-dynamic-evidence-asan \
  --target hss_dynamic_evidence_verifier_test --parallel
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir /tmp/hss-dynamic-evidence-asan \
  -R '^hss_dynamic_evidence_verifier$' --output-on-failure
git diff --check
```

Observed final result: configuration and focused build exited 0 with warnings
treated as errors. CTest passed 1/1 under ASan and UBSan in 0.59 seconds.
The first focused execution exposed one bad negative-test fixture: its synthetic
point was exactly on the exclusion boundary, so the required uncertainty margin
correctly returned `invalid_observation` before the intended unstable-track
gate. The fixture was moved 0.01 m inside the bound while retaining a residual
above threshold; the final run then exercised `unstable_correspondence` and
passed. No production threshold was relaxed.

## Integration boundary and physical limits

No `hss_engine` or C ABI integration is claimed. The current live runtime and
`HSSDynamicEvidence` input do not expose enough evidence to call this verifier:
they do not jointly retain or resolve a source-local correspondence point,
accepted calibrated project transform and pose identity, accepted epoch
transform revision, absolute metric uncertainty including pose/calibration
error, geometric correspondence residual, occlusion-edge clearance, and an
independence-group identity. Constructing those fields from a semantic label,
mask, caller Boolean, repeated smoothed depth, or the existing already-
verified ABI record would be circular and is intentionally unsupported.

The missing native contract is a bounded retained-observation resolver that
derives these fields from exact durable depth/mesh records, validates current
source lineage and accepted epoch transforms, and supplies calibrated absolute
uncertainty from physical reference trials. Only after that resolver exists
can the produced `VerifiedDynamicEvidence` be passed to the existing B-035
mask/runtime path without weakening its meaning.

The test uses authored synthetic transforms and points. It does not establish
sceneDepth accuracy, pose accuracy, independence of real frames, calibrated
uncertainty, correspondence quality, occlusion behavior, person/curtain/screen
detection, false-positive/false-negative rates, device performance, memory,
thermal behavior, iOS integration or physical dynamic-object verification.
No simulator or iPhone run was performed. The constant-velocity acceptance
gate is a conservative proof filter for supplied evidence, not a detector,
tracker, semantic classifier or physical metrology result.

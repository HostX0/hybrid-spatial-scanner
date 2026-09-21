# B-045 — iPhone physical metrology and acceptance protocol

Status: protocol only. No iPhone capture, physical measurement, or acceptance result has been executed.

This protocol is the later physical acceptance procedure for an iPhone 15 Pro Max. It is derived from `docs/D17_UX_Delivery.md` §12, the ENG-13 work-pack, `D14`–`D17`, and the existing B-003/B-004 evidence limits. It does not modify code, ABI, schemas, or baseline fixtures. The product must reach the appropriate capture/export milestone before a user runs this protocol.

## 1. Scope and evidence rules

Run only on the declared iPhone 15 Pro Max and a signed build. Record app, core, and tuning identifiers exactly; a simulator, host reference test, synthetic mesh, screenshot, or marketing claim is not physical evidence.

Keep two scene sets immutable after reservation:

| Set | Use | Rule |
| --- | --- | --- |
| Tuning/development | Select declared parameters and rehearse the procedure | May inform tuning; record every parameter change and source scene. |
| Held-out acceptance | Estimate final performance | Reserve scene IDs, references, and endpoints before tuning; do not inspect results to fit parameters or thresholds. |

Do not pool held-out results back into tuning. If a procedure, endpoint, material condition, or device/build changes, mark the run invalid or create a new declared run; do not silently replace it. Report the tested domain and limitations, not a universal accuracy claim.

## 2. Scene set and required geometry

Reserve at least one tuning and one held-out instance of every applicable scene family. Use multiple instances where the product claim depends on a domain rather than a single room. Photograph and sketch each scene before capture, without exposing held-out measurements to tuning.

| Scene family | Required construction/feature | Required observations |
| --- | --- | --- |
| S01 baseline | Rectangular room with accessible finished wall faces and floor junctions | Four wall endpoints, diagonals, height or known plane, net area. |
| S02 non-orthogonal | At least one acute and one obtuse corner; no Manhattan assumption | Each wall length, surveyed/triangulated corner angle, closure. |
| S03 narrow rib/column | Narrow projecting rib or column with two visible side faces and a short return | Width, depth, side-face presence, false/missed feature status. |
| S04 openings | Door/window or architectural cut with a real boundary and return | Opening endpoints, width/height/depth as applicable, hole/void retention. |
| S05 low relief | Low-bump/trim or shallow recess near the stated product domain | Local height/width, visibility and surface completeness; do not accept area-only success. |
| S06 mixed/occluded | Mixed materials, furniture/partial occlusion, and a view that requires repositioning | Requested viewpoints, interventions, gaps, relocalization/epoch handling. |

Record which families are not applicable and why. A missing narrow face, opening, rib, or low-relief surface is a feature failure even if total area is within tolerance.

## 3. Reference method and measurement definition

Before capture, assign a stable scene ID and mark endpoints with removable, non-ambiguous targets. Define every dimension as one of: finished wall face to finished wall face; floor junction to floor junction; architectural cut boundary; feature crest/valley; or another named local surface. Do not mix trim, skirting, temporary targets, or unfinished edges into a nominal endpoint.

Use an independent reference method appropriate to the scene (for example, calibrated total station/survey, calibrated photogrammetry, or a verified laser/tape combination). Record instrument identity, calibration/verification date, operator, method, endpoint visibility, and uncertainty. A consumer tape or laser is not automatically ground truth. Nonorthogonal scenes require enough independent distances or surveyed coordinates to constrain the angle and closure; two wall lengths are insufficient.

For every reference dimension, take at least three readings without changing the endpoint definition. Record each reading, mean/median rule, spread, and reference uncertainty `u_ref`; if an endpoint is inaccessible or unsafe, record that limitation and do not invent a value. Use SI metres and degrees. Preserve the reference sketch/coordinate convention with the run record.

For a product estimate `x̂` and reference `x_ref`, record signed error `e = x̂ − x_ref`, absolute error `|e|`, and the applicable predeclared tolerance. When uncertainty propagation is justified, record the stated combined uncertainty and method; never turn an observed spread into a hidden tolerance.

## 4. Capture procedure

1. Create the run record and verify the reserved set, scene ID, build identifiers, device, operator, lighting/material condition, reference method, and predeclared tolerance.
2. Check permission/support/readiness. Record camera permission, world tracking, `sceneDepth`, mesh reconstruction, available storage, and the app's readiness state. Stop and mark blocked if a required capability is absent.
3. Confirm that the capture owner uses one `ARSession`. Record requested and effective configuration, including `sceneDepth` (not `smoothedSceneDepth` as independent metric evidence), depth intrinsics, depth crop/region, camera orientation, and portrait/landscape state.
4. Capture each scene using the documented workflow. Record start/end time, duration, pauses, relocalization, epoch changes, operator interventions, requested extra views, and any manual correction. Do not restart merely to hide a failure; if a restart is required, create a new run and preserve the first record.
5. Check mesh-anchor add/update/remove behavior and any mesh replacement/rebuild events. Record whether stale precision is hidden during limited tracking/relocalization and whether overlays follow orientation and the accepted epoch.
6. Save, close/reopen, interrupt (call/background/permission or app interruption as applicable), and recover according to the product milestone. Record storage pressure and whether the same revision, epochs, warnings, and geometry survive.
7. Export offline without network dependency. Retain the source revision, exported units/axis metadata, warnings, and a checksum or filename. Do not upload room scans.
8. Compare only after capture and after the held-out boundary is locked. Complete the metric tables below; assign pass/fail per metric, not only a scene-level impression.

Thermal and accessibility checks are later gates unless the product milestone explicitly includes them. When executed, record thermal state/duration and VoiceOver, Dynamic Type/large text, contrast, touch-target, haptic-independent feedback, rotation, and RTL observations separately from geometry accuracy.

## 5. Acceptance metrics

Use the predeclared tolerance for the tested domain. A blank value is `not measured`, not pass.

| Family | Metric | Minimum record | Pass rule |
| --- | --- | --- | --- |
| Accuracy | wall/feature length, width, depth, height | `x_ref`, `x̂`, signed/absolute error, `u_ref`, tolerance | `|e|` within declared tolerance and endpoint definition valid. |
| Accuracy | non-90° corner angle | reference angle, estimated angle, error, uncertainty | Error within declared angular tolerance. |
| Repeatability | same scene repeated under the same declared condition | run IDs, mean, SD/range, operator/intervention | Spread within declared repeatability limit; does not replace accuracy. |
| Closure | boundary loop closure | endpoint residual/vector and accepted epoch | Residual within declared closure tolerance; no silent gap or forced join. |
| Area | gross/net area, with holes excluded/included explicitly | boundary convention, reference area, estimate, error | Area rule and tolerance pass; never substitutes for feature checks. |
| Errors | false features, false boundaries, spurious surfaces | ID, location, severity, evidence | Zero or within predeclared severity/count limit. |
| Features | missed ribs, narrow faces, openings, low relief | expected feature inventory and detection status | Every required protected feature detected with required faces/boundaries. |
| Surface completeness | visible target surfaces retained | target surface inventory, occlusion/visibility reason, gaps | Required visible surfaces present; unexplained omission fails. |

For each row record `pass`, `fail`, `blocked`, or `not applicable` plus the reason. A blocked hardware/readiness prerequisite is not a pass and must not be converted into a numerical result.

## 6. Field/system acceptance checks

These checks accompany the metric tables and remain distinct from accuracy:

| Check | Evidence to retain | Result |
| --- | --- | --- |
| Permission/support/readiness | permission state, support report, storage, readiness transition | blank until run |
| One-session ownership | session owner/identity and configuration transitions | blank until run |
| Depth/calibration geometry | `sceneDepth`, intrinsics matched to depth image, crop, off-axis framing, orientation | blank until run |
| Mesh lifecycle | anchor add/update/remove and replacement/rebuild records | blank until run |
| Epoch/relocalization | epoch IDs, accepted transforms, pause/resume and stale-overlay behavior | blank until run |
| Save/reopen/interruption/storage | revision, recovery log, storage condition, retained warnings | blank until run |
| Overlay | portrait/landscape alignment, current epoch, hidden stale precision | blank until run |
| Offline export | file, units/axis, source revision, warnings/checksum, no network dependency | blank until run |
| Thermal/accessibility (later) | thermal duration/state and accessibility observations | not scheduled in this protocol run |

## 7. Copyable run template

Copy this block once per scene/run; keep the original reference record with it.

```text
B-045 RUN
Run ID / date-time / status (planned|valid|invalid|blocked):
Scene set (tuning|held-out) / scene ID / family:
Scene reservation locked before tuning? (yes/no):
Device: iPhone 15 Pro Max / iOS build:
App version/commit:     Core version/commit:     Tuning profile/commit:
Operator / reviewer:
Lighting / material / temperature / room notes:
Reference instrument + calibration/verification / method:
Reference coordinate convention and endpoint definitions:
Reference readings (>=3 each), aggregation rule, u_ref, inaccessible endpoints:
Declared tolerance(s), repeatability limit, feature inventory:

READINESS AND CAPTURE
Camera permission / world tracking / sceneDepth / mesh / storage / readiness:
One ARSession owner/identity:     Requested vs effective configuration:
Depth stream (sceneDepth only for metric evidence):
Depth intrinsics / crop / off-axis framing / orientation:
Start / end / duration / pauses / interventions / extra views:
Epoch IDs / relocalization / tracking limitations:
Mesh add/update/remove/replacement events:
Manual corrections (what, why, when):

METRICS (one row per metric; add rows as needed)
Metric | endpoint/feature | x_ref | x_hat | signed error | abs error | u_ref | tolerance | pass/fail/block | note

Repeatability run IDs / readings / mean / SD or range / pass-fail:
Closure residual/vector / rule / pass-fail:
Area convention / reference / estimate / error / pass-fail:
False features (IDs/location/severity):
Missed features (IDs/location/severity):
Surface completeness / omitted surfaces and reason:

RECOVERY, OVERLAY, EXPORT
Save/reopen result and source revision:
Interruption/background/low-storage result:
Overlay orientation/epoch/relocalization result:
Offline export path/name/checksum, units/axis, warnings:
Thermal/accessibility observations (if later gate is enabled):

Overall run disposition and reasons:
Evidence paths / reviewer sign-off:
Limits, deviations, and follow-up:
```

## 8. Reporting boundary

The final report must state device, OS, app/core/tuning versions, operator, scene/material domain, reference uncertainty, capture duration/interventions, all metric-level dispositions, and what was not tested. It may claim only the tested iPhone 15 Pro Max/domain and declared workflow. It must not claim that this protocol has been executed, that a physical gate passed, or that the product is generally accurate until the retained device evidence supports that claim.

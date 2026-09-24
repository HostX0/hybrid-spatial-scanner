# ENG-PLANE-SCORING-01 — Exact plane-candidate scoring

Date: 2026-09-23. Target: existing structural plane-fitting dependency in D15 section 9, without claiming completion of an original backlog acceptance gate. This is an independently named engineering slice; the original task ID must be checked before changing backlog status.

## Source and delivery identity

Base commit: `598b43d4901f2c5f1e5886d80fea6373ddf737a0`.
Baseline fitter blob: `1ed46d88b6a372ae58a9b0bcb031a5b19bcc0577`.
Tested candidate fitter blob: `d2c077283761082dc4817f822c70fa0a6f26328d`.
Public header unchanged: `35735ffc667c45357bef2023c28875ff46219dd1`.
Original seven-scenario unit test unchanged: `9af5adc580f2fb58669bdaeaac8b93419ef44164`.
New probe blob: `40e2cf8cc2dbfa3590d7a315c6cc7ad44d3c391c`.
Delivery branch: `hss/plane-fit-exact-scoring`. The containing Git commit/PR is the publication record; no self-referential commit SHA is invented here.

## Implemented behavior and why it is safe

The production fitter previously sorted coordinates/groups and solved an unweighted covariance eigensystem for every supported RANSAC hypothesis, including hypotheses with exactly the same ordered inliers. `live_reconstruction.cpp` calls this fitter for mesh patches, with at most 96 samples and 24 candidate iterations at this base.

The implementation reuses the current best candidate's distinct support count, group diversity and minor covariance variance only when the ordered inlier index vector is exactly equal. It does not cache or reuse the hypothesis normal, offset or residual median. It also omits expensive extent scoring when a candidate provably loses on the existing lexicographic ranking prefix. Distinct count is bounded by sample count, but those counts are never treated as equivalent. Tied prefixes still evaluate covariance, residual and canonical-plane tie-breaks.

Inductively, these changes preserve the selected best candidate: an equal support vector supplies the same support-only arithmetic inputs in the same order; a strictly worse ranking prefix cannot win on later fields. Pruning is only enabled after a valid best exists, so skipped rejected-candidate flags cannot affect the no-best failure result. Weighted refinement and all its failure paths remain untouched.

No public API, C ABI, JSON schema, random generator, iteration count, residual counter, source data, tuning threshold, geometry prior, minimum support, rank check or acceptance behavior changes. No third-party implementation was copied or dependency added. Extra persistent cache allocation: none; the already-owned best candidate is reused. No new cloud or network dependency.

## Research decision

Reviewed current primary documentation on 2026-09-23:

- Open3D tensor PointCloud plane segmentation: https://www.open3d.org/docs/latest/tutorial/t_geometry/pointcloud.html#plane-segmentation. It distinguishes residual threshold, sample size, iteration budget and success probability; it documents fixed-budget behavior for reproducibility. Decision: retain this engine's fixed draws rather than introduce probability-based early termination in a parity-preserving change. Open3D was a design comparator, not installed, benchmarked or linked into the app.
- PCL sample-consensus module: https://pointclouds.org/documentation/group__sample__consensus.html. It distinguishes RANSAC, MSAC, MLESAC, PROSAC and randomized variants. Decision: changing the estimator/ranking would require a new accuracy experiment, so this slice optimizes the existing ranking instead. PCL code was not imported.
- Apple ARDepthData/confidence documentation: https://developer.apple.com/documentation/arkit/ardepthdata/confidencemap. The platform provides per-pixel confidence levels. This slice does not reinterpret confidence as a calibrated probability or change depth eligibility. Depth eligibility remains a separate research/implementation dependency.

These are inspected sources, not a claim that all open-source engines/articles were audited. No claim of algorithmic novelty, competitor superiority or sensor accuracy follows from this optimization.

## Reproduction

From a full checkout with the pinned base commit in local history:

```sh
python3 tools/verify_plane_fit_scoring.py --cxx clang++ --report /tmp/hss-plane-release.json
python3 tools/verify_plane_fit_scoring.py --cxx clang++ --sanitize --report /tmp/hss-plane-sanitized.json
python3 tools/verify_plane_fit_scoring.py --cxx g++ --skip-benchmarks --report /tmp/hss-plane-gcc.json
```

The actual local runs used the same commands with `--baseline-source baseline/robust_plane_fit.cpp` because this environment held a targeted source snapshot rather than a full clone. The runner verified this file against the pinned Git blob hash before compilation. It similarly verifies the unchanged public header. The actual reports are retained in `ENG-PLANE-SCORING-01.json`.

Clang 17.0.0 and GCC 14.2.0; Linux x86_64 / glibc 2.41. Release flags: `-std=c++20 -Wall -Wextra -Werror -O3`; sanitizer flags replace `-O3` with `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`. No fast-math.

All three configurations passed the original seven-scenario unit suite against both baseline and candidate, plus 408 exact differential cases (17 families x 24 seeds), with zero mismatches. These are 408 unique cases repeated across configurations, not 1,224 independent scenes. The probe compares every public double as its exact bit representation, the full inlier index vector, support/diversity fields, failure status and work counters. The serialized output SHA-256 is `d588c909ebe12c9fc7d41f54d68e4b0e3af153bdc1adc76a1000e0286c916c87` in each observed run.

The fixtures include exact mesh reuse, weighted oblique noise, outliers, equal-size parallel planes with diversity ties, duplicate-heavy competing support, near-collinearity, repeated points, threshold-boundary support changes, translated coordinates, wide weight ranges, invalid input/options, insufficient evidence, work caps, nonconvergence and the live adapter's exact fitter options. 212 fixtures returned a plane and 196 returned matching fail-closed outcomes; these intentional rejection cases are not failed tests. The probe is not exhaustive for every possible numerical-failure input.

## Measured host microbenchmark

Clang release; one warmup and eight fits per sample; five samples per binary with alternating execution order. Table uses medians, including all samples. Raw samples are retained; the small live case includes a visible scheduling outlier. Timing has no pass/fail threshold.

| Synthetic workload | Baseline ms/fit | Candidate ms/fit | Ratio |
| --- | ---: | ---: | ---: |
| Live-config mesh patch: 96 samples, 24 candidates | 0.0804899 | 0.0315814 | 2.55x |
| Weighted oblique surface plus outliers: 4096 samples, 200 candidates | 29.7415 | 10.8503 | 2.74x |
| Unstructured clutter: 4096 samples, 200 candidates | 12.3241 | 8.45748 | 1.46x |

The clutter workload returns refinement nonconvergence in both versions; its timing measures identical rejected work, not successful reconstruction. The larger workload is a fitter stress case, not the live patch size. These are host function timings, NOT whole-app speedups, Apple CPU/GPU timings, energy/thermal results, real-room precision or competitor benchmarks.

## Encountered failures and repair

The first duplicate-heavy fixture assumed 200 random hypotheses always discovered a minority plane. The unchanged baseline disproved that assumption. The ranking-focused fixture now explicitly uses 2048 hypotheses within existing caps, without changing production defaults or acceptance thresholds. GCC then rejected ambiguous test-driver indentation under `-Werror`; explicit loop braces repaired the test source. Both repairs were rerun on final source. No failed check was skipped or warning disabled.

## Changed files and gates

`SpatialCore/src/robust_plane_fit.cpp` is the only changed production file. New files are the probe, offline verification runner, scoped GitHub Actions workflow, this note and its raw JSON evidence. Existing CMake tests are untouched; the new differential runner is separate from CTest and wired into a narrowly scoped workflow with read-only checkout permissions. The baseline is deliberately pinned; future intentional fitter semantics/API changes must review and explicitly evolve this parity contract, not silently reset it.

Local evidence does not establish hosted workflow success. Check the actual PR check run after publication. Full CMake/CTest, Xcode, simulator, physical iPhone/LiDAR/Metal and end-to-end scan/export tests were not executed in this environment. No original B/ENG production or hardware acceptance item is marked complete.

## Next hourly checkpoint

Read this PR and current main before doing new work. First resolve hosted/integration check results without repeating unchanged successful microbenchmarks. Next high-value inspection: `live_reconstruction.cpp` patch adjacency/normal/offset grouping, with a synthetic narrow-return and slightly noisy connected-wall replay regression. Its current near-identical-normal gate and grouping limits need evidence before any tuning; do not merge separate parallel faces or delete protected topology to increase fit rate. Then evaluate depth eligibility and resource accounting against retained sources. Do not implement ungated TSDF/AI replacement as a shortcut.

Hourly work is scheduled 48 times from 2026-09-23 18:40 through 2026-09-25 17:40 Asia/Baghdad. Do not reset the timer, duplicate tasks or claim a successful code delivery merely because a scheduled run started. Record actual revisions/tests and stop after the final round.

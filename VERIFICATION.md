# Verification report

## Executed host checks

The retained run completed 99 Python/pytest cases; two native CTest executables passed with Clang address/undefined sanitizers enabled; and Swift successfully type-checked import/config initialization from the C header. Read the exact environment in verification/environment.json and raw results in verification/. CMake and Swift compile paths are host x86_64 Linux, not iOS.

The initial Python run produced 98 passes and one failure. The oblique fixture's expected area was incorrectly authored as 14 m²; independent calculation gives 15 m². The expectation was corrected and the original failure log retained. No physical benchmark threshold was changed. See verification/REPAIRS.md.

## What these checks cover

Selected projection and rigid-transform conventions; valid/invalid polygon rings and holes; small oblique-plane fits with outliers; local distance/lock equality feasibility and contradictory constraints; TSDF eligibility/unknown/saturation rules; anchor replacement; schema shapes and command examples; counter/path/frame validation; and a POSIX commit/recovery reference with injected failures.

## What remains unexecuted

Production scanner-function linkage and runtime ownership; iOS ARM64 build; ARSession/RoomPlan coexistence; sensor depth convention on hardware; Metal compilation/execution/parity; production meshing, texture pipeline and general solver; device memory/thermal/interruptions; actual LiDAR accuracy; external CAD consumer compatibility; and competitor/user benchmarks. The wireframes and expected exports are authored examples, not application screenshots/results.

## Reproduce without overwriting evidence

Use tools/run_verification.sh. It writes a new verification_runs directory and fails on actual command/test failures. Version or platform changes may expose new failures; record them rather than editing expected results without an independent rationale. A missing Swift toolchain is explicitly Not run. Never map these host passes directly to completed physical Cases in S02.

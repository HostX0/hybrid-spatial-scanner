# ENG-READINESS-SCHEDULING-ATOMICITY-01

## Purpose and scope

Two demonstrated blockers to practical capture/editing are repaired on the existing PR #18 integration line: anchor-sized ambiguity/cleanup work hidden inside one reconstruction quantum, and a failed scene publication incorrectly remembered as an applied command on exact retry. No new scanner, UI mode, cloud service, C ABI or Scene schema is introduced.

Base: `40025b198485457754e6bbeaf57df4f9191286a2`.
An isolated exact export in `/home/abdulazeez/hss-work/readiness-40025b1.hDZiY7/source` was checked against all 457 Git blobs before editing: zero mismatches. The original dirty server checkout and its separate unfinished changes were not overwritten.

## Cooperative ambiguity and retirement

The published base marked all roots sharing a canonical key in one stage-4 call, then cleared anchor-sized maps during finalization. The new path marks one ambiguous root per `advance()` and retires one map entry per cleanup quantum. Per-component sample maps remain capped at 96. Raw triangle copying, derived identities, normal/distance thresholds and accepted geometry are unchanged. Normal successful completion releases the large scratch maps incrementally; cancellation/destruction and allocator latency are not advertised as hard-real-time guarantees.

A deterministic regression drives the actual internal `Reconstruction::advance()` boundary with 2,048 identical indexed triangles. It requires separate scan, accumulation, indexing, ambiguity marking, slice exclusion and retirement work, no partial snapshot publication, exact retained indices and a valid output Scene with no invented fitted plane.

- Before repair: 8,223 advance calls, regression FAIL at the minimum separate-work assertion.
- After repair: 12,324 advance calls, regression PASS. More yielded calls here are the intended responsiveness change, not a throughput-speedup claim.
- Existing public-C-ABI checks still cover 16 permutations per protected narrow-return, noisy wall, disconnected and parallel-offset fixture; raw vertex/index preservation; multi-anchor issue uniqueness; and identical final geometry under 1 us/50 us requested step budgets.
- After repair, the 8,192-triangle host fixture observed 70,369 calls/p95 70.382 us/max 6,829.04 us for requested 1 us, and 23,845 calls/p95 75.351 us/max 7,018.12 us for requested 50 us. Single Linux ASan observations only, not iPhone latency, hard deadlines or a comparative speed claim.

## Command publication atomicity

The base committed its volatile command gate/journal before attempting immutable scene publication. With `max_working_bytes=4096`, the first zero-target `mark_unknown` returned `HSS_RESOURCE_LIMIT` but its byte-identical retry returned `HSS_OK` via a stored result although the scene had never changed. The actual C-ABI regression reproduced `first=10, exact_retry=0` and failed before the repair.

The repaired path stages command-result bytes until scene/pending state publication succeeds. On publication, count-validation, result-size or allocation failure, it aborts only the exact latest volatile command record, matched by identity, digest, outcome and revision. Older acknowledged records and durable history are not rewritten. The processor stages fallible journal/pending allocations before semantic commit. The lower-level gate also finishes record/result allocations before changing its accepted scene. Static assertions enforce the non-throwing move assumptions used after allocation. Pending-exclusion map insertion is similarly committed before exposing its result.

Actual C-ABI tests now observe `first=10, exact_retry=10`, retain scene revision zero and accepted-command count zero, expose no phantom result, and preserve a previously exposed pending-command result in a second case. Gate tests reject wrong-digest/outcome/revision and non-latest aborts, retain older records, and prove an exact retry executes the callback again after a valid abort. This is publication/budget failure coverage, not exhaustive allocator fault injection at every instruction.

This slice does NOT implement source-after-command retained intent or generic target-lineage rebasing. Those known separate gaps remain open; the historical larger Mac candidate is not silently substituted for this exact Linux source.

## Exact tested file identities

- `SpatialCore/include/hss/revisioned_command.hpp`: `e1d32fde24aebc08adb2608296e87efd7ac59650`
- `SpatialCore/include/hss/scene_editing.hpp`: `49247ae9678993c17476e17da9df751cb5ff0f30`
- `SpatialCore/src/hss_engine.cpp`: `f0b838f3ab9d9918be20cc859b3a12e90c66bcf3`
- `SpatialCore/src/live_reconstruction.cpp`: `752ca39e23b08f73f624482db506d40f79f3658a`
- `SpatialCore/src/revisioned_command.cpp`: `5a4053444d1aea6f357bd6b396e3afa7ec15658c`
- `SpatialCore/src/scene_editing.cpp`: `011fcaef14c94823e5819d5b73150366d93ef733`
- `tests/hss_engine_scene_editing_adapter_test.cpp`: `f4bdfee137d852f6dd5f5771bed9384b21a1dd26`
- `tests/hss_live_permutation_characterization_test.cpp`: `02d91ce2c80ea30747f8218d1c7192ff6433f1a3`
- `tests/hss_revisioned_command_test.cpp`: `719a9ddaa99c244cf38aeaa261bc5b7df64596e2`

## Executed final verification

Linux x86_64 dedicated server; GCC 13.3.0; CMake 3.31.6; C++20; root warnings-as-errors; ASan+UBSan; local ZLIB 1.3. The real root build was used, not a replacement harness. Compile commands were checked for Werror and sanitizer flags and for the absence of `NDEBUG` in the affected implementations/tests.

```sh
ROOT=/home/abdulazeez/hss-work
RUN=$ROOT/readiness-40025b1.hDZiY7
$ROOT/tools/bin/cmake -S "$RUN/source" -B "$RUN/build" \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZLIB_INCLUDE_DIR="$ROOT/deps/zlib/usr/include" \
  -DZLIB_LIBRARY="$ROOT/deps/zlib/usr/lib/x86_64-linux-gnu/libz.a"
$ROOT/tools/bin/cmake --build "$RUN/build" --parallel 2
$ROOT/tools/bin/ctest --test-dir "$RUN/build" -N
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  $ROOT/tools/bin/ctest --test-dir "$RUN/build" --output-on-failure \
  --no-tests=error --timeout 180 --output-junit "$RUN/final-tests.xml"
```

Final complete suite after all code and test changes: **54 passed, 0 failed, 0 skipped**, 8.54 seconds. JUnit was parsed independently. No sanitizer diagnostic was observed. The unchanged test count reflects expanded existing regressions, not omitted tests. Baseline failures, targeted results, build logs and the exact change manifest remain in the isolated server workspace.

Primary C++ requirements consulted for the implementation: associative-container erase/exception rules at https://eel.is/c++draft/associative.reqmts and vector modification exception guarantees at https://eel.is/c++draft/vector.modifiers . No third-party source was copied and no runtime dependency added.

## Remaining readiness

Do not label this a physical-device release or complete PR acceptance. Source refinement must preserve accepted user intent safely; targeted edits still need evidence-backed lineage handling. Native iOS packaging/tests on the resulting tree and actual LiDAR/metrology remain separate. The three requested modes, realistic challenge corpus and offline camera coaching remain staged requirements, not implementations supplied by this patch. Preserve failed-operation honesty and measured/source-derived separation throughout the next milestone.

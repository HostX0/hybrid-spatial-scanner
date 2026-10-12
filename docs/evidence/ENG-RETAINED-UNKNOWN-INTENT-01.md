# ENG-RETAINED-UNKNOWN-INTENT-01

## Product failure and scope

Continue scanning after marking an unknown area without silently losing that accepted note. This is a narrow production repair on PR #18, not generic targeted-geometry rebasing or a new scanning mode.

Base commit: `4620513bab90f630485aa53b459cbc0e05017902`.
Isolated server workspace: `/home/abdulazeez/hss-work/intent-4620513.s21trj5i`.
The initial source export matched all 458 published Git blobs. Original dirty server and Mac worktrees were not modified. The previous partial local overlay was not copied as if it were published or tested.

A tests-first extension of the actual C-ABI/project-store adapter submits and persists: epoch -> mesh -> floor reference -> pending exclusion -> applied zero-target mark_unknown -> replacement mesh. On the published base, CTest exits 8 at `accepted unknown intent survives source rebuild`. The raw replacement is present but the accepted annotation disappears. This failure is preserved in `baseline-regression.log`.

## Implemented behavior

The scene-editing processor creates an immutable retained payload only from validated, applied zero-target `mark_unknown` commands. It matches complete command-derived feature/issue identities and shapes against the actual candidate, rather than treating all unknown-looking entities as user intent. The exact journal command/result remains the audit authority. Candidate membership handles the existing strict Undo/Redo path: removed entities do not reappear.

Protection applied specifically to such a command-owned, nonmetric unknown feature remains supported: only exact original topology-protection commands and constraints targeting that stable annotation are retained. This is not evidence of lineage for measured walls/nodes/surfaces. Other targeted geometric edits are deliberately not rebased or redirected.

The immutable Snapshot owns a shared retained payload and its budget reservation. Subsequent source rebuilds share this payload in constant-time bookkeeping rather than copying/replaying the complete command journal during an engine quantum. New scene output is charged separately. Features, annotation-owned protection constraints and issues are emitted one record per serialization quantum into the existing full Scene; current source epochs, geometry, derived regions and issues are not replaced by a minimal oracle. Retained issue IDs/reasons/source sequences stay unchanged, while each issue receives the new source scene revision. No extra unjournaled scene revision is published.

The bounded path admits at most 256 retained unknown notes and 256 associated protection constraints. Exceeding retention capacity returns resource-limit without recording a phantom result or consuming a command identity; no partial retention is accepted. An Undo may release capacity. Overlay allocation/reservation failure participates in the existing publication rollback. Failed/malformed source input retains the previous immutable scene and exact command result.

No public C ABI layout, schema version, geometric threshold, source coordinates or raw triangle processing changed. No cloud service or third-party runtime dependency was added.

## Executed regressions

Expanded the existing `hss_engine_scene_editing_adapter_test` rather than creating placeholder targets:

- Actual command -> mesh replacement -> checkpoint/reopen/replay of six events, complete final Scene/result byte equality, fresh raw vertex values, one retained note and issue, and valid new typed plan after source rebuild.
- Two notes with escaped quotes, backslash/newline and Arabic text; strict Undo/Redo before refinement; annotation-owned protection; epoch/mesh/plan rebuilds; no duplicated features/issues/constraints; current issue revision; unchanged earlier pinned snapshot and exact command result.
- Malformed replacement transform rejection preserves current geometry and notes.
- A budget that fits the edited Scene but not the retained payload fails safely on both first application and exact retry, without exposing an applied-looking result.
- Capacity accepts 256 notes, refuses an additional note without a journal/result mutation, then accepts a valid resubmission after Undo frees a slot. No tests were skipped to avoid this stress case.

Final actual root configure/build/CTest used Linux x86_64, GCC 13.3.0, CMake 3.31.6, C++20, Werror, assertions active and ASan+UBSan. Affected compile commands were inspected. Local ZLIB 1.3 is the existing build dependency.

Final complete result after all implementation and test changes: **54 passed / 0 failed / 0 skipped**, CTest real time **85.02 s**. The added capacity stress is inside an existing test, so unchanged test count does not mean unchanged coverage. This is not an app latency measurement. Each subprocess exit code was recorded as zero and JUnit was parsed independently. No sanitizer diagnostic was observed.

An intermediate wrapper reported exit 1 despite printing 54 passes; that intermediate invocation is not used as final success evidence. The final explicit subprocess supervisor and parsed JUnit provide the results above. A bounded-capacity trial before candidate entity indexing also passed; single-run times are not claimed as a performance comparison.

Reproduction (ROOT=/home/abdulazeez/hss-work, RUN=$ROOT/intent-4620513.s21trj5i):

```sh
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

## Exact tested source identities

- `SpatialCore/include/hss/live_reconstruction.hpp`: `0b1cb581ca9e9f8dd7b7e5a845001ffb2d2981bf`
- `SpatialCore/include/hss/scene_editing.hpp`: `7e75ae5fcb6a059a249f941c53218d9cbaa291aa`
- `SpatialCore/src/hss_engine.cpp`: `35599ba31ea050249a4e64c832c76d3cd5eea330`
- `SpatialCore/src/live_reconstruction.cpp`: `b31b05325efbfd448ebea19b2a906086db5dd192`
- `SpatialCore/src/scene_editing.cpp`: `66570cf17ffc2a4b4a547001b3c88c9992fee63d`
- `tests/hss_engine_scene_editing_adapter_test.cpp`: `e0852f3f3efb1fa857d0fe2bb3d27a415c0f3ad9`

## Research decision and remaining gates

The C++ standard draft's shared-ownership assignment requirements specify nonthrowing shared_ptr assignment: https://eel.is/c++draft/util.smartptr.shared.assign . This informed attaching immutable retained payload ownership to the existing snapshot instead of adding a mutable post-publication sidecar. No third-party source was copied. An attempted Apple documentation read did not yield usable API text and is not counted as implementation evidence.

Targeted metric edits still need explicit stable lineage/conflict handling when source geometry changes; this patch does not claim that problem is solved. Undo/Redo after an intervening source refinement still follows the existing strict snapshot-lineage checks and may reject; only pre-refinement reversal and the safe annotation/protection retention above were validated. Native iOS build, signing/install, physical LiDAR/metrology, device latency/thermal behavior and competitive ranking remain unverified for this head. PR #18 stays draft until its actual remaining acceptance gates are resolved. The three capture modes, offline camera coach and realistic held-out corpus remain staged requirements, not features added here.

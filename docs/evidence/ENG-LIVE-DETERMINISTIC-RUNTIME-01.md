# ENG-LIVE-DETERMINISTIC-RUNTIME-01

## Scope and exact source

This is a production-path reconstruction slice on PR #18, not a replacement engine or a claim of complete application acceptance. It replaces arrival-ordered patch grouping in `SpatialCore/src/live_reconstruction.cpp` with anchor-local indexed-edge components, deterministic bounded sample selection and canonical triangle-ordered slicing. It preserves raw vertex values, submitted triangle order (with the established per-anchor global rebase), existing coordinate transforms, public ABI, Scene 1.1 schema, offline behavior and existing fitting thresholds. The normal/distance policy is the explicit symmetric shared-edge policy; it does not union disconnected faces or use non-manifold edges as bridges.

Base commit: `f3bb91c9b3ecb7c3cfe117028c311d5d07fb4101`.
Base tree: `8fc0eddd5bc9bddd107288aa0601b27f3057165c`.

The following exact Git blobs were obtained with `git hash-object` from the tested server source and independently matched the GitHub `create_blob` responses before publication:

- `CMakeLists.txt`: `4233571e0243512081ef3a3951882264a4300ae5`
- `SpatialCore/src/live_reconstruction.cpp`: `0e57e130126f02e6bb872df2672728dc90417bd5`
- `tests/hss_live_permutation_characterization_test.cpp`: `a53d0988dc5b72c10f09ac499e80abe7f2ad1d63`

The original server checkout and its unfinished, unstaged command-transaction edits were not overwritten or included. An exact `git archive` of the base plus only the staged three-file runtime slice was built in `/home/abdulazeez/hss-work/runtime-verify.ttRXwY/source`. The existing regression was then extended in that isolated source. This is not the larger Mac candidate and does not include its retained-unknown overlay or transaction changes.

## Executed verification

Environment: Linux x86_64 AI server, GCC 13.3.0, CMake 3.31.6, root warnings-as-errors and AddressSanitizer/UndefinedBehaviorSanitizer enabled. ZLIB headers/static library were the existing user-local dependency. No replacement translation units, placeholder libraries, deleted tests or disabled warnings were used.

With `ROOT=/home/abdulazeez/hss-work`, `RUN=$ROOT/runtime-verify.ttRXwY`, and `CMAKE=$ROOT/tools/bin/cmake`, the actual root configure/build commands were:

```sh
"$CMAKE" -S "$RUN/source" -B "$RUN/build" \
  -DHSS_BUILD_TESTS=ON -DHSS_ENABLE_SANITIZERS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DZLIB_INCLUDE_DIR="$ROOT/deps/zlib/usr/include" \
  -DZLIB_LIBRARY="$ROOT/deps/zlib/usr/lib/x86_64-linux-gnu/libz.a"
"$CMAKE" --build "$RUN/build" --parallel 2
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$ROOT/tools/bin/ctest" --test-dir "$RUN/build" --output-on-failure \
  --no-tests=error --timeout 180 --output-junit "$RUN/ctest.xml"
```

Observed: configure PASS, full build PASS, **54/54 CTest PASS**, zero failures, real CTest time 2.92 seconds. This full run used the initial runtime regression, before the test-only extension described next. Production source and root CMake did not change afterward.

The same registered C-ABI regression was then extended, rebuilt and executed:

```sh
"$CMAKE" --build "$RUN/build" --target hss_live_permutation_regression_test --parallel 2
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$ROOT/tools/bin/ctest" --test-dir "$RUN/build" \
  -R '^hss_live_permutation_regression$' -V --timeout 180 --no-tests=error
```

Observed: changed target build PASS; extended regression **1/1 PASS**, 4.85 seconds; no sanitizer diagnostic observed. This is not a claim of a second complete 54-test execution after the test-only extension.

## What the actual production C ABI checked

- Two triangle-list orders for the synthetic 0.20-degree crease: 0.50 m width remains one fitted component under the existing 2 mm cross-plane policy; 1.00 m width remains two. Complete derived Scene JSON matches across each pair and raw geometry remains exact. These are numerical fixtures, not a physical LiDAR angular-resolution claim.
- Sixteen deterministic triangle-list permutations each for the five-face approach/0.50 m return/0.15 m front/0.50 m return/continuation fixture, a 12-by-12-cell wall with 0.05 mm synthetic vertex noise, disconnected coplanar faces, and disconnected faces separated by a 20 mm parallel offset. Expected fitted surface counts are 5, 1, 2 and 2 respectively; complete derived Scene JSON and raw geometry assertions pass with a confirmed floor.
- Duplicate/non-manifold and collinear evidence retained without invented fitted planes, with explicit issues. Multiple problematic anchors validate through the actual scene-editing Scene validator, including unique issue/entity IDs and correct global vertex rebasing.
- An 8192-triangle synthetic grid completes across many engine calls, not a single initial call, and produces identical derived Scene JSON for requested 1 us versus 50 us budgets.

Observed single-run Linux ASan timing for the latter fixture: requested 1 us -> 49,757 measured calls, p95 71.040 us, maximum 6,600.08 us; requested 50 us -> 19,370 measured calls, p95 73.246 us, maximum 6,615.11 us. These are elapsed host observations, NOT hard deadlines, iPhone latency, thermal measurements or a speedup over baseline. They demonstrate why requested cooperative budgets must not be reported as guaranteed timing bounds.

## Encountered failure and correction

The first extended regression failed Scene validation. The earlier test IDs set only the first byte and were not version/variant-valid UUIDs. The production validator was kept unchanged; the synthetic test identity helper now sets a version-4 nibble and RFC variant bits. After that test-fixture correction the extended regression passed. Failure logs remain in the isolated workspace (`extended-test.log`, `invalid-scene.log`); final logs are `build-extended-uuid.log` and `extended-test-uuid.log`. Full-run evidence is in `configure.log`, `build.log`, `ctest.log`, `ctest.xml` and `build/compile_commands.json`.

## Remaining review gates and deliberate exclusions

PR #18 must remain draft. The canonical-collision branch in anchor stage 4 still marks every colliding root in a loop in one quantum; scratch cleanup is also batched. A normal command attempting a narrower per-root fix plus a 2048-duplicate regression was denied by the tool safety layer before execution. That proposed change was NOT applied or tested and is not in these blobs. This published checkpoint must not be represented as satisfying worst-case cooperative scheduling for arbitrary anchor data.

Source-after-command retained intent and command/result/publication atomicity changes remain a separate unfinished local workstream. Passing the existing adapter test here does not prove those later behaviors. The preceding Mac 54-test result belongs to a different larger local candidate and must not be substituted as evidence for this slice.

No physical-room accuracy, iPhone capture/Metal performance, native iOS build, hosted Actions success, game-engine import or the three requested scanning modes is established here. The three modes, realistic challenge corpus and offline camera coaching remain accepted staged requirements. Do not start those implementations in place of closing the current verification and publication gates.

Next concrete acceptance: make the ambiguous-root stage cooperative through an allowed edit, preserve all currently passing geometry assertions, exercise a duplicate-heavy large anchor, execute the full suite on the resulting exact tree, and then address the separately preserved command/data-loss tests. Do not bypass tool denials or branch safeguards and do not discard the original local work.

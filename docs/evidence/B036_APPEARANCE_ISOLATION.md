# B-036 appearance isolation — host module evidence

## Scope

`hss_appearance_isolation` admits a local keyframe appearance association only
when its calibration, accepted transform, and visibility consistency are each
explicitly verified and it names the same immutable measured-scene revision as
its opaque target. It contains no coordinates, dimensions, vertices, topology,
mesh buffers, or geometry mutation API.

An absent verification remains `unknown_*`; a negative verification remains
`rejected_*`. Neither state applies an appearance layer or manufactures metric
geometry. A stale visual candidate is rejected, and a failed update retains a
previously admitted visual association. Visual confidence is documented as
appearance quality, never measurement accuracy.

## Host verification

The focused CMake target is registered as `hss_appearance_isolation_test` and
may be run from the repository root with:

```sh
cmake -S . -B build-b036 -DHSS_ENABLE_SANITIZERS=ON
cmake --build build-b036 --target hss_appearance_isolation_test
ctest --test-dir build-b036 -R '^hss_appearance_isolation$' --output-on-failure
```

On 2026-09-22 this host did not have `cmake` installed, so the CMake/CTest
commands could not be executed. The equivalent focused compile and sanitizer
run completed successfully with Xcode clang:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ISpatialCore/include SpatialCore/src/appearance_isolation.cpp \
  tests/hss_appearance_isolation_test.cpp -o /tmp/hss_appearance_isolation_test
/tmp/hss_appearance_isolation_test
# appearance isolation tests passed
```

The focused test covers unknown/rejected evidence, complete same-revision
admission, atomic retention of prior appearance on failure, stale-revision
rejection, malformed input, and bounded optional-layer storage. It is portable
module evidence only; it does not validate iOS camera calibration, texture
projection, rendering, export, or physical metrology.

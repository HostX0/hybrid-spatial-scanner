# I-PLAN-ANNOTATION-CORE host evidence

Date: 2026-09-24

This integration adopts B-031 plan semantics and B-032 annotation geometry in
the immutable core snapshot. Exact captured-mesh cut segments retain their
source IDs through a new `kCapturedSection` kind rather than fabricating B-017
plane-intersection records. Unknown floors, open boundaries, multiple loops,
resource limits, and unmapped edited scenes remain explicit. A confirmed
single loop is still provisional and has null semantic area because captured
mesh alone does not establish finished-room classification or calibrated
quality. B-032 publishes only fitted lengths from the exact retained fragment
anchors; an unavailable plan suppresses their numeric values.

The additive ABI exposes a fixed summary and value-owned plan/annotation JSON
copy functions on an acquired revision. Existing layouts and calls are
unchanged. Durable source and plan-reference replay regenerates the views; no
new persisted authority or schema migration is introduced.

Focused AppleClang 21.0.0.21000334 ASan/UBSan verification:

```sh
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B /tmp/hss-i-plan-annotation-core-asan \
  -DHSS_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build /tmp/hss-i-plan-annotation-core-asan \
  --target hss_plan_annotation_runtime_test hss_plan_semantics_test \
           hss_annotation_geometry_test hss_engine_contract_test \
           hss_live_reconstruction_test hss_engine_scene_editing_adapter_test \
           abi_smoke --parallel 4
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir /tmp/hss-i-plan-annotation-core-asan \
  -R '^(hss_plan_annotation_runtime|hss_plan_semantics|hss_annotation_geometry|hss_engine_contract|hss_live_reconstruction|hss_engine_scene_editing_adapter|abi_smoke)$' \
  --output-on-failure
mkdir -p /tmp/hss-i-plan-annotation-native-compile
/usr/bin/clang++ -std=c++20 -Wall -Wextra -Werror -Wpedantic \
  -Iinclude -ISpatialCore/include -c SpatialCore/src/live_reconstruction.cpp \
  -o /tmp/hss-i-plan-annotation-native-compile/live_reconstruction.o
/usr/bin/clang++ -std=c++20 -Wall -Wextra -Werror -Wpedantic \
  -Iinclude -ISpatialCore/include -c SpatialCore/src/hss_engine.cpp \
  -o /tmp/hss-i-plan-annotation-native-compile/hss_engine.o
! nm -u /tmp/hss-i-plan-annotation-native-compile/live_reconstruction.o \
        /tmp/hss-i-plan-annotation-native-compile/hss_engine.o | \
  rg 'plan_semantics|annotation_geometry'
git diff --check
```

The runtime test covers no-plan startup, exact-revision unknown-floor state,
confirmed-floor provisional state, explicit captured-section lineage, null
semantic area, revision-bound fitted lengths, open-boundary replacement, copy
capacity behavior, and immutable historical snapshots. This is synthetic host
evidence. The scene-edit adapter also verifies that an unmapped edit and its
durable replay cannot reuse the stale pre-command typed plan. Renderer/UI
adoption, exporter adoption of these new snapshot calls,
iOS compilation, device capture, physical metrology, hole/overhead
classification, localized label placement, and collision avoidance were not
run. The direct-source Xcode target does not yet compile the B-031/B-032 module
sources and therefore leaves this optional view unsupported until native
project adoption; this branch deliberately does not edit `HSSApp`. The two
direct-source translation units compiled cleanly without the optional adapter
define and had no unresolved B-031/B-032 symbols.

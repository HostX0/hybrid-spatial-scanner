# B-037 frozen OBJ/MTL revision bundle — host module evidence

## Scope

`hss_obj_mtl_exporter` accepts one caller-supplied, immutable captured-mesh
snapshot and produces OBJ, MTL, and a UTF-8 manifest. It validates raw face
indices and retained source IDs, then copies vertices and face topology without
smoothing, filling, re-triangulating, or generating geometry. The manifest
declares metres, coordinate axes, frozen scene revision, captured-mesh ID,
per-face provenance, warnings, and every appearance state.

Only a B-036 `AppearanceBinding` for the same opaque target and scene revision
can reference a local texture in the MTL. Unknown/rejected visual candidates
remain in the manifest but generate no texture mapping. Texture-free mode omits
every image path/reference while preserving all OBJ vertex and face indices.
The module neither reads images nor writes files or uploads data.

## Host verification

The focused sanitizer CMake run completed successfully on 2026-09-22 using the
pinned host toolchain (the `cmake` executable is not on the default shell
path):

```sh
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  -S . -B build-b037 -DHSS_ENABLE_SANITIZERS=ON
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/cmake \
  --build build-b037 --target hss_obj_mtl_exporter_test -j2
/Users/abdulazeez/Documents/Codex/2026-09-22/pf-x20/work/host-venv/bin/ctest \
  --test-dir build-b037 -R '^hss_obj_mtl_exporter$' --output-on-failure
# 1/1 hss_obj_mtl_exporter passed with AddressSanitizer and UBSan
```

The equivalent direct Xcode-clang ASan/UBSan command also completed
successfully:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -ISpatialCore/include SpatialCore/src/obj_mtl_exporter.cpp \
  tests/hss_obj_mtl_exporter_test.cpp -o /tmp/hss_obj_mtl_exporter_test
/tmp/hss_obj_mtl_exporter_test
# OBJ/MTL exporter tests passed
```

The synthetic host fixture tests a 1 m edge, unchanged raw topology in
textured/texture-free output, revision/provenance retention, explicit
unknown/rejected appearance states, stale-binding rejection, topology failure,
unsafe texture path rejection, and bounded options. It does not prove iOS
bundle writes, image decoding, texture projection, external CAD compatibility,
or physical measurement accuracy.

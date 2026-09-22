# B-036 native appearance presentation adapter

## Scope

`AppearancePresentationAdapter` is a pure Swift seam between a copied native
B-036 update result and a renderer. It accepts a native `.applied` binding, or
an explicitly retained prior binding after a failed update, only when
`metricGeometryChanged` is false and the binding has valid opaque identifiers,
a nonzero measured-scene revision, and finite visual confidence in `0...1`.

The adapter accepts no metric vertices, indices, dimensions, transforms,
coordinates, mesh buffers, or mutation callbacks. Its render result retains the
caller-supplied `AppearanceMetricSnapshotIdentity` verbatim, which contains
only an opaque topology token and immutable scene revision. It cannot change
the metric snapshot or derive metric geometry. Visual confidence remains
appearance quality and is not a measurement-accuracy claim.

For a target mismatch or a stale revision, the adapter returns `suppressed` and
does not present the binding. When an admitted binding's local texture is not
available to the renderer, it returns the same target/snapshot with the neutral
material. A missing texture therefore neither decorates a different target nor
creates measured geometry.

## Focused verification

Run from the repository root:

```sh
swiftc -parse HSSApp/Appearance/AppearancePresentationAdapter.swift
swiftc -typecheck -warnings-as-errors HSSApp/Appearance/AppearancePresentationAdapter.swift
swiftc -D APPEARANCE_STANDALONE -warnings-as-errors \
  HSSApp/Appearance/AppearancePresentationAdapter.swift \
  tests/AppearancePresentationAdapterTests.swift \
  -o /tmp/hss_appearance_presentation_test
/tmp/hss_appearance_presentation_test
```

The standalone test checks successful texture presentation, exact preservation
of the opaque immutable metric identity, neutral fallback for a missing
texture, stale-revision suppression, target-mismatch suppression, preservation
of an explicitly retained same-revision binding, and rejection of unadmitted or
metric-mutating native results.

## Limits

This evidence does not compile the unmodified Xcode project because adding the
new source to that project is deliberately out of scope for this isolated
change. It does not exercise an iOS device, ARKit, a C/C++ bridge, Metal texture
loading/projection, renderer draw calls, export, or physical metrology. The
native B-036 appearance-isolation module remains separately evidenced in
`docs/evidence/B036_APPEARANCE_ISOLATION.md`.

# B-043 deterministic cross-export regression

`hss_cross_export_regression_test` is a host-only regression check for one frozen,
provisional scene authority. It exercises PDF/SVG, DXF, and OBJ/MTL independently
from the same revision (`43`), project identifier, captured-mesh identifier
(`mesh-7`), and metre coordinate convention.

It verifies that repeated export is byte-stable for each format, that every
manifest declares the same revision and metres, and that the retained captured
mesh provenance reaches each artifact. The fixture includes an unknown plan
fragment with no authored segment. The check requires that it remains in
metadata, but that neither PDF/SVG nor OBJ obtains generated geometry from it.

This is portable module integration evidence only. It does not establish physical
capture accuracy, device rendering, printer scale, or archive-write atomicity.

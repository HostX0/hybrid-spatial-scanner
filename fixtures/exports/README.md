# Frozen PDF/SVG export fixture

`hss_pdf_svg_export_test` constructs an accepted revision `42` in code so that
the test can also exercise direct value ownership. It contains one explicit
observed segment, one retained unknown fragment without a segment, a
provisional Arabic label (`غرفة غير مكتملة`), and retained mesh/depth support
IDs. The fixture proves that unknown evidence survives the sidecar but never
becomes a vector path or numeric measurement.

The fixture is synthetic host data. It is not a scan, a physical accuracy
result, or proof of PDF font shaping in an iOS renderer.

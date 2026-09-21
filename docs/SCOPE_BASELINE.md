# B-001 scope baseline

B-001 (P1, TC-001) fixes the review baseline for the hybrid spatial scanner's
non-rectangular synthetic scene. It is a scope and arithmetic record, not a
claim that an iPhone capture or metrology gate has passed.

## Signature attached rib

The synthetic room is 6.00 m by 5.00 m. Its ordered outer contour is:

`(0,0) → (2,0) → (2,0.50) → (2.15,0.50) → (2.15,0) → (6,0) → (6,5) → (0,5)`.

The 0.50 m deep, 0.15 m wide inward rib therefore remains five consecutive
wall segments: approach, first return, front, second return and continuation.
The expected outer area is **29.925 m²**. This is synthetic geometry; the
fixture must not be interpreted as a hard-coded measurement or default UI
dimension.

## Detached column variant

`rib_with_column.json` retains that outer contour and adds a detached 0.30 m ×
0.50 m hole (0.150 m²). Its expected net fixed-obstruction area is
**29.775 m²**. The column is an interior obstruction and does not alter the
attached-rib topology.

## Invariants for later implementation

- Preserve arbitrary supported angles and the ordered feature topology; do not
  globally snap to 90° or merge the rib into one wall.
- Keep observed, fitted, inferred, user-measured and unobserved geometry
  distinct, with field-level provenance. A semantic label does not establish a
  dimension; unseen surfaces remain unknown.
- Capture, correction, save/reopen and export remain usable offline. Human
  corrections are revisioned and reversible; protecting topology does not lock
  coordinates.
- These fixtures and values are reference inputs only. Physical accuracy,
  device behavior and iPhone 15 Pro Max results require later physical tests.

No interface, schema, fixture expectation or production implementation was
changed for B-001.

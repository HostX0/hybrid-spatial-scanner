# Reference validation repairs

The initial test run detected a wrong expected area in the authored oblique synthetic polygon: the coordinates enclose 15 m2, not the manually entered 14 m2. The expected fixture was corrected after an independent shoelace calculation (cross-term sum 30 / 2). The geometry algorithm was not changed to make it pass. The initial failure log is retained.

The standalone illustrative scene uses inferred provenance and source_is_synthetic=true; it does not claim a physical user measurement.

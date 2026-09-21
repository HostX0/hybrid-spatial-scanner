# v1.1

Research-informed architecture and dependency decisions; detailed sparse fusion, submap rebuild and hard-constraint design; explicit C ABI and seven schemas; fifteen command example pairs; synthetic geometry/exports; executable reference math and durability models; visual interaction assets; reproducible host checks. The Google Docs entry summary is now English. Existing five-phase product gates remain unpassed until actual implementation evidence exists.

The illustrative v1.0 ABI is replaced by complete v1.1 declarations. Only extensible messages use version/size prefixes; fixed math PODs do not. In-memory uint64 counters serialize as decimal strings. Snapshots have one owner per acquisition, not undocumented retain semantics. Command retries reuse exact validated UTF-8 bytes.

See verification/REPAIRS.md for the independent synthetic expected-area correction and provenance clarification. This release adds a reference repository directory; it does not add a finished scanner or remote GitHub repository.

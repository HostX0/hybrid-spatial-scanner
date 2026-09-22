# Hybrid Spatial Scanner

An offline native iPhone scanner under active development: captured 3D, measured room plans, source-preserving editing, and a separate detailed-object capture workflow.

## Current state

The repository contains a SwiftUI/Xcode application target, ARKit depth and mesh adapters, a C ownership facade, portable geometry/storage/replay components, tests, and the original engineering reference pack. Native capture-to-engine-to-save/reopen integration and the product interface are in progress. The current integrated engine still has placeholder snapshots; the existing provisional-plan example is synthetic only. This is not yet a complete installable product or a validated measurement tool.

`coordination/HSS_STATUS.json` records the latest backed-up implementation state. `docs/evidence/` distinguishes host, simulator and physical evidence. Physical iPhone 15 Pro Max testing is planned after software readiness. No accuracy, large-hall capacity, thermal endurance, or competitor-superiority claim has been established.

## Development

Read `AGENTS.md`, then the relevant sections of `docs/D14_Research.md`, `docs/D15_Engine.md`, `docs/D16_Contracts.md` and `docs/D17_UX_Delivery.md`. The original acceptance dependencies remain in `coordination/backlog-source.json`.

Portable C++20 checks require CMake and a C/C++ compiler:

```sh
cmake -S . -B build -DHSS_ENABLE_SANITIZERS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Run only relevant tests while editing. A combined native smoke test belongs at a connected workflow milestone. Python reference tests use `requirements.txt`; reference contracts do not establish native product readiness.

Open `HSSApp/HybridSpatialScanner.xcodeproj` and use its shared `HybridSpatialScanner` scheme. The recorded toolchain is Xcode 27.0 / Swift 6.4. The next integrated native build is pending; signing and installation instructions will be consolidated when ready. Simulator support provides no LiDAR capture evidence.

## Architecture and source map

- `HSSApp/`: SwiftUI, one owned ARSession, native adapters and orchestration.
- `SpatialCore/`: portable C++ geometry, source retention, replay and engine.
- `include/` and `schemas/`: C interfaces and strict scene/event contracts.
- `docs/`: implementation specifications and factual test evidence.
- `coordination/`: original backlog/workpacks and current coordination snapshots.
- `fixtures/`, `expected/`, `tests/`: synthetic data and host verification.
- `reference/`, `design/`, `research/`, `verification/`: reference material, wireframes and source decisions.

Captured geometry, the metric scene and appearance remain separate. Canonical coordinates use metres and a right-handed project frame. Only accepted epoch relationships connect scans. Unknown space is not completed into measured geometry; display simplification cannot remove protected measurement topology. Saved means a durable commit, not engine receipt.

The first captured geometry path uses ARKit mesh. D15 permits custom dense fusion to remain gated until implemented and validated. Detailed object photogrammetry stays separate from authoritative room measurements. See `docs/HSS_ENGINE_SOURCE_DECISIONS.md` for researched dependencies and limits.

## Backup and data

The owner authorized the private GitHub backup at `HostX0/hybrid-spatial-scanner`. No public release has been made. Keep signing keys, credentials, local build products and private physical captures out of Git. Sample geometry is synthetic; listing an external library or paper does not mean its code ships in the app.

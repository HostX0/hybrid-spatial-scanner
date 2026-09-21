# Hybrid Spatial Scanner — Engineering Reference v1.1

An English-language engineering handoff for an offline iPhone application combining captured 3D, dimensioned 2D plans and live human-assisted scanning.

## What is delivered

This is a **specification and executable reference pack**, not an installable iPhone app or a complete production engine. It includes four extended engineering documents plus text snapshots of all five original phase documents; a C-compatible interface header; seven JSON schemas; valid and invalid examples for all fifteen live commands; twelve polygon cases; calibrated synthetic metadata/data; original Python/C++ mathematical and storage references; a Metal fusion reference kernel; eight screen wireframes and four diagrams; selected synthetic exports; and actual host verification logs.

The native Google Docs remain the navigable project baseline. This pack carries implementation-level examples and longer derivations. Existing v1.0 short ABI examples are superseded by `include/hss/hss_api.h` and `docs/D16_Contracts.md`.

## Read first

Read `docs/D14_Research.md`, `docs/D15_Engine.md`, `docs/D16_Contracts.md` and `docs/D17_UX_Delivery.md`. Then inspect the exact header, relevant schema, fixture and test before implementing a work packet. Read `AGENTS.md` and `VERIFICATION.md` before reporting completion.

## Quick verification

Run from this directory in a Python 3.11+ virtual environment (tested on 3.13.5) with CMake and a C/C++ compiler available:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m pytest -q
cmake -S . -B build -DHSS_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

With Swift installed, separately run:

```sh
swiftc -typecheck -I include reference/swift/ContractSmoke.swift
```

`tools/run_verification.sh` executes the available checks and stores fresh logs outside the delivered evidence directory. It fails on test errors and explicitly records a missing Swift compiler rather than claiming success. No Apple developer key is required for the host references. Initial package installation may need internet; this development setup is not the application's offline capture path.

## Architecture

Swift owns capture/lifecycle, local persistence, UI and export orchestration. One tested ARSession supplies estimated depth, poses and mesh updates; RoomPlan proposals are optional. A C facade isolates ownership and exceptions from the C++ metric feature/constraint core. A replaceable Metal backend handles eligible dense operations only after CPU parity and device tests. The same accepted scene revision drives 2D and 3D. No cloud AI is a trusted measurement authority.

## File map

- `docs/baseline/`: English text snapshots of D01–D13 from the updated native Google Docs; Drive remains the live task/status authority.
- `include/`: normative ABI declarations and Swift import module.
- `schemas/`: strict Draft 2020-12 data contracts, with semantic checks additionally required.
- `reference/`: selected original C++, Python, Swift and Metal references, each with bounded scope.
- `fixtures/` and `expected/`: explicit synthetic examples, not physical captures.
- `tests/`: executed host contract and numerical tests.
- `design/`: eight schematic screens, four diagrams, SVG/PNG assets and diagram sources.
- `research/`: primary sources, inspected file identities and candidate dependency decisions.
- `verification/`: actual results and original failure/repair record.

## Important boundaries

There is no Xcode application target, fully implemented `HSSEngine`, production mesh extractor, complete optimizer, texture pipeline, trained model or cloud service in this pack. The Metal kernel has not been compiled on an Apple SDK. Swift host import is not an iOS build. The numerical solver covers selected small distance/lock constraints, not every production residual. The POSIX storage oracle is not an iOS APFS durability certification. No physical LiDAR accuracy, large-hall limit, battery/thermal outcome or competitor superiority is established.

## First production milestone

Adopt the contracts; create a real iOS target; prove shared-session configuration and calibration on a supported physical device; record a consented difficult-room capture; save/reopen offline; replay the normalized inputs; and output a minimal nonrectangular plan. Review the 0.50 m by 0.15 m protrusion before adding cloud accounts or generative appearance features.

## Data and licences

All sample geometry is authored synthetic data. No private room captures, third-party repositories, papers, datasets, weights, credentials or fonts are redistributed. External component inclusion requires a pinned artifact and licence/dependency review. Merely listing a component in research does not approve shipping it. No public GitHub repository or access permissions were created by this handoff.

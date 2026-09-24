#!/usr/bin/env python3
"""Offline, pinned differential regression and optional host microbenchmark.

No library download, device inference or network access. Both executables use
identical compiler flags, public header, tests and fixtures. The only differing
input is the baseline/candidate fitter translation unit. A parity pass compares
all public fields, including exact double bits, failure status and work counters.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile

BASE_COMMIT = "598b43d4901f2c5f1e5886d80fea6373ddf737a0"
BASE_BLOB = "1ed46d88b6a372ae58a9b0bcb031a5b19bcc0577"
HEADER_BLOB = "35735ffc667c45357bef2023c28875ff46219dd1"
SOURCE = pathlib.Path("SpatialCore/src/robust_plane_fit.cpp")
HEADER = pathlib.Path("SpatialCore/include/hss/robust_plane_fit.hpp")
UNIT = pathlib.Path("tests/hss_robust_plane_fit_test.cpp")
PROBE = pathlib.Path("tests/hss_plane_fit_scoring_probe.cpp")
EXPECTED_CASES = {f"{kind}:{seed}" for seed in range(1, 25) for kind in range(17)}


def blob_id(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def run(command: list[str], cwd: pathlib.Path) -> bytes:
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=120, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command!r}\n"
                           + result.stdout.decode(errors="replace")
                           + result.stderr.decode(errors="replace"))
    return result.stdout


def validate_output(data: bytes) -> collections.Counter[str]:
    lines = data.decode("ascii").splitlines()
    keys = [line.split()[0] for line in lines]
    if len(keys) != len(EXPECTED_CASES) or set(keys) != EXPECTED_CASES:
        raise RuntimeError("Missing/duplicate/unknown differential cases")
    return collections.Counter(line.split()[1] for line in lines)


def benchmark_output(data: bytes) -> dict[str, tuple[float, str]]:
    parsed: dict[str, tuple[float, str]] = {}
    for line in data.decode("ascii").splitlines():
        name, elapsed, checksum = line.split()
        value = float(elapsed)
        if not 0.0 < value < float("inf") or name in parsed:
            raise RuntimeError("Invalid benchmark result")
        parsed[name] = (value, checksum)
    if set(parsed) != {"live_mesh96", "oblique4096", "clutter4096"}:
        raise RuntimeError("Incomplete benchmark output")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--baseline-source", type=pathlib.Path,
                        help="Offline source copy; its Git blob hash must match the pinned baseline")
    parser.add_argument("--sanitize", action="store_true", help="ASan+UBSan; do not benchmark instrumented binaries")
    parser.add_argument("--skip-benchmarks", action="store_true")
    parser.add_argument("--benchmark-rounds", type=int, default=5)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    if not 1 <= args.benchmark_rounds <= 15:
        parser.error("--benchmark-rounds must be between 1 and 15")
    root = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which(args.cxx)
    if not compiler:
        parser.error(f"Compiler not found: {args.cxx}")
    baseline = (args.baseline_source.read_bytes() if args.baseline_source else
                run(["git", "show", f"{BASE_COMMIT}:{SOURCE.as_posix()}"], root))
    if blob_id(baseline) != BASE_BLOB:
        raise RuntimeError("Baseline source hash mismatch; refusing an unverified comparator")
    if blob_id((root / HEADER).read_bytes()) != HEADER_BLOB:
        raise RuntimeError("Public header changed; review the pinned parity contract before proceeding")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1" if args.sanitize else "-O3"]
    if args.sanitize:
        flags += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    report: dict[str, object] = {
        "baseline_commit": BASE_COMMIT, "baseline_source_blob": BASE_BLOB,
        "candidate_source_blob": blob_id((root / SOURCE).read_bytes()),
        "header_blob": HEADER_BLOB, "unit_test_blob": blob_id((root / UNIT).read_bytes()),
        "probe_blob": blob_id((root / PROBE).read_bytes()),
        "compiler": run([compiler, "--version"], root).decode().splitlines()[0],
        "platform": platform.platform(), "machine": platform.machine(),
        "flags": flags, "sanitizers": args.sanitize,
        "scope": "Targeted host module only; not full CTest, Apple build, sensor metrology or app FPS",
    }
    with tempfile.TemporaryDirectory(prefix="hss-plane-fit-") as temporary:
        work = pathlib.Path(temporary)
        baseline_path = work / "baseline.cpp"
        baseline_path.write_bytes(baseline)
        outputs: dict[str, bytes] = {}
        executables: dict[str, pathlib.Path] = {}
        for name, source in (("baseline", baseline_path), ("candidate", root / SOURCE)):
            for test, label in ((UNIT, "unit"), (PROBE, "probe")):
                executable = work / f"{name}-{label}"
                run([compiler, *flags, "-I" + str(root / "SpatialCore/include"),
                     str(source), str(root / test), "-o", str(executable)], root)
                output = run([str(executable)], root)
                if label == "unit":
                    if output.strip() != b"robust plane fitting tests passed":
                        raise RuntimeError("Unexpected unit test output")
                else:
                    validate_output(output)
                    outputs[name] = output
                    executables[name] = executable
        if outputs["baseline"] != outputs["candidate"]:
            left, right = outputs["baseline"].splitlines(), outputs["candidate"].splitlines()
            mismatch = next(i for i, (a, b) in enumerate(zip(left, right)) if a != b)
            raise RuntimeError(f"Exact result parity failed at {left[mismatch].split()[0]!r}")
        report.update({"unit_suites_passed": 2, "unit_scenarios_per_suite": 7,
                       "parity_cases_passed": len(EXPECTED_CASES), "parity_mismatches": 0,
                       "output_sha256": hashlib.sha256(outputs["candidate"]).hexdigest(),
                       "fixture_fit_status_counts": dict(validate_output(outputs["candidate"]))})
        if not args.sanitize and not args.skip_benchmarks:
            times: dict[str, dict[str, list[float]]] = {"baseline": {}, "candidate": {}}
            for round_index in range(args.benchmark_rounds):
                # Alternate order to reduce consistent first/second-run bias.
                order = ("baseline", "candidate") if round_index % 2 == 0 else ("candidate", "baseline")
                results = {name: benchmark_output(run([str(executables[name]), "--benchmark"], root)) for name in order}
                for fixture in results["baseline"]:
                    if results["baseline"][fixture][1] != results["candidate"][fixture][1]:
                        raise RuntimeError("Benchmark checksum changed")
                    for name in order:
                        times[name].setdefault(fixture, []).append(results[name][fixture][0])
            report["benchmark"] = {
                fixture: {"baseline_ms_samples": times["baseline"][fixture],
                          "candidate_ms_samples": times["candidate"][fixture],
                          "baseline_median_ms": statistics.median(times["baseline"][fixture]),
                          "candidate_median_ms": statistics.median(times["candidate"][fixture]),
                          "speedup": statistics.median(times["baseline"][fixture]) / statistics.median(times["candidate"][fixture])}
                for fixture in times["baseline"]}
            report["benchmark_method"] = "One warmup and eight fits per sample; alternating binary order. Clutter fixture intentionally ends in refinement nonconvergence, not an accepted plane. No timing pass/fail threshold."
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

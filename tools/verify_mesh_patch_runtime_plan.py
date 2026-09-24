#!/usr/bin/env python3
"""Compile and run the mesh-patch runtime-plan regression in a portable host toolchain."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    compiler = shutil.which(args.cxx)
    if compiler is None:
        raise SystemExit(f"compiler not found: {args.cxx}")

    root = Path(__file__).resolve().parents[1]
    source = root / "tests" / "hss_mesh_patch_runtime_plan_test.cpp"
    include = root / "SpatialCore" / "include"
    required = [
        source,
        include / "hss" / "mesh_patch_runtime_plan.hpp",
        include / "hss" / "mesh_patch_components.hpp",
        include / "hss" / "robust_plane_fit.hpp",
    ]
    missing = [str(path.relative_to(root)) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("missing required source files: " + ", ".join(missing))

    flags = [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
        f"-I{include}",
    ]
    if args.sanitize:
        flags += [
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
        ]
    else:
        flags += ["-O2"]

    with tempfile.TemporaryDirectory(prefix="hss-runtime-plan-") as temp:
        binary = Path(temp) / "runtime-plan-test"
        run([compiler, *flags, str(source), "-o", str(binary)])
        env = os.environ.copy()
        if args.sanitize:
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
        run([str(binary)], env=env)

    print("mesh patch runtime plan verification: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"verification failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)

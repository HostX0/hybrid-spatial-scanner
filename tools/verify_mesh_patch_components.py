#!/usr/bin/env python3
"""Compile and run the portable mesh-patch component regression suite.

This intentionally tests only the dependency-ready grouping module. It is not an
Xcode, ARKit, LiDAR, Metal, or full SpatialCore validation.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import tempfile


def run(cmd: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise SystemExit(f"compiler not found: {args.compiler}")

    flags = [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
        f"-I{root / 'SpatialCore' / 'include'}",
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

    with tempfile.TemporaryDirectory(prefix="hss-mesh-patch-") as td:
        binary = pathlib.Path(td) / "hss_mesh_patch_components_test"
        run(
            [
                compiler,
                *flags,
                str(root / "tests" / "hss_mesh_patch_components_test.cpp"),
                "-o",
                str(binary),
            ]
        )
        env = os.environ.copy()
        if args.sanitize:
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
        run([str(binary)], env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

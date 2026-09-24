#!/usr/bin/env python3
"""Compile and run the targeted anchor project-batch regression."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    source = root / "tests" / "hss_mesh_anchor_project_batch_test.cpp"
    coordinate = root / "SpatialCore" / "src" / "coordinate_basis.cpp"
    include_core = root / "SpatialCore" / "include"
    include_api = root / "include"

    flags = [
        "-std=c++20",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        f"-I{include_core}",
        f"-I{include_api}",
    ]
    if args.sanitize:
        flags = [
            "-std=c++20",
            "-O1",
            "-g",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-I{include_core}",
            f"-I{include_api}",
        ]

    with tempfile.TemporaryDirectory(prefix="hss-anchor-project-batch-") as td:
        binary = Path(td) / "hss_mesh_anchor_project_batch_test"
        compile_cmd = [
            args.cxx,
            *flags,
            str(source),
            str(coordinate),
            "-o",
            str(binary),
        ]
        print("compiler:", subprocess.check_output([args.cxx, "--version"], text=True).splitlines()[0])
        print("compile:", " ".join(compile_cmd))
        subprocess.run(compile_cmd, cwd=root, check=True)

        env = os.environ.copy()
        if args.sanitize:
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
        subprocess.run([str(binary)], cwd=root, env=env, check=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

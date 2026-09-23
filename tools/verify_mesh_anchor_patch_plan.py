#!/usr/bin/env python3
"""Compile and run the anchor-local mesh patch integration regressions."""
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
TEST = ROOT / "tests" / "hss_mesh_anchor_patch_plan_test.cpp"
INCLUDE = ROOT / "SpatialCore" / "include"


def run(cmd: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, env=env, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    flags = [
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
        f"-I{INCLUDE}",
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

    with tempfile.TemporaryDirectory(prefix="hss-anchor-plan-") as tmp:
        binary = pathlib.Path(tmp) / "hss_mesh_anchor_patch_plan_test"
        run([args.cxx, *flags, str(TEST), "-o", str(binary)])
        env = os.environ.copy()
        if args.sanitize:
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
        run([str(binary)], env=env)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"verification failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)

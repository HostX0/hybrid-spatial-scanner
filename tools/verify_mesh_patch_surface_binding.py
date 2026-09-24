#!/usr/bin/env python3
from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile and run deterministic mesh-patch surface-binding tests."
    )
    parser.add_argument("--cxx", default="c++", help="C++ compiler executable")
    parser.add_argument(
        "--sanitize",
        action="store_true",
        help="Enable AddressSanitizer and UndefinedBehaviorSanitizer",
    )
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "tests" / "hss_mesh_patch_surface_binding_test.cpp"
    include_dir = root / "SpatialCore" / "include"

    with tempfile.TemporaryDirectory(prefix="hss-surface-binding-") as tmp:
        binary = pathlib.Path(tmp) / "hss_mesh_patch_surface_binding_test"
        command = [
            args.cxx,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wpedantic",
            f"-I{include_dir}",
        ]
        if args.sanitize:
            command += [
                "-O1",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
            ]
        else:
            command += ["-O2"]
        command += [str(source), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)

    print("mesh patch surface binding verification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

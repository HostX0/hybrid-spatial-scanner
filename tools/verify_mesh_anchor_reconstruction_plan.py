#!/usr/bin/env python3
import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hss-anchor-reconstruction-") as temp:
        binary = pathlib.Path(temp) / "test"
        cmd = [
            args.cxx,
            "-std=c++20",
            "-O1" if args.sanitize else "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-I",
            str(ROOT / "SpatialCore" / "include"),
            "-I",
            str(ROOT / "include"),
            str(ROOT / "tests" / "hss_mesh_anchor_reconstruction_plan_test.cpp"),
            str(ROOT / "SpatialCore" / "src" / "coordinate_basis.cpp"),
            "-o",
            str(binary),
        ]
        if args.sanitize:
            cmd.extend(["-fno-omit-frame-pointer", "-fsanitize=address,undefined"])
        subprocess.run(cmd, check=True)
        completed = subprocess.run([str(binary)], check=True, text=True, capture_output=True)
        print(completed.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

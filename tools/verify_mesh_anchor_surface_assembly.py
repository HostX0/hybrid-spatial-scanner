#!/usr/bin/env python3
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


def run(cmd, env=None):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, env=env)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    test = root / "tests" / "hss_mesh_anchor_surface_assembly_test.cpp"
    include = root / "SpatialCore" / "include"
    public_include = root / "include"

    with tempfile.TemporaryDirectory(prefix="hss-surface-assembly-") as tmp:
        exe = pathlib.Path(tmp) / "surface_assembly_test"
        cmd = [
            args.cxx,
            "-std=c++20",
            "-O2" if not args.sanitize else "-O1",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-I",
            str(include),
            "-I",
            str(public_include),
            str(test),
            "-o",
            str(exe),
        ]
        env = os.environ.copy()
        if args.sanitize:
            cmd[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1")
        run(cmd, env=env)
        run([str(exe)], env=env)
    return 0


if __name__ == "__main__":
    sys.exit(main())

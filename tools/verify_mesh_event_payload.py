#!/usr/bin/env python3
"""Compile and run the exact mesh-event payload decoder regression."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


def run(command: list[str], cwd: pathlib.Path) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "tests" / "hss_mesh_event_payload_test.cpp"
    with tempfile.TemporaryDirectory(prefix="hss-mesh-payload-") as tmp:
        binary = pathlib.Path(tmp) / "hss_mesh_event_payload_test"
        command = [
            args.compiler,
            "-std=c++20",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-ISpatialCore/include",
            str(source),
            "-o",
            str(binary),
        ]
        if args.sanitize:
            command[2:2] = [
                "-O1",
                "-g",
                "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
            ]
            command.remove("-O2")
        run(command, root)
        run([str(binary)], root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

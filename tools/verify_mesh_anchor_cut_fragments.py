#!/usr/bin/env python3
import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--cxx', default='c++')
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='hss-cut-') as td:
        exe = pathlib.Path(td) / 'hss_mesh_anchor_cut_fragments_test'
        cmd = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(ROOT / 'SpatialCore' / 'include'),
               str(ROOT / 'tests' / 'hss_mesh_anchor_cut_fragments_test.cpp'),
               str(ROOT / 'SpatialCore' / 'src' / 'coordinate_basis.cpp'), '-o', str(exe)]
        if args.sanitize:
            cmd[2:2] = ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        subprocess.run(cmd, check=True)
        result = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
        print(result.stdout.strip())
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

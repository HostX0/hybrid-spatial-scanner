#!/usr/bin/env python3
"""Independent consumer check of saved real-RGBD OBJ/MTL artifacts.

No engine execution, renderer or geometry inference. Reads bounded local files
and verifies topology, material paths, UV indices, digests and frozen provenance.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath


def read(path, maximum=32 * 1024 * 1024):
    assert path.is_file() and not path.is_symlink(), str(path)
    assert 0 < path.stat().st_size <= maximum, str(path)
    return path.read_bytes()


def obj(path):
    vertices, uv, faces, libraries, used = [], [], [], [], set()
    for line in read(path).decode('utf-8').splitlines():
        fields = line.split()
        if not fields or fields[0].startswith('#'):
            continue
        kind = fields[0]
        if kind == 'v':
            assert len(fields) == 4
            value = tuple(map(float, fields[1:]))
            assert all(math.isfinite(v) for v in value)
            vertices.append(value)
        elif kind == 'vt':
            assert len(fields) == 3
            value = tuple(map(float, fields[1:]))
            assert all(math.isfinite(v) and 0 <= v <= 1 for v in value)
            uv.append(value)
        elif kind == 'f':
            assert len(fields) == 4
            indices = [p.split('/') for p in fields[1:]]
            assert all(len(p) in (1, 2) for p in indices)
            face = tuple(int(p[0]) - 1 for p in indices)
            assert len(set(face)) == 3 and all(0 <= p < len(vertices) for p in face)
            if len(indices[0]) == 2:
                assert all(len(p) == 2 and 0 <= int(p[1]) - 1 < len(uv) for p in indices)
            else:
                assert all(len(p) == 1 for p in indices)
            faces.append(face)
        elif kind == 'mtllib':
            assert len(fields) == 2
            libraries.append(fields[1])
        elif kind == 'usemtl':
            assert len(fields) == 2
            used.add(fields[1])
        else:
            raise AssertionError('Unexpected OBJ directive: ' + kind)
    assert vertices and faces and len(libraries) == 1
    return vertices, faces, uv, libraries[0], used


def verify(directory):
    source = obj(directory / 'capture.obj')
    bundle = directory / 'textured'
    colored = obj(bundle / 'capture.obj')
    assert source[:2] == colored[:2], 'Optional color changed exact exported geometry/topology'
    library = colored[3]
    assert library == 'capture.mtl' and source[3] == library
    materials, paths = set(), set()
    for line in read(bundle / library).decode('utf-8').splitlines():
        fields = line.split()
        if fields and fields[0] == 'newmtl':
            assert len(fields) == 2 and fields[1] not in materials
            materials.add(fields[1])
        elif fields and fields[0] == 'map_Kd':
            assert len(fields) == 2
            p = PurePosixPath(fields[1])
            assert not p.is_absolute() and p.parts[0] == 'textures' and len(p.parts) == 2
            assert '..' not in p.parts and not (bundle / 'textures').is_symlink()
            paths.add(fields[1])
    assert colored[4] <= materials
    before = json.loads(read(directory / 'export-manifest.json'))
    after = json.loads(read(bundle / 'manifest.json'))
    for key in ('project_id', 'scene_revision', 'captured_mesh_id', 'units', 'axes', 'source_is_synthetic'):
        assert before[key] == after[key], key
    assert after['units'] == 'm' and after['source_is_synthetic'] is False
    assert before['faces'] == [dict(f, appearance_target_id=None) for f in after['faces']]
    references = {}
    for slot in after['appearance']:
        assert slot['state'] == 'verified'
        texture = slot['texture']
        path, checksum = texture['path'], texture['sha256']
        assert path in paths
        assert references.get(path, checksum) == checksum
        references[path] = checksum
    assert set(references) == paths
    for path, checksum in references.items():
        assert hashlib.sha256(read(bundle / path, 4 * 1024 * 1024)).hexdigest() == checksum
    actual = {'textures/' + p.name for p in (bundle / 'textures').iterdir()}
    assert actual == paths, 'Unused or unmanifested image was packaged'
    assert read(directory / 'scene-before-reopen.json') == read(directory / 'scene-after-reopen.json')
    report = json.loads(read(directory / 'appearance-result.json'))
    assert report['metric_geometry_changed'] is False
    assert len(after['appearance']) == report['textured_faces']
    assert len(paths) == report['packaged_images']
    return dict(directory=directory.name, vertices=len(colored[0]), triangles=len(colored[1]),
                textured_faces=len(after['appearance']), images=len(paths),
                geometry_topology_source_links_unchanged=True, material_files_resolve=True,
                texture_checksums_verified=True, physically_verified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    args = parser.parse_args()
    print(json.dumps([verify(path) for path in args.directories], indent=2))

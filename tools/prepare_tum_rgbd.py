"""Bounded TUM adapter, not a tracking/depth-fusion implementation.

Requires the server acquisition's provenance.json and selected public files.
Provider-registered depth uses the documented default intrinsics, no second
depth scale correction or one-sided undistortion. Output is evaluation-only.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

K = (525.0, 525.0, 319.5, 239.5)
STRIDE = 32
MAX_POSE_BRACKET_S = 0.05


def matrix(row):
    translation = np.array(row[1:4], dtype=np.float64)
    q = np.array(row[4:8], dtype=np.float64)
    q /= np.linalg.norm(q)
    x, y, z, w = q
    result = np.eye(4)
    result[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                      [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                      [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    result[:3, 3] = translation
    return result


def pose_at(rows, timestamp):
    times = rows[:, 0]
    i = np.searchsorted(times, timestamp)
    if i == 0 or i == len(rows):
        return None
    a, b = rows[i-1].copy(), rows[i].copy()
    if max(timestamp-a[0], b[0]-timestamp) > MAX_POSE_BRACKET_S:
        return None
    t = (timestamp-a[0]) / (b[0]-a[0])
    qa, qb = a[4:8] / np.linalg.norm(a[4:8]), b[4:8] / np.linalg.norm(b[4:8])
    dot = np.dot(qa, qb)
    if dot < 0:
        qb, dot = -qb, -dot
    if dot > 0.9995:
        q = (1-t)*qa + t*qb
    else:
        theta = np.arccos(np.clip(dot, -1, 1))
        q = (np.sin((1-t)*theta)*qa + np.sin(t*theta)*qb) / np.sin(theta)
    row = np.r_[timestamp, (1-t)*a[1:4] + t*b[1:4], q / np.linalg.norm(q)]
    return matrix(row)


def triangulate(depth):
    fx, fy, cx, cy = K
    vertices, pixels = [], []
    indices = {}
    for v in range(0, 480, STRIDE):
        for u in range(0, 640, STRIDE):
            z = float(depth[v, u])
            if z > 0 and np.isfinite(z):
                indices[(u, v)] = len(vertices)
                vertices.append(((u-cx)*z/fx, -(v-cy)*z/fy, -z))
                pixels.append((u, v))
    triangles, rejected = [], 0
    for v in range(0, 480-STRIDE, STRIDE):
        for u in range(0, 640-STRIDE, STRIDE):
            corners = ((u,v), (u+STRIDE,v), (u,v+STRIDE), (u+STRIDE,v+STRIDE))
            for selection in ((0, 2, 1), (1, 2, 3)):
                ps = [corners[n] for n in selection]
                if not all(p in indices for p in ps):
                    rejected += 1
                    continue
                # Adapter policy only: reject samples spanning depth jumps.
                # Dense interior pixels must also support the triangle's
                # depth range; a foreground object inside a coarse cell is
                # not bridged just because the corners agree.
                z = np.array([depth[p[1],p[0]] for p in ps])
                tolerance = max(0.05, 0.03*float(np.min(z)))
                patch = depth[v:v+STRIDE+1, u:u+STRIDE+1]
                if np.any(patch <= 0) or np.ptp(z) > tolerance or np.min(patch) < np.min(z)-tolerance or np.max(patch) > np.max(z)+tolerance:
                    rejected += 1
                    continue
                triangles.append([indices[p] for p in ps])
    return np.asarray(vertices, dtype='<f4'), np.asarray(triangles, dtype='<u4'), pixels, rejected


def prepare(workspace, dataset, destination):
    assert dataset in ('freiburg1_xyz', 'freiburg1_room')
    assert not destination.exists(), 'Refuse overwrite'
    provenance = json.loads((workspace / 'provenance.json').read_text())
    source = workspace / dataset
    rows = np.loadtxt(source / 'groundtruth.txt', dtype=np.float64)
    assert rows.ndim == 2 and rows.shape[1] == 8 and np.isfinite(rows).all() and np.all(np.diff(rows[:,0]) > 0)
    cv_to_ar = np.diag([1., -1., -1., 1.])
    accepted, excluded = [], []
    for pair in provenance['datasets'][dataset]['selected_pairs']:
        depth_pose = pose_at(rows, pair['depth_time_s'])
        rgb_pose = pose_at(rows, pair['rgb_time_s'])
        if depth_pose is None or rgb_pose is None:
            excluded.append(dict(pair=pair, reason='No bounded reference-pose interpolation; no extrapolation'))
        else:
            accepted.append((pair, depth_pose, rgb_pose))
    assert len(accepted) >= 16, 'Insufficient predeclared evaluation samples'
    project_from_world = np.linalg.inv(accepted[0][1] @ cv_to_ar)
    origin = accepted[0][0]['depth_time_s']
    destination.mkdir(mode=0o700)
    report = dict(dataset=dataset, source_is_synthetic=False, license=provenance['license'],
                  attribution=provenance['attribution'], archive_sha256=provenance['datasets'][dataset]['sha256'],
                  calibration=dict(fx=K[0],fy=K[1],cx=K[2],cy=K[3],depth_divisor=5000,
                                   provider_depth_scale_already_applied=True, undistortion=False),
                  conventions='CV optical x right/y down/z forward -> AR local x right/y up/z backward; project=first accepted camera AR frame, not a confirmed gravity/floor reference',
                  limitations=['Reference poses supplied; no SLAM accuracy claim', 'Kinect depth, not iPhone LiDAR',
                               'Evaluation-only conservative depth triangulation, not native mesh fusion',
                               'No surveyed wall geometry or confirmed floor; observations may overlap'],
                  policy=dict(stride=STRIDE, depth_discontinuity_abs_m=0.05, depth_discontinuity_relative=0.03,
                              reject_missing_or_foreground_in_cell=True, maximum_pose_bracket_s=MAX_POSE_BRACKET_S),
                  excluded=excluded, frames=[])
    lines = [str(len(accepted))]
    for i, (pair, world_from_camera_cv, rgb_world_pose) in enumerate(accepted):
        directory = destination / str(i)
        directory.mkdir(mode=0o700)
        with Image.open(source / pair['depth']) as image:
            assert image.size == (640,480)
            depth_raw = np.asarray(image, dtype=np.uint16)
        depth = (depth_raw.astype(np.float32) / 5000).astype('<f4')
        vertices, triangles, pixels, rejected = triangulate(depth)
        assert len(triangles) > 0, 'No observed triangles; keep original failure evidence'
        pose = project_from_world @ world_from_camera_cv @ cv_to_ar
        rgb_pose = project_from_world @ rgb_world_pose @ cv_to_ar
        time_s = pair['depth_time_s'] - origin
        lines.append(f'{i} {time_s:.17g}')
        for name, payload in [('depth.f32', depth.tobytes()), ('vertices.f32',vertices.tobytes()),
                              ('triangles.u32',triangles.tobytes()), ('pose.f64',pose.astype('<f8').tobytes(order='F')),
                              ('rgb-pose.f64',rgb_pose.astype('<f8').tobytes(order='F'))]:
            (directory / name).write_bytes(payload)
        rgb_bytes = (source / pair['rgb']).read_bytes()
        (directory / 'rgb.png').write_bytes(rgb_bytes)
        report['frames'].append(dict(index=i, pair=pair, relative_time_s=time_s, valid_depth_fraction=float(np.count_nonzero(depth_raw)/depth_raw.size),
                                     raw_vertices=len(vertices), accepted_triangles=len(triangles), rejected_triangles=rejected,
                                     sampled_sensor_pixels=pixels, rgb_sha256=hashlib.sha256(rgb_bytes).hexdigest(),
                                     pose_sha256=hashlib.sha256((directory / 'pose.f64').read_bytes()).hexdigest()))
    (destination / 'frames.txt').write_text('\n'.join(lines) + '\n')
    (destination / 'adapter-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(dataset=dataset, frames=len(accepted), excluded=len(excluded), triangles=sum(f['accepted_triangles'] for f in report['frames']), output=str(destination))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('dataset', choices=['freiburg1_xyz', 'freiburg1_room'])
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.workspace.resolve(), args.dataset, args.destination.resolve())

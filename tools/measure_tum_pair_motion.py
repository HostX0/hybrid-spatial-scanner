#!/usr/bin/env python3
"""Bounded diagnostic of RGB/depth pairing assumptions, not sensor accuracy.

Uses retained provider reference poses at both timestamps. Reprojects only the
adapter's sampled valid points under the declared common 525-pixel calibration.
Does not estimate SLAM, alter poses/tolerances or reconstruct hidden geometry.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def measure(root):
    manifest = json.loads((root / 'adapter-manifest.json').read_text())
    frames = manifest['frames']
    assert 0 < len(frames) <= 24
    shifts, translations, rotations, time_gaps = [], [], [], []
    for frame in frames:
        path = root / str(frame['index'])
        depth = np.fromfile(path / 'pose.f64',dtype='<f8').reshape(4,4,order='F')
        color = np.fromfile(path / 'rgb-pose.f64',dtype='<f8').reshape(4,4,order='F')
        assert np.isfinite(depth).all() and np.isfinite(color).all()
        delta = np.linalg.inv(color) @ depth
        translations.append(float(np.linalg.norm(delta[:3,3])))
        rotations.append(float(np.degrees(np.arccos(np.clip((np.trace(delta[:3,:3])-1)/2,-1,1)))))
        points = np.fromfile(path / 'vertices.f32',dtype='<f4').reshape(-1,3).astype(np.float64)
        assert 0 < len(points) <= 20000 and np.isfinite(points).all()
        moved = points @ delta[:3,:3].T + delta[:3,3]
        mask = (points[:,2] < 0) & (moved[:,2] < 0)
        a, b = points[mask], moved[mask]
        # Constant principal point cancels in the difference.
        original = np.column_stack((525*a[:,0]/-a[:,2],525*-a[:,1]/-a[:,2]))
        paired = np.column_stack((525*b[:,0]/-b[:,2],525*-b[:,1]/-b[:,2]))
        shifts.extend(np.linalg.norm(original-paired,axis=1).tolist())
        time_gaps.append(abs(frame['pair']['delta_s']))
    return dict(dataset=manifest['dataset'], pairs=len(frames), sampled_points=len(shifts),
                max_timestamp_gap_s=max(time_gaps), max_reference_translation_m=max(translations),
                max_reference_rotation_deg=max(rotations), mean_projection_shift_pixels=float(np.mean(shifts)),
                p95_projection_shift_pixels=float(np.percentile(shifts,95)), max_projection_shift_pixels=max(shifts),
                common_pose_is_exact=all(v == 0 for v in translations+rotations),
                use='diagnostic of a public-data pairing assumption; not metrology, native pixel alignment or an accuracy score')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories',type=Path,nargs='+')
    args = parser.parse_args()
    print(json.dumps([measure(p) for p in args.directories],indent=2))

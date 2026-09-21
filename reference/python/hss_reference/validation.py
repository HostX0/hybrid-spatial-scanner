"""Defensive metadata validation; not a sandbox for untrusted native pointers."""
from pathlib import PurePosixPath
import math
import numpy as np
MAX_UINT64=2**64-1

def sequence(value):
    if not isinstance(value,str) or not value.isascii() or not value.isdigit() or (len(value)>1 and value[0]=='0'):
        raise ValueError('sequence must be canonical decimal string')
    n=int(value)
    if n>MAX_UINT64:raise ValueError('sequence overflow')
    return n

def relative_asset_path(path):
    if not isinstance(path,str) or not path or '\\' in path or '\x00' in path or ':' in path:
        raise ValueError('unsafe asset path')
    raw=path.split('/')
    if any(p in ('','.','..') for p in raw) or PurePosixPath(path).is_absolute():raise ValueError('unsafe asset path')
    return path

def frame_metadata(frame):
    w,h=frame['depth_width'],frame['depth_height'];stride=frame['depth_row_stride_bytes'];length=frame['depth_byte_length']
    if not all(isinstance(v,int) and not isinstance(v,bool) for v in (w,h,stride,length)):
        raise ValueError('integer buffer dimensions required')
    if min(w,h)<=0 or max(w,h)>8192 or stride<w*4 or length<stride*(h-1)+w*4 or length>256*1024*1024:
        raise ValueError('invalid depth buffer bounds')
    fx,fy,cx,cy=frame['intrinsics']
    if not np.isfinite([fx,fy,cx,cy]).all() or fx<=0 or fy<=0:raise ValueError('invalid intrinsics')
    T=np.asarray(frame['T_world_camera'],float).reshape(4,4,order='F')
    if not np.isfinite(T).all() or not np.allclose(T[3],[0,0,0,1],atol=1e-9):raise ValueError('bad transform')
    R=T[:3,:3]
    if not np.allclose(R.T@R,np.eye(3),atol=1e-6) or abs(np.linalg.det(R)-1)>1e-6:raise ValueError('not rigid')
    sequence(frame['event_sequence'])
    if not math.isfinite(frame['timestamp_s']):raise ValueError('invalid timestamp')
    if frame['depth_source']!='sceneDepth':raise ValueError('smoothed depth cannot be fused as independent metric evidence')
    return True

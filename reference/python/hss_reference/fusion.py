"""Scalar TSDF oracle, not dense 3D reconstruction or a sensor-noise model."""
import math
from dataclasses import dataclass
@dataclass(frozen=True)
class Voxel:
    distance:float=0.0
    weight:float=0.0

def fuse(old:Voxel, signed_distance:float, weight:float, truncation:float=.04, cap:float=64.) -> Voxel:
    if not all(math.isfinite(v) for v in (old.distance,old.weight,weight,truncation,cap)):
        raise ValueError('nonfinite fusion state')
    if truncation<=0 or cap<=0 or weight<0 or not 0<=old.weight<=cap: raise ValueError('invalid fusion options')
    if not math.isfinite(signed_distance) or signed_distance < -truncation or weight==0: return old
    accepted_weight=min(weight,cap-old.weight)
    if accepted_weight<=0:return old
    value=max(-1.,min(1.,signed_distance/truncation))
    total=old.weight+accepted_weight
    return Voxel((old.distance*old.weight+value*accepted_weight)/total,total)

def zero_crossing(a:Voxel,b:Voxel,x_a:float,x_b:float):
    if a.weight<=0 or b.weight<=0:return None
    if a.distance*b.distance>0 or a.distance==b.distance:return None
    t=a.distance/(a.distance-b.distance)
    return x_a+t*(x_b-x_a)

"""Independent geometry/solver reference for the HSS engineering specification.

No sensor accuracy is implied by numerical tolerances in this module.
"""
from __future__ import annotations
import math
from dataclasses import dataclass
import numpy as np

def points2(value) -> np.ndarray:
    a = np.asarray(value, dtype=np.float64)
    if a.ndim != 2 or a.shape[1] != 2 or len(a) < 3 or not np.isfinite(a).all():
        raise ValueError('finite Nx2 polygon, N>=3 required')
    return a

def signed_area(points) -> float:
    p = points2(points); q = p - p[0]
    return 0.5 * math.fsum(float(q[i,0]*q[(i+1)%len(q),1]-q[(i+1)%len(q),0]*q[i,1]) for i in range(len(q)))

def _cross(a,b,c):
    return float((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))

def _on_segment(a,b,p,eps=1e-9):
    return abs(_cross(a,b,p)) <= eps and np.all(p >= np.minimum(a,b)-eps) and np.all(p <= np.maximum(a,b)+eps)

def intersects(a,b,c,d,eps=1e-9):
    v = [_cross(a,b,c),_cross(a,b,d),_cross(c,d,a),_cross(c,d,b)]
    if v[0]*v[1] < -eps*eps and v[2]*v[3] < -eps*eps: return True
    return any((_on_segment(a,b,c,eps),_on_segment(a,b,d,eps),_on_segment(c,d,a,eps),_on_segment(c,d,b,eps)))

def validate_ring(points, eps=1e-9) -> None:
    p=points2(points); n=len(p)
    if abs(signed_area(p)) <= eps: raise ValueError('zero-area ring')
    for i in range(n):
        if np.linalg.norm(p[i]-p[(i+1)%n]) <= eps: raise ValueError('zero-length edge')
        for j in range(i+1,n):
            if j==i+1 or (i==0 and j==n-1): continue
            if intersects(p[i],p[(i+1)%n],p[j],p[(j+1)%n],eps): raise ValueError('self-intersection')
        # Adjacent backtracking is invalid; forward collinearity remains allowed.
        u=p[i]-p[(i-1)%n];v=p[(i+1)%n]-p[i]
        if abs(u[0]*v[1]-u[1]*v[0])<=eps and float(u@v)<-eps:
            raise ValueError('adjacent overlapping edges')

def point_location(point, ring, eps=1e-9) -> str:
    p=points2(ring); q=np.asarray(point,float);inside=False
    for i in range(len(p)):
        a,b=p[i],p[(i+1)%len(p)]
        if _on_segment(a,b,q,eps): return 'boundary'
        if (a[1]>q[1]) != (b[1]>q[1]):
            x=a[0]+(q[1]-a[1])*(b[0]-a[0])/(b[1]-a[1])
            if q[0]<x: inside=not inside
    return 'inside' if inside else 'outside'

def region_area(outer, holes=()) -> float:
    validate_ring(outer); o=points2(outer); hs=[]
    if signed_area(o)<=0: raise ValueError('outer ring must be counterclockwise')
    for hole in holes:
        h=points2(hole); validate_ring(h)
        if signed_area(h)>=0: raise ValueError('hole must be clockwise')
        if any(point_location(v,o)!='inside' for v in h): raise ValueError('hole outside/touching outer')
        for a,b in zip(h,np.roll(h,-1,axis=0)):
            for c,d in zip(o,np.roll(o,-1,axis=0)):
                if intersects(a,b,c,d): raise ValueError('hole crosses outer')
        for prev in hs:
            if point_location(h[0],prev)!='outside' or point_location(prev[0],h)!='outside':
                raise ValueError('nested/overlapping holes')
            for a,b in zip(h,np.roll(h,-1,axis=0)):
                for c,d in zip(prev,np.roll(prev,-1,axis=0)):
                    if intersects(a,b,c,d): raise ValueError('intersecting holes')
        hs.append(h)
    return signed_area(o)+sum(signed_area(h) for h in hs)

def unproject(u,v,z,fx,fy,cx,cy):
    if not np.isfinite([u,v,z,fx,fy,cx,cy]).all() or z<=0 or fx<=0 or fy<=0:
        raise ValueError('invalid calibration/depth')
    return np.array([(u-cx)*z/fx, -(v-cy)*z/fy, -z], dtype=float)

def project(point,fx,fy,cx,cy):
    p=np.asarray(point,float)
    if p.shape!=(3,) or not np.isfinite(p).all() or p[2]>=0 or fx<=0 or fy<=0: raise ValueError('point not in front of camera')
    return np.array([fx*p[0]/(-p[2])+cx,fy*(-p[1])/(-p[2])+cy])

def rigid_transform(T, points):
    T=np.asarray(T,float); p=np.asarray(points,float)
    if T.shape!=(4,4) or not np.isfinite(T).all() or not np.allclose(T[3],[0,0,0,1],atol=1e-9):
        raise ValueError('invalid homogeneous transform')
    R=T[:3,:3]
    if not np.allclose(R.T@R,np.eye(3),atol=1e-6) or abs(np.linalg.det(R)-1)>1e-6:
        raise ValueError('transform includes scale/reflection')
    if p.shape[-1]!=3 or not np.isfinite(p).all(): raise ValueError('invalid points')
    return p@R.T+T[:3,3]

def fit_plane_ransac(points, threshold=0.02, iterations=200, seed=101):
    """Deterministic proposal; uses no Manhattan orientation prior."""
    p=np.asarray(points,float)
    if p.ndim!=2 or p.shape[1]!=3 or len(p)<6 or not np.isfinite(p).all(): raise ValueError('invalid plane points')
    if threshold<=0 or iterations<1: raise ValueError('invalid fit options')
    rng=np.random.default_rng(seed);best=None;best_score=(-1,-math.inf)
    for _ in range(iterations):
        s=p[rng.choice(len(p),3,replace=False)]
        n=np.cross(s[1]-s[0],s[2]-s[0]);norm=np.linalg.norm(n)
        if norm<1e-10:continue
        n/=norm; d=-n@s[0];res=np.abs(p@n+d);mask=res<=threshold
        score=(int(mask.sum()),-float(np.median(res[mask])))
        if score>best_score: best=mask;best_score=score
    if best is None or int(best.sum())<6: raise ValueError('insufficient noncollinear support')
    for _ in range(3):
        q=p[best]; center=q.mean(axis=0);_,s,V=np.linalg.svd(q-center,full_matrices=False)
        if len(s)<3 or s[1]<1e-8: raise ValueError('rank-deficient support')
        n=V[-1];d=-n@center;res=np.abs(p@n+d);new=res<=threshold
        if new.sum()<6:break
        best=new
    k=int(np.argmax(np.abs(n)))
    if n[k]<0:n=-n;d=-d
    return {'normal':n,'offset':float(d),'inliers':best,'median_residual':float(np.median(res[best]))}

@dataclass(frozen=True)
class SolveResult:
    state: str
    coordinates: np.ndarray
    iterations: int
    constraint_max: float
    reason: str

def constrained_points(observed, distances=(), locks=None, max_iterations=40, feasibility=1e-7):
    """Small local equality-constrained reference using analytic Jacobians/KKT.

    Equalities describe the adopted model, not zero uncertainty of measurements.
    Failure returns the original coordinates unchanged. Production requires its
    own scale normalization, robust residual blocks and trust-region profiling.
    """
    p=np.asarray(observed,float)
    if p.ndim!=2 or p.shape[1]!=2 or not np.isfinite(p).all() or p.size>256:raise ValueError('invalid local problem')
    original=p.copy();x=p.ravel().copy();obs=x.copy();locks=locks or {}
    for idx,value in locks.items():
        if not 0<=idx<len(x) or not np.isfinite(value): raise ValueError('invalid lock')
    for i,j,L in distances:
        if not(0<=i<len(p) and 0<=j<len(p)) or i==j or not np.isfinite(L) or L<=0:raise ValueError('invalid distance')
    def constraints(y):
        c=[];rows=[];q=y.reshape(-1,2)
        for i,j,L in distances:
            d=q[i]-q[j];norm=np.linalg.norm(d)
            if norm<1e-12:raise ValueError('degenerate distance')
            row=np.zeros(len(y));row[2*i:2*i+2]=d/norm;row[2*j:2*j+2]=-d/norm
            c.append(norm-L);rows.append(row)
        for idx,value in sorted(locks.items()):
            row=np.zeros(len(y));row[idx]=1;c.append(y[idx]-value);rows.append(row)
        return np.asarray(c),np.asarray(rows).reshape(-1,len(y))
    for it in range(max_iterations):
        try:c,A=constraints(x)
        except ValueError:return SolveResult('rejected',original,it,math.inf,'degenerate distance')
        if len(c)==0:return SolveResult('applied',p.copy(),it,0.,'no constraints')
        H=np.eye(len(x));g=x-obs
        K=np.block([[H,A.T],[A,np.zeros((len(c),len(c)))]])
        rhs=-np.r_[g,c];solution,_,_,_=np.linalg.lstsq(K,rhs,rcond=1e-11)
        if np.linalg.norm(K@solution-rhs,ord=np.inf)>1e-6:
            return SolveResult('conflict',original,it,float(np.max(np.abs(c))),'inconsistent linearized equalities')
        step=solution[:len(x)]
        if max(abs(c))<=feasibility and np.linalg.norm(step,np.inf)<1e-8:
            return SolveResult('applied',x.reshape(-1,2).copy(),it,float(max(abs(c))),'feasible stationary point')
        rho=max(10.0,2*float(np.max(np.abs(solution[len(x):]))+1))
        def merit(y):
            cc,_=constraints(y)
            return .5*np.sum((y-obs)**2)+rho*np.sum(np.abs(cc))
        old=merit(x);alpha=1.0;accepted=False
        for _ in range(20):
            try:new=merit(x+alpha*step)
            except ValueError:new=math.inf
            if new<old-1e-12 or (np.linalg.norm(step,np.inf)<1e-8 and max(abs(c))<=feasibility):
                x+=alpha*step;accepted=True;break
            alpha*=.5
        if not accepted:
            return SolveResult('rejected',original,it,float(max(abs(c))),'line search did not improve merit')
    c,_=constraints(x)
    return SolveResult('rejected',original,max_iterations,float(max(abs(c))),'iteration budget exhausted')

class AnchorStore:
    """Regression oracle for replacement semantics, not Apple's reconstruction."""
    def __init__(self):self.anchors={};self.revisions={}
    def apply(self, identity, revision, vertices=None, remove=False):
        if revision<=self.revisions.get(identity,-1):raise ValueError('stale anchor revision')
        if not remove and (vertices is None or not np.isfinite(vertices).all()):raise ValueError('invalid vertices')
        self.revisions[identity]=revision
        if remove:self.anchors.pop(identity,None)
        else:self.anchors[identity]=np.asarray(vertices,float).copy()

from pathlib import Path
import json,copy,math
import numpy as np
import pytest
from jsonschema import Draft202012Validator,FormatChecker
from hss_reference.geometry import (signed_area,region_area,unproject,project,rigid_transform,fit_plane_ransac,constrained_points,AnchorStore)
from hss_reference.fusion import Voxel,fuse,zero_crossing
from hss_reference.validation import sequence,relative_asset_path,frame_metadata
from hss_reference.storage import commit_scene,recover_scene
ROOT=Path(__file__).resolve().parents[1]
def read(p):return json.loads(p.read_text())
def validator(name):return Draft202012Validator(read(ROOT/'schemas'/name),format_checker=FormatChecker())
POLYGONS=[p for p in sorted((ROOT/'fixtures/scenes').glob('*.json')) if 'outer' in read(p)]

@pytest.mark.parametrize('path',POLYGONS,ids=lambda p:p.stem)
def test_polygon_fixtures(path):
    d=read(path)
    if d['expected_valid']:assert region_area(d['outer'],d['holes'])==pytest.approx(d['expected_area_m2'],abs=1e-10)
    else:
        with pytest.raises(ValueError):region_area(d['outer'],d['holes'])

@pytest.mark.parametrize('path',sorted((ROOT/'schemas').glob('*.json')),ids=lambda p:p.stem)
def test_schema_metaschema(path):Draft202012Validator.check_schema(read(path))

@pytest.mark.parametrize('path',sorted((ROOT/'fixtures/commands/valid').glob('*.json')),ids=lambda p:p.stem)
def test_valid_commands(path):validator('command.schema.json').validate(read(path))

@pytest.mark.parametrize('path',sorted((ROOT/'fixtures/commands/invalid').glob('*.json')),ids=lambda p:p.stem)
def test_invalid_commands(path):assert list(validator('command.schema.json').iter_errors(read(path)))

@pytest.mark.parametrize('z',[.3,1.,3.,7.])
def test_projection_roundtrip(z):
    uv=np.array([72.,31.]);p=unproject(*uv,z,120,118,50,40)
    assert np.allclose(project(p,120,118,50,40),uv)
    assert p[2]<0

@pytest.mark.parametrize('T',[np.diag([2,1,1,1]),np.diag([-1,1,1,1]),np.zeros((4,4))])
def test_invalid_transforms(T):
    with pytest.raises(ValueError):rigid_transform(T,[[1,2,3]])

def test_translation_and_area_origin():
    p=[[0,0],[6,0],[6,5],[0,5]]
    assert signed_area(np.asarray(p)+1e6)==pytest.approx(30.)
    T=np.eye(4);T[:3,3]=[1,2,3];assert np.allclose(rigid_transform(T,[[0,0,0]]),[[1,2,3]])

@pytest.mark.parametrize('normal',[[1,0,0],[1,2,0],[1,2,3],[0,1,.6]])
def test_ransac_plane_outliers(normal):
    n=np.asarray(normal,float);n/=np.linalg.norm(n)
    u=np.cross(n,[0,0,1])
    if np.linalg.norm(u)<1e-6:u=np.cross(n,[0,1,0])
    u/=np.linalg.norm(u);v=np.cross(n,u)
    rng=np.random.default_rng(2);coeff=rng.uniform(-2,2,(150,2))
    p=coeff[:,0,None]*u+coeff[:,1,None]*v+2*n+rng.normal(0,.001,(150,3))
    p=np.r_[p,rng.uniform(-4,4,(40,3))]
    fit=fit_plane_ransac(p,threshold=.01)
    assert abs(fit['normal']@n)>.999
    assert int(fit['inliers'].sum())>=148

def test_ransac_degenerate():
    p=np.array([[i,0,0] for i in range(10)])
    with pytest.raises(ValueError):fit_plane_ransac(p)

def test_constraint_distance_and_lock():
    r=constrained_points([[0,0],[1.8,.1]],[(0,1,2.)],{0:0.,1:0.})
    assert r.state=='applied',r.reason
    assert np.linalg.norm(r.coordinates[1]-r.coordinates[0])==pytest.approx(2.,abs=1e-7)
    assert np.max(np.abs(r.coordinates[0]))<1e-7

def test_constraint_conflict_keeps_original():
    p=np.array([[0,0],[1,0]],float)
    r=constrained_points(p,[(0,1,2.)],{0:0,1:0,2:1,3:0})
    assert r.state=='conflict';assert np.array_equal(r.coordinates,p)

def test_anchor_replacement_and_removal():
    s=AnchorStore();s.apply('wall',1,[[0,0,0],[1,0,0]])
    s.apply('wall',2,[[0,0,0],[2,0,0]]);assert len(s.anchors)==1
    assert s.anchors['wall'][1,0]==2
    with pytest.raises(ValueError):s.apply('wall',1,[[0,0,0]])
    s.apply('wall',3,remove=True);assert not s.anchors

@pytest.mark.parametrize('distance,expected',[(-1,0),(-.04,1),(0,1),(.01,1),(float('nan'),0)])
def test_tsdf_observation_gate(distance,expected):assert fuse(Voxel(),distance,1).weight==expected

def test_tsdf_unknown_is_not_surface():
    assert zero_crossing(Voxel(0,0),Voxel(-1,1),0,1) is None
    assert zero_crossing(Voxel(1,1),Voxel(-1,1),0,1)==.5

def test_tsdf_weight_saturation():
    old=Voxel(.2,64);assert fuse(old,-.01,1)==old

@pytest.mark.parametrize('value',['18446744073709551616','01','-1','1.0','１',1])
def test_bad_counter(value):
    with pytest.raises(ValueError):sequence(value)

def test_uint64_roundtrip():assert sequence('18446744073709551615')==2**64-1

@pytest.mark.parametrize('p',['../x','/x','a/../b','a//b','a\\b','C:x','a/./b','x\x00y'])
def test_bad_path(p):
    with pytest.raises(ValueError):relative_asset_path(p)

def test_good_path():assert relative_asset_path('capture/one/depth.bin')=='capture/one/depth.bin'

def test_scene_schema():validator('scene.schema.json').validate(read(ROOT/'fixtures/scenes/scene.example.json'))

def test_frame_schema_and_semantics():
    f=read(ROOT/'fixtures/frames/frame.example.json');validator('frame.schema.json').validate(f);assert frame_metadata(f)

@pytest.mark.parametrize('patch',[{'depth_row_stride_bytes':1},{'depth_byte_length':1},{'depth_source':'smoothedSceneDepth'},{'intrinsics':[0,1,0,0]}])
def test_bad_frame_semantics(patch):
    f=read(ROOT/'fixtures/frames/frame.example.json');f.update(patch)
    with pytest.raises(ValueError):frame_metadata(f)

@pytest.mark.parametrize('stage',['scene','previous','temporary'])
def test_commit_interruption_preserves_previous(tmp_path,stage):
    commit_scene(tmp_path,{'scene_revision':'1','value':'old'})
    with pytest.raises(RuntimeError):commit_scene(tmp_path,{'scene_revision':'2','value':'new'},fail_after=stage)
    assert recover_scene(tmp_path)['scene_revision']=='1'

def test_commit_and_corruption_fallback(tmp_path):
    commit_scene(tmp_path,{'scene_revision':'1'});commit_scene(tmp_path,{'scene_revision':'2'})
    assert recover_scene(tmp_path)['scene_revision']=='2'
    (tmp_path/'scene-2.json').write_text('corrupted')
    assert recover_scene(tmp_path)['scene_revision']=='1'

def test_nonfinite_json_rejected(tmp_path):
    with pytest.raises(ValueError):commit_scene(tmp_path,{'scene_revision':'1','x':float('nan')})

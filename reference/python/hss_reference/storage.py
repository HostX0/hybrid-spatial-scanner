"""Crash-boundary oracle for immutable scene + manifest-last commit.

This POSIX reference does not certify iOS data-protection or APFS semantics.
"""
import hashlib,json,os
from pathlib import Path

def _write_fsync(path:Path,data:bytes):
    with open(path,'xb') as f:f.write(data);f.flush();os.fsync(f.fileno())

def commit_scene(folder,scene,fail_after=None):
    root=Path(folder);root.mkdir(parents=True,exist_ok=True)
    revision=str(scene['scene_revision']);name=f'scene-{revision}.json'
    payload=json.dumps(scene,allow_nan=False,sort_keys=True,separators=(',',':')).encode()
    data_path=root/name
    if data_path.exists():raise ValueError('immutable scene already exists')
    _write_fsync(data_path,payload)
    if fail_after=='scene':raise RuntimeError('injected interruption after scene')
    manifest={'scene_path':name,'scene_revision':revision,'sha256':hashlib.sha256(payload).hexdigest()}
    old=root/'manifest.json';prev=root/'manifest.previous.json'
    if old.exists():
        prev_tmp=root/'manifest.previous.tmp';prev_tmp.unlink(missing_ok=True)
        _write_fsync(prev_tmp,old.read_bytes());os.replace(prev_tmp,prev)
    if fail_after=='previous':raise RuntimeError('injected interruption after backup')
    tmp=root/'manifest.tmp';tmp.unlink(missing_ok=True)
    _write_fsync(tmp,json.dumps(manifest).encode())
    if fail_after=='temporary':raise RuntimeError('injected interruption before manifest replace')
    os.replace(tmp,old)
    fd=os.open(root,os.O_RDONLY)
    try:os.fsync(fd)
    finally:os.close(fd)
    return manifest

def recover_scene(folder):
    root=Path(folder)
    for manifest_name in ['manifest.json','manifest.previous.json']:
        try:
            m=json.loads((root/manifest_name).read_text());name=m['scene_path']
            if '/' in name or '\\' in name or name.startswith('.') :continue
            payload=(root/name).read_bytes()
            if hashlib.sha256(payload).hexdigest()!=m['sha256']:continue
            result=json.loads(payload)
            if str(result['scene_revision'])!=m['scene_revision']:continue
            return result
        except (OSError,ValueError,KeyError,TypeError):continue
    raise ValueError('no valid committed snapshot')

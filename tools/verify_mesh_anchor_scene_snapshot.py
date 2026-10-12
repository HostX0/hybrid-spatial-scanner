#!/usr/bin/env python3
import argparse, json, pathlib, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cxx', default='c++')
    p.add_argument('--sanitize', action='store_true')
    a=p.parse_args()
    with tempfile.TemporaryDirectory() as td:
        exe=pathlib.Path(td)/'scene_snapshot_test'
        cmd=[a.cxx,'-std=c++20','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(ROOT/'include'),str(ROOT/'tests/hss_mesh_anchor_scene_snapshot_test.cpp'),'-o',str(exe)]
        if a.sanitize:
            cmd[2:2]=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined']
        else:
            cmd[2:2]=['-O2']
        subprocess.run(cmd,check=True)
        run=subprocess.run([str(exe)],check=True,text=True,capture_output=True)
        print(run.stdout.splitlines()[0])
        line=next((x for x in run.stdout.splitlines() if x.startswith('scene_json=')), None)
        if line is None:
            raise SystemExit('missing scene_json output')
        scene=json.loads(line[len('scene_json='):])
        expected_top=['schema_version','project_id','scene_revision','source_is_synthetic','units','basis','epochs','nodes','edges','surfaces','features','regions','objects','constraints','issues','quality_state']
        if list(scene.keys()) != expected_top:
            raise SystemExit(f'top-level order/fields mismatch: {list(scene.keys())}')
        if scene['schema_version']!='1.1' or scene['units']!='m' or scene['quality_state']!='provisional':
            raise SystemExit('fixed Scene 1.1 fields mismatch')
        if scene['scene_revision']!='18446744073709551615':
            raise SystemExit('uint64 revision was not preserved as canonical string')
        if len(scene['epochs'])!=2 or scene['epochs'][0]['capture_id']!='aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa':
            raise SystemExit('epoch ordering mismatch')
        for empty_key in ['features','regions','objects','constraints','issues']:
            if scene[empty_key] != []:
                raise SystemExit(f'{empty_key} must remain empty in this mesh-only oracle')
        print(f'json_parse=PASS nodes={len(scene["nodes"])} edges={len(scene["edges"])} surfaces={len(scene["surfaces"])} epochs={len(scene["epochs"])}')

    # Structural check of the helper source's fixed Scene 1.1 top-level contract.
    text=(ROOT/'include/hss/mesh_anchor_scene_snapshot.hpp').read_text()
    required=['schema_version','project_id','scene_revision','source_is_synthetic','units','basis','epochs','nodes','edges','surfaces','features','regions','objects','constraints','issues','quality_state']
    for key in required:
        if f'\\"{key}\\"' not in text:
            raise SystemExit(f'missing emitted top-level key: {key}')
    print('python_contract=PASS keys='+str(len(required)))

if __name__=='__main__': main()

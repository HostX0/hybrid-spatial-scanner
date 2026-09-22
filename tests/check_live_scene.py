"""Validate the emitted C ABI synthetic fixture once, including metric semantics.

Usage: python tests/check_live_scene.py work/live-scene.json
The C++ host test generates this file; it is never a hardware capture.
"""
import json
import math
import sys
from pathlib import Path

import jsonschema

scene = json.loads(Path(sys.argv[1]).read_text())
schema = json.loads((Path(__file__).resolve().parents[1] / "schemas/scene.schema.json").read_text())
jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker()).validate(scene)
collections = ("nodes", "edges", "surfaces", "regions", "issues")
ids = {x["id"] for key in collections for x in scene[key]}
assert len(ids) == sum(len(scene[key]) for key in collections)
nodes = {n["id"]: n for n in scene["nodes"]}
surfaces = {s["id"]: s for s in scene["surfaces"]}
for edge in scene["edges"]:
    assert edge["start_node_id"] in nodes and edge["end_node_id"] in nodes
    assert set(edge["surface_ids"]) <= surfaces.keys()
    assert edge["protected_topology"]
for surface in scene["surfaces"]:
    if surface["kind"] == "plane":
        assert math.isclose(sum(x*x for x in surface["normal_unit"]), 1, abs_tol=1e-10)
outline = [(0,0), (6,0), (6,5), (4,5), (4,4.5), (3.85,4.5), (3.85,5), (2,5), (0,4)]
area = sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(outline, outline[1:]+outline[:1])) / 2
assert len(scene["regions"]) == 1
assert math.isclose(scene["regions"][0]["area_m2"], area, abs_tol=1e-6)
assert scene["source_is_synthetic"] is True
assert scene["scene_revision"] == "3"
assert all(n["provenance"]["uncertainty_m"] is None for n in scene["nodes"])
print(f"Scene schema, UUID/reference, plane, provenance and revision checks PASS; independent area {area}; emitted {scene['regions'][0]['area_m2']}")

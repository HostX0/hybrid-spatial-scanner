#!/usr/bin/env python3
"""Deterministic characterization of live mesh patch grouping.

This tool mirrors the local adjacency/normal/offset grouping policy used by
SpatialCore/src/live_reconstruction.cpp. It is intentionally a characterization
harness: it records order sensitivity and narrow-feature preservation without
silently changing production thresholds.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

Vec3 = tuple[float, float, float]
Tri = tuple[int, int, int]


@dataclass(frozen=True)
class Policy:
    max_prior_samples: int
    normal_dot_min: float
    plane_offset_max_m: float

    @property
    def normal_angle_max_deg(self) -> float:
        return math.degrees(math.acos(min(1.0, max(-1.0, self.normal_dot_min))))


@dataclass
class Patch:
    normal: Vec3
    offset_m: float
    sample_count: int
    triangle_ids: list[int]


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def norm(v: Vec3) -> float:
    return math.sqrt(dot(v, v))


def plane(a: Vec3, b: Vec3, c: Vec3) -> tuple[Vec3, float] | None:
    n = cross(sub(b, a), sub(c, a))
    length = norm(n)
    if length <= 1.0e-10:
        return None
    n = (n[0] / length, n[1] / length, n[2] / length)
    return n, -dot(n, a)


def extract_policy(path: Path) -> tuple[Policy, str]:
    source = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"prior\.samples\.size\(\)\s*<=\s*(\d+)\s*&&\s*"
        r"std::abs\(dot\(prior\.normal,\s*n\)\)\s*>\s*([0-9.eE+-]+)\s*&&\s*"
        r"std::abs\(dot\(prior\.normal,\s*p\[0\]\)\s*\+\s*prior\.offset\)\s*<\s*([0-9.eE+-]+)",
        re.MULTILINE,
    )
    match = pattern.search(source)
    if not match:
        raise RuntimeError("could not locate live patch-grouping policy in source")
    policy = Policy(int(match.group(1)), float(match.group(2)), float(match.group(3)))
    return policy, hashlib.sha256(source.encode("utf-8")).hexdigest()


def group(
    vertices: Sequence[Vec3], triangles: Sequence[Tri], policy: Policy
) -> tuple[list[Patch], list[int | None]]:
    patches: list[Patch] = []
    adjacency: dict[tuple[int, int], int] = {}
    assignment: list[int | None] = []

    for triangle_id, triangle in enumerate(triangles):
        points = [vertices[index] for index in triangle]
        fitted = plane(points[0], points[1], points[2])
        if fitted is None:
            assignment.append(None)
            continue
        normal, offset_m = fitted
        patch_index = len(patches)
        for edge_index in range(3):
            edge = tuple(
                sorted((triangle[edge_index], triangle[(edge_index + 1) % 3]))
            )
            prior_index = adjacency.get(edge)
            if prior_index is None:
                continue
            prior = patches[prior_index]
            if (
                prior.sample_count <= policy.max_prior_samples
                and abs(dot(prior.normal, normal)) > policy.normal_dot_min
                and abs(dot(prior.normal, points[0]) + prior.offset_m)
                < policy.plane_offset_max_m
            ):
                patch_index = prior_index
                break

        if patch_index == len(patches):
            patches.append(Patch(normal, offset_m, 0, []))
        patches[patch_index].sample_count += 3
        patches[patch_index].triangle_ids.append(triangle_id)
        for edge_index in range(3):
            edge = tuple(
                sorted((triangle[edge_index], triangle[(edge_index + 1) % 3]))
            )
            adjacency[edge] = patch_index
        assignment.append(patch_index)

    return patches, assignment


def planar_grid(
    jitter_m: float = 0.0, *, nx: int = 20, ny: int = 10
) -> tuple[list[Vec3], list[Tri]]:
    dx, dy = 0.05, 0.10
    vertices: list[Vec3] = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            # Stable integer-derived perturbation: no RNG/version dependence.
            pattern = (((i * 17 + j * 31) % 11) - 5) / 5.0
            vertices.append((i * dx, j * dy, jitter_m * pattern))

    def index(i: int, j: int) -> int:
        return j * (nx + 1) + i

    triangles: list[Tri] = []
    for j in range(ny):
        for i in range(nx):
            a, b = index(i, j), index(i + 1, j)
            c, d = index(i + 1, j + 1), index(i, j + 1)
            triangles.extend(((a, b, c), (a, c, d)))
    return vertices, triangles


def shallow_crease(
    angle_deg: float, *, bridge_first: bool
) -> tuple[list[Vec3], list[Tri]]:
    width, height = 0.50, 1.00
    angle = math.radians(angle_deg)
    vertices: list[Vec3] = [
        (0.0, 0.0, 0.0),
        (0.0, height, 0.0),
        (-width, 0.0, 0.0),
        (-width, height, 0.0),
        (width * math.cos(angle), 0.0, width * math.sin(angle)),
        (width * math.cos(angle), height, width * math.sin(angle)),
    ]
    first_face: list[Tri] = [(2, 0, 1), (2, 1, 3)]
    second_face: list[Tri] = (
        [(0, 5, 1), (0, 4, 5)]
        if bridge_first
        else [(0, 4, 5), (0, 5, 1)]
    )
    return vertices, first_face + second_face


def protrusion_chain() -> tuple[list[Vec3], list[Tri]]:
    # Horizontal plan chain extruded vertically: approach -> 0.50 m return ->
    # 0.15 m front -> 0.50 m return -> continuation. These dimensions are
    # synthetic regression geometry, not product defaults.
    plan = [
        (-1.00, 0.0),
        (0.00, 0.0),
        (0.00, 0.50),
        (0.15, 0.50),
        (0.15, 0.0),
        (1.15, 0.0),
    ]
    vertices: list[Vec3] = []
    for x, z in plan:
        vertices.extend(((x, 0.0, z), (x, 2.0, z)))
    triangles: list[Tri] = []
    for i in range(len(plan) - 1):
        a, b = 2 * i, 2 * (i + 1)
        triangles.extend(((a, b, b + 1), (a, b + 1, a + 1)))
    return vertices, triangles


def summarize(policy: Policy, source_sha256: str | None) -> dict[str, object]:
    perfect_patches, _ = group(*planar_grid(0.0), policy)
    low_noise_patches, _ = group(*planar_grid(0.00005), policy)
    higher_noise_patches, _ = group(*planar_grid(0.00010), policy)

    crease_cases: dict[str, object] = {}
    for angle in (0.20, 0.25, 0.30, 1.00):
        natural, natural_assignment = group(
            *shallow_crease(angle, bridge_first=False), policy
        )
        bridge, bridge_assignment = group(
            *shallow_crease(angle, bridge_first=True), policy
        )
        crease_cases[f"{angle:.2f}"] = {
            "natural_order_patch_count": len(natural),
            "bridge_first_patch_count": len(bridge),
            "natural_assignment": natural_assignment,
            "bridge_first_assignment": bridge_assignment,
            "order_sensitive": len(natural) != len(bridge),
        }

    protrusion, protrusion_assignment = group(*protrusion_chain(), policy)

    sweeps: list[dict[str, object]] = []
    for max_angle_deg in (policy.normal_angle_max_deg, 0.5, 1.0, 2.0):
        candidate = Policy(
            policy.max_prior_samples,
            math.cos(math.radians(max_angle_deg)),
            policy.plane_offset_max_m,
        )
        noisy, _ = group(*planar_grid(0.00010), candidate)
        crease, _ = group(*shallow_crease(0.50, bridge_first=True), candidate)
        sweeps.append(
            {
                "normal_angle_max_deg": max_angle_deg,
                "noisy_0_10mm_patch_count": len(noisy),
                "half_degree_bridge_first_patch_count": len(crease),
            }
        )

    order_sensitive_angles = [
        angle for angle, case in crease_cases.items() if bool(case["order_sensitive"])
    ]
    return {
        "source_sha256": source_sha256,
        "policy": {
            "max_prior_samples": policy.max_prior_samples,
            "normal_dot_min": policy.normal_dot_min,
            "normal_angle_max_deg": policy.normal_angle_max_deg,
            "plane_offset_max_m": policy.plane_offset_max_m,
        },
        "characterization": {
            "perfect_grid": {
                "triangles": 400,
                "patch_count": len(perfect_patches),
                "triangle_counts_per_patch": [
                    len(patch.triangle_ids) for patch in perfect_patches
                ],
            },
            "coplanar_0_05mm_jitter": {"patch_count": len(low_noise_patches)},
            "coplanar_0_10mm_jitter": {"patch_count": len(higher_noise_patches)},
            "shallow_crease": crease_cases,
            "protected_protrusion_chain": {
                "patch_count": len(protrusion),
                "assignment": protrusion_assignment,
                "expected_plan_segments": 5,
            },
        },
        "threshold_sweep": sweeps,
        "findings": {
            "triangle_order_sensitivity_detected": bool(order_sensitive_angles),
            "order_sensitive_angles_deg": order_sensitive_angles,
            "threshold_relaxation_is_not_safe_by_itself": any(
                row["half_degree_bridge_first_patch_count"] == 1
                for row in sweeps[1:]
            ),
            "production_threshold_changed": False,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("SpatialCore/src/live_reconstruction.cpp"),
        help="production live_reconstruction.cpp used to extract the policy",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    policy, source_sha256 = extract_policy(args.source)
    report = summarize(policy, source_sha256)

    # Characterization acceptance: the current production policy must expose
    # the known order-sensitive shallow-crease behavior rather than a harness
    # silently reporting a clean result. This is evidence for the next design
    # change, not a claim that the behavior is acceptable.
    findings = report["findings"]
    if not findings["triangle_order_sensitivity_detected"]:
        raise RuntimeError("expected current order-sensitivity was not reproduced")
    if not findings["threshold_relaxation_is_not_safe_by_itself"]:
        raise RuntimeError(
            "threshold sweep no longer demonstrates the topology tradeoff"
        )
    protrusion = report["characterization"]["protected_protrusion_chain"]
    if protrusion["patch_count"] != protrusion["expected_plan_segments"]:
        raise RuntimeError(
            "narrow protrusion chain no longer preserves each supported face"
        )

    payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

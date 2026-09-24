#!/usr/bin/env python3
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import textwrap

ROOT = pathlib.Path(__file__).resolve().parents[1]
TEST = ROOT / "tests" / "hss_mesh_anchor_scene_entities_test.cpp"
INCLUDE = ROOT / "SpatialCore" / "include"

SCENE_GRAPH_STUB = r'''#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
struct HSSVec3d {
  double x{}, y{}, z{};
  bool operator==(const HSSVec3d &) const = default;
};
namespace hss {
namespace coordinate {
struct PlanPoint {
  double u_m{}, v_m{};
  bool operator==(const PlanPoint &) const = default;
};
} // namespace coordinate
namespace mesh_anchor_scene_graph {
enum class Status : uint8_t {
  kOk, kCutUnavailable, kSurfaceUnavailable, kInvalidArgument, kInvalidPlan,
  kInvalidGeometry, kResourceLimit
};
struct JoinCell {
  int64_t x{}, y{}, z{};
  bool operator==(const JoinCell &) const = default;
};
struct Node {
  JoinCell cell{};
  HSSVec3d point_m{};
  coordinate::PlanPoint uv{};
  std::string node_key;
  std::vector<std::string> surface_keys;
  std::vector<size_t> edge_indices;
  bool operator==(const Node &) const = default;
};
struct Edge {
  size_t a{}, b{};
  std::string edge_key;
  std::vector<std::string> surface_keys;
  std::vector<size_t> source_fragment_indices;
  std::vector<size_t> source_triangle_indices;
  bool operator==(const Edge &) const = default;
};
struct Result {
  Status status{Status::kOk};
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  size_t merged_endpoint_count{};
  size_t merged_edge_fragment_count{};
  size_t multi_surface_edge_count{};
  bool operator==(const Result &) const = default;
};
} // namespace mesh_anchor_scene_graph
} // namespace hss
'''

SURFACE_STUB = r'''#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace hss {
namespace plane_fit {
struct Point3d {
  double x{}, y{}, z{};
  bool operator==(const Point3d &) const = default;
};
struct ResidualSummary {
  double weighted_rms_m{}, median_absolute_m{}, maximum_absolute_m{};
  bool operator==(const ResidualSummary &) const = default;
};
struct Plane {
  Point3d normal;
  double offset_m{};
  ResidualSummary residuals;
  bool operator==(const Plane &) const = default;
};
enum class Status : uint8_t {
  kOk, kInvalidOptions, kNonFiniteInput, kInvalidSampleWeight,
  kInsufficientEvidence, kDegenerateInput, kRankDeficient,
  kWorkLimitExceeded, kRefinementDidNotConverge, kNumericalFailure
};
} // namespace plane_fit
namespace mesh_patch {
using Triangle = std::array<uint32_t, 3>;
} // namespace mesh_patch
namespace mesh_anchor_surface {
enum class Status : uint8_t {
  kOk, kCaptureUnavailable, kMetricUnavailable, kCutUnavailable,
  kInvalidArgument, kInvalidPlan, kResourceLimit
};
struct Surface {
  std::string surface_key;
  std::string support_identity;
  mesh_patch::Triangle canonical_triangle{};
  size_t triangle_count{};
  plane_fit::Status fit_status{plane_fit::Status::kInvalidOptions};
  std::optional<plane_fit::Plane> plane;
  std::vector<size_t> cut_fragment_indices;
  bool operator==(const Surface &) const = default;
};
struct Result {
  Status status{Status::kOk};
  std::vector<Surface> surfaces;
  size_t fitted_surface_count{};
  size_t fallback_surface_count{};
};
} // namespace mesh_anchor_surface
} // namespace hss
'''


def write_stubs(root: pathlib.Path) -> pathlib.Path:
    include = root / "hss"
    include.mkdir(parents=True)
    (include / "mesh_anchor_scene_graph.hpp").write_text(
        textwrap.dedent(SCENE_GRAPH_STUB), encoding="utf-8"
    )
    (include / "mesh_anchor_surface_assembly.hpp").write_text(
        textwrap.dedent(SURFACE_STUB), encoding="utf-8"
    )
    return root


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="hss-scene-entities-") as td:
        temp = pathlib.Path(td)
        stub_include = write_stubs(temp / "stubs")
        exe = temp / "scene_entities_test"
        optimization = "-O1" if args.sanitize else "-O2"
        cmd = [
            args.cxx,
            "-std=c++20",
            optimization,
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-I",
            str(stub_include),
            "-I",
            str(INCLUDE),
            str(TEST),
            "-o",
            str(exe),
        ]
        if args.sanitize:
            cmd[3:3] = [
                "-g",
                "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
            ]
        print("compile:", " ".join(cmd))
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(exe), "--emit-json"], text=True, capture_output=True)
        sys.stderr.write(run.stderr)
        if run.returncode != 0:
            sys.stdout.write(run.stdout)
            return run.returncode

        tagged = {}
        summary = None
        for line in run.stdout.splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in {"NODES_JSON", "EDGES_JSON", "SURFACES_JSON"}:
                tagged[key] = json.loads(value)
            elif key == "assertions":
                summary = line
        if set(tagged) != {"NODES_JSON", "EDGES_JSON", "SURFACES_JSON"}:
            raise RuntimeError("test did not emit all schema-shaped JSON arrays")

        node_fields = {"id", "position_m", "provenance", "position_locked"}
        edge_fields = {"id", "start_node_id", "end_node_id", "surface_ids",
                       "parent_edge_ids", "protected_topology", "provenance"}
        surface_fields = {"id", "kind", "normal_unit", "offset_m",
                          "boundary_node_ids", "mesh_asset", "fit_residual_m",
                          "provenance"}
        if any(set(item) != node_fields for item in tagged["NODES_JSON"]):
            raise RuntimeError("node JSON field set differs from scene schema")
        if any(set(item) != edge_fields for item in tagged["EDGES_JSON"]):
            raise RuntimeError("edge JSON field set differs from scene schema")
        if any(set(item) != surface_fields for item in tagged["SURFACES_JSON"]):
            raise RuntimeError("surface JSON field set differs from scene schema")
        if summary is not None:
            print(summary)
        print("json_parse_and_field_contract=PASS")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())

#include "hss/mesh_anchor_surface_assembly.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
size_t assertions = 0;
size_t failures = 0;
void check(bool condition, const char *message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using hss::mesh_patch::Triangle;

Triangle canonical(Triangle t) {
  std::sort(t.begin(), t.end());
  return t;
}

std::string suffix(const Triangle &t) {
  const auto c = canonical(t);
  return std::to_string(c[0]) + ":" + std::to_string(c[1]) + ":" +
         std::to_string(c[2]);
}

hss::mesh_anchor_patch::ComponentPlan component(size_t face,
                                                 bool force_fit_failure = false) {
  const uint32_t base = static_cast<uint32_t>(face * 4U);
  Triangle key{base, base + 1U, base + 2U};
  hss::mesh_anchor_patch::ComponentPlan c;
  c.canonical_triangle = key;
  c.triangle_count = 2U;
  c.surface_key = "support-A:patch:" + suffix(key);
  for (uint32_t i = 0; i < 6U; ++i) {
    c.sample_vertex_indices.push_back(base + (i % 4U));
    const double x = force_fit_failure ? -1000.0 : static_cast<double>(face) + 0.1 * i;
    c.fit_samples.push_back({{x, static_cast<double>(i % 3U), 0.25 * face}, 1.0,
                             77U});
  }
  return c;
}

struct Fixture {
  hss::mesh_anchor_reconstruction::Prepared prepared;
  hss::mesh_anchor_cut::Result cuts;
};

std::vector<size_t> permutation(size_t round) {
  std::vector<size_t> p(10U);
  std::iota(p.begin(), p.end(), 0U);
  uint64_t state = 0x9e3779b97f4a7c15ULL ^ (round * 0x5851f42d4c957f2dULL);
  for (size_t i = p.size(); i > 1U; --i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const size_t j = static_cast<size_t>(state % i);
    std::swap(p[i - 1U], p[j]);
  }
  return p;
}

Fixture make_fixture(size_t round, bool fail_last_fit = false) {
  Fixture f;
  f.prepared.capture_status = hss::mesh_anchor_reconstruction::CaptureStatus::kOk;
  f.prepared.metric_plan.status = hss::mesh_anchor_patch::Status::kOk;
  f.cuts.status = hss::mesh_anchor_cut::Status::kOk;
  f.cuts.support_identity = "support-A";
  for (size_t face = 0; face < 5U; ++face)
    f.prepared.metric_plan.components.push_back(
        component(face, fail_last_fit && face == 4U));

  std::vector<Triangle> original;
  for (size_t face = 0; face < 5U; ++face) {
    const uint32_t base = static_cast<uint32_t>(face * 4U);
    original.push_back({base, base + 1U, base + 2U});
    original.push_back({base, base + 2U, base + 3U});
  }
  const auto p = permutation(round);
  f.prepared.local_triangles.resize(original.size());
  f.prepared.metric_plan.component_of_triangle.resize(original.size());
  std::array<size_t, 5> first_triangle{};
  first_triangle.fill(static_cast<size_t>(-1));
  for (size_t i = 0; i < p.size(); ++i) {
    const size_t source = p[i];
    const size_t face = source / 2U;
    f.prepared.local_triangles[i] = original[source];
    f.prepared.metric_plan.component_of_triangle[i] = face;
    if (first_triangle[face] == static_cast<size_t>(-1))
      first_triangle[face] = i;
  }

  for (size_t face = 0; face < 5U; ++face) {
    const size_t ti = first_triangle[face];
    f.cuts.fragments.push_back(
        {ti, f.prepared.local_triangles[ti],
         f.prepared.metric_plan.components[face].surface_key});
  }
  return f;
}

std::vector<std::tuple<std::string, bool, size_t>> signature(
    const hss::mesh_anchor_surface::Result &result) {
  std::vector<std::tuple<std::string, bool, size_t>> out;
  for (const auto &surface : result.surfaces)
    out.emplace_back(surface.surface_key, surface.plane.has_value(),
                     surface.cut_fragment_indices.size());
  return out;
}
} // namespace

namespace hss::plane_fit {
size_t g_fit_calls = 0U;
bool g_options_ok = true;
Result fit(const std::vector<WeightedSample> &samples, const Options &options) {
  ++g_fit_calls;
  g_options_ok = g_options_ok && options.inlier_threshold_m == 0.002 &&
                 options.max_input_samples == 96U &&
                 options.candidate_iterations == 24U &&
                 options.max_refinement_iterations == 4U &&
                 options.max_residual_evaluations == 4096U &&
                 options.min_support_samples == 3U &&
                 options.min_distinct_support_points == 3U &&
                 options.seed == 424242U;
  if (samples.empty() || samples.front().point.x < -999.0)
    return {Status::kInsufficientEvidence, std::nullopt};
  Plane p;
  p.normal = {0.0, 0.0, 1.0};
  p.offset_m = -samples.front().point.z;
  p.residuals.weighted_rms_m = 0.0005;
  p.support.sample_count = samples.size();
  p.support.distinct_point_count = samples.size();
  p.support.total_weight = static_cast<double>(samples.size());
  p.diversity.group_count = 1U;
  p.inlier_indices.resize(samples.size());
  std::iota(p.inlier_indices.begin(), p.inlier_indices.end(), 0U);
  return {Status::kOk, std::move(p)};
}
} // namespace hss::plane_fit

int main() {
  using namespace hss;
  using mesh_anchor_surface::Status;

  std::vector<std::tuple<std::string, bool, size_t>> baseline;
  for (size_t round = 0; round < 32U; ++round) {
    auto f = make_fixture(round);
    plane_fit::g_fit_calls = 0U;
    plane_fit::g_options_ok = true;
    mesh_anchor_surface::Options options;
    options.deterministic_seed = 424242U;
    auto result = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A", options);
    check(result.status == Status::kOk, "permutation assembly succeeds");
    check(result.surfaces.size() == 5U, "five protected surfaces retained");
    check(result.fitted_surface_count == 5U, "all five fixture surfaces fit");
    check(result.fallback_surface_count == 0U, "no fallback for supported fixture");
    check(plane_fit::g_fit_calls == 5U, "fit called once per finalized component");
    check(plane_fit::g_options_ok, "live bounded plane-fit options preserved exactly");
    for (size_t face = 0; face < result.surfaces.size(); ++face) {
      const auto &surface = result.surfaces[face];
      check(surface.support_identity == "support-A", "support identity preserved");
      check(surface.triangle_count == 2U, "component triangle count preserved");
      check(surface.cut_fragment_indices.size() == 1U,
            "one deterministic cut fragment bound per protected face");
      check(surface.cut_fragment_indices[0] == face,
            "cut-fragment result ordering remains stable");
      check(surface.fit_status == plane_fit::Status::kOk, "fit status preserved");
      check(surface.plane.has_value(), "fitted plane retained");
    }
    const auto sig = signature(result);
    if (round == 0U)
      baseline = sig;
    else
      check(sig == baseline, "surface assembly signature is triangle-order invariant");
  }

  {
    auto f = make_fixture(0U, true);
    mesh_anchor_surface::Options options;
    options.deterministic_seed = 424242U;
    auto result = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A", options);
    check(result.status == Status::kOk, "fit failure does not discard stable surface assembly");
    check(result.fitted_surface_count == 4U, "four components remain fitted");
    check(result.fallback_surface_count == 1U, "failed fit becomes one mesh fallback");
    check(!result.surfaces.back().plane.has_value(), "failed fit exposes no partial plane");
    check(result.surfaces.back().fit_status == plane_fit::Status::kInsufficientEvidence,
          "fit failure reason preserved");
  }

  {
    auto f = make_fixture(0U);
    f.prepared.capture_status = mesh_anchor_reconstruction::CaptureStatus::kInvalidGeometry;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kCaptureUnavailable && r.surfaces.empty(),
          "capture failure remains explicit and derived output empty");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.status = mesh_anchor_patch::Status::kResourceLimit;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kMetricUnavailable && r.surfaces.empty(),
          "metric failure remains explicit without invented surfaces");
  }
  {
    auto f = make_fixture(0U);
    f.cuts.status = mesh_anchor_cut::Status::kResourceLimit;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kCutUnavailable && r.surfaces.empty(),
          "cut failure remains explicit");
  }
  {
    auto f = make_fixture(0U);
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "");
    check(r.status == Status::kInvalidArgument, "empty support rejected");
    f.cuts.support_identity = "other";
    r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidArgument, "cut support mismatch rejected");
  }
  {
    auto f = make_fixture(0U);
    mesh_anchor_surface::Options o;
    o.max_surfaces = 4U;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A", o);
    check(r.status == Status::kResourceLimit, "surface budget enforced before fitting");
    o.max_surfaces = 0U;
    r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A", o);
    check(r.status == Status::kInvalidArgument, "zero surface budget rejected");
    o.max_surfaces = mesh_anchor_surface::kDefaultMaxSurfaces + 1U;
    r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A", o);
    check(r.status == Status::kInvalidArgument, "oversized surface budget rejected");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.components[1].surface_key =
        f.prepared.metric_plan.components[0].surface_key;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "duplicate surface key rejected");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.components[0].surface_key = "support-B:patch:0:1:2";
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "cross-support component key rejected");
  }
  {
    auto f = make_fixture(0U);
    std::swap(f.prepared.metric_plan.components[0].canonical_triangle,
              f.prepared.metric_plan.components[1].canonical_triangle);
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "noncanonical component ordering rejected");
  }
  {
    auto f = make_fixture(0U);
    ++f.prepared.metric_plan.components[0].triangle_count;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "component triangle count mismatch rejected");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.component_of_triangle[0] = 99U;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "out-of-range component map rejected");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.components[0].fit_samples.resize(97U);
    f.prepared.metric_plan.components[0].sample_vertex_indices.resize(97U);
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "fit samples above live cap rejected");
  }
  {
    auto f = make_fixture(0U);
    f.prepared.metric_plan.components[0].sample_vertex_indices.pop_back();
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "sample index/fit sample mismatch rejected");
  }
  {
    auto f = make_fixture(0U);
    f.cuts.fragments[0].source_triangle_index = 99U;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "cut source triangle range validated");
  }
  {
    auto f = make_fixture(0U);
    f.cuts.fragments[0].local_triangle = {900U, 901U, 902U};
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "cut local triangle provenance validated");
  }
  {
    auto f = make_fixture(0U);
    f.cuts.fragments[0].surface_key = f.prepared.metric_plan.components[1].surface_key;
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "cut surface must match finalized triangle component");
  }
  {
    auto f = make_fixture(0U);
    f.cuts.fragments.push_back(f.cuts.fragments[0]);
    auto r = mesh_anchor_surface::assemble(f.prepared, f.cuts, "support-A");
    check(r.status == Status::kInvalidPlan, "duplicate cut attribution for one source triangle rejected");
  }

  std::cout << "assertions=" << assertions << " failures=" << failures << '\n';
  return failures == 0U ? 0 : 1;
}

// Differential driver: compile unchanged against the pinned baseline and the
// candidate fitter. Compare every public result field, not rounded summaries.
#include "hss/robust_plane_fit.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace hss::plane_fit;
struct Case { std::string name; std::vector<WeightedSample> samples; Options options; };
uint64_t random_bits(uint64_t &s) {
  s ^= s << 13U; s ^= s >> 7U; s ^= s << 17U; return s;
}
double random_unit(uint64_t &s) {
  return static_cast<double>(random_bits(s) >> 11U) / 9007199254740992.0;
}
Options defaults(uint64_t seed) {
  Options o; o.inlier_threshold_m = 0.005; o.seed = seed;
  o.candidate_iterations = 200; o.max_refinement_iterations = 8;
  return o;
}
std::vector<WeightedSample> mesh_patch() {
  std::vector<WeightedSample> out;
  for (size_t y = 0; y < 4; ++y) {
    for (size_t x = 0; x < 4; ++x) {
      std::array<Point3d, 4> p{{{double(x), double(y), 1.0},
        {double(x+1), double(y), 1.0}, {double(x), double(y+1), 1.0},
        {double(x+1), double(y+1), 1.0}}};
      for (size_t k : {0U, 1U, 2U, 1U, 3U, 2U}) out.push_back({p[k], 1.0, 17U});
    }
  }
  return out;
}
Case make_case(size_t kind, uint64_t seed, size_t count = 192) {
  Case c{std::to_string(kind) + ":" + std::to_string(seed), {}, defaults(seed)};
  uint64_t rng = seed + 0x72ab91d53ULL;
  for (size_t i = 0; i < count; ++i) {
    double x = 4 * random_unit(rng) - 2;
    double y = 4 * random_unit(rng) - 2;
    double noise = (2 * random_unit(rng) - 1) * 0.0004;
    c.samples.push_back({{x, y, 1 + 0.3*x - 0.2*y + noise},
                        0.1 + random_unit(rng), i % 7});
  }
  switch (kind) {
  case 0: c.samples = mesh_patch(); break;
  case 1: // Broad oblique surface with heterogeneous weights and far outliers.
    for (size_t i = 0; i < c.samples.size(); i += 4) c.samples[i].point.z += 0.25 + random_unit(rng);
    break;
  case 2: // Equal support/extent; diversity decides before residual/plane ties.
    c.samples.clear();
    for (size_t plane = 0; plane < 2; ++plane)
      for (size_t i = 0; i < 16; ++i)
        c.samples.push_back({{double(i%4), double(i/4), double(plane)}, 1.0, i % (plane ? 4 : 2)});
    break;
  case 3: // More observations must not defeat greater DISTINCT support.
    // Rare distinct plane: enough hypotheses to expose the ranking, not a
    // claim that 200 draws always discover a minority plane.
    c.options.candidate_iterations = 2048;
    c.samples.clear();
    for (size_t i = 0; i < 72; ++i)
      c.samples.push_back({{double(i%3), double((i%9)/3), 0.0}, 1.0, i%2});
    for (size_t i = 0; i < 16; ++i)
      c.samples.push_back({{double(i%4), double(i/4), 2.0}, 1.0, i%4});
    break;
  case 4: // Nearly collinear supports must still fail the covariance rank gate.
    for (size_t i = 0; i < count; ++i) c.samples[i].point = {double(i)*0.1, i%2 ? -1e-10 : 1e-10, 0.0};
    c.options.min_second_to_largest_ratio = 1e-6; break;
  case 5:
    for (auto &s : c.samples) { s.point = {1.0, 1.0, 1.0}; }
    break;
  case 6: // Several competing offsets with support changes near the threshold.
    for (size_t i = 0; i < count; ++i) c.samples[i].point.z += double(i%5)*0.003;
    c.options.inlier_threshold_m = 0.003; break;
  case 7: // Correlated duplicate coordinates interleaved with distinct samples.
    for (size_t i = 1; i < count; i += 3) { c.samples[i] = c.samples[0]; }
    break;
  case 8: // Translation and weights spanning many magnitudes.
    for (size_t i = 0; i < count; ++i) {
      c.samples[i].point.x += 1000; c.samples[i].point.y -= 2000;
      c.samples[i].weight = std::pow(10.0, int(i%13)-6);
    } break;
  case 9:
    c.options.max_residual_evaluations = (c.options.candidate_iterations + c.options.max_refinement_iterations)*count - 1;
    break;
  case 10: c.samples[0].point.x = std::numeric_limits<double>::quiet_NaN(); break;
  case 11: c.samples[0].weight = 0.0; break;
  case 12: c.options.inlier_threshold_m = 0.0; break;
  case 13: c.samples.resize(2); break;
  case 14: c.options.min_diversity_groups = 6;
    for (auto &s : c.samples) { s.diversity_group = 1; }
    break;
  case 15: // Random non-planar cloud exercises mostly non-repeated supports.
    for (auto &s : c.samples) s.point.z = 4*random_unit(rng)-2;
    c.options.inlier_threshold_m = 0.1; break;
  case 16: // Exact live_reconstruction.cpp patch-fit options at the baseline.
    c.samples = mesh_patch();
    c.options.inlier_threshold_m = 0.002;
    c.options.max_input_samples = 96;
    c.options.candidate_iterations = 24;
    c.options.max_refinement_iterations = 4;
    c.options.max_residual_evaluations = 4096;
    c.options.min_support_samples = 3;
    c.options.min_distinct_support_points = 3;
    break;
  default: throw std::runtime_error("unknown fixture");
  }
  return c;
}
void verify(const Case &c, const Result &r) {
  if ((r.status == Status::kOk) != r.plane.has_value()) throw std::runtime_error("partial/absent plane: " + c.name);
  if (!r.plane) return;
  const auto &p = *r.plane;
  if (std::abs(std::hypot(p.normal.x,p.normal.y,p.normal.z)-1) > 1e-12 ||
      p.residuals.maximum_absolute_m > c.options.inlier_threshold_m ||
      p.inlier_indices.size() != p.support.sample_count ||
      p.residual_evaluations > c.options.max_residual_evaluations ||
      !std::is_sorted(p.inlier_indices.begin(),p.inlier_indices.end()) ||
      p.inlier_indices.back() >= c.samples.size()) throw std::runtime_error("fit invariant: " + c.name);
}
void emit(const Case &c, const Result &r) {
  std::cout << c.name << ' ' << int(r.status) << ' ' << bool(r.plane);
  if (r.plane) {
    const auto &p = *r.plane;
    for (double v : {p.normal.x,p.normal.y,p.normal.z,p.offset_m,
         p.residuals.weighted_rms_m,p.residuals.median_absolute_m,p.residuals.maximum_absolute_m,
         p.support.total_weight,p.support.major_variance_m2,p.support.minor_variance_m2})
      std::cout << ' ' << std::bit_cast<uint64_t>(v);
    for (size_t v : {p.support.sample_count,p.support.distinct_point_count,p.diversity.group_count,
         p.candidate_iterations_run,p.refinement_iterations_run,p.residual_evaluations}) std::cout << ' ' << v;
    std::cout << " indices";
    for (size_t i : p.inlier_indices) std::cout << ' ' << i;
  }
  std::cout << '\n';
}
void benchmark() {
  for (size_t kind : {16U,1U,15U}) {
    Case c = make_case(kind,101,kind == 16 ? 96 : 4096);
    c.name = kind == 16 ? "live_mesh96" : kind == 1 ? "oblique4096" : "clutter4096";
    // Same options/data in both executables; timings exclude fixture creation.
    verify(c,fit(c.samples,c.options)); // warm up
    uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    constexpr size_t repeats = 8;
    for (size_t i=0;i<repeats;++i) {
      const auto r=fit(c.samples,c.options);
      checksum += uint64_t(r.status) + (r.plane ? r.plane->residual_evaluations : 0);
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/repeats;
    std::cout << c.name << ' ' << ms << ' ' << checksum << '\n';
  }
}
} // namespace
int main(int argc,char **argv) {
  try {
    if (argc==2 && std::string(argv[1])=="--benchmark") { benchmark(); return 0; }
    if (argc!=1) throw std::runtime_error("usage: probe [--benchmark]");
    for (uint64_t seed=1;seed<=24;++seed)
      for (size_t kind=0;kind<17;++kind) {
        const auto c=make_case(kind,seed); const auto r=fit(c.samples,c.options);
        verify(c,r);
        // Independent semantic assertions in addition to baseline equivalence.
        if ((kind==0 || kind==16) && (!r.plane || r.plane->support.distinct_point_count!=25)) throw std::runtime_error("mesh support lost");
        if (kind==2 && (!r.plane || std::abs(r.plane->offset_m+1)>1e-12)) throw std::runtime_error("diversity tie broken incorrectly");
        if (kind==3 && (!r.plane || std::abs(r.plane->offset_m+2)>1e-12)) throw std::runtime_error("duplicate population beat distinct support");
        emit(c,r);
      }
    return 0;
  } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

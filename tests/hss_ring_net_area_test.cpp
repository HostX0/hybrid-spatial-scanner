#include "hss/ring_net_area.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hss::area::AreaPolicy;
using hss::area::Attachment;
using hss::area::Availability;
using hss::area::Decision;
using hss::area::GeometryOrigin;
using hss::area::Hole;
using hss::area::HoleKind;
using hss::area::Input;
using hss::area::Point2d;
using hss::area::QualityState;
using hss::area::Status;
using hss::area::TopologyState;
using hss::area::ValidatedRing;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(double actual, double expected, double tolerance = 1.0e-12) {
  return std::fabs(actual - expected) <= tolerance;
}

std::vector<std::string> split(std::string_view input, char delimiter) {
  std::vector<std::string> fields;
  size_t start = 0U;
  while (start <= input.size()) {
    const size_t end = input.find(delimiter, start);
    fields.emplace_back(input.substr(
        start, end == std::string_view::npos ? input.size() - start
                                             : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1U;
  }
  return fields;
}

GeometryOrigin geometry_origin(std::string_view value) {
  if (value == "observed")
    return GeometryOrigin::kObserved;
  if (value == "user_measured")
    return GeometryOrigin::kUserMeasured;
  throw std::runtime_error("unknown geometry origin in area fixture");
}

HoleKind hole_kind(std::string_view value) {
  if (value == "fixed_obstruction")
    return HoleKind::kFixedObstruction;
  if (value == "furniture")
    return HoleKind::kFurniture;
  if (value == "other")
    return HoleKind::kOther;
  throw std::runtime_error("unknown hole kind in area fixture");
}

Attachment attachment(std::string_view value) {
  if (value == "detached")
    return Attachment::kDetached;
  if (value == "attached")
    return Attachment::kAttached;
  if (value == "uncertain")
    return Attachment::kUncertain;
  throw std::runtime_error("unknown attachment in area fixture");
}

struct Fixture {
  ValidatedRing outer;
  std::vector<Hole> holes;
};

Fixture load_fixture(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open ring-area fixture");

  Fixture fixture;
  ValidatedRing *current = nullptr;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "RING") {
      if (fields.size() != 11U)
        throw std::runtime_error("invalid RING fixture row");
      ValidatedRing next_ring;
      next_ring.id = fields[1];
      next_ring.topology_state = fields[6] == "accepted"
                                     ? TopologyState::kAccepted
                                     : TopologyState::kUnaccepted;
      next_ring.provenance.geometry_origin = geometry_origin(fields[7]);
      next_ring.provenance.quality_state = fields[8] == "ready"
                                               ? QualityState::kReady
                                               : QualityState::kInvalid;
      next_ring.provenance.source_revision = fields[9];
      next_ring.provenance.support_ids = split(fields[10], ';');
      if (fields[2] == "outer") {
        fixture.outer = std::move(next_ring);
        current = &fixture.outer;
      } else if (fields[2] == "hole") {
        fixture.holes.push_back(
            {std::move(next_ring), hole_kind(fields[3]), attachment(fields[4]),
             fields[5] == "1"});
        current = &fixture.holes.back().ring;
      } else {
        throw std::runtime_error("unknown ring role in area fixture");
      }
    } else if (fields.front() == "POINT") {
      if (fields.size() != 3U || current == nullptr)
        throw std::runtime_error("invalid POINT fixture row");
      current->vertices_m.push_back(
          {std::stod(fields[1]), std::stod(fields[2])});
    } else {
      throw std::runtime_error("unknown ring-area fixture row");
    }
  }
  return fixture;
}

Input fixture_input(AreaPolicy policy) {
  const Fixture fixture = load_fixture(
      std::string(HSS_SOURCE_DIR) +
      "/fixtures/area/rib_with_fixed_column.csv");
  return {hss::area::LinearUnit::kMetres, policy, fixture.outer,
          fixture.holes};
}

ValidatedRing ring(std::string id, std::vector<Point2d> points,
                   std::string support_id) {
  ValidatedRing value;
  value.id = std::move(id);
  value.vertices_m = std::move(points);
  value.topology_state = TopologyState::kAccepted;
  value.provenance.geometry_origin = GeometryOrigin::kObserved;
  value.provenance.quality_state = QualityState::kReady;
  value.provenance.source_revision = "accepted-scene-9";
  value.provenance.support_ids = {std::move(support_id)};
  return value;
}

std::vector<Point2d> clockwise_square(double x, double y, double width) {
  return {{x, y}, {x, y + width}, {x + width, y + width},
          {x + width, y}};
}

void expect_no_area(const hss::area::Result &result, Status status,
                    Availability availability, const std::string &message) {
  check(result.status == status,
        message + " has explicit status " + hss::area::status_name(status));
  check(result.availability == availability, message + " labels availability");
  check(!result.area_m2.has_value(), message + " publishes null area");
  check(result.assessments.empty(), message + " exposes no partial area");
}

void test_exact_rib_and_column_fixture() {
  Input gross = fixture_input(AreaPolicy::kInternalFinishedFace);
  const auto gross_result = hss::area::calculate(gross);
  check(gross_result.status == Status::kOk && gross_result.area_m2.has_value(),
        "accepted fixture produces ready internal-face area");
  if (gross_result.area_m2)
    check(near(*gross_result.area_m2, 29.925),
          "protected rib fixture remains 29.925 square metres");
  check(gross_result.assessments.size() == 2U &&
            gross_result.assessments[1].decision ==
                Decision::kIgnoredByInternalFinishedFacePolicy,
        "gross policy records that the column was not subtracted");

  Input net = fixture_input(AreaPolicy::kNetFixedObstructions);
  const auto net_result = hss::area::calculate(net);
  check(net_result.status == Status::kOk && net_result.area_m2.has_value(),
        "accepted fixture produces ready net area");
  if (net_result.area_m2)
    check(near(*net_result.area_m2, 29.775),
          "only the 0.30 by 0.50 metre detached fixed column is subtracted");
  check(net_result.area_unit == hss::area::AreaUnit::kSquareMetres &&
            net_result.policy == AreaPolicy::kNetFixedObstructions,
        "result preserves square-metre units and selected policy");
  check(net_result.assessments.size() == 2U &&
            net_result.assessments[0].ring_id == "rib-room" &&
            net_result.assessments[1].ring_id == "detached-column" &&
            net_result.assessments[1].provenance ==
                net.holes[0].ring.provenance &&
            near(net_result.assessments[1].signed_area_m2, -0.15) &&
            near(net_result.assessments[1].applied_delta_m2, -0.15),
        "signed contribution, stable identity, and provenance survive calculation");
}

void test_policy_distinguishes_non_subtractable_candidates() {
  Input input;
  input.policy = AreaPolicy::kNetFixedObstructions;
  input.outer = ring("outer", {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                     "outer-support");
  input.holes = {
      {ring("fixed-detached", clockwise_square(1, 1, 1), "fixed-support"),
       HoleKind::kFixedObstruction, Attachment::kDetached, true},
      {ring("not-fixed", clockwise_square(3, 1, 1), "candidate-support"),
       HoleKind::kFixedObstruction, Attachment::kDetached, false},
      {ring("attached", clockwise_square(5, 1, 1), "attached-support"),
       HoleKind::kFixedObstruction, Attachment::kAttached, true},
      {ring("uncertain", clockwise_square(7, 1, 1), "uncertain-support"),
       HoleKind::kFixedObstruction, Attachment::kUncertain, true},
      {ring("furniture", clockwise_square(1, 3, 1), "furniture-support"),
       HoleKind::kFurniture, Attachment::kDetached, true},
      {ring("other", clockwise_square(3, 3, 1), "other-support"),
       HoleKind::kOther, Attachment::kDetached, true},
  };
  const auto result = hss::area::calculate(input);
  check(result.status == Status::kOk && result.area_m2 &&
            near(*result.area_m2, 99.0),
        "net policy subtracts exactly one explicitly fixed detached obstruction");
  const std::vector<Decision> expected = {
      Decision::kSubtractedFixedDetachedObstruction,
      Decision::kIgnoredNotExplicitlyFixed,
      Decision::kIgnoredAttached,
      Decision::kIgnoredUncertainAttachment,
      Decision::kIgnoredFurniture,
      Decision::kIgnoredOther,
  };
  check(result.assessments.size() == expected.size() + 1U,
        "one assessment exists for every consumed ring");
  for (size_t index = 0U;
       index < expected.size() && index + 1U < result.assessments.size();
       ++index) {
    check(result.assessments[index + 1U].decision == expected[index],
          std::string("policy decision is explicit: ") +
              hss::area::decision_name(expected[index]));
    check(result.assessments[index + 1U].provenance ==
              input.holes[index].ring.provenance,
          "ignored and subtracted candidates both retain provenance");
  }
}

void test_incomplete_and_unaccepted_topology_never_publish_area() {
  Input incomplete = fixture_input(AreaPolicy::kNetFixedObstructions);
  incomplete.holes[0].ring.topology_state = TopologyState::kIncomplete;
  expect_no_area(hss::area::calculate(incomplete), Status::kTopologyIncomplete,
                 Availability::kProvisional, "incomplete hole topology");

  Input unaccepted = fixture_input(AreaPolicy::kInternalFinishedFace);
  unaccepted.outer.topology_state = TopologyState::kUnaccepted;
  expect_no_area(hss::area::calculate(unaccepted),
                 Status::kTopologyUnaccepted, Availability::kUnavailable,
                 "unaccepted outer topology");

  Input both = fixture_input(AreaPolicy::kNetFixedObstructions);
  both.outer.topology_state = TopologyState::kIncomplete;
  both.holes[0].ring.topology_state = TopologyState::kUnaccepted;
  expect_no_area(hss::area::calculate(both), Status::kTopologyUnaccepted,
                 Availability::kUnavailable,
                 "rejection takes precedence over provisional topology");
}

void test_no_winding_repair_or_nonfinite_area() {
  Input wrong_outer = fixture_input(AreaPolicy::kNetFixedObstructions);
  std::reverse(wrong_outer.outer.vertices_m.begin(),
               wrong_outer.outer.vertices_m.end());
  expect_no_area(hss::area::calculate(wrong_outer), Status::kWrongWinding,
                 Availability::kUnavailable, "clockwise outer ring");

  Input wrong_hole = fixture_input(AreaPolicy::kNetFixedObstructions);
  std::reverse(wrong_hole.holes[0].ring.vertices_m.begin(),
               wrong_hole.holes[0].ring.vertices_m.end());
  expect_no_area(hss::area::calculate(wrong_hole), Status::kWrongWinding,
                 Availability::kUnavailable, "counterclockwise hole ring");

  Input short_ring = fixture_input(AreaPolicy::kNetFixedObstructions);
  short_ring.outer.vertices_m.resize(2U);
  expect_no_area(hss::area::calculate(short_ring), Status::kInvalidRing,
                 Availability::kUnavailable,
                 "ring with fewer than three vertices");

  Input nonfinite = fixture_input(AreaPolicy::kNetFixedObstructions);
  nonfinite.outer.vertices_m[0].x_m =
      std::numeric_limits<double>::quiet_NaN();
  expect_no_area(hss::area::calculate(nonfinite),
                 Status::kNonFiniteCoordinate, Availability::kUnavailable,
                 "nonfinite accepted-ring coordinate");
}

void test_translated_compensated_sum_at_large_origin() {
  const double origin = 1.0e12;
  Input input;
  input.policy = AreaPolicy::kNetFixedObstructions;
  input.outer = ring("translated-outer",
                     {{origin, origin},
                      {origin + 3.0, origin},
                      {origin + 3.0, origin + 5.0},
                      {origin, origin + 5.0}},
                     "large-origin-support");
  input.holes.push_back(
      {ring("translated-hole",
            clockwise_square(origin + 1.0, origin + 1.0, 1.0),
            "large-origin-hole-support"),
       HoleKind::kFixedObstruction, Attachment::kDetached, true});
  const auto result = hss::area::calculate(input);
  check(result.status == Status::kOk && result.area_m2 &&
            near(*result.area_m2, 14.0),
        "translated compensated calculation retains small area at large origin");
}

void test_bounds_and_invalid_enum_inputs_fail_closed() {
  Input input = fixture_input(AreaPolicy::kNetFixedObstructions);
  hss::area::Options options;
  options.max_rings = 1U;
  expect_no_area(hss::area::calculate(input, options),
                 Status::kResourceLimitExceeded, Availability::kUnavailable,
                 "declared ring cap");

  options = {};
  options.max_vertices = 3U;
  expect_no_area(hss::area::calculate(input, options),
                 Status::kResourceLimitExceeded, Availability::kUnavailable,
                 "declared vertex cap");

  options = {};
  options.max_rings = 0U;
  expect_no_area(hss::area::calculate(input, options), Status::kInvalidOptions,
                 Availability::kUnavailable, "invalid zero ring cap");

  input = fixture_input(AreaPolicy::kNetFixedObstructions);
  input.coordinate_unit = static_cast<hss::area::LinearUnit>(255U);
  expect_no_area(hss::area::calculate(input), Status::kInvalidUnit,
                 Availability::kUnavailable, "unsupported coordinate unit");

  input = fixture_input(AreaPolicy::kNetFixedObstructions);
  input.policy = static_cast<AreaPolicy>(255U);
  expect_no_area(hss::area::calculate(input), Status::kInvalidPolicy,
                 Availability::kUnavailable, "unsupported area policy");

  input = fixture_input(AreaPolicy::kNetFixedObstructions);
  input.holes[0].attachment = static_cast<Attachment>(255U);
  expect_no_area(hss::area::calculate(input),
                 Status::kInvalidClassification,
                 Availability::kUnavailable,
                 "unsupported attachment cannot cause subtraction");

  input = fixture_input(AreaPolicy::kNetFixedObstructions);
  input.outer.topology_state = static_cast<TopologyState>(255U);
  expect_no_area(hss::area::calculate(input), Status::kTopologyUnaccepted,
                 Availability::kUnavailable,
                 "unsupported topology state is not treated as accepted");
}

} // namespace

int main() {
  test_exact_rib_and_column_fixture();
  test_policy_distinguishes_non_subtractable_candidates();
  test_incomplete_and_unaccepted_topology_never_publish_area();
  test_no_winding_repair_or_nonfinite_area();
  test_translated_compensated_sum_at_large_origin();
  test_bounds_and_invalid_enum_inputs_fail_closed();
  if (failures != 0) {
    std::cerr << failures << " ring-net-area test(s) failed\n";
    return 1;
  }
  std::cout << "validated ring net-area policy checks passed\n";
  return 0;
}

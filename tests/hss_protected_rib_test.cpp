#include "hss/protected_rib.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hss::rib::Chain;
using hss::rib::Options;
using hss::rib::Point2d;
using hss::rib::Segment;
using hss::rib::SegmentRole;
using hss::rib::Status;
using hss::rib::Vertex;

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Options options() {
  Options value;
  value.min_segment_length_m = 0.01;
  value.geometric_epsilon_m = 1.0e-8;
  value.max_abs_coordinate_m = 1'000.0;
  return value;
}

std::string uuid(size_t value) {
  std::ostringstream stream;
  stream << "99000000-0000-4000-8000-" << std::hex << std::nouppercase
         << std::setw(12) << std::setfill('0') << value;
  return stream.str();
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

std::vector<std::string> ids(std::string_view field) {
  return field == "-" ? std::vector<std::string>{} : split(field, ';');
}

SegmentRole role(std::string_view value) {
  if (value == "approach")
    return SegmentRole::kApproach;
  if (value == "first_return")
    return SegmentRole::kFirstReturn;
  if (value == "front")
    return SegmentRole::kFront;
  if (value == "second_return")
    return SegmentRole::kSecondReturn;
  if (value == "continuation")
    return SegmentRole::kContinuation;
  throw std::runtime_error("unknown protected-rib role");
}

Chain load_fixture(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open protected-rib fixture");
  Chain chain;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "FEATURE") {
      if (fields.size() != 3U)
        throw std::runtime_error("invalid FEATURE fixture row");
      chain.feature_id = fields[1];
      chain.revision = std::stoull(fields[2]);
    } else if (fields.front() == "VERTEX") {
      if (fields.size() != 8U)
        throw std::runtime_error("invalid VERTEX fixture row");
      chain.vertices.push_back(
          {fields[1],
           {std::stod(fields[2]), std::stod(fields[3])},
           ids(fields[4]),
           ids(fields[5]),
           ids(fields[6]),
           fields[7] == "1"});
    } else if (fields.front() == "SEGMENT") {
      if (fields.size() != 10U)
        throw std::runtime_error("invalid SEGMENT fixture row");
      chain.ordered_segments.push_back(
          {fields[1], role(fields[2]), fields[3], fields[4], ids(fields[5]),
           ids(fields[6]), ids(fields[7]), ids(fields[8]),
           fields[9] == "1"});
    } else {
      throw std::runtime_error("unknown protected-rib fixture row");
    }
  }
  return chain;
}

Chain fixture() {
  return load_fixture(std::string(HSS_SOURCE_DIR) +
                      "/fixtures/protected_rib/oblique_unequal_chain.csv");
}

void expect_failure(const Chain &chain, Status expected,
                    const std::string &message,
                    Options validation_options = options()) {
  const auto result = hss::rib::validate(chain, validation_options);
  check(result.status == expected,
        message + " has explicit status " + hss::rib::status_name(expected));
  check(!result.chain, message + " exposes no partial accepted chain");
}

void test_fixture_preserves_order_evidence_and_arbitrary_geometry() {
  const Chain chain = fixture();
  const auto result = hss::rib::validate(chain, options());
  check(result.status == Status::kOk && result.chain.has_value(),
        "oblique unequal protected chain is accepted");
  check(result.chain == chain,
        "accepted chain preserves coordinates, order, identities, lineage, evidence, and flags");
  if (!result.chain)
    return;
  check(result.chain->ordered_segments.size() == 5U &&
            result.chain->vertices.size() == 6U,
        "validator neither simplifies nor expands the five-role chain");
  check(result.chain->ordered_segments[2].role == SegmentRole::kFront &&
            result.chain->ordered_segments[1].protected_topology &&
            result.chain->ordered_segments[3].protected_topology,
        "narrow front and both returns remain explicit and protected");
  const auto distance = [](Point2d first, Point2d second) {
    return std::hypot(second.x - first.x, second.y - first.y);
  };
  const double first_return =
      distance(result.chain->vertices[1].position_m,
               result.chain->vertices[2].position_m);
  const double second_return =
      distance(result.chain->vertices[3].position_m,
               result.chain->vertices[4].position_m);
  check(std::abs(first_return - second_return) > 0.1,
        "unequal return dimensions are not symmetrized");
}

void test_lineage_checked_update_preserves_chain() {
  const Chain previous = fixture();
  Chain candidate = previous;
  candidate.revision = 2U;
  candidate.vertices[2].position_m = {2.11, 0.91};
  candidate.vertices[3].position_m = {2.51, 1.01};

  const std::string prior_vertex_id = candidate.vertices[2].id;
  candidate.vertices[2].id = uuid(1'001U);
  candidate.vertices[2].parent_vertex_ids.push_back(prior_vertex_id);
  candidate.ordered_segments[1].end_vertex_id = candidate.vertices[2].id;
  candidate.ordered_segments[2].start_vertex_id = candidate.vertices[2].id;

  const std::string prior_front_id = candidate.ordered_segments[2].id;
  candidate.ordered_segments[2].id = uuid(2'001U);
  candidate.ordered_segments[2].parent_segment_ids.push_back(prior_front_id);

  const auto result =
      hss::rib::validate_update(previous, candidate, options());
  check(result.status == Status::kOk && result.chain == candidate,
        "refined coordinates and lineage-linked identities preserve exact chain update");

  Chain missing_segment_lineage = candidate;
  missing_segment_lineage.ordered_segments[2].parent_segment_ids.clear();
  auto rejected =
      hss::rib::validate_update(previous, missing_segment_lineage, options());
  check(rejected.status == Status::kTopologyChange && !rejected.chain,
        "replacement segment without predecessor lineage is rejected atomically");

  Chain missing_vertex_lineage = candidate;
  missing_vertex_lineage.vertices[2].parent_vertex_ids.clear();
  rejected = hss::rib::validate_update(previous, missing_vertex_lineage,
                                       options());
  check(rejected.status == Status::kTopologyChange && !rejected.chain,
        "replacement vertex without predecessor lineage is rejected atomically");

  Chain stale = candidate;
  stale.revision = previous.revision;
  rejected = hss::rib::validate_update(previous, stale, options());
  check(rejected.status == Status::kInvalidRevision && !rejected.chain,
        "non-increasing update revision is rejected");
}

void test_missing_ambiguous_and_broad_wall_replacements_fail() {
  Chain missing = fixture();
  missing.ordered_segments.erase(missing.ordered_segments.begin() + 2);
  expect_failure(missing, Status::kWrongSegmentCount,
                 "missing narrow front segment");

  Chain wrong_order = fixture();
  std::swap(wrong_order.ordered_segments[1].role,
            wrong_order.ordered_segments[2].role);
  expect_failure(wrong_order, Status::kWrongRoleOrder,
                 "reordered return/front semantics");

  Chain open = fixture();
  open.ordered_segments[2].start_vertex_id = open.vertices[1].id;
  expect_failure(open, Status::kOpenChain, "disconnected ordered evidence");

  Chain ambiguous = fixture();
  ambiguous.ordered_segments.back().end_vertex_id = ambiguous.vertices[1].id;
  expect_failure(ambiguous, Status::kAmbiguousChain,
                 "chain that reuses a prior vertex");

  Chain broad_wall = fixture();
  for (size_t index = 0U; index < broad_wall.vertices.size(); ++index)
    broad_wall.vertices[index].position_m =
        {static_cast<double>(index), 0.0};
  expect_failure(broad_wall, Status::kDegenerateFeature,
                 "five labels applied to one broad straight wall");
}

void test_protection_evidence_and_lineage_failures() {
  Chain unprotected = fixture();
  unprotected.ordered_segments[2].protected_topology = false;
  expect_failure(unprotected, Status::kUnprotectedSegment,
                 "unprotected narrow front");

  Chain missing_surface = fixture();
  missing_surface.ordered_segments[1].supporting_surface_ids.clear();
  expect_failure(missing_surface, Status::kMissingEvidence,
                 "return without supporting surface evidence");

  Chain missing_provenance = fixture();
  missing_provenance.vertices[3].provenance_support_ids.clear();
  expect_failure(missing_provenance, Status::kMissingEvidence,
                 "junction without source provenance");

  Chain self_lineage = fixture();
  self_lineage.ordered_segments[0].parent_segment_ids =
      {self_lineage.ordered_segments[0].id};
  expect_failure(self_lineage, Status::kInvalidLineage,
                 "self-referential segment lineage");

  Chain conflicting_lineage = fixture();
  conflicting_lineage.vertices[0].parent_vertex_ids = {uuid(7'001U)};
  conflicting_lineage.vertices[0].replacement_vertex_ids = {uuid(7'001U)};
  expect_failure(conflicting_lineage, Status::kInvalidLineage,
                 "conflicting vertex lineage");
}

void test_geometric_identity_and_bounds_failures() {
  Chain crossing = fixture();
  const std::vector<Point2d> crossing_points{
      {0.0, 0.0}, {4.0, 0.0}, {2.0, 2.0},
      {2.0, -1.0}, {4.0, 2.0}, {5.0, 0.0}};
  for (size_t index = 0U; index < crossing_points.size(); ++index)
    crossing.vertices[index].position_m = crossing_points[index];
  expect_failure(crossing, Status::kSelfIntersection,
                 "self-intersecting five-role chain");

  Chain near_zero = fixture();
  near_zero.vertices[3].position_m = {2.0501, 0.9501};
  expect_failure(near_zero, Status::kNearZeroSegment,
                 "near-zero narrow front");

  Chain duplicate = fixture();
  duplicate.vertices[5].position_m = duplicate.vertices[0].position_m;
  expect_failure(duplicate, Status::kDuplicateVertex,
                 "duplicate endpoint coordinate");

  Chain nonfinite = fixture();
  nonfinite.vertices[0].position_m.x =
      std::numeric_limits<double>::quiet_NaN();
  expect_failure(nonfinite, Status::kNonFiniteCoordinate,
                 "nonfinite coordinate");

  Chain invalid_reference = fixture();
  invalid_reference.ordered_segments[0].start_vertex_id = uuid(90'001U);
  expect_failure(invalid_reference, Status::kInvalidReference,
                 "unknown endpoint reference");

  Chain invalid_id = fixture();
  invalid_id.feature_id = "not-a-uuid";
  expect_failure(invalid_id, Status::kInvalidId, "malformed feature ID");

  Chain zero_revision = fixture();
  zero_revision.revision = 0U;
  expect_failure(zero_revision, Status::kInvalidRevision,
                 "zero revision");

  Options invalid_options;
  expect_failure(fixture(), Status::kInvalidOptions,
                 "undeclared geometric policy", invalid_options);
}

} // namespace

int main() {
  test_fixture_preserves_order_evidence_and_arbitrary_geometry();
  test_lineage_checked_update_preserves_chain();
  test_missing_ambiguous_and_broad_wall_replacements_fail();
  test_protection_evidence_and_lineage_failures();
  test_geometric_identity_and_bounds_failures();
  if (failures == 0)
    std::cout << "protected attached-rib validation tests passed\n";
  return failures == 0 ? 0 : 1;
}

#include "hss/topology_protection.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hss::topology_protection::Feature;
using hss::topology_protection::Options;
using hss::topology_protection::Point2d;
using hss::topology_protection::Status;

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
  stream << "a0000000-0000-4000-8000-" << std::hex << std::nouppercase
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

Feature fixture() {
  std::ifstream input(std::string(HSS_SOURCE_DIR) +
                      "/fixtures/topology_protection/oblique_protected_chain.csv");
  if (!input)
    throw std::runtime_error("could not open topology-protection fixture");
  Feature feature;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "FEATURE") {
      if (fields.size() != 3U)
        throw std::runtime_error("invalid FEATURE fixture row");
      feature.id = fields[1];
      feature.revision = std::stoull(fields[2]);
    } else if (fields.front() == "VERTEX") {
      if (fields.size() != 7U)
        throw std::runtime_error("invalid VERTEX fixture row");
      feature.vertices.push_back(
          {fields[1], {std::stod(fields[2]), std::stod(fields[3])},
           ids(fields[4]), ids(fields[5]), fields[6] == "1"});
    } else if (fields.front() == "SEGMENT") {
      if (fields.size() != 7U)
        throw std::runtime_error("invalid SEGMENT fixture row");
      feature.ordered_segments.push_back(
          {fields[1], fields[2], fields[3], ids(fields[4]), ids(fields[5]),
           fields[6] == "1"});
    } else {
      throw std::runtime_error("unknown topology-protection fixture row");
    }
  }
  return feature;
}

void expect_refinement_failure(const Feature &previous, const Feature &candidate,
                               Status expected, const std::string &message) {
  const auto result =
      hss::topology_protection::validate_refinement(previous, candidate, options());
  check(result.status == expected,
        message + " has explicit status " +
            hss::topology_protection::status_name(expected));
  check(!result.feature, message + " exposes no partial accepted candidate");
}

void test_fixture_preserves_protection_and_lock_as_distinct_state() {
  const Feature feature = fixture();
  const auto result = hss::topology_protection::validate(feature, options());
  check(result.status == Status::kOk && result.feature == feature,
        "accepted feature is an exact copy of the protected oblique chain");
  check(feature.vertices.front().position_locked,
        "fixture has an explicit position lock independent of topology protection");
  check(std::all_of(feature.ordered_segments.begin(), feature.ordered_segments.end(),
                    [](const auto &segment) { return segment.protected_topology; }),
        "every ordered segment remains explicitly topology protected");
}

void test_evidence_supported_refinement_and_lineage_linked_replacement() {
  const Feature previous = fixture();
  Feature candidate = previous;
  candidate.revision = 2U;
  candidate.vertices[2].position_m = {1.46, 0.99};
  candidate.vertices[3].position_m = {2.10, 1.19};
  auto result = hss::topology_protection::validate_refinement(previous, candidate,
                                                               options());
  check(result.status == Status::kOk && result.feature == candidate,
        "protected topology permits provenance-retaining coordinate refinement");

  const std::string previous_vertex_id = candidate.vertices[2].id;
  candidate.vertices[2].id = uuid(2'001U);
  candidate.vertices[2].parent_vertex_ids.push_back(previous_vertex_id);
  candidate.ordered_segments[1].end_vertex_id = candidate.vertices[2].id;
  candidate.ordered_segments[2].start_vertex_id = candidate.vertices[2].id;
  const std::string previous_segment_id = candidate.ordered_segments[2].id;
  candidate.ordered_segments[2].id = uuid(3'001U);
  candidate.ordered_segments[2].parent_segment_ids.push_back(previous_segment_id);
  result = hss::topology_protection::validate_refinement(previous, candidate, options());
  check(result.status == Status::kOk && result.feature == candidate,
        "identity replacement is accepted only with corresponding predecessor lineage");
}

void test_removal_reordering_and_identity_substitution_fail_atomically() {
  const Feature previous = fixture();
  Feature removal = previous;
  removal.revision = 2U;
  removal.vertices.pop_back();
  removal.ordered_segments.pop_back();
  expect_refinement_failure(previous, removal, Status::kTopologyChange,
                            "protected segment removal");

  Feature reordered = previous;
  reordered.revision = 2U;
  std::reverse(reordered.ordered_segments.begin(), reordered.ordered_segments.end());
  for (auto &segment : reordered.ordered_segments)
    std::swap(segment.start_vertex_id, segment.end_vertex_id);
  expect_refinement_failure(previous, reordered, Status::kTopologyChange,
                            "protected ordered-chain reversal");

  Feature substituted = previous;
  substituted.revision = 2U;
  substituted.vertices[2].id = uuid(4'001U);
  substituted.ordered_segments[1].end_vertex_id = substituted.vertices[2].id;
  substituted.ordered_segments[2].start_vertex_id = substituted.vertices[2].id;
  expect_refinement_failure(previous, substituted, Status::kTopologyChange,
                            "identity substitution without predecessor lineage");
}

void test_locks_lineage_evidence_and_invalid_geometry_fail_atomically() {
  const Feature previous = fixture();
  Feature moved_lock = previous;
  moved_lock.revision = 2U;
  moved_lock.vertices.front().position_m = {0.01, 0.0};
  expect_refinement_failure(previous, moved_lock, Status::kPositionLockViolation,
                            "position-locked coordinate refinement");

  Feature unlocked = previous;
  unlocked.revision = 2U;
  unlocked.vertices.front().position_locked = false;
  expect_refinement_failure(previous, unlocked, Status::kPositionLockChange,
                            "silent position unlock");

  Feature invalid_lineage = previous;
  invalid_lineage.revision = 2U;
  invalid_lineage.vertices[2].parent_vertex_ids = {invalid_lineage.vertices[2].id};
  expect_refinement_failure(previous, invalid_lineage, Status::kInvalidLineage,
                            "self-referential replacement lineage");

  Feature missing_evidence = previous;
  missing_evidence.revision = 2U;
  missing_evidence.vertices[3].provenance_support_ids.clear();
  expect_refinement_failure(previous, missing_evidence, Status::kMissingEvidence,
                            "coordinate refinement without retained evidence");

  Feature unprotected = previous;
  unprotected.revision = 2U;
  unprotected.ordered_segments[2].protected_topology = false;
  expect_refinement_failure(previous, unprotected, Status::kUnprotectedSegment,
                            "silent topology protection removal");

  Feature crossing = previous;
  crossing.revision = 2U;
  const std::vector<Point2d> crossing_points{
      {0.0, 0.0}, {4.0, 0.0}, {2.0, 2.0},
      {2.0, -1.0}, {4.0, 2.0}, {5.0, 0.0}};
  for (size_t index = 0U; index < crossing_points.size(); ++index)
    crossing.vertices[index].position_m = crossing_points[index];
  expect_refinement_failure(previous, crossing, Status::kSelfIntersection,
                            "self-intersecting candidate refinement");
}

} // namespace

int main() {
  test_fixture_preserves_protection_and_lock_as_distinct_state();
  test_evidence_supported_refinement_and_lineage_linked_replacement();
  test_removal_reordering_and_identity_substitution_fail_atomically();
  test_locks_lineage_evidence_and_invalid_geometry_fail_atomically();
  if (failures == 0)
    std::cout << "topology protection/refinement guard tests passed\n";
  return failures == 0 ? 0 : 1;
}

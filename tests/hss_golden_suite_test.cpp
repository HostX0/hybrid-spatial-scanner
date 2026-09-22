#include "hss/plan_semantics.hpp"
#include "hss/scene_editing.hpp"
#include "hss/topology_protection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace area = hss::area;
namespace plan = hss::plan_semantics;
namespace topology = hss::topology_protection;

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

std::vector<std::string> ids(std::string_view field) {
  return field == "-" ? std::vector<std::string>{} : split(field, ';');
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("could not open " + path.string());
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

std::vector<std::string> read_nonempty_lines(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open " + path.string());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line))
    if (!line.empty() && line.front() != '#')
      lines.push_back(line);
  return lines;
}

topology::Feature protected_rib(const std::filesystem::path &path) {
  topology::Feature feature;
  for (const std::string &line : read_nonempty_lines(path)) {
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "FEATURE") {
      if (fields.size() != 3U)
        throw std::runtime_error("invalid FEATURE golden row");
      feature.id = fields[1];
      feature.revision = std::stoull(fields[2]);
    } else if (fields.front() == "VERTEX") {
      if (fields.size() != 7U)
        throw std::runtime_error("invalid VERTEX golden row");
      feature.vertices.push_back({fields[1], {std::stod(fields[2]), std::stod(fields[3])},
                                  ids(fields[4]), ids(fields[5]), fields[6] == "1"});
    } else if (fields.front() == "SEGMENT") {
      if (fields.size() != 7U)
        throw std::runtime_error("invalid SEGMENT golden row");
      feature.ordered_segments.push_back(
          {fields[1], fields[2], fields[3], ids(fields[4]), ids(fields[5]),
           fields[6] == "1"});
    } else {
      throw std::runtime_error("unknown protected-rib golden row");
    }
  }
  return feature;
}

struct PlanGolden {
  std::string id;
  double expected_net_area_m2{0.0};
  double expected_outer_area_m2{0.0};
  double expected_hole_area_m2{0.0};
  std::vector<area::Point2d> outer;
  std::vector<area::Point2d> hole;
};

PlanGolden golden_plan(const std::filesystem::path &path) {
  PlanGolden golden;
  for (const std::string &line : read_nonempty_lines(path)) {
    const std::vector<std::string> fields = split(line, ',');
    if (fields.front() == "CASE") {
      if (fields.size() != 5U)
        throw std::runtime_error("invalid CASE golden row");
      golden.id = fields[1];
      golden.expected_net_area_m2 = std::stod(fields[2]);
      golden.expected_outer_area_m2 = std::stod(fields[3]);
      golden.expected_hole_area_m2 = std::stod(fields[4]);
    } else if (fields.front() == "OUTER" || fields.front() == "HOLE") {
      if (fields.size() != 3U)
        throw std::runtime_error("invalid coordinate golden row");
      const area::Point2d point{std::stod(fields[1]), std::stod(fields[2])};
      (fields.front() == "OUTER" ? golden.outer : golden.hole).push_back(point);
    } else {
      throw std::runtime_error("unknown plan golden row");
    }
  }
  if (golden.id.empty() || golden.outer.size() < 3U || golden.hole.size() < 3U)
    throw std::runtime_error("incomplete plan golden fixture");
  return golden;
}

topology::Options topology_options() {
  topology::Options options;
  options.min_segment_length_m = 0.01;
  options.geometric_epsilon_m = 1.0e-8;
  options.max_abs_coordinate_m = 1'000.0;
  return options;
}

void test_protected_oblique_rib(const std::filesystem::path &root) {
  const topology::Feature accepted =
      protected_rib(root / "oblique_protected_rib.csv");
  const auto validated = topology::validate(accepted, topology_options());
  check(validated.status == topology::Status::kOk && validated.feature == accepted,
        "golden oblique protected chain validates as an exact submitted copy");
  check(accepted.vertices.front().position_locked,
        "topology fixture retains a separate adopted-position lock");
  check(std::all_of(accepted.ordered_segments.begin(), accepted.ordered_segments.end(),
                    [](const auto &segment) { return segment.protected_topology; }),
        "every rib segment is explicitly protected rather than inferred protected");

  topology::Feature refined = accepted;
  refined.revision = accepted.revision + 1U;
  refined.vertices[2].position_m = {1.46, 0.99};
  const auto refinement =
      topology::validate_refinement(accepted, refined, topology_options());
  check(refinement.status == topology::Status::kOk && refinement.feature == refined,
        "evidence-retaining refinement keeps the oblique rib order and identities");

  topology::Feature removed = accepted;
  removed.revision = accepted.revision + 1U;
  removed.vertices.pop_back();
  removed.ordered_segments.pop_back();
  const auto rejected =
      topology::validate_refinement(accepted, removed, topology_options());
  check(rejected.status == topology::Status::kTopologyChange && !rejected.feature,
        "protected rib removal is rejected atomically with no replacement geometry");
}

area::Provenance provenance(std::vector<std::string> support_ids) {
  return {area::GeometryOrigin::kObserved, area::QualityState::kReady,
          "golden-revision-44", std::move(support_ids)};
}

plan::Fragment supported_fragment(size_t index, const area::Point2d &start,
                                  const area::Point2d &end) {
  const std::string suffix = std::to_string(index);
  const std::string outer_support = "outer-support-" + suffix;
  const std::string first_source = "surface-a-" + suffix;
  const std::string second_source = "surface-b-" + suffix;
  plan::Fragment fragment;
  fragment.id = "oblique-edge-" + suffix;
  fragment.state = plan::EvidenceState::kSupported;
  fragment.kind = plan::EvidenceKind::kSurfaceIntersection;
  fragment.geometry_origin = area::GeometryOrigin::kObserved;
  fragment.segment = plan::Segment2d{start, end};
  fragment.source_height_range_m = plan::HeightRange{1.20, 1.20};
  fragment.source_revision = "golden-revision-44";
  fragment.support_ids = {outer_support, "hole-support", first_source, second_source};
  hss::surface_proposal::IntersectionProposal intersection;
  intersection.epoch_id = 44U;
  intersection.start_m = {start.x_m, 0.0, start.y_m};
  intersection.end_m = {end.x_m, 0.0, end.y_m};
  intersection.direction = {0.0, 1.0, 0.0};
  intersection.normal_angle_rad = 1.0;
  intersection.conditioning_sine = 0.8;
  intersection.condition_number = 1.25;
  intersection.first.surface_id = "surface-first-" + suffix;
  intersection.first.all_source_ids = {first_source};
  intersection.first.intersection_source_ids = {first_source};
  intersection.second.surface_id = "surface-second-" + suffix;
  intersection.second.all_source_ids = {second_source};
  intersection.second.intersection_source_ids = {second_source};
  fragment.intersection = std::move(intersection);
  return fragment;
}

plan::Input ready_plan(const PlanGolden &golden) {
  plan::Input input;
  input.scene_revision = "golden-revision-44";
  input.source_is_synthetic = true;
  input.view = plan::View::kArchitecturalCut;
  input.height.cut_height_m = 1.20;
  input.height.sampling_band_m = 0.10;
  input.height.reference_state = plan::ReferenceState::kConfirmedFloor;
  input.height.floor_reference_id = "golden-floor-frame";
  input.boundary_state = plan::BoundaryState::kClosedAccepted;
  std::vector<std::string> outer_support_ids;
  for (size_t index = 0U; index < golden.outer.size(); ++index) {
    const size_t next = (index + 1U) % golden.outer.size();
    input.fragments.push_back(
        supported_fragment(index, golden.outer[index], golden.outer[next]));
    outer_support_ids.push_back("outer-support-" + std::to_string(index));
  }
  area::Input area_input;
  area_input.policy = area::AreaPolicy::kNetFixedObstructions;
  area_input.outer = {"golden-oblique-rib", golden.outer,
                      area::TopologyState::kAccepted, provenance(outer_support_ids)};
  area::Hole hole;
  hole.ring = {"golden-detached-hole", golden.hole, area::TopologyState::kAccepted,
               provenance({"hole-support"})};
  hole.kind = area::HoleKind::kFixedObstruction;
  hole.attachment = area::Attachment::kDetached;
  hole.explicitly_fixed = true;
  area_input.holes = {hole};
  input.area_input = std::move(area_input);
  return input;
}

bool has_issue(const plan::PlanLayer &layer, plan::IssueCode expected,
               const std::string &target) {
  return std::any_of(layer.issues.begin(), layer.issues.end(),
                     [&](const plan::Issue &issue) {
                       return issue.code == expected && issue.target_id == target;
                     });
}

void test_oblique_plan_hole_and_semantic_uncertainty(
    const std::filesystem::path &root) {
  const PlanGolden golden = golden_plan(root / "oblique_rib_with_hole.csv");
  check(near(golden.outer[3].x_m - golden.outer[2].x_m, 0.15),
        "golden plan carries an explicit 0.15 m narrow rib front");
  check(!near(golden.outer[6].x_m - golden.outer[5].x_m, 0.0) &&
            !near(golden.outer[6].y_m - golden.outer[5].y_m, 0.0),
        "golden plan includes an oblique outer surface rather than a Manhattan-only case");

  const auto ready = plan::assemble(ready_plan(golden));
  check(ready.status == plan::Status::kOk && ready.layer &&
            ready.layer->validity == plan::Availability::kReady &&
            ready.layer->area.area_m2 &&
            near(*ready.layer->area.area_m2, golden.expected_net_area_m2),
        "closed observed oblique plan publishes only the authored net fixed-obstruction area");
  if (ready.layer && ready.layer->area.assessments.size() == 2U) {
    check(near(ready.layer->area.assessments[0].signed_area_m2,
               golden.expected_outer_area_m2) &&
              near(ready.layer->area.assessments[1].signed_area_m2,
                   -golden.expected_hole_area_m2) &&
              ready.layer->area.assessments[1].decision ==
                  area::Decision::kSubtractedFixedDetachedObstruction,
          "hole winding, detached/fixed classification, and subtraction remain explicit");
  } else {
    check(false, "golden plan returns both outer and detached-hole assessments");
  }

  const std::vector<std::string> uncertainty =
      read_nonempty_lines(root / "semantic_uncertainty.csv");
  check(uncertainty.size() == 3U, "semantic uncertainty fixture has a stable three-line contract");
  const std::vector<std::string> unknown_row = split(uncertainty.at(0), ',');
  const std::vector<std::string> issue_row = split(uncertainty.at(1), ',');
  const std::vector<std::string> area_row = split(uncertainty.at(2), ',');
  check(unknown_row.size() == 2U && issue_row.size() == 2U && area_row.size() == 2U,
        "semantic uncertainty fixture remains parseable without inferred geometry");

  plan::Input unknown = ready_plan(golden);
  unknown.boundary_state = plan::BoundaryState::kUnknown;
  unknown.fragments.clear();
  plan::Fragment unknown_fragment;
  unknown_fragment.id = unknown_row.at(1);
  unknown_fragment.state = plan::EvidenceState::kUnknown;
  unknown_fragment.kind = plan::EvidenceKind::kUnknownBoundary;
  unknown_fragment.geometry_origin = area::GeometryOrigin::kUnknown;
  unknown_fragment.source_revision = unknown.scene_revision;
  unknown_fragment.support_ids = {"unknown-support"};
  unknown.fragments = {unknown_fragment};
  unknown.area_input->holes.clear();
  unknown.area_input->outer.topology_state = area::TopologyState::kUnaccepted;
  unknown.area_input->outer.provenance = provenance({"unknown-support"});
  const auto unresolved = plan::assemble(unknown);
  check(unresolved.status == plan::Status::kOk && unresolved.layer &&
            unresolved.layer->validity == plan::Availability::kUnavailable &&
            !unresolved.layer->area.area_m2 &&
            !unresolved.layer->fragments.front().segment &&
            issue_row.at(1) == "unknown_boundary_evidence" &&
            area_row.at(1) == "none" &&
            has_issue(*unresolved.layer, plan::IssueCode::kUnknownBoundaryEvidence,
                      unknown_fragment.id),
        "semantic uncertainty remains an explicit issue with no generated segment or area");
}

std::string derived_uuid(std::string_view command_id, std::string_view role) {
  std::string seed(command_id);
  seed += "|";
  seed += role;
  seed += "|0";
  auto digest = hss::command::sha256(seed);
  digest[6] = static_cast<uint8_t>((digest[6] & 0x0fU) | 0x50U);
  digest[8] = static_cast<uint8_t>((digest[8] & 0x3fU) | 0x80U);
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (size_t i = 0U; i < 16U; ++i) {
    if (i == 4U || i == 6U || i == 8U || i == 10U)
      result.push_back('-');
    result.push_back(hex[digest[i] >> 4U]);
    result.push_back(hex[digest[i] & 0x0fU]);
  }
  return result;
}

void test_revisioned_undo(const std::filesystem::path &source,
                          const std::filesystem::path &root) {
  const std::vector<std::string> commands =
      read_nonempty_lines(root / "revisioned_undo.ndjson");
  check(commands.size() == 3U, "revisioned undo fixture contains split, undo, and redo commands");
  if (commands.size() != 3U)
    return;
  hss::scene_editing::Processor processor;
  const std::string original =
      read_file(source / "fixtures/scenes/scene.example.json");
  const auto split_result = processor.process(original, commands[0]);
  check(split_result.status == hss::scene_editing::Status::kApplied &&
            split_result.scene_changed && split_result.journal_record &&
            split_result.candidate_scene_json.find("\"scene_revision\":\"2\"") !=
                std::string::npos,
        "revisioned split publishes one immutable candidate revision");
  if (split_result.status != hss::scene_editing::Status::kApplied)
    return;
  const std::string split_node = derived_uuid(
      "44000000-0000-5000-8000-000000000001", "split-node");
  check(split_result.candidate_scene_json.find(split_node) != std::string::npos,
        "split candidate exposes deterministic derived lineage identity");

  const auto undo_result = processor.process(split_result.candidate_scene_json, commands[1]);
  check(undo_result.status == hss::scene_editing::Status::kApplied &&
            undo_result.scene_changed && undo_result.journal_record &&
            undo_result.candidate_scene_json.find("\"scene_revision\":\"3\"") !=
                std::string::npos &&
            undo_result.candidate_scene_json.find(split_node) == std::string::npos &&
            processor.journal().size() == 2U &&
            processor.journal()[1].original_command_id ==
                "44000000-0000-5000-8000-000000000001",
        "undo appends a compensating record and restores the prior model without retargeting");
  if (undo_result.status != hss::scene_editing::Status::kApplied)
    return;

  const auto redo_result = processor.process(undo_result.candidate_scene_json, commands[2]);
  check(redo_result.status == hss::scene_editing::Status::kApplied &&
            redo_result.candidate_scene_json.find(split_node) != std::string::npos &&
            processor.journal().size() == 3U,
        "redo appends a revisioned record and restores the original split lineage");
}

} // namespace

int main() {
  try {
    const std::filesystem::path source = HSS_SOURCE_DIR;
    const std::filesystem::path golden = source / "tests/fixtures/golden";
    test_protected_oblique_rib(golden);
    test_oblique_plan_hole_and_semantic_uncertainty(golden);
    test_revisioned_undo(source, golden);
  } catch (const std::exception &error) {
    std::cerr << "FAIL: uncaught fixture/test error: " << error.what() << '\n';
    ++failures;
  }
  if (failures != 0)
    return 1;
  std::cout << "B-044 golden suite passed\n";
  return 0;
}

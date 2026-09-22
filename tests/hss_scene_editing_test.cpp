#include "hss/scene_editing.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream.good());
  std::ostringstream bytes;
  bytes << stream.rdbuf();
  return bytes.str();
}

void write_file(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  assert(stream.good());
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  assert(stream.good());
}

std::string command(std::string id, std::string revision, std::string sequence,
                    std::string targets, std::string type,
                    std::string payload) {
  return "{\"schema_version\":\"1.1\",\"command_id\":\"" + id +
         "\",\"project_id\":\"d5708cd1-a901-5786-b7f7-d422ad92be92\","
         "\"expected_scene_revision\":\"" + revision +
         "\",\"event_sequence\":\"" + sequence +
         "\",\"targets\":" + targets +
         ",\"user_intent\":\"host regression\",\"type\":\"" + type +
         "\",\"payload\":" + payload + "}";
}

std::string derived_uuid(std::string_view command_id, std::string_view role,
                         size_t index = 0U) {
  std::string seed(command_id);
  seed += "|";
  seed += role;
  seed += "|";
  seed += std::to_string(index);
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

constexpr const char *kFirstEdge = "8d24f966-9247-5a7d-8682-d3ce654b55b3";
constexpr const char *kSecondEdge = "03e82eca-afee-56e6-9ce8-441a0301afff";
constexpr const char *kBottomEdge = "58cbe781-8405-5f29-81c9-48a423f5a70d";
constexpr const char *kClosingEdge = "2ee273c0-8a9f-57f9-ab72-507c4e088bcf";
constexpr const char *kRibFrontA = "1f56edc2-a03e-5206-8141-e506af308ffd";
constexpr const char *kRibFrontB = "91376da5-1282-53db-ba37-55eb69e7c3e0";

void test_split_retry_undo_redo(const std::string &scene,
                                const std::filesystem::path &emit_dir) {
  hss::scene_editing::Processor processor;
  const std::string split_id = "aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa1";
  const std::string split =
      command(split_id, "1", "10", "[\"" + std::string(kFirstEdge) + "\"]",
              "split_edge", "{\"edge_parameter\":0.4}");
  auto applied = processor.process(scene, split);
  assert(applied.status == hss::scene_editing::Status::kApplied);
  assert(applied.scene_changed && applied.journal_record.has_value());
  assert(applied.candidate_scene_json.find("\"scene_revision\":\"2\"") !=
         std::string::npos);
  write_file(emit_dir / "scene_editing.applied.scene.json",
             applied.candidate_scene_json);
  write_file(emit_dir / "scene_editing.applied.result.json",
             applied.exact_result_json);

  auto retry = processor.process(applied.candidate_scene_json, split);
  assert(retry.status == hss::scene_editing::Status::kStoredRetry);
  assert(!retry.scene_changed);
  assert(retry.exact_result_json == applied.exact_result_json);

  std::string different = split;
  different.insert(different.begin(), ' ');
  auto mismatch = processor.process(applied.candidate_scene_json, different);
  assert(mismatch.status == hss::scene_editing::Status::kDuplicateMismatch);
  assert(!mismatch.scene_changed);

  const std::string undo = command(
      "aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa2", "2", "11", "[]", "undo",
      "{\"original_command_id\":\"" + split_id + "\"}");
  auto undone = processor.process(applied.candidate_scene_json, undo);
  assert(undone.status == hss::scene_editing::Status::kApplied);
  assert(undone.candidate_scene_json.find("\"scene_revision\":\"3\"") !=
         std::string::npos);
  assert(undone.candidate_scene_json.find(derived_uuid(split_id, "split-node")) ==
         std::string::npos);

  const std::string redo = command(
      "aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa3", "3", "12", "[]", "redo",
      "{\"original_command_id\":\"" + split_id + "\"}");
  auto redone = processor.process(undone.candidate_scene_json, redo);
  assert(redone.status == hss::scene_editing::Status::kApplied);
  assert(redone.candidate_scene_json.find(derived_uuid(split_id, "split-node")) !=
         std::string::npos);

  const std::string stale = command(
      "aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa4", "1", "13",
      "[\"" + std::string(kFirstEdge) + "\"]", "protect_feature",
      "{\"preserve_topology\":true,\"lock_position\":false}");
  auto stale_result = processor.process(redone.candidate_scene_json, stale);
  assert(stale_result.status == hss::scene_editing::Status::kStaleRevision);
  assert(!stale_result.scene_changed);
}

void test_exact_and_conflicting_constraints(const std::string &scene,
                                            const std::filesystem::path &emit_dir) {
  hss::scene_editing::Processor exact_processor;
  const std::string exact = command(
      "bbbbbbbb-bbbb-5bbb-8bbb-bbbbbbbbbbb1", "1", "20", "[]",
      "set_dimension",
      "{\"endpoint_a\":\"" + std::string(kRibFrontA) +
          "\",\"endpoint_b\":\"" + std::string(kRibFrontB) +
          "\",\"value\":20,\"unit\":\"cm\",\"source\":\"laser\","
          "\"uncertainty_m\":0.003,\"mode\":\"exact_model\",\"weight\":1}");
  auto dimension = exact_processor.process(scene, exact);
  if (dimension.status != hss::scene_editing::Status::kApplied)
    std::cerr << "dimension failed: "
              << hss::scene_editing::status_name(dimension.status) << " "
              << dimension.exact_result_json << "\n";
  assert(dimension.status == hss::scene_editing::Status::kApplied);
  assert(dimension.candidate_scene_json.find("\"kind\":\"distance\"") !=
         std::string::npos);
  write_file(emit_dir / "scene_editing.dimension.scene.json",
             dimension.candidate_scene_json);
  write_file(emit_dir / "scene_editing.dimension.result.json",
             dimension.exact_result_json);

  hss::scene_editing::Processor conflict_processor;
  const std::string lock_a_id = "cccccccc-cccc-5ccc-8ccc-ccccccccccc1";
  auto lock_a = conflict_processor.process(
      scene,
      command(lock_a_id, "1", "21", "[\"" + std::string(kRibFrontA) + "\"]",
              "lock_position",
              "{\"position_m\":[2,0,-0.5],\"explicit_confirmation\":true}"));
  assert(lock_a.status == hss::scene_editing::Status::kApplied);
  auto lock_b = conflict_processor.process(
              lock_a.candidate_scene_json,
              command("cccccccc-cccc-5ccc-8ccc-ccccccccccc2", "2", "22",
              "[\"" + std::string(kRibFrontB) + "\"]", "lock_position",
              "{\"position_m\":[2.15,0,-0.5],\"explicit_confirmation\":true}"));
  assert(lock_b.status == hss::scene_editing::Status::kApplied);
  auto conflict = conflict_processor.process(
      lock_b.candidate_scene_json,
              command("cccccccc-cccc-5ccc-8ccc-ccccccccccc3", "3", "23", "[]",
              "set_dimension",
              "{\"endpoint_a\":\"" + std::string(kRibFrontA) +
                  "\",\"endpoint_b\":\"" + std::string(kRibFrontB) +
                  "\",\"value\":0.3,\"unit\":\"m\",\"source\":\"tape\","
                  "\"uncertainty_m\":null,\"mode\":\"exact_model\",\"weight\":1}"));
  assert(conflict.status == hss::scene_editing::Status::kConflict);
  assert(!conflict.scene_changed);
  assert(conflict.exact_result_json.find("\"state\":\"conflict\"") !=
         std::string::npos);
  write_file(emit_dir / "scene_editing.conflict.result.json",
             conflict.exact_result_json);

  const std::string unlock_constraint = derived_uuid(lock_a_id, "position-lock");
  auto unlock = conflict_processor.process(
      lock_b.candidate_scene_json,
      command("cccccccc-cccc-5ccc-8ccc-ccccccccccc4", "3", "24",
              "[\"" + std::string(kRibFrontA) + "\"]", "unlock_position",
              "{\"constraint_id\":\"" + unlock_constraint + "\"}"));
  assert(unlock.status == hss::scene_editing::Status::kApplied);
}

void test_topology_unknown_and_pending_exclusion(const std::string &scene) {
  hss::scene_editing::Processor protection_processor;
  auto protected_result = protection_processor.process(
      scene,
      command("dddddddd-dddd-5ddd-8ddd-ddddddddddd1", "1", "30",
              "[\"" + std::string(kFirstEdge) + "\"]", "protect_feature",
              "{\"preserve_topology\":true,\"lock_position\":false}"));
  assert(protected_result.status == hss::scene_editing::Status::kApplied);
  auto merge = protection_processor.process(
      protected_result.candidate_scene_json,
      command("dddddddd-dddd-5ddd-8ddd-ddddddddddd2", "2", "31",
              "[\"" + std::string(kFirstEdge) + "\",\"" +
                  std::string(kSecondEdge) + "\"]",
              "merge_edges", "{\"preserve_source_lineage\":true}"));
  assert(merge.status == hss::scene_editing::Status::kConflict);
  assert(!merge.scene_changed);

  hss::scene_editing::Processor unknown_processor;
  auto unknown = unknown_processor.process(
      scene,
      command("dddddddd-dddd-5ddd-8ddd-ddddddddddd3", "1", "32",
              "[\"" + std::string(kFirstEdge) + "\"]", "mark_unknown",
              "{\"reason\":\"No supported view behind cabinet\"}"));
  assert(unknown.status == hss::scene_editing::Status::kApplied);
  assert(unknown.candidate_scene_json.find("\"geometry_origin\":\"unknown\"") !=
         std::string::npos);

  hss::scene_editing::Processor exclusion_processor;
  auto exclusion = exclusion_processor.process(
      scene,
      command("dddddddd-dddd-5ddd-8ddd-ddddddddddd4", "1", "33", "[]",
              "exclude_dynamic_region",
              "{\"min_m\":[1,0,-2],\"max_m\":[2,2,-1],"
              "\"start_sequence\":\"10\",\"end_sequence\":\"20\","
              "\"reason\":\"moving person\"}"));
  assert(exclusion.status ==
         hss::scene_editing::Status::kPendingUnsupported);
  assert(!exclusion.scene_changed && exclusion.pending_dynamic_exclusion.has_value());
  assert(exclusion.exact_result_json.find("\"state\":\"superseded\"") !=
         std::string::npos);
  assert(exclusion_processor.pending_dynamic_exclusions().size() == 1U);
}

void test_remaining_functional_variants(const std::string &scene) {
  hss::scene_editing::Processor corner_processor;
  auto corner = corner_processor.process(
      scene,
      command("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee1", "1", "40",
              "[\"" + std::string(kFirstEdge) + "\"]", "mark_corner",
              "{\"point_m\":[1,0,0],\"support_ids\":[\"f28998ec-31a8-51a3-a300-b3b45bd1cc7a\"],"
              "\"pending_ray\":null}"));
  assert(corner.status == hss::scene_editing::Status::kApplied);

  hss::scene_editing::Processor merge_processor;
  auto merged = merge_processor.process(
      scene,
      command("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee2", "1", "41",
              "[\"" + std::string(kBottomEdge) + "\",\"" +
                  std::string(kClosingEdge) + "\"]",
              "merge_edges", "{\"preserve_source_lineage\":true}"));
  assert(merged.status == hss::scene_editing::Status::kApplied);

  hss::scene_editing::Processor soft_processor;
  auto soft = soft_processor.process(
      scene,
      command("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee3", "1", "42", "[]",
              "set_dimension",
              "{\"endpoint_a\":\"" + std::string(kRibFrontA) +
                  "\",\"endpoint_b\":\"" + std::string(kRibFrontB) +
                  "\",\"value\":0.18,\"unit\":\"m\",\"source\":\"guess\","
                  "\"uncertainty_m\":null,\"mode\":\"soft_measurement\",\"weight\":2}"));
  assert(soft.status == hss::scene_editing::Status::kApplied);
  assert(soft.candidate_scene_json.find("\"hard\":false") != std::string::npos);

  hss::scene_editing::Processor feature_processor;
  auto feature = feature_processor.process(
      scene,
      command("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee4", "1", "43",
              "[\"" + std::string(kBottomEdge) + "\"]", "mark_protrusion",
              "{\"parent_edge_id\":\"" + std::string(kBottomEdge) +
                  "\",\"ordered_points_m\":[[5,0,-5],[5,0,-4.8],[4.5,0,-4.8],"
                  "[4.5,0,-5]],\"support_ids\":[],\"preserve_topology\":true}"));
  assert(feature.status == hss::scene_editing::Status::kApplied);
  assert(feature.candidate_scene_json.find("\"kind\":\"protrusion\"") !=
         std::string::npos);

  hss::scene_editing::Processor obstruction_processor;
  const std::string obstruction_id = "eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee5";
  auto obstruction = obstruction_processor.process(
      scene,
      command(obstruction_id, "1", "44", "[]", "mark_obstruction",
              "{\"footprint_m\":[[3,2],[3,2.5],[3.3,2.5],[3.3,2]],"
              "\"attachment\":\"detached\",\"fixed\":true}"));
  assert(obstruction.status == hss::scene_editing::Status::kApplied);
  assert(obstruction.candidate_scene_json.find("29.775") != std::string::npos);
  const std::string object_id = derived_uuid(obstruction_id, "obstruction-object");
  auto classified = obstruction_processor.process(
      obstruction.candidate_scene_json,
      command("eeeeeeee-eeee-5eee-8eee-eeeeeeeeeee6", "2", "45",
              "[\"" + object_id + "\"]", "classify_object",
              "{\"label\":\"column\",\"semantic_origin\":\"user\","
              "\"confidence\":null}"));
  assert(classified.status == hss::scene_editing::Status::kApplied);
  assert(classified.candidate_scene_json.find("\"label\":\"column\"") !=
         std::string::npos);
}

void test_derivative() {
  const std::array<double, 3U> a{1.2, -0.4, 2.0};
  const std::array<double, 3U> b{-0.3, 0.7, 1.1};
  std::array<double, 6U> analytic{};
  assert(hss::scene_editing::distance_constraint_derivative(a, b, &analytic));
  const double epsilon = 1e-6;
  auto distance = [](const std::array<double, 3U> &x,
                     const std::array<double, 3U> &y) {
    double sum = 0.0;
    for (size_t i = 0U; i < 3U; ++i) {
      const double d = x[i] - y[i];
      sum += d * d;
    }
    return std::sqrt(sum);
  };
  for (size_t variable = 0U; variable < 6U; ++variable) {
    auto plus_a = a, minus_a = a, plus_b = b, minus_b = b;
    if (variable < 3U) {
      plus_a[variable] += epsilon;
      minus_a[variable] -= epsilon;
    } else {
      plus_b[variable - 3U] += epsilon;
      minus_b[variable - 3U] -= epsilon;
    }
    const double numeric =
        (distance(plus_a, plus_b) - distance(minus_a, minus_b)) /
        (2.0 * epsilon);
    assert(std::abs(numeric - analytic[variable]) < 1e-8);
  }
  assert(!hss::scene_editing::distance_constraint_derivative(a, a, &analytic));
}

void test_command_schema_examples(const std::string &scene,
                                  const std::filesystem::path &source) {
  for (const auto &entry :
       std::filesystem::directory_iterator(source / "fixtures/commands/valid")) {
    if (entry.path().extension() != ".json")
      continue;
    hss::scene_editing::Processor processor;
    const auto result = processor.process(scene, read_file(entry.path()));
    assert(result.status != hss::scene_editing::Status::kInvalidCommand);
    assert(result.status != hss::scene_editing::Status::kInvalidScene);
  }
  for (const auto &entry :
       std::filesystem::directory_iterator(source / "fixtures/commands/invalid")) {
    if (entry.path().extension() != ".json")
      continue;
    hss::scene_editing::Processor processor;
    const auto result = processor.process(scene, read_file(entry.path()));
    assert(result.status == hss::scene_editing::Status::kInvalidCommand);
  }
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path source = HSS_SOURCE_DIR;
  const std::string scene = read_file(source / "fixtures/scenes/scene.example.json");
  const std::filesystem::path emit_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
  std::filesystem::create_directories(emit_dir);
  test_split_retry_undo_redo(scene, emit_dir);
  test_exact_and_conflicting_constraints(scene, emit_dir);
  test_topology_unknown_and_pending_exclusion(scene);
  test_remaining_functional_variants(scene);
  test_derivative();
  test_command_schema_examples(scene, source);
  std::cout << "scene editing regression passed\n";
  return 0;
}

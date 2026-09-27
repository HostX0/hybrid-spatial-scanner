#include "hss/hss_api.h"
#include "hss/hss_project_store.h"
#include "hss/scene_editing.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HSSId id(uint8_t first) {
  HSSId result{};
  result.bytes[0] = first;
  result.bytes[6] = 0x50U;
  result.bytes[8] = 0x80U;
  return result;
}

HSSMat4d identity() {
  HSSMat4d result{};
  result.column_major[0] = result.column_major[5] =
      result.column_major[10] = result.column_major[15] = 1.0;
  return result;
}

HSSConfig engine_config() {
  HSSConfig result{};
  result.abi_version = HSS_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.project_id = id(1U);
  result.structure_voxel_m = 0.02;
  result.detail_voxel_m = 0.01;
  result.truncation_m = 0.04;
  result.max_working_bytes = 16U * 1024U * 1024U;
  result.max_pending_frames = 2U;
  return result;
}

HSSProjectEventContext context(uint64_t sequence, uint64_t revision) {
  HSSProjectEventContext result{};
  result.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.capture_id = id(2U);
  result.event_sequence = sequence;
  result.accepted_scene_revision = revision;
  result.session_epoch = 0U;
  result.monotonic_time_s = static_cast<double>(sequence);
  return result;
}

HSSProjectStoreConfig store_config(const std::string &root, uint32_t mode) {
  HSSProjectStoreConfig result{};
  result.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.project_id = id(1U);
  result.trusted_root_directory_utf8 = {
      reinterpret_cast<const uint8_t *>(root.data()), root.size()};
  result.open_mode = mode;
  result.source_is_synthetic = 1U;
  result.max_epoch_records = 8U;
  result.max_depth_records = 8U;
  result.max_mesh_records = 8U;
  result.max_command_records = 8U;
  result.max_retained_bytes = 16U * 1024U * 1024U;
  result.max_record_bytes = 2U * 1024U * 1024U;
  result.max_pending_bytes = 8U * 1024U * 1024U;
  return result;
}

void drain(HSSEngine *engine, uint64_t expected_revision) {
  for (size_t iteration = 0U; iteration != 200000U; ++iteration) {
    HSSEngineStats stats{};
    check(hss_engine_get_stats(engine, &stats) == HSS_OK, "read engine stats");
    if (stats.lifecycle_state == 1U) {
      check(stats.scene_revision == expected_revision, "expected published revision");
      return;
    }
    check(hss_engine_step(engine, 100U) == HSS_OK, "advance source rebuild");
  }
  check(false, "source rebuild completed within bounded work");
}

std::string scene_json(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK, "acquire snapshot");
  uint64_t required = 0U;
  check(hss_snapshot_copy_scene_json(snapshot, nullptr, 0U, &required) ==
            HSS_BUFFER_TOO_SMALL,
        "query scene JSON");
  std::string result(required, '\0');
  check(hss_snapshot_copy_scene_json(
            snapshot, reinterpret_cast<uint8_t *>(result.data()), result.size(),
            &required) == HSS_OK,
        "copy scene JSON");
  hss_snapshot_release(snapshot);
  return result;
}

std::vector<HSSVec3d> mesh_vertices(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK, "acquire mesh snapshot");
  HSSCounts counts{};
  check(hss_snapshot_get_counts(snapshot, &counts) == HSS_OK, "read mesh counts");
  std::vector<HSSVec3d> result(counts.vertex_count);
  uint64_t required = 0U;
  check(hss_snapshot_copy_vertices(snapshot, result.data(), result.size(), &required) ==
            HSS_OK &&
            required == result.size(),
        "copy captured vertices");
  hss_snapshot_release(snapshot);
  return result;
}

bool has_typed_plan(HSSEngine *engine) {
  HSSSnapshot *snapshot = nullptr;
  check(hss_engine_acquire_snapshot(engine, &snapshot) == HSS_OK,
        "acquire plan snapshot");
  HSSPlanAnnotationSummary summary{};
  check(hss_snapshot_get_plan_annotation_summary(snapshot, &summary) == HSS_OK,
        "read plan summary");
  hss_snapshot_release(snapshot);
  return summary.plan_available != 0U;
}

bool same_vertices(const std::vector<HSSVec3d> &left,
                   const std::vector<HSSVec3d> &right) {
  if (left.size() != right.size())
    return false;
  for (size_t index = 0U; index < left.size(); ++index) {
    if (left[index].x != right[index].x || left[index].y != right[index].y ||
        left[index].z != right[index].z)
      return false;
  }
  return true;
}

std::string result_json(HSSEngine *engine) {
  uint64_t required = 0U;
  const HSSStatus initial = hss_engine_copy_command_result_json(
      engine, nullptr, 0U, &required);
  check(initial == HSS_BUFFER_TOO_SMALL && required != 0U,
        "query exact command result");
  std::string result(required, '\0');
  check(hss_engine_copy_command_result_json(
            engine, reinterpret_cast<uint8_t *>(result.data()), result.size(),
            &required) == HSS_OK,
        "copy exact command result");
  return result;
}

std::string project_id_from_scene(const std::string &scene) {
  constexpr std::string_view prefix = "\"project_id\":\"";
  const size_t begin = scene.find(prefix);
  check(begin != std::string::npos, "scene project id present");
  const size_t first = begin + prefix.size();
  const size_t end = scene.find('"', first);
  check(end != std::string::npos, "scene project id closed");
  return scene.substr(first, end - first);
}

std::string mark_unknown_command(const std::string &project_id) {
  return "{\"schema_version\":\"1.1\",\"command_id\":"
         "\"aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa1\",\"project_id\":\"" +
         project_id +
         "\",\"expected_scene_revision\":\"3\",\"event_sequence\":\"5\","
         "\"targets\":[],\"user_intent\":\"host regression\","
         "\"type\":\"mark_unknown\",\"payload\":{\"reason\":"
         "\"occluded rear face remains unknown\"}}";
}

std::string pending_exclusion_command(const std::string &project_id) {
  return "{\"schema_version\":\"1.1\",\"command_id\":"
         "\"aaaaaaaa-aaaa-5aaa-8aaa-aaaaaaaaaaa2\",\"project_id\":\"" +
         project_id +
         "\",\"expected_scene_revision\":\"3\",\"event_sequence\":\"4\","
         "\"targets\":[],\"user_intent\":\"host regression\","
         "\"type\":\"exclude_dynamic_region\",\"payload\":{"
         "\"min_m\":[0,0,-1],\"max_m\":[1,1,0],"
         "\"start_sequence\":\"1\",\"end_sequence\":\"2\","
         "\"reason\":\"moving person\"}}";
}

void submit_baseline(HSSEngine *engine, HSSProjectStore *store,
                     const std::string &command, std::string *edited_scene,
                     std::string *exact_result) {
  HSSProjectStoreReceipt receipt{};
  HSSEpoch epoch{};
  epoch.abi_version = HSS_ABI_VERSION;
  epoch.struct_size = sizeof(epoch);
  epoch.capture_id = id(2U);
  epoch.event_sequence = 1U;
  epoch.relationship_state = 1U;
  epoch.T_project_world = identity();
  epoch.transform_revision = 1U;
  const auto c1 = context(1U, 1U);
  check(hss_engine_set_epoch(engine, &epoch) == HSS_OK, "queue epoch");
  drain(engine, 1U);
  check(hss_project_store_append_epoch(store, &c1, &epoch, &receipt) == HSS_OK,
        "persist epoch");

  const std::array<float, 9U> vertices{0.F, 0.F, 0.F, 2.F, 0.F, 0.F,
                                        0.F, 3.F, 0.F};
  const std::array<uint32_t, 3U> triangles{0U, 1U, 2U};
  HSSMeshEvent mesh{};
  mesh.abi_version = HSS_ABI_VERSION;
  mesh.struct_size = sizeof(mesh);
  mesh.capture_id = id(2U);
  mesh.anchor_id = id(3U);
  mesh.event_sequence = 2U;
  mesh.anchor_revision = 1U;
  mesh.T_world_anchor = identity();
  mesh.vertices_xyz = vertices.data();
  mesh.vertex_count = 3U;
  mesh.triangle_indices = triangles.data();
  mesh.triangle_count = 1U;
  const auto c2 = context(2U, 2U);
  check(hss_engine_submit_mesh_event(engine, &mesh) == HSS_OK, "queue mesh");
  drain(engine, 2U);
  check(hss_project_store_append_mesh_event(store, &c2, &mesh, &receipt) ==
            HSS_OK,
        "persist mesh");

  const HSSMat4d floor = identity();
  HSSProjectPlanReference plan{};
  plan.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  plan.struct_size = sizeof(plan);
  plan.T_project_floor = floor;
  plan.cut_height_m = 1.2;
  plan.before_scene_revision = 2U;
  plan.after_scene_revision = 3U;
  const auto c3 = context(3U, 3U);
  check(hss_engine_set_plan_reference(engine, &floor, plan.cut_height_m) == HSS_OK,
        "apply floor control");
  drain(engine, 3U);
  check(hss_project_store_append_plan_reference(store, &c3, &plan, &receipt) ==
            HSS_OK,
        "persist floor control");

  const auto raw_vertices = mesh_vertices(engine);
  const std::string pending = pending_exclusion_command(project_id_from_scene(scene_json(engine)));
  const std::string before_pending = scene_json(engine);
  check(hss_engine_apply_command_json(
            engine, {reinterpret_cast<const uint8_t *>(pending.data()), pending.size()}) ==
            HSS_UNSUPPORTED_CAPABILITY,
        "typed dynamic exclusion remains pending without a B035 consumer");
  const std::string pending_result = result_json(engine);
  check(!pending_result.empty() && scene_json(engine) == before_pending,
        "pending exclusion retains exact result without replacing snapshot");
  const auto c_pending = context(4U, 3U);
  HSSProjectCommandRecord pending_record{};
  pending_record.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  pending_record.struct_size = sizeof(pending_record);
  pending_record.context = c_pending;
  pending_record.before_scene_revision = 3U;
  pending_record.after_scene_revision = 3U;
  pending_record.disposition = HSS_PROJECT_COMMAND_PENDING_EXCLUSION;
  pending_record.command_json = {reinterpret_cast<const uint8_t *>(pending.data()), pending.size()};
  pending_record.result_json = {reinterpret_cast<const uint8_t *>(pending_result.data()),
                                pending_result.size()};
  HSSProjectStoreReceipt pending_receipt{};
  check(hss_project_store_append_command_record(store, &pending_record,
                                                &pending_receipt) == HSS_OK,
        "persist pending typed exclusion exact result");
  const HSSStatus edit_status = hss_engine_apply_command_json(
      engine, {reinterpret_cast<const uint8_t *>(command.data()), command.size()});
  if (edit_status != HSS_OK)
    std::cerr << "scene edit status " << edit_status << " result "
              << result_json(engine) << "\n";
  check(edit_status == HSS_OK, "apply accepted scene edit");
  *exact_result = result_json(engine);
  *edited_scene = scene_json(engine);
  check(edited_scene->find("\"geometry_origin\":\"unknown\"") !=
            std::string::npos,
        "edited metric scene is published");
  check(same_vertices(mesh_vertices(engine), raw_vertices),
        "editing preserves captured mesh buffers exactly");
  check(!has_typed_plan(engine),
        "unmapped edit cannot reuse stale pre-command plan semantics");

  const auto c4 = context(5U, 4U);
  HSSProjectCommandRecord record{};
  record.abi_version = HSS_PROJECT_STORE_ABI_VERSION;
  record.struct_size = sizeof(record);
  record.context = c4;
  record.before_scene_revision = 3U;
  record.after_scene_revision = 4U;
  record.command_json = {reinterpret_cast<const uint8_t *>(command.data()), command.size()};
  record.result_json = {reinterpret_cast<const uint8_t *>(exact_result->data()),
                        exact_result->size()};
  check(hss_project_store_append_command_record(store, &record, &receipt) == HSS_OK,
        "persist exact command/result control");
}


// Publication is deliberately too large for the tiny budget. A failed edit
// must not enter the retry journal or overwrite an earlier exposed result.
void publication_failure_is_atomic() {
  for (bool keep_previous_result : {false, true}) {
    auto configuration = engine_config();
    configuration.max_working_bytes = 4096U;
    HSSEngine *engine=nullptr;
    check(hss_engine_create(&configuration, &engine)==HSS_OK, "tiny budget create");
    const auto before=scene_json(engine);
    auto at_revision_zero = [](std::string command) {
      const std::string old="\"expected_scene_revision\":\"3\"";
      const size_t at=command.find(old);
      check(at!=std::string::npos, "fixture expected revision");
      command.replace(at, old.size(), "\"expected_scene_revision\":\"0\"");
      return command;
    };
    std::string previous;
    if (keep_previous_result) {
      const auto pending=at_revision_zero(pending_exclusion_command(project_id_from_scene(before)));
      check(hss_engine_apply_command_json(engine,
          {reinterpret_cast<const uint8_t*>(pending.data()),pending.size()})==HSS_UNSUPPORTED_CAPABILITY,
          "establish previous pending result");
      previous=result_json(engine);
    }
    const auto command=at_revision_zero(mark_unknown_command(project_id_from_scene(before)));
    const HSSBytes bytes{reinterpret_cast<const uint8_t*>(command.data()),command.size()};
    const auto first=hss_engine_apply_command_json(engine,bytes);
    const auto retry=hss_engine_apply_command_json(engine,bytes);
    std::cout<<"publication first="<<first<<" exact_retry="<<retry<<'\n';
    check(first==HSS_RESOURCE_LIMIT && retry==HSS_RESOURCE_LIMIT,
          "failed publication exact retry must execute, never false StoredRetry");
    check(scene_json(engine)==before, "failed publication leaves complete scene unchanged");
    HSSEngineStats stats{};
    check(hss_engine_get_stats(engine,&stats)==HSS_OK &&
              stats.scene_revision==0U && stats.accepted_commands==0U,
          "failed publication does not advance revision or accepted counter");
    if (keep_previous_result) {
      check(result_json(engine)==previous, "prior exact result survives failed publication");
    } else {
      uint64_t size=99U;
      check(hss_engine_copy_command_result_json(engine,nullptr,0U,&size)==HSS_NOT_READY,
            "no phantom applied result on failed publication");
    }
    hss_engine_destroy(engine);
  }
}


size_t occurrences(const std::string &text, const std::string &needle) {
  size_t count = 0U, at = 0U;
  while ((at = text.find(needle, at)) != std::string::npos) { ++count; at += needle.size(); }
  return count;
}

std::string derived_id(const std::string &command, const std::string &role) {
  auto bytes = hss::command::sha256(command + "|" + role + "|0");
  bytes[6] = (bytes[6] & 15U) | 0x50U;
  bytes[8] = (bytes[8] & 63U) | 0x80U;
  const char *hex = "0123456789abcdef";
  std::string out;
  for (size_t i=0U;i<16U;++i) {
    if(i==4U||i==6U||i==8U||i==10U) out += '-';
    out += hex[bytes[i]>>4U]; out += hex[bytes[i]&15U];
  }
  return out;
}

void overlay_reservation_failure() {
  HSSEngine *engine=nullptr;
  auto cfg=engine_config();
  check(hss_engine_create(&cfg,&engine)==HSS_OK,"measure overlay fixture");
  std::string command=mark_unknown_command(project_id_from_scene(scene_json(engine)));
  const std::string old="\"expected_scene_revision\":\"3\"";
  command.replace(command.find(old),old.size(),"\"expected_scene_revision\":\"0\"");
  const HSSBytes bytes{reinterpret_cast<const uint8_t*>(command.data()),command.size()};
  check(hss_engine_apply_command_json(engine,bytes)==HSS_OK,"measure edited scene reservation");
  const uint64_t scene_charge=4096U+scene_json(engine).size();
  hss_engine_destroy(engine);
  cfg.max_working_bytes=scene_charge;
  check(hss_engine_create(&cfg,&engine)==HSS_OK,"scene fits but overlay does not");
  const auto before=scene_json(engine);
  for(int i=0;i<2;++i)
    check(hss_engine_apply_command_json(engine,bytes)==HSS_RESOURCE_LIMIT,
          "overlay reservation failure rolls back volatile command for true retry");
  check(scene_json(engine)==before,"failed overlay allocation leaves original snapshot");
  uint64_t needed=0U;
  check(hss_engine_copy_command_result_json(engine,nullptr,0U,&needed)==HSS_NOT_READY,
        "no result leaked from failed overlay reservation");
  hss_engine_destroy(engine);
}

void retained_capacity_is_fail_closed() {
  HSSEngine *engine=nullptr; const auto cfg=engine_config();
  check(hss_engine_create(&cfg,&engine)==HSS_OK,"capacity baseline scene");
  std::string scene=scene_json(engine);hss_engine_destroy(engine);
  const auto project=project_id_from_scene(scene);
  hss::scene_editing::Processor processor;
  const auto build=[&](uint64_t index,uint64_t revision,const std::string &type,
                       const std::string &payload) {
    return "{\"schema_version\":\"1.1\",\"command_id\":\""+
      derived_id("capacity-fixture",std::to_string(index))+"\",\"project_id\":\""+
      project+"\",\"expected_scene_revision\":\""+std::to_string(revision)+
      "\",\"event_sequence\":\""+std::to_string(revision+1U)+
      "\",\"targets\":[],\"user_intent\":\"bounded retention\",\"type\":\""+
      type+"\",\"payload\":"+payload+"}";
  };
  const size_t cap=hss::scene_editing::kMaxRetainedUnknowns;
  for(size_t i=0;i<cap;++i) {
    auto applied=processor.process(scene,build(i,i,"mark_unknown",R"({"reason":"capacity fixture"})"));
    check(applied.status==hss::scene_editing::Status::kApplied &&
          applied.retained_unknowns && applied.retained_unknowns->size()==i+1U,
          "retention accepts up to documented capacity");
    scene=std::move(applied.candidate_scene_json);
  }
  const auto overflow=build(cap,cap,"mark_unknown",R"({"reason":"capacity overflow"})");
  for(int i=0;i<2;++i) {
    const auto rejected=processor.process(scene,overflow);
    check(rejected.status==hss::scene_editing::Status::kResourceLimitExceeded &&
          !rejected.scene_changed && !rejected.owns_new_record &&
          rejected.exact_result_json.empty() && processor.journal().size()==cap,
          "capacity failure retains scene/history and cannot become phantom stored retry");
  }
  const auto undo=processor.process(scene,build(cap+1U,cap,"undo",
      "{\"original_command_id\":\""+derived_id("capacity-fixture",std::to_string(cap-1U))+"\"}"));
  check(undo.status==hss::scene_editing::Status::kApplied &&
        undo.retained_unknowns->size()==cap-1U,"undo releases annotation capacity");
  const auto accepted=processor.process(undo.candidate_scene_json,
      build(cap,cap+1U,"mark_unknown",R"({"reason":"capacity overflow"})"));
  check(accepted.status==hss::scene_editing::Status::kApplied &&
        accepted.retained_unknowns->size()==cap,"failed capacity command can be validly resubmitted after undo");
}

void retained_unknown_lifecycle() {
  HSSEngine *engine=nullptr;
  const auto cfg=engine_config();
  check(hss_engine_create(&cfg,&engine)==HSS_OK, "overlay lifecycle create");
  check(hss_engine_set_source_is_synthetic(engine,1U)==HSS_OK, "overlay synthetic");
  const auto project=project_id_from_scene(scene_json(engine));
  const std::string one="cccccccc-cccc-5ccc-8ccc-ccccccccccc1";
  const std::string two="cccccccc-cccc-5ccc-8ccc-ccccccccccc2";
  const auto apply = [&](const std::string &cid, uint64_t rev, const std::string &type,
                         const std::string &payload, const std::string &targets="[]") {
    const std::string cmd="{\"schema_version\":\"1.1\",\"command_id\":\""+cid+
       "\",\"project_id\":\""+project+"\",\"expected_scene_revision\":\""+
       std::to_string(rev)+"\",\"event_sequence\":\""+std::to_string(rev+1U)+
       "\",\"targets\":"+targets+",\"user_intent\":\"retention test\",\"type\":\""+
       type+"\",\"payload\":"+payload+"}";
    const auto rc=hss_engine_apply_command_json(engine,
        {reinterpret_cast<const uint8_t*>(cmd.data()),cmd.size()});
    check(rc==HSS_OK, "accepted annotation lifecycle action");
    return cmd;
  };
  const auto first=apply(one,0U,"mark_unknown",R"({"reason":"quoted \"note\" \\ next\n غير مرئي"})");
  (void)first;
  apply(two,1U,"mark_unknown",R"({"reason":"second unknown note"})");
  apply("cccccccc-cccc-5ccc-8ccc-ccccccccccc3",2U,"undo",
        "{\"original_command_id\":\""+two+"\"}");
  check(occurrences(scene_json(engine),"HSS_UNKNOWN_REGION")==1U, "undo removes only second intent");
  apply("cccccccc-cccc-5ccc-8ccc-ccccccccccc4",3U,"redo",
        "{\"original_command_id\":\""+two+"\"}");
  const auto feature_id=derived_id(one,"unknown-feature");
  apply("cccccccc-cccc-5ccc-8ccc-ccccccccccc5",4U,"protect_feature",
        R"({"preserve_topology":true,"lock_position":false})", "[\""+feature_id+"\"]");
  const std::string before_source=scene_json(engine), before_result=result_json(engine);
  HSSSnapshot *held=nullptr;
  check(hss_engine_acquire_snapshot(engine,&held)==HSS_OK, "pin accepted annotation snapshot");
  HSSEpoch epoch{};epoch.abi_version=HSS_ABI_VERSION;epoch.struct_size=sizeof(epoch);
  epoch.capture_id=id(2U);epoch.event_sequence=8U;epoch.relationship_state=1U;
  epoch.transform_revision=1U;epoch.T_project_world=identity();
  check(hss_engine_set_epoch(engine,&epoch)==HSS_OK,"epoch after annotation");drain(engine,6U);
  const std::array<float,9> vertices{0,0,0,2,0,0,0,3,0};
  const std::array<uint32_t,3> triangles{0,1,2};
  HSSMeshEvent mesh{};mesh.abi_version=HSS_ABI_VERSION;mesh.struct_size=sizeof(mesh);
  mesh.capture_id=id(2U);mesh.anchor_id=id(3U);mesh.event_sequence=9U;mesh.anchor_revision=1U;
  mesh.T_world_anchor=identity();mesh.vertices_xyz=vertices.data();mesh.vertex_count=3U;
  mesh.triangle_indices=triangles.data();mesh.triangle_count=1U;
  check(hss_engine_submit_mesh_event(engine,&mesh)==HSS_OK,"mesh after annotations");drain(engine,7U);
  const auto floor=identity();
  check(hss_engine_set_plan_reference(engine,&floor,1.2)==HSS_OK,"plan reference retains intent");drain(engine,8U);
  const auto final=scene_json(engine);
  check(hss::scene_editing::scene_counts(final).has_value(),"retained scene validates all references/escaping");
  check(occurrences(final,"\"kind\":\"unknown\"")==2U &&
        occurrences(final,"\"code\":\"HSS_UNKNOWN_REGION\"")==2U,
        "repeated source rebuilds retain each intent exactly once");
  check(occurrences(final,"\"kind\":\"topology_protection\"")==1U &&
        final.find(feature_id)!=std::string::npos,
        "protect existing nonmetric annotation remains supported and retained");
  check(final.find("غير مرئي")!=std::string::npos &&
        final.find("second unknown note")!=std::string::npos,
        "escaped unicode reasons survive source serialization");
  for (const auto &original : {one, two}) {
    const auto at=final.find("\"id\":\""+derived_id(original,"unknown-issue")+"\"");
    const auto revision=final.find("\"scene_revision\":\"8\"",at);
    const auto severity=final.find("\"severity\":",at);
    check(at!=std::string::npos && revision!=std::string::npos && revision<severity,
          "retained issue revision matches newest immutable source snapshot");
  }
  check(result_json(engine)==before_result, "source revisions never rewrite exact result");
  mesh.kind=1U;mesh.anchor_revision=2U;mesh.event_sequence=10U;mesh.T_world_anchor.column_major[0]=2.0;
  check(hss_engine_submit_mesh_event(engine,&mesh)==HSS_INVALID_COORDINATE_FRAME,
        "invalid replacement rejected without altering retained annotations");
  check(scene_json(engine)==final && result_json(engine)==before_result,
        "rejected source keeps previous geometry, intent, exact result");
  uint64_t count=0U;
  check(hss_snapshot_copy_scene_json(held,nullptr,0U,&count)==HSS_BUFFER_TOO_SMALL,"pinned size");
  std::string pinned(count,'\0');
  check(hss_snapshot_copy_scene_json(held,reinterpret_cast<uint8_t*>(pinned.data()),
        pinned.size(),&count)==HSS_OK && pinned==before_source,"old immutable snapshot unchanged");
  hss_snapshot_release(held);hss_engine_destroy(engine);
}

} // namespace

int main() {
  publication_failure_is_atomic();
  retained_unknown_lifecycle();
  overlay_reservation_failure();
  retained_capacity_is_fail_closed();
  char temporary[] = "/tmp/hss-engine-edit-XXXXXX";
  check(mkdtemp(temporary) != nullptr, "temporary project root");
  const std::string root(temporary);

  HSSProjectStore *store = nullptr;
  auto store_create = store_config(root, HSS_PROJECT_STORE_CREATE_NEW);
  check(hss_project_store_create(&store_create, &store) == HSS_OK, "create store");
  HSSEngine *engine = nullptr;
  const auto config = engine_config();
  check(hss_engine_create(&config, &engine) == HSS_OK, "create engine");
  check(hss_engine_set_source_is_synthetic(engine, 1U) == HSS_OK,
        "set fixture provenance before source");

  // The command is constructed only after the captured baseline has supplied
  // its actual project identity; it never imports a fixture's unrelated IDs.
  const std::string provisional_project = project_id_from_scene(scene_json(engine));
  const std::string command = mark_unknown_command(provisional_project);
  std::string edited_scene;
  std::string exact_result;
  submit_baseline(engine, store, command, &edited_scene, &exact_result);

  // Byte-identical retry returns the recorded result and cannot publish a
  // second revision. Pending source work prevents applying an edit mid-build.
  const auto before_retry = scene_json(engine);
  check(hss_engine_apply_command_json(
            engine, {reinterpret_cast<const uint8_t *>(command.data()), command.size()}) == HSS_OK,
        "byte-identical command retry");
  check(result_json(engine) == exact_result && scene_json(engine) == before_retry,
        "retry preserves exact result and snapshot revision");

  // A fresh source revision must keep an accepted target-independent note.
  const std::array<float, 9U> replacement_vertices{0.F, 0.F, 0.F, 2.5F, 0.F, 0.F,
                                                 0.F, 3.5F, 0.F};
  const std::array<uint32_t, 3U> replacement_indices{0U, 1U, 2U};
  HSSMeshEvent replacement{};
  replacement.abi_version = HSS_ABI_VERSION;
  replacement.struct_size = sizeof(replacement);
  replacement.capture_id = id(2U);
  replacement.anchor_id = id(3U);
  replacement.event_sequence = 6U;
  replacement.anchor_revision = 2U;
  replacement.kind = 1U;
  replacement.T_world_anchor = identity();
  replacement.vertices_xyz = replacement_vertices.data();
  replacement.vertex_count = 3U;
  replacement.triangle_indices = replacement_indices.data();
  replacement.triangle_count = 1U;
  check(hss_engine_submit_mesh_event(engine, &replacement) == HSS_OK,
        "queue source replacement after accepted user command");
  drain(engine, 5U);
  const auto updated_vertices = mesh_vertices(engine);
  check(updated_vertices.size() == 3U && updated_vertices[1].x == 2.5 &&
            updated_vertices[2].y == 3.5, "new raw observations are not replaced by stale edited geometry");
  const auto refined_scene = scene_json(engine);
  check(refined_scene.find("HSS_UNKNOWN_REGION") != std::string::npos &&
            refined_scene.find("occluded rear face remains unknown") != std::string::npos,
        "accepted unknown intent survives source rebuild");
  check(refined_scene.find("\"kind\":\"unknown\"") != std::string::npos,
        "unknown feature survives without inventing measured geometry");
  check(occurrences(refined_scene, "\"kind\":\"unknown\"") == 1U &&
            occurrences(refined_scene, "HSS_UNKNOWN_REGION") == 1U,
        "source rebuild does not duplicate retained intent");
  check(hss::scene_editing::scene_counts(refined_scene).has_value(),
        "refined complete Scene is valid");
  check(result_json(engine) == exact_result, "source rebuild does not rewrite exact command result");
  check(has_typed_plan(engine), "fresh source rebuild produces its current typed plan");
  HSSProjectStoreReceipt replacement_receipt{};
  const auto replacement_context = context(6U, 5U);
  check(hss_project_store_append_mesh_event(store, &replacement_context,
              &replacement, &replacement_receipt) == HSS_OK, "persist source after command");
  edited_scene = refined_scene;

  HSSProjectCheckpointResult checkpoint{};
  check(hss_project_store_checkpoint(store, &checkpoint) == HSS_OK,
        "checkpoint command after captured baseline");
  hss_engine_destroy(engine);
  hss_project_store_destroy(store);

  auto store_open = store_config(root, HSS_PROJECT_STORE_OPEN_EXISTING);
  check(hss_project_store_create(&store_open, &store) == HSS_OK, "reopen store");
  check(hss_engine_create(&config, &engine) == HSS_OK, "create replay engine");
  HSSProjectReplayResult replay{};
  check(hss_project_store_replay(store, engine, &replay) == HSS_OK,
        "replay source baseline then command once");
  check(replay.applied_event_count == 6U && replay.engine_status == HSS_OK,
        "all durable source/control events replayed");
  check(scene_json(engine) == edited_scene && result_json(engine) == exact_result,
        "reopened scene and exact command result match accepted publication");
  check(has_typed_plan(engine),
        "replay preserves fresh source-rebuilt typed-plan state");

  hss_engine_destroy(engine);
  hss_project_store_destroy(store);
  std::filesystem::remove_all(root);
  std::cout << "live scene-edit adapter publishes immutable edits and replays committed commands\n";
}

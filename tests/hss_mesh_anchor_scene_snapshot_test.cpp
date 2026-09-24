#include "hss/mesh_anchor_scene_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace snap = hss::mesh_anchor_scene_snapshot;
namespace entities = hss::mesh_anchor_scene_entities;

static int assertions = 0;
static int failures = 0;
static std::string golden_json;
#define CHECK(x) do { ++assertions; if(!(x)){ ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; } } while(0)

static HSSMat4d identity() {
  HSSMat4d m{};
  m.column_major[0]=m.column_major[5]=m.column_major[10]=m.column_major[15]=1.0;
  return m;
}

static hss::coordinate::FloorFrame basis() {
  return {{0,0,0},{1,0,0},{0,0,-1},{0,1,0}};
}

static entities::Result entity_fixture() {
  entities::Result e;
  e.status=entities::Status::kOk;
  e.nodes_json="[{\"id\":\"11111111-1111-8111-8111-111111111111\",\"position_m\":[0,0,0],\"provenance\":{\"geometry_origin\":\"fitted\",\"semantic_origin\":\"unknown\",\"support_ids\":[\"aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa\"],\"uncertainty_m\":null,\"quality_state\":\"provisional\"},\"position_locked\":false}]";
  e.edges_json="[{\"id\":\"22222222-2222-8222-8222-222222222222\",\"start_node_id\":\"11111111-1111-8111-8111-111111111111\",\"end_node_id\":\"33333333-3333-8333-8333-333333333333\",\"surface_ids\":[\"44444444-4444-8444-8444-444444444444\"],\"parent_edge_ids\":[],\"protected_topology\":true,\"provenance\":{\"geometry_origin\":\"fitted\",\"semantic_origin\":\"unknown\",\"support_ids\":[\"aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa\"],\"uncertainty_m\":null,\"quality_state\":\"provisional\"}}]";
  e.surfaces_json="[{\"id\":\"44444444-4444-8444-8444-444444444444\",\"kind\":\"mesh_patch\",\"normal_unit\":null,\"offset_m\":null,\"boundary_node_ids\":[],\"mesh_asset\":null,\"fit_residual_m\":null,\"provenance\":{\"geometry_origin\":\"observed\",\"semantic_origin\":\"unknown\",\"support_ids\":[\"aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa\"],\"uncertainty_m\":null,\"quality_state\":\"provisional\"}}]";
  return e;
}

static snap::Input input_fixture() {
  snap::Input in;
  in.project_id="01234567-89ab-8cde-8123-456789abcdef";
  in.scene_revision=18446744073709551615ULL;
  in.source_is_synthetic=false;
  in.basis=basis();
  snap::EpochRecord a;
  a.capture_id="aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa";
  a.session_epoch=2;
  a.transform_revision=9;
  a.relationship_accepted=true;
  a.T_project_world=identity();
  snap::EpochRecord b;
  b.capture_id="bbbbbbbb-bbbb-8bbb-8bbb-bbbbbbbbbbbb";
  b.session_epoch=1;
  b.transform_revision=3;
  b.relationship_accepted=false;
  in.epochs={b,a};
  return in;
}

static void test_happy_and_epoch_permutations() {
  const auto entities_fixture=entity_fixture();
  const auto base=input_fixture();
  const auto expected=snap::assemble(entities_fixture,base);
  CHECK(expected.status==snap::Status::kOk);
  CHECK(!expected.scene_json.empty());
  CHECK(expected.emitted_json_bytes==expected.scene_json.size());
  CHECK(expected.scene_json.find("\"schema_version\":\"1.1\"")!=std::string::npos);
  CHECK(expected.scene_json.find("\"scene_revision\":\"18446744073709551615\"")!=std::string::npos);
  CHECK(expected.scene_json.find("\"source_is_synthetic\":false")!=std::string::npos);
  CHECK(expected.scene_json.find("\"quality_state\":\"provisional\"")!=std::string::npos);
  CHECK(expected.scene_json.find("\"features\":[]")!=std::string::npos);
  CHECK(expected.scene_json.find("\"issues\":[]")!=std::string::npos);
  CHECK(expected.scene_json.find("aaaaaaaa-aaaa-8aaa-8aaa-aaaaaaaaaaaa")<expected.scene_json.find("bbbbbbbb-bbbb-8bbb-8bbb-bbbbbbbbbbbb"));
  CHECK(expected.scene_json.find("\"T_project_world\":null")!=std::string::npos);
  golden_json = expected.scene_json;

  for(int mask=0;mask<32;++mask){
    auto in=base;
    if(mask&1) std::reverse(in.epochs.begin(),in.epochs.end());
    in.source_is_synthetic=(mask&2)!=0;
    auto result=snap::assemble(entities_fixture,in);
    CHECK(result.status==snap::Status::kOk);
    CHECK(result.emitted_json_bytes==result.scene_json.size());
    if(mask&2){
      CHECK(result.scene_json.find("\"source_is_synthetic\":true")!=std::string::npos);
      auto normalized=result.scene_json;
      auto p=normalized.find("\"source_is_synthetic\":true");
      normalized.replace(p,26,"\"source_is_synthetic\":false");
      CHECK(normalized==expected.scene_json);
    } else {
      CHECK(result.scene_json==expected.scene_json);
      CHECK(result.scene_json.find("\"source_is_synthetic\":false")!=std::string::npos);
    }
  }
}

static void test_fail_closed_contracts() {
  const auto e=entity_fixture();
  const auto good=input_fixture();

  for(auto s : {entities::Status::kGraphUnavailable,entities::Status::kSurfaceUnavailable,entities::Status::kInvalidPlan,entities::Status::kResourceLimit}){
    auto broken=e; broken.status=s; auto r=snap::assemble(broken,good);
    CHECK(r.status==snap::Status::kEntitiesUnavailable); CHECK(r.scene_json.empty()); CHECK(r.emitted_json_bytes==0);
  }

  for(const std::string bad : {"", "01234567-89AB-8cde-8123-456789abcdef", "not-a-uuid", "0123456789ab-8cde-8123-456789abcdef"}){
    auto in=good; in.project_id=bad; auto r=snap::assemble(e,in);
    CHECK(r.status==snap::Status::kInvalidArgument); CHECK(r.scene_json.empty());
  }

  { auto in=good; in.basis.u={2,0,0}; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidBasis); CHECK(r.scene_json.empty()); }
  { auto in=good; in.basis.v={0,0,1}; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidBasis); }
  { auto in=good; in.basis.origin_m.x=10000.0; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kOk); }
  { auto in=good; in.basis.origin_m.x=10000.0001; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidBasis); }
  { auto in=good; in.basis.origin_m.x=std::numeric_limits<double>::infinity(); auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidBasis); }

  { auto in=good; in.epochs[0].capture_id="BAD"; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }
  { auto in=good; in.epochs[0].transform_revision=0; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }
  { auto in=good; in.epochs[0].relationship_accepted=true; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }
  { auto in=good; in.epochs[1].relationship_accepted=false; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }
  { auto in=good; auto bad=*in.epochs[1].T_project_world; bad.column_major[0]=2; in.epochs[1].T_project_world=bad; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }
  { auto in=good; in.epochs.push_back(in.epochs.front()); in.epochs.back().transform_revision++; auto r=snap::assemble(e,in); CHECK(r.status==snap::Status::kInvalidEpoch); }

  { auto in=good; snap::Options o; o.max_epochs=1; auto r=snap::assemble(e,in,o); CHECK(r.status==snap::Status::kResourceLimit); CHECK(r.scene_json.empty()); }
  { auto in=good; snap::Options o; o.max_json_bytes=64; auto r=snap::assemble(e,in,o); CHECK(r.status==snap::Status::kResourceLimit); CHECK(r.scene_json.empty()); CHECK(r.emitted_json_bytes==0); }
  { auto in=good; snap::Options o; o.max_epochs=0; auto r=snap::assemble(e,in,o); CHECK(r.status==snap::Status::kInvalidArgument); }
  { auto in=good; snap::Options o; o.max_epochs=snap::kDefaultMaxEpochs+1; auto r=snap::assemble(e,in,o); CHECK(r.status==snap::Status::kInvalidArgument); }
  { auto in=good; snap::Options o; o.max_json_bytes=snap::kDefaultMaxJsonBytes+1; auto r=snap::assemble(e,in,o); CHECK(r.status==snap::Status::kInvalidArgument); }
}

static void test_empty_epoch_and_entity_arrays() {
  auto e=entities::Result{};
  auto in=input_fixture(); in.epochs.clear(); in.scene_revision=0; in.source_is_synthetic=true;
  auto r=snap::assemble(e,in);
  CHECK(r.status==snap::Status::kOk);
  CHECK(r.scene_json.find("\"scene_revision\":\"0\"")!=std::string::npos);
  CHECK(r.scene_json.find("\"epochs\":[]")!=std::string::npos);
  CHECK(r.scene_json.find("\"nodes\":[]")!=std::string::npos);
  CHECK(r.scene_json.find("\"edges\":[]")!=std::string::npos);
  CHECK(r.scene_json.find("\"surfaces\":[]")!=std::string::npos);
  CHECK(r.scene_json.find("\"source_is_synthetic\":true")!=std::string::npos);
}

int main(){
  test_happy_and_epoch_permutations();
  test_fail_closed_contracts();
  test_empty_epoch_and_entity_arrays();
  std::cout << "assertions=" << assertions << " failures=" << failures << "\n";
  std::cout << "scene_json=" << golden_json << "\n";
  return failures?1:0;
}

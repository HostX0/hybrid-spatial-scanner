cmake_minimum_required(VERSION 3.20)
project(HSSReference VERSION 1.1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HSS_ENABLE_SANITIZERS "Host address and undefined behavior sanitizers" OFF)
option(HSS_BUILD_TESTS "Build portable host tests" ON)
find_package(ZLIB REQUIRED)

# Portable captured-mesh reconstruction; physical/device acceptance remains separate.
add_library(SpatialCore STATIC
  SpatialCore/src/bounded_ingress.cpp
  SpatialCore/src/hss_engine.cpp
  SpatialCore/src/live_reconstruction.cpp
  SpatialCore/src/coordinate_basis.cpp
  SpatialCore/src/durable_source_store.cpp
  SpatialCore/src/interruption_recovery.cpp
  SpatialCore/src/resource_policy.cpp
  SpatialCore/src/normalized_replay.cpp
  SpatialCore/src/project_event_encoder.cpp
  SpatialCore/src/project_store.cpp
  SpatialCore/src/provisional_plan.cpp
  SpatialCore/src/project_import_validation.cpp
  SpatialCore/src/readiness.cpp
)
target_include_directories(SpatialCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(SpatialCore PUBLIC cxx_std_20)
target_compile_definitions(SpatialCore PRIVATE HSS_ENABLE_PLAN_ANNOTATION_RUNTIME=1)
target_compile_options(SpatialCore PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_library(hss_robust_plane_fit STATIC
  SpatialCore/src/robust_plane_fit.cpp
)
target_include_directories(hss_robust_plane_fit
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_robust_plane_fit PUBLIC cxx_std_20)
target_compile_options(hss_robust_plane_fit PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_robust_plane_fit PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_robust_plane_fit INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_boundary_graph STATIC
  SpatialCore/src/boundary_graph.cpp
)
target_include_directories(hss_boundary_graph
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_boundary_graph PUBLIC cxx_std_20)
target_compile_options(hss_boundary_graph PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_boundary_graph PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_boundary_graph INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_surface_separation STATIC
  SpatialCore/src/surface_separation.cpp
)
target_include_directories(hss_surface_separation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_surface_separation PUBLIC cxx_std_20)
target_compile_options(hss_surface_separation PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_surface_separation PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_surface_separation INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_surface_proposal STATIC
  SpatialCore/src/surface_proposal.cpp
)
target_include_directories(hss_surface_proposal
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_surface_proposal PUBLIC cxx_std_20)
target_compile_options(hss_surface_proposal PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_surface_proposal PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_surface_proposal INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_protected_rib STATIC
  SpatialCore/src/protected_rib.cpp
)
target_include_directories(hss_protected_rib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_protected_rib PUBLIC cxx_std_20)
target_compile_options(hss_protected_rib PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_protected_rib PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_protected_rib INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_ring_net_area STATIC
  SpatialCore/src/ring_net_area.cpp
)
target_include_directories(hss_ring_net_area
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_ring_net_area PUBLIC cxx_std_20)
target_compile_options(hss_ring_net_area PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_ring_net_area PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_ring_net_area INTERFACE -fsanitize=address,undefined)
endif()

add_library(hss_topology_protection STATIC
  SpatialCore/src/topology_protection.cpp
)
target_include_directories(hss_topology_protection
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_topology_protection PUBLIC cxx_std_20)
target_compile_options(hss_topology_protection PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_topology_protection PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_topology_protection INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_ceiling_overhead STATIC
  SpatialCore/src/ceiling_overhead.cpp
)
target_include_directories(hss_ceiling_overhead
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_ceiling_overhead PUBLIC cxx_std_20)
target_compile_options(hss_ceiling_overhead PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_ceiling_overhead PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hss_ceiling_overhead INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_plan_semantics STATIC
  SpatialCore/src/plan_semantics.cpp
)
target_include_directories(hss_plan_semantics
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_plan_semantics
  PUBLIC
    hss_surface_proposal
    hss_ring_net_area
    hss_ceiling_overhead
)
target_compile_features(hss_plan_semantics PUBLIC cxx_std_20)
target_compile_options(hss_plan_semantics PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_plan_semantics PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_plan_semantics INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_annotation_geometry STATIC
  SpatialCore/src/annotation_geometry.cpp
)
target_include_directories(hss_annotation_geometry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_annotation_geometry PUBLIC hss_plan_semantics)
target_compile_features(hss_annotation_geometry PUBLIC cxx_std_20)
target_compile_options(hss_annotation_geometry PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_annotation_geometry PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_annotation_geometry INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_pdf_svg_export STATIC
  SpatialCore/src/pdf_svg_export.cpp
)
target_include_directories(hss_pdf_svg_export
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_pdf_svg_export PUBLIC hss_annotation_geometry)
target_compile_features(hss_pdf_svg_export PUBLIC cxx_std_20)
target_compile_options(hss_pdf_svg_export PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_pdf_svg_export PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_pdf_svg_export INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_dxf_exporter STATIC
  SpatialCore/src/dxf_exporter.cpp
)
target_include_directories(hss_dxf_exporter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_dxf_exporter PUBLIC hss_annotation_geometry)
target_compile_features(hss_dxf_exporter PUBLIC cxx_std_20)
target_compile_options(hss_dxf_exporter PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_dxf_exporter PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dxf_exporter INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_revisioned_command STATIC
  SpatialCore/src/revisioned_command.cpp
)
target_include_directories(hss_revisioned_command
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_revisioned_command PUBLIC cxx_std_20)
target_compile_options(hss_revisioned_command PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_revisioned_command PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hss_revisioned_command INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_scene_editing STATIC
  SpatialCore/src/scene_editing.cpp
)
target_include_directories(hss_scene_editing
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_scene_editing PUBLIC hss_revisioned_command)
target_compile_features(hss_scene_editing PUBLIC cxx_std_20)
target_compile_options(hss_scene_editing PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_scene_editing PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_scene_editing INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_dynamic_provenance STATIC
  SpatialCore/src/dynamic_provenance.cpp
)
target_include_directories(hss_dynamic_provenance
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_dynamic_provenance PUBLIC hss_scene_editing)
target_compile_features(hss_dynamic_provenance PUBLIC cxx_std_20)
target_compile_options(hss_dynamic_provenance PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_dynamic_provenance PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dynamic_provenance INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_dynamic_evidence_verifier STATIC
  SpatialCore/src/dynamic_evidence_verifier.cpp
)
target_include_directories(hss_dynamic_evidence_verifier
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_dynamic_evidence_verifier PUBLIC hss_dynamic_provenance)
target_compile_features(hss_dynamic_evidence_verifier PUBLIC cxx_std_20)
target_compile_options(hss_dynamic_evidence_verifier PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_dynamic_evidence_verifier PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dynamic_evidence_verifier INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_appearance_isolation STATIC
  SpatialCore/src/appearance_isolation.cpp
)
target_include_directories(hss_appearance_isolation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_appearance_isolation PUBLIC cxx_std_20)
target_compile_options(hss_appearance_isolation PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_appearance_isolation PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_appearance_isolation INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_obj_mtl_exporter STATIC
  SpatialCore/src/obj_mtl_exporter.cpp
)
target_include_directories(hss_obj_mtl_exporter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_link_libraries(hss_obj_mtl_exporter PUBLIC hss_appearance_isolation)
target_compile_features(hss_obj_mtl_exporter PUBLIC cxx_std_20)
target_compile_options(hss_obj_mtl_exporter PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_obj_mtl_exporter PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_obj_mtl_exporter INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_guidance_policy STATIC
  SpatialCore/src/guidance_policy.cpp
)
target_include_directories(hss_guidance_policy
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_guidance_policy PUBLIC cxx_std_20)
target_compile_options(hss_guidance_policy PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_guidance_policy PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hss_guidance_policy INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_target_tools STATIC
  SpatialCore/src/target_tools.cpp
)
target_include_directories(hss_target_tools
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_target_tools PUBLIC cxx_std_20)
target_compile_options(hss_target_tools PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_target_tools PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_target_tools INTERFACE -fsanitize=address,undefined)
endif()
add_library(hss_privacy_policy STATIC
  SpatialCore/src/privacy_policy.cpp
)
target_include_directories(hss_privacy_policy
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(hss_privacy_policy PUBLIC cxx_std_20)
target_compile_options(hss_privacy_policy PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_privacy_policy PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_privacy_policy INTERFACE -fsanitize=address,undefined)
endif()

target_link_libraries(SpatialCore PUBLIC hss_robust_plane_fit hss_boundary_graph hss_topology_protection hss_ring_net_area hss_scene_editing hss_dynamic_provenance hss_annotation_geometry)

if(HSS_BUILD_TESTS)
add_executable(abi_smoke tests/abi_smoke.c)
target_include_directories(abi_smoke PRIVATE include)
target_link_libraries(abi_smoke PRIVATE SpatialCore)
add_executable(cpp_math_test tests/cpp_math_test.cpp)
target_include_directories(cpp_math_test PRIVATE include reference/cpp)
add_executable(hss_engine_contract_test tests/hss_engine_contract_test.cpp)
target_link_libraries(hss_engine_contract_test PRIVATE SpatialCore)
add_executable(hss_bounded_ingress_test tests/hss_bounded_ingress_test.cpp)
target_link_libraries(hss_bounded_ingress_test PRIVATE SpatialCore)
add_executable(hss_coordinate_basis_test tests/hss_coordinate_basis_test.cpp)
target_link_libraries(hss_coordinate_basis_test PRIVATE SpatialCore)
add_executable(hss_durable_source_store_test tests/hss_durable_source_store_test.cpp)
target_link_libraries(hss_durable_source_store_test PRIVATE SpatialCore)
add_executable(hss_normalized_replay_test tests/hss_normalized_replay_test.cpp)
target_link_libraries(hss_normalized_replay_test PRIVATE SpatialCore)
target_compile_definitions(hss_normalized_replay_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(hss_project_store_test
  tests/hss_project_store_test.cpp
  SpatialCore/src/coordinate_basis.cpp
  SpatialCore/src/durable_source_store.cpp
  SpatialCore/src/normalized_replay.cpp
  SpatialCore/src/project_event_encoder.cpp
  SpatialCore/src/project_store.cpp
)
target_include_directories(hss_project_store_test PRIVATE include SpatialCore/include)
add_executable(hss_provisional_plan_test tests/hss_provisional_plan_test.cpp)
target_link_libraries(hss_provisional_plan_test PRIVATE SpatialCore)
target_compile_definitions(hss_provisional_plan_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(hss_interruption_recovery_test tests/hss_interruption_recovery_test.cpp)
target_link_libraries(hss_interruption_recovery_test PRIVATE SpatialCore)
add_executable(hss_robust_plane_fit_test tests/hss_robust_plane_fit_test.cpp)
target_link_libraries(hss_robust_plane_fit_test PRIVATE hss_robust_plane_fit)
target_compile_options(hss_robust_plane_fit_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_boundary_graph_test tests/hss_boundary_graph_test.cpp)
target_link_libraries(hss_boundary_graph_test PRIVATE hss_boundary_graph)
target_compile_definitions(hss_boundary_graph_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_boundary_graph_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_surface_separation_test tests/hss_surface_separation_test.cpp)
target_link_libraries(hss_surface_separation_test PRIVATE hss_surface_separation)
target_compile_definitions(hss_surface_separation_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_surface_separation_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_surface_proposal_test tests/hss_surface_proposal_test.cpp)
target_link_libraries(hss_surface_proposal_test PRIVATE hss_surface_proposal)
target_compile_definitions(hss_surface_proposal_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_surface_proposal_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_protected_rib_test tests/hss_protected_rib_test.cpp)
target_link_libraries(hss_protected_rib_test PRIVATE hss_protected_rib)
target_compile_definitions(hss_protected_rib_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_protected_rib_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_ring_net_area_test tests/hss_ring_net_area_test.cpp)
target_link_libraries(hss_ring_net_area_test PRIVATE hss_ring_net_area)
target_compile_definitions(hss_ring_net_area_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_ring_net_area_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_topology_protection_test tests/hss_topology_protection_test.cpp)
target_link_libraries(hss_topology_protection_test PRIVATE hss_topology_protection)
target_compile_definitions(hss_topology_protection_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_topology_protection_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_revisioned_command_test tests/hss_revisioned_command_test.cpp)
target_link_libraries(hss_revisioned_command_test PRIVATE hss_revisioned_command)
target_compile_definitions(hss_revisioned_command_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_revisioned_command_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_scene_editing_test tests/hss_scene_editing_test.cpp)
target_link_libraries(hss_scene_editing_test PRIVATE hss_scene_editing)
target_compile_definitions(hss_scene_editing_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_scene_editing_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_dynamic_provenance_test tests/hss_dynamic_provenance_test.cpp)
target_link_libraries(hss_dynamic_provenance_test PRIVATE hss_dynamic_provenance)
target_compile_options(hss_dynamic_provenance_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_dynamic_evidence_verifier_test tests/hss_dynamic_evidence_verifier_test.cpp)
target_link_libraries(hss_dynamic_evidence_verifier_test PRIVATE hss_dynamic_evidence_verifier)
target_compile_options(hss_dynamic_evidence_verifier_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_appearance_isolation_test tests/hss_appearance_isolation_test.cpp)
target_link_libraries(hss_appearance_isolation_test PRIVATE hss_appearance_isolation)
target_compile_options(hss_appearance_isolation_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_obj_mtl_exporter_test tests/hss_obj_mtl_exporter_test.cpp)
target_link_libraries(hss_obj_mtl_exporter_test PRIVATE hss_obj_mtl_exporter)
target_compile_options(hss_obj_mtl_exporter_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_guidance_policy_test tests/hss_guidance_policy_test.cpp)
target_link_libraries(hss_guidance_policy_test PRIVATE hss_guidance_policy)
target_compile_options(hss_guidance_policy_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(hss_target_tools_test tests/hss_target_tools_test.cpp)
target_link_libraries(hss_target_tools_test PRIVATE hss_target_tools)
target_compile_options(hss_target_tools_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_robust_plane_fit_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_robust_plane_fit_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_boundary_graph_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_boundary_graph_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_surface_separation_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_surface_separation_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_surface_proposal_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_surface_proposal_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_protected_rib_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_protected_rib_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_ring_net_area_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_ring_net_area_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_topology_protection_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_topology_protection_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_privacy_policy_test tests/hss_privacy_policy_test.cpp)
target_link_libraries(hss_privacy_policy_test PRIVATE hss_privacy_policy)
target_compile_options(hss_privacy_policy_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_privacy_policy_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_privacy_policy_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_ceiling_overhead_test tests/hss_ceiling_overhead_test.cpp)
target_include_directories(hss_ceiling_overhead_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hss_ceiling_overhead_test PRIVATE hss_ceiling_overhead)
target_compile_options(hss_ceiling_overhead_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_ceiling_overhead_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_ceiling_overhead_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_plan_semantics_test tests/hss_plan_semantics_test.cpp)
target_link_libraries(hss_plan_semantics_test PRIVATE hss_plan_semantics)
target_compile_options(hss_plan_semantics_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_plan_semantics_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_plan_semantics_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_golden_suite_test tests/hss_golden_suite_test.cpp)
target_link_libraries(hss_golden_suite_test PRIVATE hss_plan_semantics hss_topology_protection hss_scene_editing)
target_compile_definitions(hss_golden_suite_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hss_golden_suite_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_golden_suite_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_golden_suite_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_annotation_geometry_test tests/hss_annotation_geometry_test.cpp)
target_link_libraries(hss_annotation_geometry_test PRIVATE hss_annotation_geometry)
target_compile_options(hss_annotation_geometry_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_annotation_geometry_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_annotation_geometry_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_pdf_svg_export_test tests/hss_pdf_svg_export_test.cpp)
target_link_libraries(hss_pdf_svg_export_test PRIVATE hss_pdf_svg_export)
target_compile_options(hss_pdf_svg_export_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_pdf_svg_export_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_pdf_svg_export_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_dxf_exporter_test tests/hss_dxf_exporter_test.cpp)
target_link_libraries(hss_dxf_exporter_test PRIVATE hss_dxf_exporter)
target_compile_options(hss_dxf_exporter_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_dxf_exporter_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dxf_exporter_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_cross_export_regression_test tests/hss_cross_export_regression_test.cpp)
target_link_libraries(hss_cross_export_regression_test PRIVATE
  hss_pdf_svg_export
  hss_dxf_exporter
  hss_obj_mtl_exporter
)
target_compile_options(hss_cross_export_regression_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_cross_export_regression_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_cross_export_regression_test PRIVATE -fsanitize=address,undefined)
endif()
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_revisioned_command_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_revisioned_command_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_scene_editing_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_scene_editing_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_dynamic_provenance_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dynamic_provenance_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_dynamic_evidence_verifier_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_dynamic_evidence_verifier_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_appearance_isolation_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_appearance_isolation_test PRIVATE -fsanitize=address,undefined)
 target_compile_options(hss_obj_mtl_exporter_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_obj_mtl_exporter_test PRIVATE -fsanitize=address,undefined)
endif()
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hss_guidance_policy_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hss_guidance_policy_test PRIVATE -fsanitize=address,undefined)
  target_compile_options(hss_target_tools_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hss_target_tools_test PRIVATE -fsanitize=address,undefined)
endif()
add_executable(hss_project_import_validation_test tests/hss_project_import_validation_test.cpp)
target_link_libraries(hss_project_import_validation_test PRIVATE SpatialCore)
add_executable(hss_native_project_import_test
  tests/hss_native_project_import_test.cpp
  HSSApp/HSSProjectArchiveImport.cpp
  HSSApp/HSSProjectArchiveExport.cpp
)
target_include_directories(hss_native_project_import_test PRIVATE HSSApp)
target_link_libraries(hss_native_project_import_test PRIVATE SpatialCore ZLIB::ZLIB)
add_executable(hss_resource_policy_test tests/hss_resource_policy_test.cpp)
target_link_libraries(hss_resource_policy_test PRIVATE SpatialCore)
foreach(target abi_smoke cpp_math_test hss_engine_contract_test hss_bounded_ingress_test hss_coordinate_basis_test hss_durable_source_store_test hss_normalized_replay_test hss_project_store_test hss_provisional_plan_test hss_interruption_recovery_test hss_project_import_validation_test hss_native_project_import_test hss_resource_policy_test)
 target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic)
 if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
 endif()
endforeach()
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(SpatialCore PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(SpatialCore INTERFACE -fsanitize=address,undefined)
endif()
enable_testing()
add_test(NAME abi_smoke COMMAND abi_smoke)
add_test(NAME cpp_math COMMAND cpp_math_test)
add_test(NAME hss_engine_contract COMMAND hss_engine_contract_test)
add_test(NAME hss_bounded_ingress COMMAND hss_bounded_ingress_test)
add_test(NAME hss_coordinate_basis COMMAND hss_coordinate_basis_test)
add_test(NAME hss_durable_source_store COMMAND hss_durable_source_store_test)
add_test(NAME hss_normalized_replay COMMAND hss_normalized_replay_test)
add_test(NAME hss_project_store COMMAND hss_project_store_test)
add_test(NAME hss_provisional_plan COMMAND hss_provisional_plan_test)
add_test(NAME hss_interruption_recovery COMMAND hss_interruption_recovery_test)
add_test(NAME hss_robust_plane_fit COMMAND hss_robust_plane_fit_test)
add_test(NAME hss_boundary_graph COMMAND hss_boundary_graph_test)
add_test(NAME hss_surface_separation COMMAND hss_surface_separation_test)
add_test(NAME hss_surface_proposal COMMAND hss_surface_proposal_test)
add_test(NAME hss_protected_rib COMMAND hss_protected_rib_test)
add_test(NAME hss_ring_net_area COMMAND hss_ring_net_area_test)
add_test(NAME hss_topology_protection COMMAND hss_topology_protection_test)
add_test(NAME hss_ceiling_overhead COMMAND hss_ceiling_overhead_test)
add_test(NAME hss_plan_semantics COMMAND hss_plan_semantics_test)
add_test(NAME hss_golden_suite COMMAND hss_golden_suite_test)
add_test(NAME hss_annotation_geometry COMMAND hss_annotation_geometry_test)
add_test(NAME hss_pdf_svg_export COMMAND hss_pdf_svg_export_test)
add_test(NAME hss_dxf_exporter COMMAND hss_dxf_exporter_test)
add_test(NAME hss_cross_export_regression COMMAND hss_cross_export_regression_test)
add_test(NAME hss_revisioned_command COMMAND hss_revisioned_command_test)
add_test(NAME hss_scene_editing COMMAND hss_scene_editing_test)
add_test(NAME hss_dynamic_provenance COMMAND hss_dynamic_provenance_test)
add_test(NAME hss_dynamic_evidence_verifier COMMAND hss_dynamic_evidence_verifier_test)
add_test(NAME hss_appearance_isolation COMMAND hss_appearance_isolation_test)
add_test(NAME hss_obj_mtl_exporter COMMAND hss_obj_mtl_exporter_test)
add_test(NAME hss_guidance_policy COMMAND hss_guidance_policy_test)
add_test(NAME hss_target_tools COMMAND hss_target_tools_test)
add_test(NAME hss_project_import_validation COMMAND hss_project_import_validation_test)
add_test(NAME hss_native_project_import COMMAND hss_native_project_import_test)
add_test(NAME hss_resource_policy COMMAND hss_resource_policy_test)
add_test(NAME hss_privacy_policy COMMAND hss_privacy_policy_test)
endif()

if(HSS_BUILD_TESTS)
add_executable(hss_live_reconstruction_test tests/hss_live_reconstruction_test.cpp)
target_link_libraries(hss_live_reconstruction_test PRIVATE SpatialCore)
target_compile_options(hss_live_reconstruction_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_live_reconstruction_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_live_reconstruction_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME hss_live_reconstruction COMMAND hss_live_reconstruction_test)
add_executable(hss_plan_annotation_runtime_test
  tests/hss_plan_annotation_runtime_test.cpp
)
target_link_libraries(hss_plan_annotation_runtime_test PRIVATE SpatialCore)
target_compile_options(hss_plan_annotation_runtime_test PRIVATE
  -Wall -Wextra -Werror -Wpedantic
)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_plan_annotation_runtime_test PRIVATE
   -fsanitize=address,undefined -fno-omit-frame-pointer
 )
 target_link_options(hss_plan_annotation_runtime_test PRIVATE
   -fsanitize=address,undefined
 )
endif()
add_test(NAME hss_plan_annotation_runtime COMMAND hss_plan_annotation_runtime_test)
add_executable(hss_engine_scene_editing_adapter_test tests/hss_engine_scene_editing_adapter_test.cpp)
target_link_libraries(hss_engine_scene_editing_adapter_test PRIVATE SpatialCore)
target_compile_options(hss_engine_scene_editing_adapter_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_engine_scene_editing_adapter_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
 target_link_options(hss_engine_scene_editing_adapter_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME hss_engine_scene_editing_adapter COMMAND hss_engine_scene_editing_adapter_test)
add_executable(hss_dynamic_runtime_integration_test
  tests/hss_dynamic_runtime_integration_test.cpp
)
target_link_libraries(hss_dynamic_runtime_integration_test PRIVATE SpatialCore)
target_compile_options(hss_dynamic_runtime_integration_test PRIVATE
  -Wall -Wextra -Werror -Wpedantic
)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_dynamic_runtime_integration_test PRIVATE
   -fsanitize=address,undefined -fno-omit-frame-pointer
 )
 target_link_options(hss_dynamic_runtime_integration_test PRIVATE
   -fsanitize=address,undefined
 )
endif()
add_test(NAME hss_dynamic_runtime_integration
  COMMAND hss_dynamic_runtime_integration_test
)
add_executable(hss_resource_runtime_integration_test
  tests/hss_resource_runtime_integration_test.cpp
)
target_link_libraries(hss_resource_runtime_integration_test PRIVATE SpatialCore)
target_compile_options(hss_resource_runtime_integration_test PRIVATE
  -Wall -Wextra -Werror -Wpedantic
)
if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
 target_compile_options(hss_resource_runtime_integration_test PRIVATE
   -fsanitize=address,undefined -fno-omit-frame-pointer
 )
 target_link_options(hss_resource_runtime_integration_test PRIVATE
   -fsanitize=address,undefined
 )
endif()
add_test(NAME hss_resource_runtime_integration
  COMMAND hss_resource_runtime_integration_test
)
endif()

# Deterministic mesh-pipeline host tests from the stacked reconstruction work.
#
# This module is intentionally included only by the reconciled current-main
# candidate. It keeps test registration separate from production runtime
# wiring, preserves the existing top-level targets, and makes CTest discovery
# explicit. A source that exists must compile; missing stack sources are skipped
# so this file can be reviewed independently without fabricating coverage.

if(NOT HSS_BUILD_TESTS)
  return()
endif()

function(hss_add_mesh_pipeline_test target test_name source)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    message(STATUS "HSS mesh pipeline test source not present: ${source}")
    return()
  endif()

  add_executable(${target} ${source})
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include
  )
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic)

  if(ARGC GREATER 3)
    target_link_libraries(${target} PRIVATE ${ARGN})
  endif()

  if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()

  add_test(NAME ${test_name} COMMAND ${target})
endfunction()

# Header-only component/grouping/binding contracts.
hss_add_mesh_pipeline_test(
  hss_mesh_patch_components_test
  hss_mesh_patch_components
  tests/hss_mesh_patch_components_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_patch_runtime_plan_test
  hss_mesh_patch_runtime_plan
  tests/hss_mesh_patch_runtime_plan_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_patch_surface_binding_test
  hss_mesh_patch_surface_binding
  tests/hss_mesh_patch_surface_binding_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_patch_plan_test
  hss_mesh_anchor_patch_plan
  tests/hss_mesh_anchor_patch_plan_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_event_payload_test
  hss_mesh_event_payload
  tests/hss_mesh_event_payload_test.cpp
)

# These exercise the repository's real coordinate-basis implementation.
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_project_batch_test
  hss_mesh_anchor_project_batch
  tests/hss_mesh_anchor_project_batch_test.cpp
  SpatialCore
)
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_reconstruction_plan_test
  hss_mesh_anchor_reconstruction_plan
  tests/hss_mesh_anchor_reconstruction_plan_test.cpp
  SpatialCore
)
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_cut_fragments_test
  hss_mesh_anchor_cut_fragments
  tests/hss_mesh_anchor_cut_fragments_test.cpp
  SpatialCore
)

# The surface-assembly regression deliberately supplies its own deterministic
# hss::plane_fit::fit stub to verify glue semantics and exact option forwarding.
# Do not link it to hss_robust_plane_fit/SpatialCore or duplicate the symbol.
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_surface_assembly_test
  hss_mesh_anchor_surface_assembly
  tests/hss_mesh_anchor_surface_assembly_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_scene_graph_test
  hss_mesh_anchor_scene_graph
  tests/hss_mesh_anchor_scene_graph_test.cpp
)
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_scene_entities_test
  hss_mesh_anchor_scene_entities
  tests/hss_mesh_anchor_scene_entities_test.cpp
)

# Snapshot validation calls the real coordinate-basis rigidity/floor checks.
hss_add_mesh_pipeline_test(
  hss_mesh_anchor_scene_snapshot_test
  hss_mesh_anchor_scene_snapshot
  tests/hss_mesh_anchor_scene_snapshot_test.cpp
  SpatialCore
)

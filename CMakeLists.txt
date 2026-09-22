cmake_minimum_required(VERSION 3.20)
project(HSSReference VERSION 1.1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HSS_ENABLE_SANITIZERS "Host address and undefined behavior sanitizers" OFF)
option(HSS_BUILD_TESTS "Build portable host tests" ON)

# This portable facade implements ABI ownership, status, synchronous-copy, and
# immutable-empty-snapshot contracts only. It does not implement a scanner.
add_library(SpatialCore STATIC
  SpatialCore/src/bounded_ingress.cpp
  SpatialCore/src/hss_engine.cpp
  SpatialCore/src/coordinate_basis.cpp
  SpatialCore/src/durable_source_store.cpp
  SpatialCore/src/normalized_replay.cpp
  SpatialCore/src/provisional_plan.cpp
  SpatialCore/src/readiness.cpp
)
target_include_directories(SpatialCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(SpatialCore PUBLIC cxx_std_20)
target_compile_options(SpatialCore PRIVATE -Wall -Wextra -Werror -Wpedantic)

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
add_executable(hss_provisional_plan_test tests/hss_provisional_plan_test.cpp)
target_link_libraries(hss_provisional_plan_test PRIVATE SpatialCore)
target_compile_definitions(hss_provisional_plan_test PRIVATE HSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
foreach(target abi_smoke cpp_math_test hss_engine_contract_test hss_bounded_ingress_test hss_coordinate_basis_test hss_durable_source_store_test hss_normalized_replay_test hss_provisional_plan_test)
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
add_test(NAME hss_provisional_plan COMMAND hss_provisional_plan_test)
endif()

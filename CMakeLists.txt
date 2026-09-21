cmake_minimum_required(VERSION 3.20)
project(HSSReference VERSION 1.1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(HSS_ENABLE_SANITIZERS "Host address and undefined behavior sanitizers" OFF)
option(HSS_BUILD_TESTS "Build portable host tests" ON)

# This is intentionally only the portable foundation. The ABI declared in
# include/hss/hss_api.h is implemented by B-009, after its ownership and
# lifecycle tests exist. Keeping the small readiness unit separate lets the
# iOS app link the same C++20 library without claiming scanner behavior.
add_library(SpatialCore STATIC
  SpatialCore/src/readiness.cpp
)
target_include_directories(SpatialCore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SpatialCore/include>
)
target_compile_features(SpatialCore PUBLIC cxx_std_20)
target_compile_options(SpatialCore PRIVATE -Wall -Wextra -Werror -Wpedantic)

if(HSS_BUILD_TESTS)
add_executable(abi_smoke tests/abi_smoke.c)
target_include_directories(abi_smoke PRIVATE include)
add_executable(cpp_math_test tests/cpp_math_test.cpp)
target_include_directories(cpp_math_test PRIVATE include reference/cpp)
foreach(target abi_smoke cpp_math_test)
 target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic)
 if(HSS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
 endif()
endforeach()
enable_testing()
add_test(NAME abi_smoke COMMAND abi_smoke)
add_test(NAME cpp_math COMMAND cpp_math_test)
endif()

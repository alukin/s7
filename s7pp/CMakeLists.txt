# s7/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(s7_project LANGUAGES CXX) # Give your overall project a name

enable_testing()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No need for CMAKE_POSITION_INDEPENDENT_CODE here,
# as it will be set in the s7core/CMakeLists.txt for the library itself.

# Add s7core as a subdirectory. Its CMakeLists.txt will be processed.
# This makes the 's7core' target (library) available for linking later.

# --- nlohmann/json (header-only, included via target_include_directories in s7core) ---
# The submodule lives at external/json; no add_subdirectory needed.

# --- rsocket-cpp (vendored submodule) ---
# rsocket uses generic BUILD_* cache flags in its own CMakeLists.
set(BUILD_TESTS OFF CACHE BOOL "Disable rsocket tests in superbuild" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable rsocket examples in superbuild" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "Disable rsocket benchmarks in superbuild" FORCE)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(external/rsocket)

add_subdirectory(s7core)
add_subdirectory(s7io)

# Add s7main as a subdirectory. Its CMakeLists.txt will be processed.
add_subdirectory(s7main)
add_subdirectory(s7ctl)
add_subdirectory(s7tests)

# Optional: Set a default target if you want 'cmake --build .' to build something specific
# For example, to build the s7main executable by default:
add_custom_target(all_targets ALL DEPENDS s7main)

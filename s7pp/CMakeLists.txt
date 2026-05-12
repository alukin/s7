# s7/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(s7_project LANGUAGES CXX) # Give your overall project a name

enable_testing()

option(S7_BUILD_UI "Build the Qt-based s7ui application" ON)

if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost 1.75 REQUIRED COMPONENTS system json)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No need for CMAKE_POSITION_INDEPENDENT_CODE here,
# as it will be set in the s7core/CMakeLists.txt for the library itself.

# Add s7core as a subdirectory. Its CMakeLists.txt will be processed.
# This makes the 's7core' target (library) available for linking later.

add_subdirectory(s7core)
add_subdirectory(s7io)

# Add s7main as a subdirectory. Its CMakeLists.txt will be processed.
add_subdirectory(s7main)
add_subdirectory(s7ctl)
add_subdirectory(s7tests)

if (S7_BUILD_UI)
  add_subdirectory(s7ui)
endif()

# Optional: Set a default target if you want 'cmake --build .' to build something specific
# For example, to build the s7main executable by default:
add_custom_target(all_targets ALL DEPENDS s7main)

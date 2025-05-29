# s7/CMakeLists.txt
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(s7_project LANGUAGES CXX) # Give your overall project a name

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No need for CMAKE_POSITION_INDEPENDENT_CODE here,
# as it will be set in the s7core/CMakeLists.txt for the library itself.

# Add s7core as a subdirectory. Its CMakeLists.txt will be processed.
# This makes the 's7core' target (library) available for linking later.
add_subdirectory(s7core)

# Add s7main as a subdirectory. Its CMakeLists.txt will be processed.
add_subdirectory(s7main)

# Optional: Set a default target if you want 'cmake --build .' to build something specific
# For example, to build the s7main executable by default:
add_custom_target(all_targets ALL DEPENDS s7main)

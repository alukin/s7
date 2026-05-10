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

# Help fresh build directories discover a previously bootstrapped folly install.
option(S7_AUTO_CONFIGURE_RSOCKET_DEPS "Auto-detect rsocket/folly dependency paths" ON)
set(
  S7_FALLBACK_FOLLY_INSTALL_DIR
  "${CMAKE_SOURCE_DIR}/build_verify/folly-install"
  CACHE PATH
  "Fallback path to a prebuilt folly install"
)

if (S7_AUTO_CONFIGURE_RSOCKET_DEPS)
  set(_s7_folly_config "${S7_FALLBACK_FOLLY_INSTALL_DIR}/lib/cmake/folly/folly-config.cmake")

  if (NOT DEFINED FOLLY_INSTALL_DIR AND EXISTS "${_s7_folly_config}")
	set(FOLLY_INSTALL_DIR "${S7_FALLBACK_FOLLY_INSTALL_DIR}" CACHE PATH "folly install directory for rsocket" FORCE)
  endif ()

  if (EXISTS "${_s7_folly_config}")
	list(FIND CMAKE_PREFIX_PATH "${S7_FALLBACK_FOLLY_INSTALL_DIR}" _s7_folly_prefix_index)
	if (_s7_folly_prefix_index EQUAL -1)
	  list(APPEND CMAKE_PREFIX_PATH "${S7_FALLBACK_FOLLY_INSTALL_DIR}")
	  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "CMake package search path" FORCE)
	endif ()
  elseif (NOT DEFINED RSOCKET_INSTALL_DEPS)
	# If no prebuilt folly is available, ask rsocket to bootstrap its deps.
	set(RSOCKET_INSTALL_DEPS ON CACHE BOOL "Build rsocket third-party dependencies" FORCE)
  endif ()
endif ()

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

cmake_minimum_required(VERSION 3.20)
project(s7l2 VERSION 0.1.0 LANGUAGES CXX)

if (POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

# C++26 on GCC 15+ or Clang 22+ with libstdc++: the type system needs 16- and
# 128-bit floats and __int128 (s7core/include/s7/float_types.hpp; libc++ has
# no <stdfloat>). A Windows port would use MinGW-w64 (GCC).
if (NOT ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
		OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22)))
	message(FATAL_ERROR "S7 requires GCC 15+ or Clang 22+ (found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}).")
endif()
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# GNU mode: libstdc++ treats __int128 as an integer type (traits, numeric_limits)
# only with GNU extensions enabled.
set(CMAKE_CXX_EXTENSIONS ON)

function(s7_enable_project_warnings target_name)
	if (NOT TARGET ${target_name})
		return()
	endif()

	if (MSVC)
		target_compile_options(${target_name} PRIVATE /W4)
	else()
		target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
		if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(${target_name} PRIVATE -Wno-implicit-const-int-float-conversion)
		endif()
	endif()
endfunction()

function(s7_suppress_target_warnings target_name)
	if (NOT TARGET ${target_name})
		return()
	endif()

	if (MSVC)
		target_compile_options(${target_name} PRIVATE /W0)
	else()
		target_compile_options(${target_name} PRIVATE -w)
	endif()
endfunction()

option(S7_BUILD_STUDIO "Build the s7studio runtime client" ON)

find_package(Boost 1.75 REQUIRED COMPONENTS json OPTIONAL_COMPONENTS system)
if (NOT TARGET Boost::system)
	# Boost >= 1.89 makes Boost.System header-only and drops its CMake component.
	add_library(Boost::system INTERFACE IMPORTED)
	target_link_libraries(Boost::system INTERFACE Boost::headers)
endif()
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
if (S7_BUILD_STUDIO)
	find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()

enable_testing()

if (S7_BUILD_STUDIO)
	add_subdirectory(external/QSchematic)
endif()
add_subdirectory(s7core)
add_subdirectory(s7io)
add_subdirectory(s7transport)
add_subdirectory(s7net)
add_subdirectory(s7observe)
add_subdirectory(s7noise)
add_subdirectory(s7runtime)
add_subdirectory(s7ctl)
add_subdirectory(s7tests)

if (S7_BUILD_STUDIO)
	add_subdirectory(s7studio)
endif()

# Suppress diagnostics for vendored/external code that is built as part of this tree.
s7_suppress_target_warnings(qschematic-static)
s7_suppress_target_warnings(qschematic-shared)
s7_suppress_target_warnings(gpds-static)
s7_suppress_target_warnings(gpds-shared)
s7_suppress_target_warnings(s7_qcustomplot)

# Keep warnings enabled for first-party targets.
s7_enable_project_warnings(s7core)
s7_enable_project_warnings(s7io)
s7_enable_project_warnings(s7transport)
s7_enable_project_warnings(s7net)
s7_enable_project_warnings(s7observe)
s7_enable_project_warnings(s7core_smoke)
s7_enable_project_warnings(s7core_signal_types_smoke)
s7_enable_project_warnings(s7observe_smoke)
s7_enable_project_warnings(s7net_smoke)
s7_enable_project_warnings(s7noise)
s7_enable_project_warnings(s7runtime)
s7_enable_project_warnings(s7ctl)
s7_enable_project_warnings(s7io_smoke)
s7_enable_project_warnings(s7runtime_control_smoke)
s7_enable_project_warnings(s7studio)
s7_enable_project_warnings(s7spectral)

set(S7_ALL_TARGETS
	s7core
	s7io
	s7transport
	s7net
	s7observe
	s7core_smoke
	s7observe_smoke
	s7net_smoke
	s7noise
	s7runtime
	s7ctl
	s7io_smoke
	s7runtime_control_smoke
)

if (TARGET s7spectral)
	list(APPEND S7_ALL_TARGETS s7spectral)
endif()

if (S7_BUILD_STUDIO)
	list(APPEND S7_ALL_TARGETS s7studio)
endif()

add_custom_target(all_targets ALL DEPENDS ${S7_ALL_TARGETS})

# Step-rate benchmark of a 100-stage chain (not a test).
add_executable(s7_benchmark_chain examples/benchmark_read_input.cpp)
target_link_libraries(s7_benchmark_chain PRIVATE s7core)

cmake_minimum_required(VERSION 3.20)
project(s7l2 LANGUAGES CXX)

if (POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

find_package(Boost 1.75 REQUIRED COMPONENTS json system)
find_package(Threads REQUIRED)
if (S7_BUILD_STUDIO)
	find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
endif()

enable_testing()

add_subdirectory(external/QSchematic)
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
s7_enable_project_warnings(s7noise)
s7_enable_project_warnings(s7runtime)
s7_enable_project_warnings(s7ctl)
s7_enable_project_warnings(s7tests)
s7_enable_project_warnings(s7io_smoke)
s7_enable_project_warnings(s7transport_smoke)
s7_enable_project_warnings(s7studio)
s7_enable_project_warnings(s7spectral)

set(S7_ALL_TARGETS
	s7core
	s7io
	s7transport
	s7net
	s7observe
	s7noise
	s7runtime
	s7ctl
	s7tests
	s7io_smoke
	s7transport_smoke
)

if (TARGET s7spectral)
	list(APPEND S7_ALL_TARGETS s7spectral)
endif()

if (S7_BUILD_STUDIO)
	list(APPEND S7_ALL_TARGETS s7studio)
endif()

add_custom_target(all_targets ALL DEPENDS ${S7_ALL_TARGETS})

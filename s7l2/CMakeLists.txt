cmake_minimum_required(VERSION 3.20)
project(s7l2 LANGUAGES CXX)

if (POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
add_subdirectory(s7net)
add_subdirectory(s7observe)
add_subdirectory(s7runtime)
add_subdirectory(s7ctl)
add_subdirectory(s7tests)

if (S7_BUILD_STUDIO)
	add_subdirectory(s7studio)
endif()

set(S7_ALL_TARGETS s7runtime s7ctl s7tests)

if (S7_BUILD_STUDIO)
	list(APPEND S7_ALL_TARGETS s7studio)
endif()

add_custom_target(all_targets ALL DEPENDS ${S7_ALL_TARGETS})

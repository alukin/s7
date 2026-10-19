cmake_minimum_required(VERSION 3.20)
project(s7l2 VERSION 0.1.0 LANGUAGES CXX)

if (POLICY CMP0167)
	cmake_policy(SET CMP0167 NEW)
endif()

# C++26 on GCC 15+ or Clang 22+ with libstdc++: the type system needs 16- and
# 128-bit floats and __int128 (s7core/include/s7/float_types.hpp; libc++ has
# no <stdfloat>). Windows builds use Clang with MinGW-w64 (UCRT) and libstdc++,
# cross-compiled on Linux: cmake/toolchains/x86_64-w64-mingw32-clang.cmake.
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

if (WIN32)
	# One Boost.JSON for the host and every plugin DLL (the static one would be
	# re-exported by libs7core.dll and collide with the executables' copy).
	set(Boost_USE_STATIC_LIBS OFF)
endif()
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
	if (CMAKE_CROSSCOMPILING)
		# The target Qt's moc/uic/rcc are Windows programs: generate code with
		# the host Qt's (QT_HOST_PATH, from the toolchain file).
		foreach (tool moc uic rcc)
			find_program(S7_QT_HOST_${tool} ${tool} REQUIRED NO_DEFAULT_PATH
				PATHS ${QT_HOST_PATH}/lib/qt6/libexec ${QT_HOST_PATH}/libexec ${QT_HOST_PATH}/bin)
			get_target_property(configs Qt6::${tool} IMPORTED_CONFIGURATIONS)
			foreach (config IN LISTS configs)
				set_target_properties(Qt6::${tool} PROPERTIES IMPORTED_LOCATION_${config} ${S7_QT_HOST_${tool}})
			endforeach()
			set_target_properties(Qt6::${tool} PROPERTIES IMPORTED_LOCATION ${S7_QT_HOST_${tool}})
		endforeach()
	endif()
endif()

enable_testing()

if (WIN32)
	# Windows 10 APIs for Asio.
	add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
	if (CMAKE_CROSSCOMPILING)
		# Tests run under Wine, through a script generated below.
		set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_BINARY_DIR}/wine-run.sh)
	endif()
endif()
# What the shell-script tests need to build and run the target's programs.
set(S7_SCRIPT_TEST_ENV "")
if (CMAKE_CROSSCOMPILING)
	get_filename_component(S7_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
	set(S7_SCRIPT_TEST_ENV
		"S7_RUN=${CMAKE_CROSSCOMPILING_EMULATOR}"
		"S7_EXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
		"S7_TOOLCHAIN_FILE=${S7_TOOLCHAIN_FILE}"
		"PKG_CONFIG_LIBDIR=$ENV{PKG_CONFIG_LIBDIR}")
endif()

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

if (WIN32 AND CMAKE_CROSSCOMPILING)
	# WINEPATH: the directories of this build's DLLs, then the sysroot's.
	set(S7_WINE_DLL_PATH "$<TARGET_FILE_DIR:s7core>;$<TARGET_FILE_DIR:s7noise>;${CMAKE_SYSROOT}/bin")
	file(READ ${PROJECT_SOURCE_DIR}/tools/windows/wine-run.sh.in S7_WINE_RUN)
	string(REPLACE "@S7_WINE_DLL_PATH@" "${S7_WINE_DLL_PATH}" S7_WINE_RUN "${S7_WINE_RUN}")
	string(REPLACE "@S7_WINE_PREFIX@" "${CMAKE_BINARY_DIR}/wine-prefix" S7_WINE_RUN "${S7_WINE_RUN}")
	file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/wine-run.sh CONTENT "${S7_WINE_RUN}"
		FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

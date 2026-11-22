# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(s7l2 VERSION 0.1.2 LANGUAGES CXX)

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

# Give selected real-time DSP sources optimization in standard development builds.
set(S7_REALTIME_COMPILE_OPTIONS "$<$<OR:$<CONFIG:Debug>,$<STREQUAL:$<CONFIG>,>>:-O2>")

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
set(S7_BUILD_SDR_DEFAULT ON)
if(WIN32)
    set(S7_BUILD_SDR_DEFAULT OFF)
endif()
option(S7_BUILD_SDR "Build SDR elements, providers and tools" ${S7_BUILD_SDR_DEFAULT})
if(NOT S7_BUILD_SDR)
    if(S7_SOAPYSDR STREQUAL "ON")
        message(FATAL_ERROR "S7_SOAPYSDR=ON requires S7_BUILD_SDR=ON")
    endif()
    set(S7_SOAPYSDR OFF CACHE STRING "SoapySDR provider (disabled with SDR elements)" FORCE)
endif()
option(S7_NATIVE_PACKAGES "Configure distribution-native Linux RPM/DEB packages" OFF)
include(GNUInstallDirs)
# Keep the standalone SDK installation complete in ordinary development builds.
# Native packages separate versioned libraries from development symlinks/headers.
set(S7_CORE_INSTALL_COMPONENT s7sdk)
set(S7_HARDWARE_INSTALL_COMPONENT s7sdk)
if(S7_NATIVE_PACKAGES)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Native packages require a native Linux build")
    endif()
    set(S7_CORE_INSTALL_COMPONENT s7libs)
    set(S7_HARDWARE_INSTALL_COMPONENT s7hardware)
endif()
add_compile_definitions(S7_INSTALL_PLUGIN_SUBDIR="${CMAKE_INSTALL_LIBDIR}/s7/plugins")

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

# Test binaries remain individually buildable, but never join the default build.
# CTest only runs binaries; build this aggregate target before running the suite.
add_custom_target(build-tests)
function(s7_add_test_executable target_name)
	add_executable(${target_name} EXCLUDE_FROM_ALL ${ARGN})
	# Numerical/document regressions use assert as well as Qt Test. Keep their
	# checks effective in Release/RelWithDebInfo CI configurations.
	if(MSVC)
		target_compile_options(${target_name} PRIVATE /UNDEBUG)
	else()
		target_compile_options(${target_name} PRIVATE -UNDEBUG)
	endif()
	add_dependencies(build-tests ${target_name})
endfunction()

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
	# Pull in only libraries needed by Studio during a normal build.
	add_subdirectory(external/QSchematic EXCLUDE_FROM_ALL)
	add_dependencies(build-tests qschematic-tests qschematic-wiresystem-tests)
endif()
add_subdirectory(s7core)
add_subdirectory(s7hardware)
add_subdirectory(s7audio)
add_subdirectory(s7io)
if(S7_BUILD_SDR)
    add_subdirectory(s7radio)
    add_subdirectory(s7sdr)
    target_link_libraries(s7audio_plugin PRIVATE s7sdr)
    target_compile_definitions(s7audio_plugin PRIVATE S7_WITH_SDR=1)
endif()
add_executable(s7hw s7hardware/tools/s7hw.cpp)
target_link_libraries(s7hw PRIVATE s7audio s7io)
if(S7_BUILD_SDR)
    target_link_libraries(s7hw PRIVATE s7sdr s7radio)
    target_compile_definitions(s7hw PRIVATE S7_WITH_SDR=1)
endif()
s7_enable_project_warnings(s7hw)
set_target_properties(s7hw PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS s7hw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT s7hardware)
# Laboratory tool: never built/run by normal CTest or packaged by default.
if(S7_BUILD_SDR)
    add_executable(s7hardware_probe EXCLUDE_FROM_ALL s7hardware/tests/live_probe.cpp)
    target_link_libraries(s7hardware_probe PRIVATE s7audio s7sdr)
    target_compile_options(s7hardware_probe PRIVATE -O2)
    s7_enable_project_warnings(s7hardware_probe)
endif()
set(S7_HARDWARE_EXAMPLES audio_capture audio_playback)
if(WIN32)
    set(S7_HARDWARE_EXAMPLES portaudio_capture portaudio_playback)
endif()
if(S7_BUILD_SDR)
    add_test(NAME s7hardware_offline_sdr_receive COMMAND s7hw --check ${PROJECT_SOURCE_DIR}/examples/sdr/sdr_receive.json)
    add_test(NAME s7hardware_offline_fm_receiver COMMAND s7hw --check ${PROJECT_SOURCE_DIR}/examples/sdr/fm_receiver.s7diagram)
endif()
foreach(example IN LISTS S7_HARDWARE_EXAMPLES)
    add_test(NAME s7hardware_offline_${example} COMMAND s7hw --check ${PROJECT_SOURCE_DIR}/examples/hardware/${example}.json)
endforeach()
s7_add_test_executable(s7hardware_plugin_smoke s7hardware/tests/plugin_smoke.cpp s7runtime/src/plugin_loader.cpp)
target_include_directories(s7hardware_plugin_smoke PRIVATE s7runtime/include)
target_link_libraries(s7hardware_plugin_smoke PRIVATE s7hardware)
add_dependencies(s7hardware_plugin_smoke s7audio_plugin)
set(S7_HARDWARE_TEST_PLUGINS $<TARGET_FILE:s7audio_plugin>)
if(S7_BUILD_SDR)
    add_dependencies(s7hardware_plugin_smoke s7sdr_plugin)
    list(APPEND S7_HARDWARE_TEST_PLUGINS $<TARGET_FILE:s7sdr_plugin>)
endif()
add_test(NAME s7hardware_plugin_smoke COMMAND s7hardware_plugin_smoke ${S7_HARDWARE_TEST_PLUGINS})
get_property(S7_TEST_PORTAUDIO GLOBAL PROPERTY S7_HAVE_PORTAUDIO)
if(WIN32 AND CMAKE_CROSSCOMPILING)
    # The standalone build-directory plugin needs its external DLL beside it,
    # just as an independently distributed plugin does with the native loader.
    add_custom_target(s7hardware_plugin_test_dependencies
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:s7hardware>"
            "$<TARGET_FILE_DIR:s7audio_plugin>/$<TARGET_FILE_NAME:s7hardware>")
    if(S7_TEST_PORTAUDIO)
        add_custom_command(TARGET s7hardware_plugin_test_dependencies POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SYSROOT}/bin/libportaudio.dll"
                "$<TARGET_FILE_DIR:s7audio_plugin>/libportaudio.dll")
    endif()
    add_dependencies(s7hardware_plugin_test_dependencies s7hardware s7audio_plugin)
    add_dependencies(s7hardware_plugin_smoke s7hardware_plugin_test_dependencies)
endif()
if((UNIX AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING) OR (WIN32 AND CMAKE_CROSSCOMPILING))
    add_test(NAME s7hardware_sdk_smoke COMMAND ${CMAKE_COMMAND}
        -DS7_BUILD=${CMAKE_BINARY_DIR} -DS7_SOURCE=${PROJECT_SOURCE_DIR}
        -DS7_TEST_ROOT=${CMAKE_BINARY_DIR}/s7hardware-sdk-test -DS7_CXX=${CMAKE_CXX_COMPILER}
        -DS7_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DS7_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}
        -P ${PROJECT_SOURCE_DIR}/s7hardware/tests/sdk_smoke.cmake)
endif()

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
	s7hardware
	s7audio
	s7audio_plugin
	s7hw
	s7io
	s7transport
	s7net
	s7observe
	s7noise
	s7runtime
	s7ctl
)
if(S7_BUILD_SDR)
    list(APPEND S7_ALL_TARGETS s7sdr s7sdr_plugin s7radio_plugin)
endif()

if (TARGET s7spectral)
	list(APPEND S7_ALL_TARGETS s7spectral)
endif()

if (S7_BUILD_STUDIO)
	list(APPEND S7_ALL_TARGETS s7studio)
endif()

add_custom_target(all_targets ALL DEPENDS ${S7_ALL_TARGETS})
# Some tests run the production programs directly rather than dedicated binaries.
add_dependencies(build-tests ${S7_ALL_TARGETS})

if (WIN32 AND CMAKE_CROSSCOMPILING)
	# WINEPATH: the directories of this build's DLLs, then the sysroot's.
	set(S7_WINE_DLL_PATH "$<TARGET_FILE_DIR:s7core>;$<TARGET_FILE_DIR:s7noise>;$<TARGET_FILE_DIR:s7hardware>;$<TARGET_FILE_DIR:s7audio>")
    if(S7_BUILD_SDR)
        string(APPEND S7_WINE_DLL_PATH ";$<TARGET_FILE_DIR:s7sdr>")
    endif()
    string(APPEND S7_WINE_DLL_PATH ";${CMAKE_SYSROOT}/bin")
	file(READ ${PROJECT_SOURCE_DIR}/tools/windows/wine-run.sh.in S7_WINE_RUN)
	string(REPLACE "@S7_WINE_DLL_PATH@" "${S7_WINE_DLL_PATH}" S7_WINE_RUN "${S7_WINE_RUN}")
	string(REPLACE "@S7_WINE_PREFIX@" "${CMAKE_BINARY_DIR}/wine-prefix" S7_WINE_RUN "${S7_WINE_RUN}")
	file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/wine-run.sh CONTENT "${S7_WINE_RUN}"
		FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

include(cmake/LinuxInstall.cmake)
if(S7_NATIVE_PACKAGES)
    include(cmake/NativePackages.cmake)
endif()

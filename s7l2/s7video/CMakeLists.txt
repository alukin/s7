# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

# Opt-in, headless codec backend. Build against an installed S7 SDK.
cmake_minimum_required(VERSION 3.20)
project(s7video VERSION 0.1.0 LANGUAGES CXX)

find_package(s7core 0.1 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec>=61 libavutil>=59 libswscale>=8)

add_library(s7video STATIC src/mjpeg.cpp)
set_target_properties(s7video PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(s7video PUBLIC include)
target_link_libraries(s7video PUBLIC s7::s7core PRIVATE PkgConfig::FFMPEG)
target_compile_options(s7video PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(s7video_mjpeg_smoke tests/mjpeg_smoke.cpp)
    target_link_libraries(s7video_mjpeg_smoke PRIVATE s7video)
    target_compile_options(s7video_mjpeg_smoke PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME s7video_mjpeg_smoke COMMAND s7video_mjpeg_smoke)
endif()

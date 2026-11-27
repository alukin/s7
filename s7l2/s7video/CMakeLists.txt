# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

# Opt-in, headless codec backend. Build against an installed S7 SDK.
cmake_minimum_required(VERSION 3.20)
project(s7video VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

find_package(s7core 0.1 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec>=61 libavutil>=59 libswscale>=8)

add_library(s7video STATIC src/mjpeg.cpp)
set_target_properties(s7video PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(s7video PUBLIC include)
target_link_libraries(s7video PUBLIC s7::s7core PRIVATE PkgConfig::FFMPEG)
target_compile_options(s7video PRIVATE -Wall -Wextra -Wpedantic)

add_library(s7video_elements MODULE src/plugin_video.cpp)
set_target_properties(s7video_elements PROPERTIES PREFIX "lib" OUTPUT_NAME "s7video-elements")
target_link_libraries(s7video_elements PRIVATE s7video)
target_compile_options(s7video_elements PRIVATE -Wall -Wextra -Wpedantic)
s7_embed_metadata(s7video_elements resources/element_metadata.json video_metadata)
install(TARGETS s7video_elements LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/s7/plugins)

add_executable(s7video_record src/record.cpp ../s7runtime/src/plugin_loader.cpp)
target_include_directories(s7video_record PRIVATE ../s7runtime/include)
target_link_libraries(s7video_record PRIVATE s7::s7core ${CMAKE_DL_LIBS})
target_compile_options(s7video_record PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(s7video_record PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
install(TARGETS s7video_record RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES examples/schema_mjpeg_roundtrip.json examples/schema_mjpeg_replay.json
    examples/mjpeg_replay.s7diagram
    DESTINATION ${CMAKE_INSTALL_DATADIR}/s7/examples/video)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(s7video_mjpeg_smoke tests/mjpeg_smoke.cpp)
    target_link_libraries(s7video_mjpeg_smoke PRIVATE s7video)
    target_compile_options(s7video_mjpeg_smoke PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME s7video_mjpeg_smoke COMMAND s7video_mjpeg_smoke)
    add_executable(s7video_graph_smoke tests/graph_smoke.cpp ../s7runtime/src/plugin_loader.cpp)
    target_include_directories(s7video_graph_smoke PRIVATE ../s7runtime/include)
    target_link_libraries(s7video_graph_smoke PRIVATE s7::s7core ${CMAKE_DL_LIBS})
    target_compile_options(s7video_graph_smoke PRIVATE -Wall -Wextra -Wpedantic)
    add_dependencies(s7video_graph_smoke s7video_elements)
    add_test(NAME s7video_graph_smoke COMMAND s7video_graph_smoke
        $<TARGET_FILE:s7video_elements> ${CMAKE_CURRENT_SOURCE_DIR}/examples/schema_mjpeg_roundtrip.json)
    add_test(NAME s7video_file_smoke COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/file_smoke.py
        $<TARGET_FILE:s7video_record> $<TARGET_FILE:s7video_graph_smoke> $<TARGET_FILE:s7video_elements>
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/schema_mjpeg_roundtrip.json
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/schema_mjpeg_replay.json)
endif()

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(s7transport STATIC
    src/transport.cpp
    src/wire_format.cpp
    src/stream_protocol.cpp
    src/clock_sync.cpp
    src/frame_transport.cpp
)

target_include_directories(s7transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7transport PUBLIC
    s7core
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ZLIB::ZLIB
)

# Optional independent block codecs. Zstd uses static workspaces from its
# advanced API, which upstream permits only with static linkage.
option(S7_FRAME_LZ4 "Enable LZ4 sample-frame compression" ON)
option(S7_FRAME_ZSTD "Enable Zstandard sample-frame compression" ON)
if (S7_FRAME_LZ4)
    find_path(S7_LZ4_INCLUDE_DIR lz4.h)
    find_library(S7_LZ4_LIBRARY NAMES lz4)
    if (S7_LZ4_INCLUDE_DIR AND S7_LZ4_LIBRARY)
        target_include_directories(s7transport PRIVATE ${S7_LZ4_INCLUDE_DIR})
        target_link_libraries(s7transport PRIVATE ${S7_LZ4_LIBRARY})
        target_compile_definitions(s7transport PRIVATE S7_FRAMES_LZ4)
    else()
        message(STATUS "LZ4 frame compression unavailable (development library missing)")
    endif()
endif()
if (S7_FRAME_ZSTD)
    find_path(S7_ZSTD_INCLUDE_DIR zstd.h)
    find_library(S7_ZSTD_STATIC_LIBRARY NAMES libzstd.a zstd_static)
    if (S7_ZSTD_INCLUDE_DIR AND S7_ZSTD_STATIC_LIBRARY)
        target_include_directories(s7transport PRIVATE ${S7_ZSTD_INCLUDE_DIR})
        target_link_libraries(s7transport PRIVATE ${S7_ZSTD_STATIC_LIBRARY})
        target_compile_definitions(s7transport PRIVATE S7_FRAMES_ZSTD)
    else()
        message(STATUS "Zstandard frame compression unavailable (static development library missing)")
    endif()
endif()

s7_add_test_executable(s7transport_frame_smoke tests/frame_smoke.cpp)
target_link_libraries(s7transport_frame_smoke PRIVATE s7transport)
s7_enable_project_warnings(s7transport_frame_smoke)
add_test(NAME s7transport_frame_smoke COMMAND s7transport_frame_smoke)
s7_add_test_executable(s7transport_frame_interop tests/frame_interop.cpp)
target_link_libraries(s7transport_frame_interop PRIVATE s7transport)
s7_enable_project_warnings(s7transport_frame_interop)
s7_add_test_executable(s7transport_frame_socket_smoke tests/frame_socket_smoke.cpp)
target_link_libraries(s7transport_frame_socket_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_frame_socket_smoke PRIVATE S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs")
s7_enable_project_warnings(s7transport_frame_socket_smoke)
add_test(NAME s7transport_frame_socket_smoke COMMAND s7transport_frame_socket_smoke)
set_tests_properties(s7transport_frame_socket_smoke PROPERTIES TIMEOUT 45)

# Opt-in capacity baseline; excluded from normal builds and correctness CTest.
add_executable(s7transport_audio_benchmark EXCLUDE_FROM_ALL benchmarks/audio_frames.cpp)
target_link_libraries(s7transport_audio_benchmark PRIVATE s7transport)
s7_enable_project_warnings(s7transport_audio_benchmark)
add_executable(s7transport_sample_benchmark EXCLUDE_FROM_ALL benchmarks/sample_stream.cpp)
target_link_libraries(s7transport_sample_benchmark PRIVATE s7transport s7net)
target_compile_definitions(s7transport_sample_benchmark PRIVATE S7_BENCH_BUILD_TYPE="$<CONFIG>")
s7_enable_project_warnings(s7transport_sample_benchmark)
if (WIN32)
    target_link_libraries(s7transport PUBLIC ws2_32 mswsock)
endif()

s7_add_test_executable(s7transport_wire_format_smoke tests/wire_format_smoke.cpp)
target_link_libraries(s7transport_wire_format_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_wire_format_smoke PRIVATE
    S7_WIRE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json")

s7_add_test_executable(s7transport_stream_protocol_smoke tests/stream_protocol_smoke.cpp)
target_link_libraries(s7transport_stream_protocol_smoke PRIVATE s7transport)

add_test(NAME s7transport_stream_protocol_smoke COMMAND s7transport_stream_protocol_smoke)
add_test(NAME s7transport_wire_format_smoke COMMAND s7transport_wire_format_smoke)

# The shared wire fixtures: a second, Python decoder passes all of them, and
# they are what the generator writes.
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME s7transport_frame_fixtures
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_frame_fixtures.py
            --bridge ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:s7transport_frame_interop>)
    add_custom_target(s7transport_sample_benchmark_check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sample_benchmark_check.py
            --executable $<TARGET_FILE:s7transport_sample_benchmark>
            --cert-dir ${PROJECT_SOURCE_DIR}/s7runtime/examples/certs
            --launcher ${CMAKE_CROSSCOMPILING_EMULATOR}
        DEPENDS s7transport_sample_benchmark
        VERBATIM)
    add_test(NAME s7transport_wire_fixtures_python
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_wire_fixtures.py ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json)
    add_test(NAME s7transport_wire_fixtures_current
        COMMAND sh -c "${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_wire_fixtures.py \"$1\" > /dev/null && cmp \"$1\" ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json"
            sh ${CMAKE_CURRENT_BINARY_DIR}/wire_v1.generated.json)
endif()


s7_add_test_executable(s7transport_beast_stream_smoke tests/beast_stream_smoke.cpp)
target_link_libraries(s7transport_beast_stream_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_beast_stream_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs")
add_test(NAME s7transport_beast_stream_smoke COMMAND s7transport_beast_stream_smoke)

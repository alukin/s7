# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(s7io STATIC
    src/io.cpp
)

target_include_directories(s7io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7io PUBLIC s7core)

s7_add_test_executable(s7io_smoke
    tests/io_smoke.cpp
)

target_link_libraries(s7io_smoke PRIVATE s7io)

add_test(NAME s7io_smoke COMMAND s7io_smoke)


s7_add_test_executable(s7io_replay_smoke tests/replay_smoke.cpp)
target_link_libraries(s7io_replay_smoke PRIVATE s7io)
add_test(NAME s7io_replay_smoke COMMAND s7io_replay_smoke)

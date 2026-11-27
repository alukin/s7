# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(s7observe STATIC
    src/observe.cpp
    src/array_mailbox.cpp
)

target_include_directories(s7observe PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7observe PUBLIC s7core)

s7_add_test_executable(s7observe_smoke
    tests/observe_smoke.cpp
)

target_link_libraries(s7observe_smoke PRIVATE s7observe)

add_test(NAME s7observe_smoke COMMAND s7observe_smoke)

s7_add_test_executable(s7observe_array_mailbox_smoke tests/array_mailbox_smoke.cpp)
target_link_libraries(s7observe_array_mailbox_smoke PRIVATE s7observe Threads::Threads)
add_test(NAME s7observe_array_mailbox_smoke COMMAND s7observe_array_mailbox_smoke)

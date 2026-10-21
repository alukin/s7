# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(s7noise)



# s7noise library
add_library(s7noise SHARED
    src/noise_elements.cpp
    src/plugin_noise.cpp
)

target_include_directories(s7noise PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(s7noise PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7noise PRIVATE
    s7core
)

set_target_properties(s7noise PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "include/s7noise/noise_elements.hpp"
)

# Installation
install(TARGETS s7noise
    LIBRARY DESTINATION lib/s7/plugins
    PUBLIC_HEADER DESTINATION include/s7noise
)

install(FILES include/s7noise/noise_elements.hpp
    DESTINATION include/s7noise
)


s7_add_test_executable(s7noise_smoke tests/noise_smoke.cpp)
target_link_libraries(s7noise_smoke PRIVATE s7noise s7core)
add_test(NAME s7noise_smoke COMMAND s7noise_smoke)

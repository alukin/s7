# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later
add_library(s7radio STATIC src/radio.cpp)
set_target_properties(s7radio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(s7radio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(s7radio PUBLIC s7core)
s7_enable_project_warnings(s7radio)

add_library(s7radio_plugin MODULE src/plugin.cpp)
target_link_libraries(s7radio_plugin PRIVATE s7radio)
set_target_properties(s7radio_plugin PROPERTIES OUTPUT_NAME s7radio-elements INSTALL_RPATH "$ORIGIN/../..")
install(TARGETS s7radio_plugin LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/s7/plugins COMPONENT s7hardware)

s7_add_test_executable(s7radio_modulation_smoke tests/modulation_smoke.cpp)
target_link_libraries(s7radio_modulation_smoke PRIVATE s7radio)
s7_enable_project_warnings(s7radio_modulation_smoke)
add_test(NAME s7radio_modulation_smoke COMMAND s7radio_modulation_smoke)

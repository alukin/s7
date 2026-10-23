# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

add_executable(s7ctl
    src/main.cpp
    ${PROJECT_SOURCE_DIR}/s7runtime/src/plugin_loader.cpp
)

target_include_directories(s7ctl PRIVATE ${PROJECT_SOURCE_DIR}/s7runtime/include)
target_link_libraries(s7ctl PRIVATE s7net s7io ${CMAKE_DL_LIBS})

add_test(NAME s7ctl_smoke COMMAND s7ctl help)

# Stream types of a diagram with a plugin element, inferred offline.
add_test(NAME s7ctl_plugin_types
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugin_types_test.sh $<TARGET_FILE:s7ctl> $<TARGET_FILE:s7_test_plugin>)

# Two s7runtime nodes over mTLS, deployed with s7ctl.
add_test(NAME s7_two_node_deployment
    COMMAND ${PROJECT_SOURCE_DIR}/tools/test_two_node_deployment.sh ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})
set_tests_properties(s7ctl_plugin_types s7_two_node_deployment PROPERTIES ENVIRONMENT "${S7_SCRIPT_TEST_ENV}")
foreach(data_security IN ITEMS mtls trusted_plaintext)
    add_test(NAME s7_two_node_split_${data_security}
        COMMAND ${PROJECT_SOURCE_DIR}/tools/test_two_node_deployment.sh ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR} ${data_security})
    set_tests_properties(s7_two_node_split_${data_security} PROPERTIES ENVIRONMENT "${S7_SCRIPT_TEST_ENV}")
endforeach()

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

# The G2 two-node audio equalizer on this host (tools/run_g2_audio_demo.py):
# every recorded sample checked against independent references and against
# the same graph run in one process. Real-time paced: about a minute.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME s7_g2_audio_equalizer_local
        COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/run_g2_audio_demo.py local
                --runtime $<TARGET_FILE:s7runtime> --ctl $<TARGET_FILE:s7ctl>)
    set_tests_properties(s7_g2_audio_equalizer_local PROPERTIES TIMEOUT 300 LABELS "integration")
    # The saved example matches its generator (tools/generate_g2_audio_example.py).
    add_test(NAME s7_g2_audio_example_current
        COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/generate_g2_audio_example.py --check)
endif()

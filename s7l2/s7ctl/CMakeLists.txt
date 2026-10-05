add_executable(s7ctl
    src/main.cpp
)

target_link_libraries(s7ctl PRIVATE s7net)

add_test(NAME s7ctl_smoke COMMAND s7ctl help)

# Two s7runtime nodes over mTLS, deployed with s7ctl.
add_test(NAME s7_two_node_deployment
    COMMAND ${PROJECT_SOURCE_DIR}/tools/test_two_node_deployment.sh ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})

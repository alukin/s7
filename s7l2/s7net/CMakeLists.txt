
add_library(s7net STATIC
    src/net.cpp
    src/diagram_deployment.cpp
)

target_include_directories(s7net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7net PUBLIC s7core s7io s7transport)

add_executable(s7net_smoke
    tests/net_smoke.cpp
)

target_link_libraries(s7net_smoke PRIVATE s7net)

add_test(NAME s7net_smoke COMMAND s7net_smoke)


add_executable(s7net_diagram_deployment_smoke tests/diagram_deployment_smoke.cpp)
target_link_libraries(s7net_diagram_deployment_smoke PRIVATE s7net)
add_test(NAME s7net_diagram_deployment_smoke COMMAND s7net_diagram_deployment_smoke)

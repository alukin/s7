# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(s7net STATIC
    src/net.cpp
    src/diagram_deployment.cpp
    src/canonical_json.cpp
    src/deployment_client.cpp
    src/base64url.cpp
)

target_include_directories(s7net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7net PUBLIC s7core s7io s7transport PRIVATE OpenSSL::Crypto)

s7_add_test_executable(s7net_smoke
    tests/net_smoke.cpp
)

target_link_libraries(s7net_smoke PRIVATE s7net)

add_test(NAME s7net_smoke COMMAND s7net_smoke)


s7_add_test_executable(s7net_diagram_deployment_smoke tests/diagram_deployment_smoke.cpp)
target_link_libraries(s7net_diagram_deployment_smoke PRIVATE s7net)
target_compile_definitions(s7net_diagram_deployment_smoke PRIVATE
    S7_WIRE_FIXTURES="${PROJECT_SOURCE_DIR}/s7transport/tests/fixtures/wire_v1.json")
add_test(NAME s7net_diagram_deployment_smoke COMMAND s7net_diagram_deployment_smoke)

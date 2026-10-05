
add_executable(s7runtime
    src/main.cpp
    src/canonical_json.cpp
    src/runtime_service.cpp
    src/control_plane_mvp.cpp
    src/plugin_loader.cpp
)

target_include_directories(s7runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7io/include
)

target_compile_definitions(s7runtime PRIVATE
    S7RUNTIME_DEFAULT_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/schema_minimal.json"
)

target_link_libraries(s7runtime PRIVATE
    s7core
    s7net
    s7transport
    s7io
    Boost::json
    Boost::system
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

add_executable(s7runtime_control_smoke
    tests/control_plane_smoke.cpp
    src/canonical_json.cpp
    src/control_plane_mvp.cpp
    src/runtime_service.cpp
)

target_include_directories(s7runtime_control_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7io/include
    ${PROJECT_SOURCE_DIR}/s7transport/include
)

target_link_libraries(s7runtime_control_smoke PRIVATE
    s7core
    s7transport
    s7net
    s7io
    Boost::json
    Boost::system
    Threads::Threads
)

target_compile_definitions(s7runtime_control_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs"
)

add_test(NAME s7runtime_control_smoke COMMAND s7runtime_control_smoke)

add_executable(s7runtime_deployment_smoke
    tests/deployment_smoke.cpp
    src/canonical_json.cpp
    src/control_plane_mvp.cpp
    src/runtime_service.cpp
)

target_include_directories(s7runtime_deployment_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7io/include
    ${PROJECT_SOURCE_DIR}/s7transport/include
)

target_link_libraries(s7runtime_deployment_smoke PRIVATE
    s7core
    s7transport
    s7net
    s7io
    Boost::json
    Boost::system
    Threads::Threads
)

target_compile_definitions(s7runtime_deployment_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs"
)

add_test(NAME s7runtime_deployment_smoke COMMAND s7runtime_deployment_smoke)


# Third-party plugin path: build s7spectral against the installed SDK and load it.
add_executable(s7runtime_plugin_sdk_smoke
    tests/plugin_sdk_smoke.cpp
    src/plugin_loader.cpp
)
target_include_directories(s7runtime_plugin_sdk_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7runtime_plugin_sdk_smoke PRIVATE s7core Boost::json ${CMAKE_DL_LIBS})
add_test(NAME s7spectral_sdk_smoke
    COMMAND ${PROJECT_SOURCE_DIR}/tools/test_third_party_plugin.sh
        ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}
        $<TARGET_FILE:s7runtime_plugin_sdk_smoke> ${CMAKE_CURRENT_BINARY_DIR}/plugin_sdk_test
)
set_tests_properties(s7spectral_sdk_smoke PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}"
)

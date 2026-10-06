
add_executable(s7runtime
    src/main.cpp
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

# The control protocol conformance script, run against the control plane.
add_executable(s7runtime_control_fixtures_smoke tests/control_fixtures_smoke.cpp src/control_plane_mvp.cpp)
target_include_directories(s7runtime_control_fixtures_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7runtime_control_fixtures_smoke PRIVATE s7core s7transport s7net Boost::json Threads::Threads)
target_compile_definitions(s7runtime_control_fixtures_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs"
    S7_CONTROL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/control_v1.json")
add_test(NAME s7runtime_control_fixtures_smoke COMMAND s7runtime_control_fixtures_smoke)
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME s7runtime_control_fixtures_current
        COMMAND sh -c "${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_control_fixtures.py \"$1\" > /dev/null && cmp \"$1\" ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/control_v1.json"
            sh ${CMAKE_CURRENT_BINARY_DIR}/control_v1.generated.json)
endif()


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

# Transactional plugin registration.
add_library(s7_test_plugin MODULE tests/test_plugin.cpp)
target_link_libraries(s7_test_plugin PRIVATE s7core)
add_library(s7_test_plugin_failing MODULE tests/test_plugin.cpp)
target_link_libraries(s7_test_plugin_failing PRIVATE s7core)
target_compile_definitions(s7_test_plugin_failing PRIVATE S7_TEST_PLUGIN_FAIL)
add_executable(s7runtime_plugin_loader_smoke
    tests/plugin_loader_smoke.cpp
    src/plugin_loader.cpp
)
target_include_directories(s7runtime_plugin_loader_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7runtime_plugin_loader_smoke PRIVATE s7core Boost::json ${CMAKE_DL_LIBS})
target_compile_definitions(s7runtime_plugin_loader_smoke PRIVATE
    S7_TEST_PLUGIN="$<TARGET_FILE:s7_test_plugin>"
    S7_FAILING_PLUGIN="$<TARGET_FILE:s7_test_plugin_failing>")
add_dependencies(s7runtime_plugin_loader_smoke s7_test_plugin s7_test_plugin_failing)
add_test(NAME s7runtime_plugin_loader_smoke COMMAND s7runtime_plugin_loader_smoke)

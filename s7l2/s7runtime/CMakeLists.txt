set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7runtime
    src/main.cpp
    src/runtime_service.cpp
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

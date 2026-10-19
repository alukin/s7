
add_library(s7transport STATIC
    src/transport.cpp
    src/wire_format.cpp
    src/stream_protocol.cpp
    src/clock_sync.cpp
)

target_include_directories(s7transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7transport PUBLIC
    s7core
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
if (WIN32)
    target_link_libraries(s7transport PUBLIC ws2_32 mswsock)
endif()

s7_add_test_executable(s7transport_wire_format_smoke tests/wire_format_smoke.cpp)
target_link_libraries(s7transport_wire_format_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_wire_format_smoke PRIVATE
    S7_WIRE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json")

s7_add_test_executable(s7transport_stream_protocol_smoke tests/stream_protocol_smoke.cpp)
target_link_libraries(s7transport_stream_protocol_smoke PRIVATE s7transport)

add_test(NAME s7transport_stream_protocol_smoke COMMAND s7transport_stream_protocol_smoke)
add_test(NAME s7transport_wire_format_smoke COMMAND s7transport_wire_format_smoke)

# The shared wire fixtures: a second, Python decoder passes all of them, and
# they are what the generator writes.
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME s7transport_wire_fixtures_python
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_wire_fixtures.py ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json)
    add_test(NAME s7transport_wire_fixtures_current
        COMMAND sh -c "${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_wire_fixtures.py \"$1\" > /dev/null && cmp \"$1\" ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json"
            sh ${CMAKE_CURRENT_BINARY_DIR}/wire_v1.generated.json)
endif()


s7_add_test_executable(s7transport_beast_stream_smoke tests/beast_stream_smoke.cpp)
target_link_libraries(s7transport_beast_stream_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_beast_stream_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs")
add_test(NAME s7transport_beast_stream_smoke COMMAND s7transport_beast_stream_smoke)

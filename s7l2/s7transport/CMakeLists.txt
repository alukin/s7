
add_library(s7transport STATIC
    src/transport.cpp
    src/wire_format.cpp
    src/stream_protocol.cpp
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

add_executable(s7transport_wire_format_smoke tests/wire_format_smoke.cpp)
target_link_libraries(s7transport_wire_format_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_wire_format_smoke PRIVATE
    S7_WIRE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json")

add_executable(s7transport_stream_protocol_smoke tests/stream_protocol_smoke.cpp)
target_link_libraries(s7transport_stream_protocol_smoke PRIVATE s7transport)

add_test(NAME s7transport_stream_protocol_smoke COMMAND s7transport_stream_protocol_smoke)
add_test(NAME s7transport_wire_format_smoke COMMAND s7transport_wire_format_smoke)


add_executable(s7transport_beast_stream_smoke tests/beast_stream_smoke.cpp)
target_link_libraries(s7transport_beast_stream_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_beast_stream_smoke PRIVATE
    S7_TEST_CERT_DIR="${PROJECT_SOURCE_DIR}/s7runtime/examples/certs")
add_test(NAME s7transport_beast_stream_smoke COMMAND s7transport_beast_stream_smoke)

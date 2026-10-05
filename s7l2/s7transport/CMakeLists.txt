
add_library(s7transport STATIC
    src/transport.cpp
    src/wire_format.cpp
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

add_executable(s7transport_smoke
    tests/transport_smoke.cpp
)

target_link_libraries(s7transport_smoke PRIVATE s7transport)

add_executable(s7transport_reconnect_smoke
    tests/transport_reconnect_smoke.cpp
)

target_link_libraries(s7transport_reconnect_smoke PRIVATE s7transport)

add_executable(s7transport_validation_smoke
    tests/transport_validation_smoke.cpp
)

target_link_libraries(s7transport_validation_smoke PRIVATE s7transport)

add_executable(s7transport_multipeer_smoke
    tests/transport_multipeer_smoke.cpp
)

target_link_libraries(s7transport_multipeer_smoke PRIVATE s7transport)

add_executable(s7transport_wire_format_smoke tests/wire_format_smoke.cpp)
target_link_libraries(s7transport_wire_format_smoke PRIVATE s7transport)
target_compile_definitions(s7transport_wire_format_smoke PRIVATE
    S7_WIRE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wire_v1.json")

add_test(NAME s7transport_smoke COMMAND s7transport_smoke)
add_test(NAME s7transport_wire_format_smoke COMMAND s7transport_wire_format_smoke)
add_test(NAME s7transport_multipeer_smoke COMMAND s7transport_multipeer_smoke)
add_test(NAME s7transport_validation_smoke COMMAND s7transport_validation_smoke)
add_test(NAME s7transport_reconnect_smoke COMMAND s7transport_reconnect_smoke)


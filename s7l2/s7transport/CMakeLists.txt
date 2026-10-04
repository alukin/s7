set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(s7transport STATIC
    src/transport.cpp
)

target_include_directories(s7transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7transport PUBLIC
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

add_test(NAME s7transport_smoke COMMAND s7transport_smoke)
add_test(NAME s7transport_validation_smoke COMMAND s7transport_validation_smoke)
add_test(NAME s7transport_reconnect_smoke COMMAND s7transport_reconnect_smoke)


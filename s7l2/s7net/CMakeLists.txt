set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(s7net STATIC
    src/net.cpp
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


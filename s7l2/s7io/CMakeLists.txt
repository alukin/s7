
add_library(s7io STATIC
    src/io.cpp
)

target_include_directories(s7io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7io PUBLIC s7core)

add_executable(s7io_smoke
    tests/io_smoke.cpp
)

target_link_libraries(s7io_smoke PRIVATE s7io)

add_test(NAME s7io_smoke COMMAND s7io_smoke)


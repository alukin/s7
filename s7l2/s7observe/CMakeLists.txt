
add_library(s7observe STATIC
    src/observe.cpp
)

target_include_directories(s7observe PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7observe PUBLIC s7core)

s7_add_test_executable(s7observe_smoke
    tests/observe_smoke.cpp
)

target_link_libraries(s7observe_smoke PRIVATE s7observe)

add_test(NAME s7observe_smoke COMMAND s7observe_smoke)


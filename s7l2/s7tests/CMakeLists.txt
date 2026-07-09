set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7tests
    src/validation_smoke.cpp
)

target_link_libraries(s7tests PRIVATE s7observe)

add_executable(s7io_smoke
    src/io_smoke.cpp
)

target_link_libraries(s7io_smoke PRIVATE s7io)

add_test(NAME s7runtime_smoke COMMAND s7runtime)
add_test(NAME s7tests_smoke COMMAND s7tests)
add_test(NAME s7io_smoke COMMAND s7io_smoke)

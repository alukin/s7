set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7tests
    src/validation_smoke.cpp
)

target_link_libraries(s7tests PRIVATE s7observe)

add_test(NAME s7runtime_smoke COMMAND s7runtime)
add_test(NAME s7tests_smoke COMMAND s7tests)


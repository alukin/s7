# s7/s7tests/CMakeLists.txt

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7tests
    src/test_dsp_control.cpp
)

target_link_libraries(s7tests PRIVATE s7core s7io)

add_test(NAME s7tests_dsp_control COMMAND s7tests)


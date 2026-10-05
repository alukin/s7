
add_executable(s7ctl
    src/main.cpp
)

target_link_libraries(s7ctl PRIVATE s7observe s7io s7net)

add_test(NAME s7ctl_smoke COMMAND s7ctl)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(s7studio
    src/main.cpp
)

target_include_directories(s7studio PRIVATE
    ${PROJECT_SOURCE_DIR}/s7core/include
    ${PROJECT_SOURCE_DIR}/s7io/include
    ${PROJECT_SOURCE_DIR}/s7net/include
    ${PROJECT_SOURCE_DIR}/s7observe/include
)

target_link_libraries(s7studio PRIVATE s7observe s7io s7net)

add_test(NAME s7studio_smoke COMMAND s7studio)



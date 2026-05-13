# s7/s7main/CMakeLists.txt
# No 'project()' command here, as it's a sub-project.

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- s7main Executable ---
add_executable(s7main
    src/main.cpp
    src/s7control_server.cpp
    src/s7json_rpc.cpp
    src/s7main_json_dto.cpp
)

# Always add include directory for headers
target_include_directories(s7main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


# Link s7main to s7core and s7io
target_link_libraries(s7main PRIVATE s7core s7io)

# Optional: Install the executable
install(TARGETS s7main DESTINATION bin)

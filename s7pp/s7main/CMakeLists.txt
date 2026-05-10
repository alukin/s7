# s7/s7main/CMakeLists.txt
# No 'project()' command here, as it's a sub-project.

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- s7main Executable ---
file(GLOB s7main_SRCS
    "src/*.cpp" # Assuming s7main source files are in s7main/src
)

# Start with basic sources
add_executable(s7main ${s7main_SRCS})

# Always add include directory for headers
target_include_directories(s7main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# rsocket-cpp is vendored as external/rsocket and provides ReactiveSocket.
target_link_libraries(s7main PRIVATE ReactiveSocket)

# Link s7main to s7core and s7io
target_link_libraries(s7main PRIVATE s7core s7io)

# Optional: Install the executable
install(TARGETS s7main DESTINATION bin)

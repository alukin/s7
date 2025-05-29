# s7/s7main/CMakeLists.txt
# No 'project()' command here, as it's a sub-project.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- s7main Executable ---
file(GLOB s7main_SRCS
    "src/*.cpp" # Assuming s7main source files are in s7main/src
)

add_executable(s7main ${s7main_SRCS})

# Link s7main to s7core.
# Since s7core is built as part of the same CMake run (due to add_subdirectory in top-level),
# its target is directly available by its name 's7core'.
# The PUBLIC dependencies of s7core (Boost, FFTW) will automatically be inherited by s7main.
target_link_libraries(s7main PRIVATE s7core)

# Optional: Install the executable
install(TARGETS s7main DESTINATION bin)

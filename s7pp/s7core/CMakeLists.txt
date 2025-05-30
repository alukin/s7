# s7/s7core/CMakeLists.txt
# No 'project()' command here, as it's a sub-project.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # Important for shared libraries
cmake_policy(SET CMP0167 OLD)

find_package(PkgConfig)

# --- Boost Configuration ---
find_package(Boost 1.80 COMPONENTS
 #   beast
    graph
    system
    filesystem
    REQUIRED
)

if (Boost_FOUND)
    message(STATUS "Boost found. Version: ${Boost_VERSION_STRING}")
    # include_directories(${Boost_INCLUDE_DIR}) # Prefer target_include_directories below
    # link_directories(${Boost_LIBRARY_DIRS}) # Prefer target_link_libraries below
else()
    message(FATAL_ERROR "Boost not found. Please ensure Boost is installed and discoverable by CMake.")
endif()

# --- FFTW Configuration ---
# find_package(FFTW REQUIRED) # For double precision.`
pkg_check_modules(FFTW3 REQUIRED fftw3)

#if (FFTW3_FOUND)
#    message(STATUS "FFTW3 found.")
#    # include_directories(${FFTW3_INCLUDE_DIR}) # Prefer target_include_directories below
#else()
#    message(FATAL_ERROR "FFTW3 not found. Please ensure FFTW is installed and discoverable by CMake.")
# endif()

# --- s7core Library ---
file(GLOB s7core_SRCS
    "src/*.cpp"
    "src/*.hpp"
)

# Add your library
add_library(s7core STATIC ${s7core_SRCS}
        include/s7value.hpp) # Or SHARED

# Add public include directories for s7core.
# Consumers will automatically get these when they link to s7core.
target_include_directories(s7core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For when s7core is built as part of this project
    $<INSTALL_INTERFACE:include> # For when s7core is installed and used by other projects
)

# Link dependencies to your library
target_link_libraries(s7core
    PUBLIC
        ${Boost_LIBRARIES}
    PUBLIC
        ${FFTW3_LIBRARIES}
)

# --- Install Rules (Optional but recommended for libraries) ---
install(TARGETS s7core
    EXPORT s7coreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT s7coreTargets
    FILE s7coreConfig.cmake
    NAMESPACE s7core::
    DESTINATION lib/cmake/s7core
)

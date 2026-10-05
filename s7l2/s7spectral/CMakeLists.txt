# s7spectral: the reference third-party plugin. It is built out of tree,
# against an installed S7 SDK, exactly as an external plugin would be:
#
#   cmake --install <s7-build> --component s7sdk --prefix <sdk>
#   cmake -S s7spectral -B <build> -DCMAKE_PREFIX_PATH=<sdk>
#   cmake --build <build>
#
# It is deliberately not part of the main build; ctest s7spectral_sdk_smoke
# runs these steps and loads the result.
cmake_minimum_required(VERSION 3.20)
project(s7spectral VERSION 1.0.0 LANGUAGES CXX)

find_package(s7core 0.1 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)

add_library(s7spectral MODULE
    src/spectral_window.cpp
    src/fft_element.cpp
    src/plugin_spectral.cpp
)
target_include_directories(s7spectral PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7spectral PRIVATE s7::s7core PkgConfig::FFTW3F)
target_compile_options(s7spectral PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(s7spectral PROPERTIES PREFIX "lib")

install(TARGETS s7spectral LIBRARY DESTINATION lib/s7/plugins)

cmake_minimum_required(VERSION 3.20)
project(s7spectral)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find FFTW3F (single precision)
find_package(FFTW3F REQUIRED)

# Find s7core
find_package(s7core REQUIRED)

# s7spectral library
add_library(s7spectral SHARED
    src/spectral_window.cpp
    src/fft_element.cpp
    src/plugin_spectral.cpp
)

target_include_directories(s7spectral PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(s7spectral PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7spectral PRIVATE
    s7core
    FFTW3::FFTW3F
)

set_target_properties(s7spectral PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "include/s7spectral/spectral_window.hpp;include/s7spectral/fft_element.hpp"
)

# Installation
install(TARGETS s7spectral
    LIBRARY DESTINATION lib/s7/plugins
    PUBLIC_HEADER DESTINATION include/s7spectral
)

install(FILES include/s7spectral/spectral_window.hpp include/s7spectral/fft_element.hpp
    DESTINATION include/s7spectral
)


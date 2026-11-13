# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: MIT-0

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

find_package(s7core 0.1 REQUIRED) # brings cxx_std_26 through s7::s7core
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)

add_library(s7spectral MODULE
    src/spectral_window.cpp
    src/fft_element.cpp
    src/plugin_spectral.cpp
)
target_include_directories(s7spectral PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(s7spectral PRIVATE s7::s7core PkgConfig::FFTW3F)
s7_embed_metadata(s7spectral resources/element_metadata.json spectral_metadata)
target_compile_options(s7spectral PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(s7spectral PROPERTIES PREFIX "lib")

install(TARGETS s7spectral LIBRARY DESTINATION lib/s7/plugins)

# The reusable, Qt-free processors are header-only and use the installed SDK.
# They can also be used without loading an element plugin.
install(DIRECTORY include/ DESTINATION include COMPONENT s7sdk)

include(CTest)
if(BUILD_TESTING)
    add_executable(spectral_analysis_smoke tests/analysis_smoke.cpp)
    target_include_directories(spectral_analysis_smoke PRIVATE include)
    target_link_libraries(spectral_analysis_smoke PRIVATE s7::s7core)
    add_test(NAME spectral_analysis_smoke COMMAND spectral_analysis_smoke)
endif()

add_library(s7spectral_analysis INTERFACE)
add_library(s7spectral::analysis ALIAS s7spectral_analysis)
set_target_properties(s7spectral_analysis PROPERTIES EXPORT_NAME analysis)
target_include_directories(s7spectral_analysis INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(s7spectral_analysis INTERFACE s7::s7core )
install(TARGETS s7spectral_analysis EXPORT s7spectralTargets COMPONENT s7sdk)
install(EXPORT s7spectralTargets NAMESPACE s7spectral:: DESTINATION lib/cmake/s7spectral COMPONENT s7sdk)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/s7spectralConfig.cmake [=[
include(CMakeFindDependencyMacro)
find_dependency(s7core 0.1)

include("${CMAKE_CURRENT_LIST_DIR}/s7spectralTargets.cmake")
]=])
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/s7spectralConfig.cmake DESTINATION lib/cmake/s7spectral COMPONENT s7sdk)

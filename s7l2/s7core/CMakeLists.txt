# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later

# Embed the same headless schema-library resource shipped for inspection/editing.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/schema_elements.s7library")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/schema_elements.s7library" S7_SCHEMA_ELEMENTS_JSON)
configure_file(resources/schema_elements.hpp.in generated/s7_schema_elements.hpp @ONLY)


# Shared, so that the host and every plugin use one copy of the registry and
# element code.
add_library(s7core SHARED
    src/schema.cpp
    src/schema_library.cpp
    src/validation.cpp
    src/compiler.cpp
    src/convolution.cpp
    src/runtime.cpp
    src/elements.cpp
    src/element_registry.cpp
    src/element_catalog.cpp
    src/metadata_json.cpp
    src/builtin_element_registry.cpp
    src/type_inference.cpp
    src/rate.cpp
    src/rate_inference.cpp
    src/convert.cpp
    src/platform.cpp
)

include(cmake/S7EmbedMetadata.cmake)
s7_embed_metadata(s7core resources/element_metadata.json core_metadata)

set_target_properties(s7core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_compile_features(s7core PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

target_include_directories(s7core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_include_directories(s7core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(s7core PUBLIC
    Boost::json
    Boost::system
    ZLIB::ZLIB
    ${CMAKE_DL_LIBS}
)
if (WIN32)
    # F128 on MinGW (s7/float_types.hpp), used by inline functions too: math
    # from libquadmath, and soft-float routines with Clang's return convention
    # (src/windows_f128_abi.cpp) for every module that links s7core.
    add_library(s7_f128_abi STATIC src/windows_f128_abi.cpp)
    # Each module keeps its own copy: a DLL must not export them (MinGW
    # exports every symbol by default), or an executable linking it would see
    # them twice.
    target_link_options(s7_f128_abi INTERFACE "SHELL:-Xlinker --exclude-symbols=__addtf3,__subtf3,__multf3,__divtf3,__negtf2,__powitf2,__extendsftf2,__extenddftf2,__extendxftf2,__extendhftf2,__floatsitf,__floatunsitf,__floatditf,__floatunditf,__floattitf,__floatuntitf")
    target_link_libraries(s7core PUBLIC s7_f128_abi quadmath)
    set(S7CORE_SDK_TARGETS s7core s7_f128_abi)
else()
    set(S7CORE_SDK_TARGETS s7core)
endif()

# --- SDK: headers, library and CMake package for out-of-tree plugins --------
include(GNUInstallDirs)
install(FILES resources/schema_elements.s7library DESTINATION ${CMAKE_INSTALL_DATADIR}/s7 COMPONENT s7sdk)
# The SDK is LGPL-3.0-or-later, which is a set of permissions on top of the GPL.
install(FILES
    ${PROJECT_SOURCE_DIR}/../LICENSING.md
    ${PROJECT_SOURCE_DIR}/../LICENSES/LGPL-3.0-or-later.txt
    ${PROJECT_SOURCE_DIR}/../LICENSES/GPL-3.0-or-later.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7core
    COMPONENT s7sdk
)
include(CMakePackageConfigHelpers)

install(TARGETS ${S7CORE_SDK_TARGETS} EXPORT s7coreTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT s7sdk
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT s7sdk
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT s7sdk
)
install(DIRECTORY include/s7 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT s7sdk)
install(EXPORT s7coreTargets
    NAMESPACE s7::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7core
    COMPONENT s7sdk
)
configure_package_config_file(cmake/s7coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/s7coreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7core
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/s7coreConfigVersion.cmake
    COMPATIBILITY SameMinorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/s7coreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/s7coreConfigVersion.cmake
    cmake/S7EmbedMetadata.cmake
    cmake/metadata_resource.hpp.in
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7core
    COMPONENT s7sdk
)

s7_add_test_executable(s7core_smoke
    tests/core_smoke.cpp
)

target_link_libraries(s7core_smoke PRIVATE s7core)

add_test(NAME s7core_smoke COMMAND s7core_smoke)

s7_add_test_executable(s7core_indexed_execution_smoke tests/indexed_execution_smoke.cpp)
target_link_libraries(s7core_indexed_execution_smoke PRIVATE s7core)
add_test(NAME s7core_indexed_execution_smoke COMMAND s7core_indexed_execution_smoke)

s7_add_test_executable(s7core_signal_types_smoke tests/signal_types_smoke.cpp)
target_link_libraries(s7core_signal_types_smoke PRIVATE s7core)
add_test(NAME s7core_signal_types_smoke COMMAND s7core_signal_types_smoke)

s7_add_test_executable(s7core_type_inference_smoke tests/type_inference_smoke.cpp)
target_link_libraries(s7core_type_inference_smoke PRIVATE s7core)
add_test(NAME s7core_type_inference_smoke COMMAND s7core_type_inference_smoke)

s7_add_test_executable(s7core_rate_inference_smoke tests/rate_inference_smoke.cpp)
target_link_libraries(s7core_rate_inference_smoke PRIVATE s7core)
add_test(NAME s7core_rate_inference_smoke COMMAND s7core_rate_inference_smoke)

# Plugins may build in strict ISO mode: the type headers must work there too.
s7_add_test_executable(s7core_signal_types_strict_smoke tests/signal_types_smoke.cpp)
target_include_directories(s7core_signal_types_strict_smoke PRIVATE include)
set_target_properties(s7core_signal_types_strict_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(s7core_signal_types_strict_smoke PRIVATE -Wall -Wextra -Wpedantic)
if (WIN32)
    target_link_libraries(s7core_signal_types_strict_smoke PRIVATE s7_f128_abi quadmath)
endif()
add_test(NAME s7core_signal_types_strict_smoke COMMAND s7core_signal_types_strict_smoke)


s7_add_test_executable(s7core_trapezoid_smoke tests/trapezoid_smoke.cpp)
target_link_libraries(s7core_trapezoid_smoke PRIVATE s7core)
add_test(NAME s7core_trapezoid_smoke COMMAND s7core_trapezoid_smoke)

s7_add_test_executable(s7core_pwm_smoke tests/pwm_smoke.cpp)
target_link_libraries(s7core_pwm_smoke PRIVATE s7core)
target_compile_definitions(s7core_pwm_smoke PRIVATE S7_PWM_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/pwm_pid_rlc.s7diagram")
add_test(NAME s7core_pwm_smoke COMMAND s7core_pwm_smoke)

s7_add_test_executable(s7core_sources_smoke tests/sources_smoke.cpp)
target_link_libraries(s7core_sources_smoke PRIVATE s7core)
add_test(NAME s7core_sources_smoke COMMAND s7core_sources_smoke)

s7_add_test_executable(s7core_control_elements_smoke tests/control_elements_smoke.cpp)
target_link_libraries(s7core_control_elements_smoke PRIVATE s7core)
add_test(NAME s7core_control_elements_smoke COMMAND s7core_control_elements_smoke)

s7_add_test_executable(s7core_schema_elements_smoke tests/schema_elements_smoke.cpp)
target_link_libraries(s7core_schema_elements_smoke PRIVATE s7core)
add_test(NAME s7core_schema_elements_smoke COMMAND s7core_schema_elements_smoke)

s7_add_test_executable(s7core_model_time_smoke tests/model_time_smoke.cpp)
target_link_libraries(s7core_model_time_smoke PRIVATE s7core)
add_test(NAME s7core_model_time_smoke COMMAND s7core_model_time_smoke)

s7_add_test_executable(s7core_metadata_json_smoke tests/metadata_json_smoke.cpp)
target_link_libraries(s7core_metadata_json_smoke PRIVATE s7core)
target_include_directories(s7core_metadata_json_smoke PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/metadata-generated)
add_test(NAME s7core_metadata_json_smoke COMMAND s7core_metadata_json_smoke)

s7_add_test_executable(s7core_convolution_smoke tests/convolution_smoke.cpp)
target_link_libraries(s7core_convolution_smoke PRIVATE s7core)
add_test(NAME s7core_convolution_smoke COMMAND s7core_convolution_smoke)

# Opt-in branch workload using the existing scalar FIR element.
add_executable(s7core_fir_branch_benchmark EXCLUDE_FROM_ALL benchmarks/fir_branches.cpp)
target_link_libraries(s7core_fir_branch_benchmark PRIVATE s7core Threads::Threads)
s7_enable_project_warnings(s7core_fir_branch_benchmark)

# Opt-in production NodeRuntime benchmark; kept outside normal CTest execution.
add_executable(s7core_indexed_benchmark EXCLUDE_FROM_ALL benchmarks/indexed_execution.cpp)
target_link_libraries(s7core_indexed_benchmark PRIVATE s7core)
target_compile_definitions(s7core_indexed_benchmark PRIVATE
    S7_PENDULUM_DIAGRAM="${PROJECT_SOURCE_DIR}/examples/double_pendulum_full.s7diagram")
s7_enable_project_warnings(s7core_indexed_benchmark)

# Opt-in buffered FFT convolution workload for studying expensive independent
# branches. FFTW remains optional and is not a runtime/core dependency.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(S7_BENCH_FFTW3 QUIET IMPORTED_TARGET fftw3)
    if(S7_BENCH_FFTW3_FOUND)
        add_executable(s7core_fft_convolution_benchmark EXCLUDE_FROM_ALL
            benchmarks/fft_convolution_branches.cpp)
        target_link_libraries(s7core_fft_convolution_benchmark PRIVATE
            PkgConfig::S7_BENCH_FFTW3 Threads::Threads)
        s7_enable_project_warnings(s7core_fft_convolution_benchmark)
    endif()
endif()

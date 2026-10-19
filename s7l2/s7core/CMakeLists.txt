
# Shared, so that the host and every plugin use one copy of the registry and
# element code.
add_library(s7core SHARED
    src/schema.cpp
    src/schema_library.cpp
    src/validation.cpp
    src/compiler.cpp
    src/runtime.cpp
    src/elements.cpp
    src/element_registry.cpp
    src/element_catalog.cpp
    src/builtin_element_registry.cpp
    src/type_inference.cpp
    src/rate.cpp
    src/rate_inference.cpp
    src/convert.cpp
    src/platform.cpp
)

set_target_properties(s7core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_compile_features(s7core PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

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
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7core
    COMPONENT s7sdk
)

add_executable(s7core_smoke
    tests/core_smoke.cpp
)

target_link_libraries(s7core_smoke PRIVATE s7core)

add_test(NAME s7core_smoke COMMAND s7core_smoke)

add_executable(s7core_signal_types_smoke tests/signal_types_smoke.cpp)
target_link_libraries(s7core_signal_types_smoke PRIVATE s7core)
add_test(NAME s7core_signal_types_smoke COMMAND s7core_signal_types_smoke)

add_executable(s7core_type_inference_smoke tests/type_inference_smoke.cpp)
target_link_libraries(s7core_type_inference_smoke PRIVATE s7core)
add_test(NAME s7core_type_inference_smoke COMMAND s7core_type_inference_smoke)

add_executable(s7core_rate_inference_smoke tests/rate_inference_smoke.cpp)
target_link_libraries(s7core_rate_inference_smoke PRIVATE s7core)
add_test(NAME s7core_rate_inference_smoke COMMAND s7core_rate_inference_smoke)

# Plugins may build in strict ISO mode: the type headers must work there too.
add_executable(s7core_signal_types_strict_smoke tests/signal_types_smoke.cpp)
target_include_directories(s7core_signal_types_strict_smoke PRIVATE include)
set_target_properties(s7core_signal_types_strict_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(s7core_signal_types_strict_smoke PRIVATE -Wall -Wextra -Wpedantic)
if (WIN32)
    target_link_libraries(s7core_signal_types_strict_smoke PRIVATE s7_f128_abi quadmath)
endif()
add_test(NAME s7core_signal_types_strict_smoke COMMAND s7core_signal_types_strict_smoke)


add_executable(s7core_trapezoid_smoke tests/trapezoid_smoke.cpp)
target_link_libraries(s7core_trapezoid_smoke PRIVATE s7core)
add_test(NAME s7core_trapezoid_smoke COMMAND s7core_trapezoid_smoke)

add_executable(s7core_pwm_smoke tests/pwm_smoke.cpp)
target_link_libraries(s7core_pwm_smoke PRIVATE s7core)
target_compile_definitions(s7core_pwm_smoke PRIVATE S7_PWM_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/pwm_pid_rlc.s7diagram")
add_test(NAME s7core_pwm_smoke COMMAND s7core_pwm_smoke)


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
)

# --- SDK: headers, library and CMake package for out-of-tree plugins --------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS s7core EXPORT s7coreTargets
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

# Plugins may build in strict ISO mode: the type headers must work there too.
add_executable(s7core_signal_types_strict_smoke tests/signal_types_smoke.cpp)
target_include_directories(s7core_signal_types_strict_smoke PRIVATE include)
set_target_properties(s7core_signal_types_strict_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(s7core_signal_types_strict_smoke PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME s7core_signal_types_strict_smoke COMMAND s7core_signal_types_strict_smoke)


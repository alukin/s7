# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later
add_library(s7hardware SHARED src/hardware.cpp src/registry.cpp src/provider_module.cpp src/backend_call.cpp)
target_link_libraries(s7hardware PUBLIC s7core Threads::Threads)
target_include_directories(s7hardware PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
set_target_properties(s7hardware PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
if(UNIX AND NOT APPLE)
    set_target_properties(s7hardware PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
s7_enable_project_warnings(s7hardware)
include(cmake/OptionalBackend.cmake)

# Exported together with s7audio/s7sdr, after all targets are defined below.
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/s7hardwareConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/s7hardwareConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7hardware)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/s7hardwareConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(TARGETS s7hardware EXPORT s7hardwareTargets RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${S7_HARDWARE_INSTALL_COMPONENT}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${S7_HARDWARE_INSTALL_COMPONENT} NAMELINK_COMPONENT s7sdk
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT s7sdk)
install(DIRECTORY include/s7 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT s7sdk)
install(EXPORT s7hardwareTargets NAMESPACE s7:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7hardware COMPONENT s7sdk)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/s7hardwareConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/s7hardwareConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/s7hardware COMPONENT s7sdk)
s7_add_test_executable(s7hardware_smoke tests/hardware_smoke.cpp)
target_link_libraries(s7hardware_smoke PRIVATE s7hardware)
s7_enable_project_warnings(s7hardware_smoke)
add_test(NAME s7hardware_smoke COMMAND s7hardware_smoke)
s7_add_test_executable(s7hardware_backend_signal_smoke tests/backend_signal_smoke.cpp)
target_link_libraries(s7hardware_backend_signal_smoke PRIVATE s7hardware)
add_test(NAME s7hardware_backend_signal_smoke COMMAND s7hardware_backend_signal_smoke)

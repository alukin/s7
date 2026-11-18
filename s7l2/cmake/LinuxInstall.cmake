# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: LGPL-3.0-or-later
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

install(TARGETS s7runtime s7ctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT s7runtime)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/s7runtime/examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/s7/runtime/examples COMPONENT s7runtime
    PATTERN certs EXCLUDE)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/s7/examples COMPONENT s7runtime)
install(FILES ${PROJECT_SOURCE_DIR}/docs/HARDWARE_IO.md ${PROJECT_SOURCE_DIR}/docs/SPLIT_TRANSPORT.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7 COMPONENT s7runtime)
install(FILES ${PROJECT_SOURCE_DIR}/../LICENSING.md ${PROJECT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7 COMPONENT ${S7_CORE_INSTALL_COMPONENT})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/../LICENSES/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/s7 COMPONENT ${S7_CORE_INSTALL_COMPONENT})

if(S7_BUILD_STUDIO)
    install(TARGETS s7studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT s7studio)
    install(FILES ${PROJECT_SOURCE_DIR}/tools/linux/s7studio.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT s7studio)
    install(FILES ${PROJECT_SOURCE_DIR}/tools/linux/s7studio.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT s7studio)
    install(FILES ${PROJECT_SOURCE_DIR}/external/QSchematic/license.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7studio RENAME QSchematic-LICENSE COMPONENT s7studio)
    install(FILES ${gpds_SOURCE_DIR}/license.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7studio RENAME GPDS-LICENSE COMPONENT s7studio)
    install(FILES ${gpds_SOURCE_DIR}/gpds/3rdparty/tinyxml2/LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7studio RENAME tinyxml2-LICENSE COMPONENT s7studio)
    install(FILES ${gpds_SOURCE_DIR}/gpds/3rdparty/miniyaml/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7studio RENAME miniyaml-LICENSE COMPONENT s7studio)
    install(FILES ${PROJECT_SOURCE_DIR}/s7studio/resources/icons/tabler/LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7studio RENAME Tabler-LICENSE COMPONENT s7studio)
endif()

if(S7_NATIVE_PACKAGES)
    # Zstandard is linked statically into runtime/Studio, so its notice travels
    # with S7 even though shared dependencies remain distribution-managed.
    find_file(S7_ZSTD_NOTICE NAMES LICENSE PATHS /usr/share/licenses/libzstd NO_DEFAULT_PATH)
    if(NOT S7_ZSTD_NOTICE)
        find_file(S7_ZSTD_NOTICE NAMES copyright PATHS /usr/share/doc/libzstd-dev NO_DEFAULT_PATH)
    endif()
    if(NOT S7_ZSTD_NOTICE)
        message(FATAL_ERROR "Native packages require the installed Zstandard copyright/license notice")
    endif()
    install(FILES ${S7_ZSTD_NOTICE} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/s7
        RENAME Zstandard-NOTICE COMPONENT s7libs)
    set(S7_NATIVE_ANALYSIS_PREFIX "" CACHE PATH "Installed out-of-tree analysis plugins to package")
    if(S7_NATIVE_ANALYSIS_PREFIX)
        foreach(module s7spectral s7wavelet)
            install(FILES ${S7_NATIVE_ANALYSIS_PREFIX}/lib/s7/plugins/lib${module}.so
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/s7/plugins COMPONENT s7analysis)
            install(DIRECTORY ${S7_NATIVE_ANALYSIS_PREFIX}/include/${module}
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT s7sdk)
            install(DIRECTORY ${S7_NATIVE_ANALYSIS_PREFIX}/lib/cmake/${module}
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake COMPONENT s7sdk)
        endforeach()
    endif()
    # Native packages use the distribution loader paths and do not bundle libraries.
    foreach(target s7core s7hardware s7audio s7sdr s7audio_plugin s7sdr_plugin
            s7noise s7runtime s7ctl s7hw s7analysis_lab s7studio)
        if(TARGET ${target})
            set_target_properties(${target} PROPERTIES INSTALL_RPATH "")
        endif()
    endforeach()
endif()

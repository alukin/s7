# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${out}\n${err}")
    endif()
endfunction()
foreach(component s7libs s7hardware s7sdk)
    run("${CMAKE_COMMAND}" --install "${S7_BUILD}" --prefix "${S7_TEST_ROOT}/sdk" --component ${component})
endforeach()
set(consumer_args "-DCMAKE_PREFIX_PATH=${S7_TEST_ROOT}/sdk" "-DCMAKE_CXX_COMPILER=${S7_CXX}")
if(S7_TOOLCHAIN_FILE)
    list(APPEND consumer_args "--toolchain" "${S7_TOOLCHAIN_FILE}"
        "-Ds7core_DIR=${S7_TEST_ROOT}/sdk/lib/cmake/s7core"
        "-Ds7hardware_DIR=${S7_TEST_ROOT}/sdk/lib/cmake/s7hardware"
        "-DCMAKE_CROSSCOMPILING_EMULATOR=${S7_EMULATOR}")
endif()
run("${CMAKE_COMMAND}" -S "${S7_SOURCE}/s7hardware/tests/sdk" -B "${S7_TEST_ROOT}/consumer" ${consumer_args})
run("${CMAKE_COMMAND}" --build "${S7_TEST_ROOT}/consumer" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${S7_TEST_ROOT}/consumer" --output-on-failure)

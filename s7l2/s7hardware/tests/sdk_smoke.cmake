# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${out}\n${err}")
    endif()
endfunction()
run("${CMAKE_COMMAND}" --install "${S7_BUILD}" --prefix "${S7_TEST_ROOT}/sdk" --component s7sdk)
run("${CMAKE_COMMAND}" -S "${S7_SOURCE}/s7hardware/tests/sdk" -B "${S7_TEST_ROOT}/consumer"
    "-DCMAKE_PREFIX_PATH=${S7_TEST_ROOT}/sdk" "-DCMAKE_CXX_COMPILER=${S7_CXX}")
run("${CMAKE_COMMAND}" --build "${S7_TEST_ROOT}/consumer" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${S7_TEST_ROOT}/consumer" --output-on-failure)

# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Whole-system integration and calculation tests live here.
# Module smoke tests belong in each subproject's `tests/` directory.
#
# Numerical-modeling validation tests (see ../doc/S7_Numerical_Modeling_Test_Plan.md)
# share numerical_harness.hpp; see its header comment for the dt convention
# they follow (s7::Schema has no schema-wide dt, so each test picks one and
# passes it explicitly to every element that needs it).

s7_add_test_executable(s7tests_linear_examples_smoke linear_examples_smoke.cpp)
target_link_libraries(s7tests_linear_examples_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_linear_examples_smoke PRIVATE
    S7_LINEAR_DIR="${PROJECT_SOURCE_DIR}/examples/linear")
add_test(NAME s7tests_linear_examples_smoke COMMAND s7tests_linear_examples_smoke)

s7_add_test_executable(s7tests_control_examples_smoke control_examples_smoke.cpp)
target_link_libraries(s7tests_control_examples_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_control_examples_smoke PRIVATE
    S7_EDUCATIONAL_DIR="${PROJECT_SOURCE_DIR}/examples")
add_test(NAME s7tests_control_examples_smoke COMMAND s7tests_control_examples_smoke)
set_tests_properties(s7tests_control_examples_smoke PROPERTIES TIMEOUT 120)

s7_add_test_executable(s7tests_nonlinear_examples_smoke nonlinear_examples_smoke.cpp)
target_link_libraries(s7tests_nonlinear_examples_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_nonlinear_examples_smoke PRIVATE
    S7_NONLINEAR_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
add_test(NAME s7tests_nonlinear_examples_smoke COMMAND s7tests_nonlinear_examples_smoke)
set_tests_properties(s7tests_nonlinear_examples_smoke PROPERTIES TIMEOUT 180)

s7_add_test_executable(s7tests_measurement_examples_smoke measurement_examples_smoke.cpp)
target_link_libraries(s7tests_measurement_examples_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_measurement_examples_smoke PRIVATE
    S7_MEASUREMENT_DIR="${PROJECT_SOURCE_DIR}/examples")
add_test(NAME s7tests_measurement_examples_smoke COMMAND s7tests_measurement_examples_smoke)
set_tests_properties(s7tests_measurement_examples_smoke PROPERTIES TIMEOUT 120)

s7_add_test_executable(s7tests_linear_scalar_smoke linear_scalar_smoke.cpp)
target_link_libraries(s7tests_linear_scalar_smoke PRIVATE s7core)
add_test(NAME s7tests_linear_scalar_smoke COMMAND s7tests_linear_scalar_smoke)

# SymplecticIntegrator vs. plain EulerIntegrator on the harmonic oscillator
# (examples/2int_osc.s7diagram's amplitude-drift finding, docs/OPEN_PROBLEMS.md).
s7_add_test_executable(s7tests_symplectic_oscillator_smoke symplectic_oscillator_smoke.cpp)
target_link_libraries(s7tests_symplectic_oscillator_smoke PRIVATE s7core)
add_test(NAME s7tests_symplectic_oscillator_smoke COMMAND s7tests_symplectic_oscillator_smoke)

# Explicit research-only benchmark; not part of production runtime or CTest.
add_executable(s7tests_numerical_research_baseline EXCLUDE_FROM_ALL numerical_research_baseline.cpp)
target_link_libraries(s7tests_numerical_research_baseline PRIVATE s7core)

# Isolated headless research regressions. SciPy/NumPy remain optional for the
# production build; full research matrices and plots are opt-in script runs.
if (NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy, scipy"
            RESULT_VARIABLE S7_RESEARCH_PYTHON_READY OUTPUT_QUIET ERROR_QUIET)
        if (S7_RESEARCH_PYTHON_READY EQUAL 0)
            add_test(NAME s7tests_linear_structure_research
                COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/test_linear_structure_research.py)
            add_test(NAME s7tests_numerical_feedback_research
                COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/test_numerical_feedback_research.py)
        endif()
    endif()
endif()

# Headless .s7diagram + probe-sidecar runner (diagram_harness.hpp,
# ../docs/VISUAL_NUMERICAL_TESTS_PLAN.md section 2); diagrams/*.s7diagram are
# Studio-openable.
s7_add_test_executable(s7tests_diagram_harness_smoke diagram_harness_smoke.cpp)
target_link_libraries(s7tests_diagram_harness_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_diagram_harness_smoke PRIVATE
    S7TESTS_DIAGRAM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/diagrams")
add_test(NAME s7tests_diagram_harness_smoke COMMAND s7tests_diagram_harness_smoke)

s7_add_test_executable(s7tests_coupled_pendulum_smoke coupled_pendulum_smoke.cpp)
target_link_libraries(s7tests_coupled_pendulum_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_coupled_pendulum_smoke PRIVATE
    S7_PENDULUM_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/double_pendulum"
    S7_PENDULUM_REPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}/coupled_pendulum")
add_test(NAME s7tests_coupled_pendulum_smoke COMMAND s7tests_coupled_pendulum_smoke)
set_tests_properties(s7tests_coupled_pendulum_smoke PROPERTIES TIMEOUT 180)

s7_add_test_executable(s7tests_nonlinear_pendulum_smoke nonlinear_pendulum_smoke.cpp)
target_link_libraries(s7tests_nonlinear_pendulum_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_nonlinear_pendulum_smoke PRIVATE
    S7_SINGLE_PENDULUM_DIR="${PROJECT_SOURCE_DIR}/examples/pendulum"
    S7_SINGLE_PENDULUM_REPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}/nonlinear_pendulum")
add_test(NAME s7tests_nonlinear_pendulum_smoke COMMAND s7tests_nonlinear_pendulum_smoke)
set_tests_properties(s7tests_nonlinear_pendulum_smoke PROPERTIES TIMEOUT 180)

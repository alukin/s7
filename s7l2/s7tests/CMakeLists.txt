# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Whole-system integration and calculation tests live here.
# Module smoke tests belong in each subproject's `tests/` directory.
#
# Numerical-modeling validation tests (see ../doc/S7_Numerical_Modeling_Test_Plan.md)
# share numerical_harness.hpp; see its header comment for the dt convention
# they follow (s7::Schema has no schema-wide dt, so each test picks one and
# passes it explicitly to every element that needs it).

s7_add_test_executable(s7tests_education_sp06 education_sp06_smoke.cpp)
target_link_libraries(s7tests_education_sp06 PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_sp06 PRIVATE
    S7_SP06_DIAGRAM="${PROJECT_SOURCE_DIR}/education/labs/SP06-three-tap-fir/sp06_three_tap_fir.s7diagram")
add_test(NAME s7tests_education_sp06 COMMAND s7tests_education_sp06)
set_tests_properties(s7tests_education_sp06 PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_foundations education_foundations_smoke.cpp)
target_link_libraries(s7tests_education_foundations PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_foundations PRIVATE
    S7_FOUNDATIONS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_foundations COMMAND s7tests_education_foundations)
set_tests_properties(s7tests_education_foundations PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_linear education_linear_smoke.cpp)
target_link_libraries(s7tests_education_linear PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_linear PRIVATE
    S7_LINEAR_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_linear COMMAND s7tests_education_linear)
set_tests_properties(s7tests_education_linear PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_signals education_signals_smoke.cpp)
target_link_libraries(s7tests_education_signals PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_signals PRIVATE
    S7_SIGNAL_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_signals COMMAND s7tests_education_signals)
set_tests_properties(s7tests_education_signals PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_lti education_lti_smoke.cpp)
target_link_libraries(s7tests_education_lti PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_lti PRIVATE
    S7_LTI_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_lti COMMAND s7tests_education_lti)
set_tests_properties(s7tests_education_lti PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_spectral education_spectral_smoke.cpp)
target_link_libraries(s7tests_education_spectral PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_spectral PRIVATE
    S7_SPECTRAL_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_spectral COMMAND s7tests_education_spectral)
set_tests_properties(s7tests_education_spectral PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_convolution education_convolution_smoke.cpp)
target_link_libraries(s7tests_education_convolution PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_convolution PRIVATE
    S7_SP10_DIAGRAM="${PROJECT_SOURCE_DIR}/education/labs/SP10-circular-convolution/sp10_circular_convolution.s7diagram")
add_test(NAME s7tests_education_convolution COMMAND s7tests_education_convolution)
set_tests_properties(s7tests_education_convolution PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_stft education_stft_smoke.cpp)
target_link_libraries(s7tests_education_stft PRIVATE s7core s7observe)
target_include_directories(s7tests_education_stft PRIVATE ${PROJECT_SOURCE_DIR}/s7spectral/include)
target_compile_definitions(s7tests_education_stft PRIVATE
    S7_SP11_DIAGRAM="${PROJECT_SOURCE_DIR}/education/labs/SP11-short-time-fourier/sp11_short_time_fourier.s7diagram")
add_test(NAME s7tests_education_stft COMMAND s7tests_education_stft)
set_tests_properties(s7tests_education_stft PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_control education_control_smoke.cpp)
target_link_libraries(s7tests_education_control PRIVATE s7core s7observe)
target_compile_definitions(s7tests_education_control PRIVATE S7_CONTROL_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_control COMMAND s7tests_education_control)
set_tests_properties(s7tests_education_control PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_loop education_loop_smoke.cpp)
target_link_libraries(s7tests_education_loop PRIVATE s7core s7observe)
target_include_directories(s7tests_education_loop PRIVATE ${PROJECT_SOURCE_DIR}/s7spectral/include)
target_compile_definitions(s7tests_education_loop PRIVATE S7_LOOP_LABS_DIR="${PROJECT_SOURCE_DIR}/education/labs")
add_test(NAME s7tests_education_loop COMMAND s7tests_education_loop)
set_tests_properties(s7tests_education_loop PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_compensator education_compensator_smoke.cpp)
target_link_libraries(s7tests_education_compensator PRIVATE s7core s7observe)
target_include_directories(s7tests_education_compensator PRIVATE ${PROJECT_SOURCE_DIR}/s7spectral/include)
target_compile_definitions(s7tests_education_compensator PRIVATE S7_CT08_DIAGRAM="${PROJECT_SOURCE_DIR}/education/labs/CT08-lead-lag/ct08_lead_lag.s7diagram")
add_test(NAME s7tests_education_compensator COMMAND s7tests_education_compensator)
set_tests_properties(s7tests_education_compensator PROPERTIES TIMEOUT 30)

s7_add_test_executable(s7tests_education_am education_am_smoke.cpp)
target_link_libraries(s7tests_education_am PRIVATE s7core s7observe)
target_include_directories(s7tests_education_am PRIVATE ${PROJECT_SOURCE_DIR}/s7spectral/include)
target_compile_definitions(s7tests_education_am PRIVATE S7_SP14_DIAGRAM="${PROJECT_SOURCE_DIR}/education/labs/SP14-coherent-am/sp14_coherent_am.s7diagram")
add_test(NAME s7tests_education_am COMMAND s7tests_education_am)
set_tests_properties(s7tests_education_am PROPERTIES TIMEOUT 30)

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

# Opt-in scheduling research; never changes Compiler/NodeRuntime or joins CTest.
add_executable(s7tests_cross_node_scheduling_spike EXCLUDE_FROM_ALL cross_node_scheduling_spike.cpp)
target_link_libraries(s7tests_cross_node_scheduling_spike PRIVATE s7net Threads::Threads)
s7_enable_project_warnings(s7tests_cross_node_scheduling_spike)

add_executable(s7tests_cross_node_s7_node EXCLUDE_FROM_ALL cross_node_s7_node.cpp)
target_link_libraries(s7tests_cross_node_s7_node PRIVATE s7net)
s7_enable_project_warnings(s7tests_cross_node_s7_node)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    s7_add_test_executable(s7tests_video_stream_spike video_stream_spike.cpp)
    target_link_libraries(s7tests_video_stream_spike PRIVATE s7net)
    s7_enable_project_warnings(s7tests_video_stream_spike)
    add_executable(s7tests_cross_node_phase4 EXCLUDE_FROM_ALL cross_node_phase4.cpp)
    target_link_libraries(s7tests_cross_node_phase4 PRIVATE s7net)
    s7_enable_project_warnings(s7tests_cross_node_phase4)
endif()

# Phase 3: compiled real-element graph experiments. The long matrix/CSV runner
# remains opt-in; the focused publication, solver and reference checks run in CTest.
s7_add_test_executable(s7tests_numerical_integration_research_smoke numerical_integration_research_smoke.cpp)
target_link_libraries(s7tests_numerical_integration_research_smoke PRIVATE s7core)
add_test(NAME s7tests_numerical_integration_research_smoke COMMAND s7tests_numerical_integration_research_smoke)
add_executable(s7tests_numerical_integration_research EXCLUDE_FROM_ALL numerical_integration_research.cpp)
target_link_libraries(s7tests_numerical_integration_research PRIVATE s7core)

# Isolated headless research regressions. SciPy/NumPy remain optional for the
# production build; full research matrices and plots are opt-in script runs.
if (NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME s7tests_teaching_assets
            COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/test_teaching_assets.py)
        if (TARGET s7tests_video_stream_spike)
            add_test(NAME s7tests_video_stream_spike
                COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/test_video_stream_spike.py
                    $<TARGET_FILE:s7tests_video_stream_spike>)
            set_tests_properties(s7tests_video_stream_spike PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "video;integration")
        endif()
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

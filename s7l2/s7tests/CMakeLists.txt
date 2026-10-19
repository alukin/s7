
# Whole-system integration and calculation tests live here.
# Module smoke tests belong in each subproject's `tests/` directory.
#
# Numerical-modeling validation tests (see ../doc/S7_Numerical_Modeling_Test_Plan.md)
# share numerical_harness.hpp; see its header comment for the dt convention
# they follow (s7::Schema has no schema-wide dt, so each test picks one and
# passes it explicitly to every element that needs it).

add_executable(s7tests_linear_scalar_smoke linear_scalar_smoke.cpp)
target_link_libraries(s7tests_linear_scalar_smoke PRIVATE s7core)
add_test(NAME s7tests_linear_scalar_smoke COMMAND s7tests_linear_scalar_smoke)

# SymplecticIntegrator vs. plain EulerIntegrator on the harmonic oscillator
# (examples/2int_osc.s7diagram's amplitude-drift finding, docs/OPEN_PROBLEMS.md).
add_executable(s7tests_symplectic_oscillator_smoke symplectic_oscillator_smoke.cpp)
target_link_libraries(s7tests_symplectic_oscillator_smoke PRIVATE s7core)
add_test(NAME s7tests_symplectic_oscillator_smoke COMMAND s7tests_symplectic_oscillator_smoke)

# Headless .s7diagram + probe-sidecar runner (diagram_harness.hpp,
# ../docs/VISUAL_NUMERICAL_TESTS_PLAN.md section 2); diagrams/*.s7diagram are
# Studio-openable.
add_executable(s7tests_diagram_harness_smoke diagram_harness_smoke.cpp)
target_link_libraries(s7tests_diagram_harness_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_diagram_harness_smoke PRIVATE
    S7TESTS_DIAGRAM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/diagrams")
add_test(NAME s7tests_diagram_harness_smoke COMMAND s7tests_diagram_harness_smoke)

add_executable(s7tests_coupled_pendulum_smoke coupled_pendulum_smoke.cpp)
target_link_libraries(s7tests_coupled_pendulum_smoke PRIVATE s7core s7observe)
target_compile_definitions(s7tests_coupled_pendulum_smoke PRIVATE
    S7_PENDULUM_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/double_pendulum"
    S7_PENDULUM_REPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}/coupled_pendulum")
add_test(NAME s7tests_coupled_pendulum_smoke COMMAND s7tests_coupled_pendulum_smoke)
set_tests_properties(s7tests_coupled_pendulum_smoke PROPERTIES TIMEOUT 180)

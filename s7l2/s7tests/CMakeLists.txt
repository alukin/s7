
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

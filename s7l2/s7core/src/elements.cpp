#include "s7/elements.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numbers>

namespace s7 {

// ============================================================================
// Real-valued specific elements
// ============================================================================

PreviousValue::PreviousValue(RealValue initial_value)
    : Element("PreviousValue", true, Feedthrough::None),
      output_value_(initial_value)
{
    register_input(0, port_names::in, SignalType::F64, nullptr);
    register_output(0, port_names::out, SignalType::F64, &output_value_);
}

void PreviousValue::step() {
    // Writes the next step's output; NodeRuntime publishes it after the step.
    output_value_ = read_input<RealValue>(0);
}

// ============================================================================
// Binary arithmetic elements
// ============================================================================

// Multiply is implemented inline in the header as a templated element.

// ============================================================================
// Type conversion elements
// ============================================================================

} // namespace s7

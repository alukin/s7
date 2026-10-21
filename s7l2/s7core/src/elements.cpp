// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

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

// ============================================================================
// Math elements
// ============================================================================

TrigFunction trig_function_from_name(const std::string_view name)
{
    if (name == "sin") return TrigFunction::Sin;
    if (name == "cos") return TrigFunction::Cos;
    if (name == "tan") return TrigFunction::Tan;
    if (name == "asin") return TrigFunction::Asin;
    if (name == "acos") return TrigFunction::Acos;
    if (name == "atan") return TrigFunction::Atan;
    if (name == "sinh") return TrigFunction::Sinh;
    if (name == "cosh") return TrigFunction::Cosh;
    if (name == "tanh") return TrigFunction::Tanh;
    throw std::invalid_argument("Trig function must be one of sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, got '" +
                                std::string(name) + "'.");
}

} // namespace s7

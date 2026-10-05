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

Int12ToDouble::Int12ToDouble()
    : Element("Int12ToDouble"),
      output_value_(0.0)
{
    register_input(0, port_names::in, SignalType::I12, nullptr);
    register_output(0, port_names::out, SignalType::F64, &output_value_);
}

// INT12 <-> DOUBLE use Q11 fixed point: code / 2048, so [-2048, 2047] maps
// to [-1.0, 1 - 2^-11].
void Int12ToDouble::step() {
    output_value_ = static_cast<RealValue>(read_input<signal_storage_t<SignalType::I12>>(0)) / 2048.0;
}

DoubleToInt12::DoubleToInt12()
    : Element("DoubleToInt12"),
      output_value_(0.0)
{
    register_input(0, port_names::in, SignalType::F64, nullptr);
    register_output(0, port_names::out, SignalType::I12, &output_value_);
}

void DoubleToInt12::step() {
    // Round half to even (default rounding mode), saturate, NaN -> 0.
    const auto scaled = read_input<RealValue>(0) * 2048.0;
    constexpr auto lo = static_cast<RealValue>(IntegerRange<SignalType::I12>::min);
    constexpr auto hi = static_cast<RealValue>(IntegerRange<SignalType::I12>::max);
    output_value_ = std::isnan(scaled) ? 0 : static_cast<signal_storage_t<SignalType::I12>>(std::clamp(std::nearbyint(scaled), lo, hi));
}

} // namespace s7

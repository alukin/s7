#include "s7/elements.hpp"
#include <cmath>
#include <stdexcept>

namespace s7 {

// ============================================================================
// Real-valued specific elements
// ============================================================================

FirFilter::FirFilter(std::vector<RealValue> taps)
    : Element("FirFilter", true),  // has_state = true
      taps_(std::move(taps)),
      history_(taps_.size(), 0.0),
      output_value_(0.0)
{
    if (taps_.empty()) {
        throw std::invalid_argument("FirFilter requires at least one tap.");
    }
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
    register_output(0, port_names::out, SignalType::DOUBLE, &output_value_);
}

void FirFilter::step() {
    // Shift history
    for (std::size_t i = history_.size() - 1; i > 0; --i) {
        history_[i] = history_[i - 1];
    }
    history_[0] = read_input<RealValue>(0);

    // Compute output
    RealValue y = 0.0;
    for (std::size_t i = 0; i < taps_.size(); ++i) {
        y += taps_[i] * history_[i];
    }
    output_value_ = y;  // Direct assignment!
}

IirFilter::IirFilter(RealValue b0, RealValue b1, RealValue a1)
    : Element("IirFilter", true),  // has_state = true
      b0_(b0), b1_(b1), a1_(a1),
      x1_(0.0), y1_(0.0),
      output_value_(0.0)
{
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
    register_output(0, port_names::out, SignalType::DOUBLE, &output_value_);
}

void IirFilter::step() {
    const RealValue x = read_input<RealValue>(0);
    const RealValue y = b0_ * x + b1_ * x1_ - a1_ * y1_;

    x1_ = x;
    y1_ = y;
    output_value_ = y;  // Direct assignment!
}

PreviousValue::PreviousValue(RealValue initial_value)
    : Element("PreviousValue", true),  // has_state = true
      previous_(initial_value),
      output_value_(initial_value)
{
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
    register_output(0, port_names::out, SignalType::DOUBLE, &output_value_);
}

void PreviousValue::step() {
    output_value_ = previous_;  // Output old value first
    previous_ = read_input<RealValue>(0);  // Then update
}

// ============================================================================
// Type conversion elements
// ============================================================================

ReImToComplex::ReImToComplex()
    : Element("ReImToComplex"),
      output_value_{0.0, 0.0}
{
    register_input(0, port_names::re, SignalType::DOUBLE, nullptr);
    register_input(1, port_names::im, SignalType::DOUBLE, nullptr);
    register_output(0, port_names::out, SignalType::COMPLEX, &output_value_);
}

void ReImToComplex::step() {
    const RealValue re = read_input<RealValue>(0);
    const RealValue im = read_input<RealValue>(1);
    output_value_ = ComplexValue{re, im};  // Direct assignment!
}

ComplexToReIm::ComplexToReIm()
    : Element("ComplexToReIm"),
      output_re_(0.0),
      output_im_(0.0)
{
    register_input(0, port_names::in, SignalType::COMPLEX, nullptr);
    register_output(0, port_names::re, SignalType::DOUBLE, &output_re_);
    register_output(1, port_names::im, SignalType::DOUBLE, &output_im_);
}

void ComplexToReIm::step() {
    const ComplexValue c = read_input<ComplexValue>(0);
    output_re_ = c.real();  // Direct assignment!
    output_im_ = c.imag();  // Direct assignment!
}

Int12ToDouble::Int12ToDouble()
    : Element("Int12ToDouble"),
      output_value_(0.0)
{
    register_input(0, port_names::in, SignalType::INT12, nullptr);
    register_output(0, port_names::out, SignalType::DOUBLE, &output_value_);
}

void Int12ToDouble::step() {
    const auto raw_value = static_cast<int16_t>(read_input<RealValue>(0));
    output_value_ = static_cast<RealValue>(raw_value) / 2048.0;  // Direct assignment!
}

DoubleToInt12::DoubleToInt12()
    : Element("DoubleToInt12"),
      output_value_(0.0)
{
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
    register_output(0, port_names::out, SignalType::INT12, &output_value_);
}

void DoubleToInt12::step() {
    const double scaled = read_input<RealValue>(0) * 2047.0;
    auto quantized = static_cast<int16_t>(scaled);

    if (quantized > 2047) quantized = 2047;
    if (quantized < -2048) quantized = -2048;

    output_value_ = static_cast<RealValue>(quantized);  // Direct assignment!
}

ComplexToMagnitudePhase::ComplexToMagnitudePhase()
    : Element("ComplexToMagnitudePhase"),
      output_magnitude_(0.0),
      output_phase_(0.0)
{
    register_input(0, port_names::in, SignalType::COMPLEX, nullptr);
    register_output(0, "magnitude", SignalType::DOUBLE, &output_magnitude_);
    register_output(1, "phase", SignalType::DOUBLE, &output_phase_);
}

void ComplexToMagnitudePhase::bind_inputs(std::span<const InputBinding> bindings) {
    Element::bind_inputs(bindings);
}

void ComplexToMagnitudePhase::step() {
    const ComplexValue c = read_input<ComplexValue>(0);
    output_magnitude_ = std::abs(c);  // Direct assignment!
    output_phase_ = std::arg(c);  // Direct assignment!
}

} // namespace s7


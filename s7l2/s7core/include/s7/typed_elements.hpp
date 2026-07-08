#pragma once

#include "s7/element.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace s7 {

namespace detail {

template <SignalType Type>
[[nodiscard]] inline signal_storage_t<Type> default_value()
{
    if constexpr (Type == SignalType::COMPLEX) {
        return ComplexValue{0.0, 0.0};
    }

    return RealValue{0.0};
}

template <SignalType Type, std::size_t Count>
inline void assign_output_bindings(std::vector<OutputBinding>& bindings,
                                   const std::array<signal_storage_t<Type>, Count>& values)
{
    bindings.clear();
    bindings.reserve(Count);
    for (const auto& value : values) {
        bindings.push_back(bind_output<Type>(value));
    }
}

} // namespace detail

/// Typed helper for source elements with one output and no inputs.
template <SignalType OutputType>
class TypedSourceElement : public Element {
protected:
    explicit TypedSourceElement(std::string_view type_name,
                                std::string_view output_name = "out",
                                bool has_causal_memory = false)
        : Element(type_name,
                  std::array<Port, 0>{},
                  std::array<Port, 1>{{Port{0, output_name, OutputType}}},
                  has_causal_memory),
          output_values_{detail::default_value<OutputType>()}
    {
        detail::assign_output_bindings<OutputType>(output_bindings_, output_values_);
    }

public:
    void step() override
    {
        process();
    }

protected:
    virtual void process() = 0;

    std::array<signal_storage_t<OutputType>, 1> output_values_{};
};

/// Typed helper for one-input/one-output elements.
template <SignalType InputType, SignalType OutputType>
class TypedUnaryElement : public Element {
protected:
    TypedUnaryElement(std::string_view type_name,
                      std::string_view input_name = "in",
                      std::string_view output_name = "out",
                      bool has_causal_memory = false)
        : Element(type_name,
                  std::array<Port, 1>{{Port{0, input_name, InputType}}},
                  std::array<Port, 1>{{Port{0, output_name, OutputType}}},
                  has_causal_memory),
          output_values_{detail::default_value<OutputType>()}
    {
        detail::assign_output_bindings<OutputType>(output_bindings_, output_values_);
    }

public:
    void step() override
    {
        process(this->template input_value<InputType>(0));
    }

protected:
    virtual void process(const signal_storage_t<InputType>& input) = 0;

    std::array<signal_storage_t<OutputType>, 1> output_values_{};
};

/// Typed helper for one-input/one-output stateful elements.
template <SignalType InputType, SignalType OutputType>
class TypedStatefulUnaryElement : public TypedUnaryElement<InputType, OutputType> {
protected:
    TypedStatefulUnaryElement(std::string_view type_name,
                              std::string_view input_name = "in",
                              std::string_view output_name = "out")
        : TypedUnaryElement<InputType, OutputType>(type_name, input_name, output_name, true)
    {
    }
};

/// Typed helper for two-input/one-output elements.
template <SignalType InputType, SignalType OutputType>
class TypedBinaryElement : public Element {
protected:
    TypedBinaryElement(std::string_view type_name,
                       std::string_view lhs_name = "lhs",
                       std::string_view rhs_name = "rhs",
                       std::string_view output_name = "out",
                       bool has_causal_memory = false)
        : Element(type_name,
                  std::array<Port, 2>{{Port{0, lhs_name, InputType}, Port{1, rhs_name, InputType}}},
                  std::array<Port, 1>{{Port{0, output_name, OutputType}}},
                  has_causal_memory),
          output_values_{detail::default_value<OutputType>()}
    {
        detail::assign_output_bindings<OutputType>(output_bindings_, output_values_);
    }

public:
    void step() override
    {
        process(this->template input_value<InputType>(0), this->template input_value<InputType>(1));
    }

protected:
    virtual void process(const signal_storage_t<InputType>& lhs, const signal_storage_t<InputType>& rhs) = 0;

    std::array<signal_storage_t<OutputType>, 1> output_values_{};
};

/// Typed helper for one-input/two-output elements.
template <SignalType InputType, SignalType FirstOutputType, SignalType SecondOutputType>
class TypedSplitElement : public Element {
protected:
    TypedSplitElement(std::string_view type_name,
                      std::string_view input_name = "in",
                      std::string_view first_output_name = "re",
                      std::string_view second_output_name = "im",
                      bool has_causal_memory = false)
        : Element(type_name,
                  std::array<Port, 1>{{Port{0, input_name, InputType}}},
                  std::array<Port, 2>{{Port{0, first_output_name, FirstOutputType}, Port{1, second_output_name, SecondOutputType}}},
                  has_causal_memory),
          first_output_value_(detail::default_value<FirstOutputType>()),
          second_output_value_(detail::default_value<SecondOutputType>())
    {
        output_bindings_.clear();
        output_bindings_.reserve(2);
        output_bindings_.push_back(bind_output<FirstOutputType>(first_output_value_));
        output_bindings_.push_back(bind_output<SecondOutputType>(second_output_value_));
    }

public:
    void step() override
    {
        process(this->template input_value<InputType>(0));
    }

protected:
    virtual void process(const signal_storage_t<InputType>& input) = 0;

    signal_storage_t<FirstOutputType> first_output_value_{};
    signal_storage_t<SecondOutputType> second_output_value_{};
};

/// Convert fixed-point int12 to double.
/// Simulates ADC output conversion (e.g., from AD9361).
class Int12ToDouble final : public TypedUnaryElement<SignalType::INT12, SignalType::DOUBLE> {
public:
    Int12ToDouble()
        : TypedUnaryElement("Int12ToDouble")
    {
    }

private:
    void process(const RealValue& input) override
    {
        const auto raw_value = static_cast<int16_t>(input);
        output_values_[0] = static_cast<RealValue>(raw_value) / 2048.0;
    }
};

/// Convert double to fixed-point int12.
/// Simulates DAC input conversion (e.g., to AD9361).
class DoubleToInt12 final : public TypedUnaryElement<SignalType::DOUBLE, SignalType::INT12> {
public:
    DoubleToInt12()
        : TypedUnaryElement("DoubleToInt12")
    {
    }

private:
    void process(const RealValue& input) override
    {
        const double scaled = input * 2047.0;
        auto quantized = static_cast<int16_t>(scaled);

        if (quantized > 2047) quantized = 2047;
        if (quantized < -2048) quantized = -2048;

        output_values_[0] = static_cast<RealValue>(quantized);
    }
};

/// Output the previous input sample.
class PreviousValue final : public TypedStatefulUnaryElement<SignalType::DOUBLE, SignalType::DOUBLE> {
public:
    explicit PreviousValue(RealValue initial_value = 0.0)
        : TypedStatefulUnaryElement<SignalType::DOUBLE, SignalType::DOUBLE>("PreviousValue"),
          previous_(initial_value)
    {
        output_values_[0] = initial_value;
    }

private:
    void process(const RealValue& input) override
    {
        output_values_[0] = previous_;
        previous_ = input;
    }

    RealValue previous_{0.0};
};

/// Split a complex signal into magnitude and phase outputs.
class ComplexToMagnitudePhase final
    : public TypedSplitElement<SignalType::COMPLEX, SignalType::DOUBLE, SignalType::DOUBLE> {
public:
    ComplexToMagnitudePhase()
        : TypedSplitElement("ComplexToMagnitudePhase", "in", "magnitude", "phase")
    {
    }

private:
    void process(const ComplexValue& input) override
    {
        first_output_value_ = std::abs(input);
        second_output_value_ = std::arg(input);
    }
};

} // namespace s7

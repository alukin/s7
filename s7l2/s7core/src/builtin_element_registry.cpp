#include "s7/builtin_element_registry.hpp"

#include "s7/elements.hpp"
#include "s7/signal_values.hpp"
#include "s7/type_inference.hpp"

#include <stdexcept>
#include <utility>

namespace s7 {
namespace {

ElementMetadata make_simple_metadata(
    const std::string& type_name,
    const std::string& display_name,
    const std::string& description,
    const std::string& category,
    int input_count,
    int output_count)
{
    ElementMetadata metadata;
    metadata.type_name = type_name;
    metadata.display_name = display_name;
    metadata.description = description;
    metadata.category = category;
    metadata.version = "1.0.0";
    metadata.visual = {"box", "#4CAF50", 110, 52, ""};

    for (int i = 0; i < input_count; ++i) {
        PortSpec port;
        port.name = i == 0 ? "in" : "in" + std::to_string(i);
        port.description = "Input";
        port.type = SignalType::F64;
        port.optional = false;
        metadata.input_ports.push_back(port);
    }

    for (int i = 0; i < output_count; ++i) {
        PortSpec port;
        port.name = i == 0 ? "out" : "out" + std::to_string(i);
        port.description = "Output";
        port.type = SignalType::F64;
        port.optional = false;
        metadata.output_ports.push_back(port);
    }

    return metadata;
}

ElementMetadata make_generic_metadata(const std::string& type_name,
                                      const std::string& display_name,
                                      const std::string& description,
                                      const std::string& category,
                                      const int input_count,
                                      const int output_count,
                                      const SignalTypeSet allowed)
{
    auto metadata = make_simple_metadata(type_name, display_name, description, category, input_count, output_count);
    metadata.type_variables.push_back({"T", allowed.types(), "type"});
    for (auto& port : metadata.input_ports) {
        port.type_rule = "T";
    }
    for (auto& port : metadata.output_ports) {
        port.type_rule = "T";
    }
    metadata.parameters.push_back({"type", "type", "Pins the element's type (otherwise inferred from connections)", nullptr, std::nullopt, std::nullopt, {}, false});
    return metadata;
}

void add_overflow_parameter(ElementMetadata& metadata)
{
    metadata.parameters.push_back({"overflow", "enum", "Integer overflow rule", "saturate", std::nullopt, std::nullopt, {"saturate", "wrap"}, false});
}

Overflow overflow_param(const ElementCreationContext& ctx)
{
    return ctx.has_param("overflow") ? overflow_from_name(ctx.get_string("overflow")) : Overflow::Saturate;
}

std::string parameter_label(const ElementCreationContext& ctx, const std::string& name)
{
    return ctx.metadata.type_name + " parameter '" + name + "'";
}

template <SignalType T>
signal_storage_t<T> param_value(const ElementCreationContext& ctx, const std::string& name, const signal_storage_t<T>& fallback)
{
    if (!ctx.has_param(name)) {
        return fallback;
    }
    return signal_value_from_json<T>(ctx.params.at(name), parameter_label(ctx, name));
}

// An array parameter of values of type T; [1, 1] when absent.
template <SignalType T>
std::vector<signal_storage_t<T>> param_values(const ElementCreationContext& ctx, const std::string& name)
{
    if (!ctx.has_param(name)) {
        return {SignalOps<T>::one(), SignalOps<T>::one()};
    }
    const auto* array = ctx.params.at(name).if_array();
    if (!array || array->size() < 2) {
        throw std::invalid_argument(parameter_label(ctx, name) + " must be an array of at least two values.");
    }
    std::vector<signal_storage_t<T>> values;
    for (std::size_t index = 0; index < array->size(); ++index) {
        values.push_back(signal_value_from_json<T>((*array)[index], parameter_label(ctx, name) + "[" + std::to_string(index) + "]"));
    }
    return values;
}

[[noreturn]] std::unique_ptr<Element> unsupported_type(const ElementCreationContext& ctx)
{
    throw std::invalid_argument(ctx.metadata.type_name + " does not support type " +
                                std::string(signal_type_name(ctx.type())) + ".");
}

auto parseRealArrayParam(const boost::json::object& params,
                        const char* key,
                        std::vector<RealValue> fallback) -> std::vector<RealValue>
{
    const auto it = params.find(key);
    if (it == params.end() || !it->value().is_array()) {
        return fallback;
    }

    const auto& values = it->value().as_array();
    if (values.empty()) {
        return fallback;
    }

    std::vector<RealValue> parsed;
    parsed.reserve(values.size());
    for (const auto& value : values) {
        if (value.is_double()) {
            parsed.push_back(value.as_double());
        } else if (value.is_int64()) {
            parsed.push_back(static_cast<RealValue>(value.as_int64()));
        } else if (value.is_uint64()) {
            parsed.push_back(static_cast<RealValue>(value.as_uint64()));
        } else {
            throw std::invalid_argument(std::string("Parameter '") + key + "' must be an array of numbers.");
        }
    }

    return parsed;
}

} // namespace

void register_builtin_elements(ElementRegistry& registry)
{
    // Ignore duplicate registration calls to keep startup idempotent.
    if (registry.find("Constant") != nullptr) {
        return;
    }

    registry.register_element(ElementDescriptor(
        "Constant",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                return std::make_unique<ConstantT<T>>(param_value<T>(ctx, "value", signal_storage_t<T>{}));
            });
        },
        [] {
            auto m = make_generic_metadata("Constant", "Constant", "Constant source", "basic", 0, 1, signal_type_sets::all);
            m.parameters.push_back({"value", "value", "Output value (exact for integer types; [re, im] for complex)", 0, std::nullopt, std::nullopt, {}, false});
            m.visual.shape = "circle";
            m.visual.color = "#66BB6A";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Gain",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (has_arithmetic(T)) {
                    return std::make_unique<GainT<T>>(param_value<T>(ctx, "factor", SignalOps<T>::one()), overflow_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Gain", "Gain", "Multiply input by factor", "basic", 1, 1, signal_type_sets::arithmetic);
            m.parameters.push_back({"factor", "value", "Gain factor, of the element's type", 1, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Adder",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (has_arithmetic(T)) {
                    return std::make_unique<AdderT<T>>(param_values<T>(ctx, "weights"), overflow_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Adder", "Adder", "Weighted sum of N inputs", "basic", 2, 1, signal_type_sets::arithmetic);
            m.parameters.push_back({"weights", "array", "Per-input weights; input count follows this array length", boost::json::array{1, 1}, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            m.input_ports[0].name = "lhs";
            m.input_ports[0].description = "Summation input 0 (weighted by weights[0]).";
            m.input_ports[1].name = "rhs";
            m.input_ports[1].description = "Summation input 1 (weighted by weights[1]).";
            m.output_ports[0].description = "Weighted sum output.";
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::F64, "T"};
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Multiply",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (has_arithmetic(T)) {
                    return std::make_unique<MultiplyT<T>>(param_values<T>(ctx, "factors"), overflow_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Multiply", "Multiply", "Product of N inputs", "basic", 2, 1, signal_type_sets::arithmetic);
            m.parameters.push_back({"factors", "array", "Per-input factors; input count follows this array length", boost::json::array{1, 1}, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            m.input_ports[0].name = "lhs";
            m.input_ports[0].description = "Multiplier input 0 (scaled by factors[0]).";
            m.input_ports[1].name = "rhs";
            m.input_ports[1].description = "Multiplier input 1 (scaled by factors[1]).";
            m.output_ports[0].description = "Product output.";
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::F64, "T"};
            return m;
        }()));

    for (const auto* name : {"Delay", "PreviousValue"}) {
        registry.register_element(ElementDescriptor(
            name,
            [name](const ElementCreationContext& ctx) {
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    return std::make_unique<DelayT<T>>(param_value<T>(ctx, "initial_value", signal_storage_t<T>{}), name);
                });
            },
            [name] {
                auto m = make_generic_metadata(name, name, "Unit delay: outputs the previous input", "filters", 1, 1, signal_type_sets::all);
                m.parameters.push_back({"initial_value", "value", "Output before the first input", 0, std::nullopt, std::nullopt, {}, false});
                return m;
            }()));
    }

    registry.register_element(ElementDescriptor(
        "Integrator",
        [](const ElementCreationContext& ctx) {
            const double initial_value = ctx.has_param("initial_value") ? ctx.get_real("initial_value") : 0.0;
            return std::make_unique<Integrator>(initial_value);
        },
        [] {
            auto m = make_simple_metadata("Integrator", "Integrator", "Accumulate input", "filters", 1, 1);
            m.parameters.push_back({"initial_value", "real", "Initial output value", 0.0, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "AnalogOscillator",
        [](const ElementCreationContext& ctx) {
            const double amplitude = ctx.has_param("amplitude") ? ctx.get_real("amplitude") : 1.0;
            const double frequency_hz = ctx.has_param("frequency_hz") ? ctx.get_real("frequency_hz") : 1.0;
            const double sample_rate_hz = ctx.has_param("sample_rate_hz") ? ctx.get_real("sample_rate_hz") : 1.0;
            const double phase = ctx.has_param("phase") ? ctx.get_real("phase") : 0.0;
            return std::make_unique<AnalogOscillator>(amplitude, frequency_hz, sample_rate_hz, phase);
        },
        [] {
            auto m = make_simple_metadata("AnalogOscillator", "Analog Oscillator", "Sinusoidal source", "sources", 0, 1);
            m.parameters.push_back({"amplitude", "real", "Signal amplitude", 1.0, 0.0, std::nullopt, {}, false});
            m.parameters.push_back({"frequency_hz", "real", "Frequency in Hz", 1.0, 0.0, std::nullopt, {}, false});
            m.parameters.push_back({"sample_rate_hz", "real", "Sample rate in Hz", 1.0, 0.0, std::nullopt, {}, false});
            m.parameters.push_back({"phase", "real", "Initial phase", 0.0, std::nullopt, std::nullopt, {}, false});
            m.visual.shape = "circle";
            m.visual.color = "#42A5F5";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "FirFilter",
        [](const ElementCreationContext& ctx) {
            auto taps = parseRealArrayParam(ctx.params, "taps", {1.0});
            return std::make_unique<FirFilter>(std::move(taps));
        },
        [] {
            auto m = make_simple_metadata("FirFilter", "FIR Filter", "Finite impulse response filter", "filters", 1, 1);
            m.parameters.push_back({"taps", "array", "Filter taps; at least one value", boost::json::array{1.0}, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].description = "Input signal sample.";
            m.output_ports[0].description = "Filtered output sample.";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "IirFilter",
        [](const ElementCreationContext& ctx) {
            const double b0 = ctx.has_param("b0") ? ctx.get_real("b0") : 1.0;
            const double b1 = ctx.has_param("b1") ? ctx.get_real("b1") : 0.0;
            const double a1 = ctx.has_param("a1") ? ctx.get_real("a1") : 0.0;
            return std::make_unique<IirFilter>(b0, b1, a1);
        },
        [] {
            auto m = make_simple_metadata("IirFilter", "IIR Filter", "First-order recursive filter", "filters", 1, 1);
            m.parameters.push_back({"b0", "real", "Feedforward coefficient b0", 1.0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"b1", "real", "Feedforward coefficient b1", 0.0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"a1", "real", "Feedback coefficient a1", 0.0, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].description = "Input signal sample.";
            m.output_ports[0].description = "Filtered output sample.";
            return m;
        }()));


    registry.register_element(ElementDescriptor(
        "ReImToComplex",
        [](const ElementCreationContext&) {
            return std::make_unique<ReImToComplex>();
        },
        [] {
            auto m = make_simple_metadata("ReImToComplex", "Re/Im to Complex", "Combine real and imaginary channels", "conversion", 2, 1);
            m.input_ports[0].name = "re";
            m.input_ports[0].description = "Real part.";
            m.input_ports[0].type = SignalType::F64;
            m.input_ports[1].name = "im";
            m.input_ports[1].description = "Imaginary part.";
            m.input_ports[1].type = SignalType::F64;
            m.output_ports[0].description = "Complex output.";
            m.output_ports[0].type = SignalType::C64;
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "ComplexToReIm",
        [](const ElementCreationContext&) {
            return std::make_unique<ComplexToReIm>();
        },
        [] {
            auto m = make_simple_metadata("ComplexToReIm", "Complex to Re/Im", "Split complex input into real and imaginary channels", "conversion", 1, 2);
            m.input_ports[0].name = "in";
            m.input_ports[0].description = "Complex input.";
            m.input_ports[0].type = SignalType::C64;
            m.output_ports[0].name = "re";
            m.output_ports[0].description = "Real output.";
            m.output_ports[0].type = SignalType::F64;
            m.output_ports[1].name = "im";
            m.output_ports[1].description = "Imaginary output.";
            m.output_ports[1].type = SignalType::F64;
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "ComplexToMagnitudePhase",
        [](const ElementCreationContext&) {
            return std::make_unique<ComplexToMagnitudePhase>();
        },
        [] {
            auto m = make_simple_metadata("ComplexToMagnitudePhase", "Complex to Magnitude/Phase", "Convert complex input to magnitude and phase", "conversion", 1, 2);
            m.input_ports[0].name = "in";
            m.input_ports[0].description = "Complex input.";
            m.input_ports[0].type = SignalType::C64;
            m.output_ports[0].name = "magnitude";
            m.output_ports[0].description = "Magnitude output.";
            m.output_ports[0].type = SignalType::F64;
            m.output_ports[1].name = "phase";
            m.output_ports[1].description = "Phase output (radians).";
            m.output_ports[1].type = SignalType::F64;
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Int12ToDouble",
        [](const ElementCreationContext&) {
            return std::make_unique<Int12ToDouble>();
        },
        [] {
            auto m = make_simple_metadata("Int12ToDouble", "Int12 to Double", "Convert INT12 code to DOUBLE (code / 2048)", "conversion", 1, 1);
            m.input_ports[0].name = "in";
            m.input_ports[0].description = "INT12 input sample.";
            m.input_ports[0].type = SignalType::I12;
            m.output_ports[0].description = "Converted DOUBLE output.";
            m.output_ports[0].type = SignalType::F64;
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "DoubleToInt12",
        [](const ElementCreationContext&) {
            return std::make_unique<DoubleToInt12>();
        },
        [] {
            auto m = make_simple_metadata("DoubleToInt12", "Double to Int12", "Quantize DOUBLE to INT12 (x * 2048, round half to even, saturate)", "conversion", 1, 1);
            m.input_ports[0].name = "in";
            m.input_ports[0].description = "DOUBLE input sample.";
            m.input_ports[0].type = SignalType::F64;
            m.output_ports[0].description = "Converted INT12 output.";
            m.output_ports[0].type = SignalType::I12;
            return m;
        }()));
}

} // namespace s7


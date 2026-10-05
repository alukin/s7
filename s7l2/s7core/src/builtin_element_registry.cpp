#include "s7/builtin_element_registry.hpp"

#include "s7/convert.hpp"
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

// An array parameter of at least min_size values of type T.
template <SignalType T>
std::vector<signal_storage_t<T>> param_array(const ElementCreationContext& ctx, const std::string& name, const std::size_t min_size)
{
    const auto* array = ctx.params.at(name).if_array();
    if (!array || array->size() < min_size) {
        throw std::invalid_argument(parameter_label(ctx, name) + " must be an array of at least " + std::to_string(min_size) +
                                    " value(s).");
    }
    std::vector<signal_storage_t<T>> values;
    for (std::size_t index = 0; index < array->size(); ++index) {
        values.push_back(signal_value_from_json<T>((*array)[index], parameter_label(ctx, name) + "[" + std::to_string(index) + "]"));
    }
    return values;
}

// Per-input weights/factors: [1, 1] when absent, at least two values.
template <SignalType T>
std::vector<signal_storage_t<T>> param_values(const ElementCreationContext& ctx, const std::string& name)
{
    if (!ctx.has_param(name)) {
        return {SignalOps<T>::one(), SignalOps<T>::one()};
    }
    return param_array<T>(ctx, name, 2);
}

// Real and complex floating-point types.
SignalTypeSet floating_types()
{
    return SignalTypeSet::where([](const SignalType type) { return signal_type_info(type).is_floating(); });
}

[[noreturn]] std::unique_ptr<Element> unsupported_type(const ElementCreationContext& ctx)
{
    throw std::invalid_argument(ctx.metadata.type_name + " does not support type " +
                                std::string(signal_type_name(ctx.type())) + ".");
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
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (has_arithmetic(T)) {
                    return std::make_unique<IntegratorT<T>>(param_value<T>(ctx, "initial_value", {}), overflow_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Integrator", "Integrator (running sum)",
                                           "Running sum y[n] = y[n-1] + x[n], no time step; see EulerIntegrator / TrapezoidIntegrator",
                                           "filters", 1, 1, signal_type_sets::arithmetic);
            m.parameters.push_back({"initial_value", "value", "Initial output value", 0, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "EulerIntegrator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_floating()) {
                    return std::make_unique<EulerIntegratorT<T>>(
                        param_value<component_signal_type(T)>(ctx, "dt", component_storage_t<T>(1)),
                        param_value<T>(ctx, "initial_value", {}));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("EulerIntegrator", "Euler Integrator",
                                           "Forward Euler (rectangle rule): y[n+1] = y[n] + dt*x[n]; causal memory, can close loops",
                                           "filters", 1, 1, floating_types());
            m.parameters.push_back({"dt", "value", "Model time per step", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"initial_value", "value", "y[0]", 0, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "TrapezoidIntegrator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_floating()) {
                    return std::make_unique<TrapezoidIntegratorT<T>>(
                        param_value<component_signal_type(T)>(ctx, "dt", component_storage_t<T>(1)),
                        param_value<T>(ctx, "initial_value", {}));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("TrapezoidIntegrator", "Trapezoid Integrator",
                                           "Trapezoidal rule: y[n] = y[n-1] + dt/2*(x[n] + x[n-1]); direct feedthrough",
                                           "filters", 1, 1, floating_types());
            m.parameters.push_back({"dt", "value", "Model time per step", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"initial_value", "value", "y[0]", 0, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "AnalogOscillator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    using V = signal_storage_t<T>;
                    return std::make_unique<AnalogOscillatorT<T>>(param_value<T>(ctx, "amplitude", V(1)),
                                                                  param_value<T>(ctx, "frequency_hz", V(1)),
                                                                  param_value<T>(ctx, "sample_rate_hz", V(1)),
                                                                  param_value<T>(ctx, "phase", V(0)));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("AnalogOscillator", "Analog Oscillator", "Sinusoidal source, computed in its type",
                                           "sources", 0, 1, signal_type_sets::floating);
            m.parameters.push_back({"amplitude", "value", "Signal amplitude", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"frequency_hz", "value", "Frequency in Hz", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"sample_rate_hz", "value", "Sample rate in Hz", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"phase", "value", "Initial phase (radians)", 0, std::nullopt, std::nullopt, {}, false});
            m.visual.shape = "circle";
            m.visual.color = "#42A5F5";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "FirFilter",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (has_arithmetic(T)) {
                    auto taps = ctx.has_param("taps") ? param_array<T>(ctx, "taps", 1)
                                                      : std::vector<signal_storage_t<T>>{SignalOps<T>::one()};
                    return std::make_unique<FirFilterT<T>>(std::move(taps), overflow_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("FirFilter", "FIR Filter", "Finite impulse response filter", "filters", 1, 1,
                                           signal_type_sets::arithmetic);
            m.parameters.push_back({"taps", "array", "Filter taps, of the element's type; at least one", boost::json::array{1}, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            m.input_ports[0].description = "Input signal sample.";
            m.output_ports[0].description = "Filtered output sample.";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "IirFilter",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_floating()) {
                    using V = signal_storage_t<T>;
                    return std::make_unique<IirFilterT<T>>(param_value<T>(ctx, "b0", V(1)), param_value<T>(ctx, "b1", V(0)),
                                                           param_value<T>(ctx, "a1", V(0)));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("IirFilter", "IIR Filter", "First-order recursive filter", "filters", 1, 1, floating_types());
            m.parameters.push_back({"b0", "value", "Feedforward coefficient b0", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"b1", "value", "Feedforward coefficient b1", 0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"a1", "value", "Feedback coefficient a1", 0, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].description = "Input signal sample.";
            m.output_ports[0].description = "Filtered output sample.";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "ReImToComplex",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_complex()) {
                    return std::make_unique<ReImToComplexT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("ReImToComplex", "Re/Im to Complex", "Combine real and imaginary channels", "conversion", 2, 1,
                                           signal_type_sets::complex_all);
            m.input_ports[0].name = "re";
            m.input_ports[0].description = "Real part.";
            m.input_ports[0].type_rule = "component(T)";
            m.input_ports[1].name = "im";
            m.input_ports[1].description = "Imaginary part.";
            m.input_ports[1].type_rule = "component(T)";
            m.output_ports[0].description = "Complex output.";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "ComplexToReIm",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_complex()) {
                    return std::make_unique<ComplexToReImT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("ComplexToReIm", "Complex to Re/Im", "Split complex input into real and imaginary channels",
                                           "conversion", 1, 2, signal_type_sets::complex_all);
            m.input_ports[0].description = "Complex input.";
            m.output_ports[0].name = "re";
            m.output_ports[0].description = "Real output.";
            m.output_ports[0].type_rule = "component(T)";
            m.output_ports[1].name = "im";
            m.output_ports[1].description = "Imaginary output.";
            m.output_ports[1].type_rule = "component(T)";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "ComplexToMagnitudePhase",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::ComplexFloat) {
                    return std::make_unique<ComplexToMagnitudePhaseT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("ComplexToMagnitudePhase", "Complex to Magnitude/Phase",
                                           "Magnitude and phase of a complex input, in its component type", "conversion", 1, 2,
                                           signal_type_sets::complex_floating);
            m.input_ports[0].description = "Complex input.";
            m.output_ports[0].name = "magnitude";
            m.output_ports[0].description = "Magnitude output.";
            m.output_ports[0].type_rule = "component(T)";
            m.output_ports[1].name = "phase";
            m.output_ports[1].description = "Phase output (radians).";
            m.output_ports[1].type_rule = "component(T)";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Convert",
        [](const ElementCreationContext& ctx) {
            if (!ctx.has_param("mode")) {
                throw std::invalid_argument("Convert needs an explicit 'mode': 'value' or 'normalized'.");
            }
            ConvertOptions options;
            options.mode = convert_mode_from_name(ctx.get_string("mode"));
            if (ctx.has_param("rounding")) {
                options.rounding = convert_rounding_from_name(ctx.get_string("rounding"));
            }
            if (ctx.has_param("overflow")) {
                options.overflow = convert_overflow_from_name(ctx.get_string("overflow"));
            }
            return make_convert(ctx.type("from"), ctx.type("to"), options);
        },
        [] {
            auto m = make_simple_metadata("Convert", "Convert",
                                          "Explicit type conversion (value or normalized), with stated rounding and overflow",
                                          "conversion", 1, 1);
            m.type_variables.push_back({"from", signal_type_sets::all.types(), "from"});
            m.type_variables.push_back({"to", signal_type_sets::all.types(), "to"});
            m.input_ports[0].type_rule = "from";
            m.output_ports[0].type_rule = "to";
            m.parameters.push_back({"from", "type", "Input type (otherwise inferred)", nullptr, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"to", "type", "Output type (otherwise inferred)", nullptr, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"mode", "enum", "value: keep the number; normalized: fixed-point scaling", "value", std::nullopt, std::nullopt, {"value", "normalized"}, true});
            m.parameters.push_back({"rounding", "enum", "Rounding of inexact results", "half_even", std::nullopt, std::nullopt, {"half_even", "toward_zero"}, false});
            m.parameters.push_back({"overflow", "enum", "Out-of-range values", "saturate", std::nullopt, std::nullopt, {"saturate", "fault"}, false});
            return m;
        }()));
}

} // namespace s7


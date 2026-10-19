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

// Real floating-point types only, no complex component (Trig: s7::math has no
// complex-analytic sin/cos/... yet, s7/float_types.hpp).
SignalTypeSet real_floating_types()
{
    return SignalTypeSet::where([](const SignalType type) { return signal_type_info(type).kind == SignalKind::Float; });
}

TrigFunction trig_function_param(const ElementCreationContext& ctx)
{
    return ctx.has_param("function") ? trig_function_from_name(ctx.get_string("function")) : TrigFunction::Sin;
}

MathFunctionOperation math_function_param(const ElementCreationContext& ctx)
{
    const auto function = ctx.get_string("function");
    if (function == "sqrt") return MathFunctionOperation::Sqrt;
    if (function == "exp") return MathFunctionOperation::Exp;
    if (function == "log") return MathFunctionOperation::Log;
    if (function == "pow") return MathFunctionOperation::Pow;
    if (function == "abs") return MathFunctionOperation::Abs;
    if (function == "reciprocal") return MathFunctionOperation::Reciprocal;
    throw std::invalid_argument("MathFunction function must be sqrt, exp, log, pow, abs or reciprocal.");
}

bool minmax_is_maximum(const ElementCreationContext& ctx)
{
    const auto operation = ctx.get_string("operation");
    if (operation == "max") return true;
    if (operation == "min") return false;
    throw std::invalid_argument("MinMax operation must be min or max.");
}

ComparatorOperation comparator_operation_param(const ElementCreationContext& ctx)
{
    const auto operation = ctx.get_string("operation");
    if (operation == "lt") return ComparatorOperation::Less;
    if (operation == "le") return ComparatorOperation::LessEqual;
    if (operation == "gt") return ComparatorOperation::Greater;
    if (operation == "ge") return ComparatorOperation::GreaterEqual;
    if (operation == "eq") return ComparatorOperation::Equal;
    if (operation == "ne") return ComparatorOperation::NotEqual;
    throw std::invalid_argument("Comparator operation must be one of lt, le, gt, ge, eq, ne.");
}

// Accumulating elements: input type T and accumulator type A (A defaults to T).
void add_accumulator_variable(ElementMetadata& metadata)
{
    metadata.type_variables.push_back({"A", signal_type_sets::arithmetic.types(), "accumulator_type", "T"});
    for (auto& port : metadata.output_ports) {
        port.type_rule = "A";
    }
    metadata.parameters.push_back({"accumulator_type", "type",
                                   "Type of the arithmetic and the output; must hold every input value exactly (default: the input type)",
                                   nullptr, std::nullopt, std::nullopt, {}, false});
}

template <typename Make>
std::unique_ptr<Element> make_accumulating(const ElementCreationContext& ctx, Make&& make)
{
    return visit_signal_type(ctx.type("T"), [&]<SignalType T>() -> std::unique_ptr<Element> {
        return visit_signal_type(ctx.type("A"), [&]<SignalType A>() -> std::unique_ptr<Element> {
            if constexpr (has_arithmetic(T) && has_arithmetic(A) && widens_exactly(T, A)) {
                return make.template operator()<T, A>();
            } else {
                throw std::invalid_argument(ctx.metadata.type_name + ": accumulator_type " + std::string(signal_type_name(A)) +
                                            " cannot hold every " + std::string(signal_type_name(T)) + " value exactly.");
            }
        });
    });
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
        "Divider",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    return std::make_unique<DividerT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Divider", "Divider", "Divide numerator by denominator", "basic", 2, 1,
                                           real_floating_types());
            m.input_ports[0].name = "numerator";
            m.input_ports[1].name = "denominator";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "MathFunction",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    return std::make_unique<MathFunctionT<T>>(math_function_param(ctx),
                                                              param_value<T>(ctx, "exponent", signal_storage_t<T>{2}));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("MathFunction", "Math Function", "Apply a scalar math function", "basic", 1, 1,
                                           real_floating_types());
            m.parameters.push_back({"function", "enum", "Function applied to the input", "sqrt", std::nullopt,
                                    std::nullopt, {"sqrt", "exp", "log", "pow", "abs", "reciprocal"}, false});
            m.parameters.push_back({"exponent", "value", "Exponent used when function is pow", 2, std::nullopt,
                                    std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Power",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    return std::make_unique<PowerT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Power", "Power", "Raise the base input to the exponent input", "basic", 2, 1,
                                           real_floating_types());
            m.input_ports[0].name = "base";
            m.input_ports[1].name = "exponent";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Saturation",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (!signal_type_info(T).is_complex()) {
                    return std::make_unique<SaturationT<T>>(param_value<T>(ctx, "lower_limit", signal_storage_t<T>{}),
                                                           param_value<T>(ctx, "upper_limit", signal_storage_t<T>{1}));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Saturation", "Saturation", "Clamp an input between inclusive limits", "basic", 1, 1,
                                           signal_type_sets::real);
            m.parameters.push_back({"lower_limit", "value", "Inclusive lower output limit", 0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"upper_limit", "value", "Inclusive upper output limit", 1, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "MinMax",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (!signal_type_info(T).is_complex()) {
                    return std::make_unique<MinMaxT<T>>(static_cast<std::size_t>(ctx.get_int("input_count")),
                                                        minmax_is_maximum(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("MinMax", "MinMax", "Select the minimum or maximum of its inputs", "basic", 2, 1,
                                           signal_type_sets::real);
            m.parameters.push_back({"operation", "enum", "Select minimum or maximum", "min", std::nullopt, std::nullopt,
                                    {"min", "max"}, false});
            m.parameters.push_back({"input_count", "int", "Number of input ports", 2, 2, 64, {}, false});
            m.input_ports[0].name = "in0";
            m.input_ports[1].name = "in1";
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::F64, "T", "input_count"};
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Sign",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (!signal_type_info(T).is_complex()) {
                    return std::make_unique<SignT<T>>();
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            return make_generic_metadata("Sign", "Sign", "Return -1, 0 or 1 according to the input sign", "basic", 1, 1,
                                         signal_type_sets::real);
        }()));

    registry.register_element(ElementDescriptor(
        "Comparator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (!signal_type_info(T).is_complex()) {
                    return std::make_unique<ComparatorT<T>>(comparator_operation_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Comparator", "Comparator",
                                           "Output 1 if lhs 'operation' rhs holds, else 0", "basic", 2, 1,
                                           signal_type_sets::real);
            m.parameters.push_back({"operation", "enum", "Relational operation applied to lhs and rhs", "lt", std::nullopt,
                                    std::nullopt, {"lt", "le", "gt", "ge", "eq", "ne"}, false});
            m.input_ports[0].name = "lhs";
            m.input_ports[1].name = "rhs";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Adder",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<AdderT<T, A>>(param_values<A>(ctx, "weights"), overflow_param(ctx));
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
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::F64, "T", "weights"};
            add_accumulator_variable(m);
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Multiply",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<MultiplyT<T, A>>(param_values<A>(ctx, "factors"), overflow_param(ctx));
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
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::F64, "T", "factors"};
            add_accumulator_variable(m);
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
        "Accumulator",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<AccumulatorT<T, A>>(param_value<A>(ctx, "initial_value", {}), overflow_param(ctx));
            });
        },
        [] {
            auto m = make_generic_metadata("Accumulator", "Accumulator (running sum)",
                                           "Running sum y[n] = y[n-1] + x[n], no time step; see EulerIntegrator / TrapezoidIntegrator",
                                           "filters", 1, 1, signal_type_sets::arithmetic);
            m.parameters.push_back({"initial_value", "value", "Initial output value", 0, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            add_accumulator_variable(m);
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
                        param_value<T>(ctx, "initial_value", {}),
                        ctx.get_string("mode") == "sampled" ? TrapezoidMode::Sampled : TrapezoidMode::Implicit);
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("TrapezoidIntegrator", "Trapezoid Integrator",
                                           "Implicit trapezoidal ODE step, solved jointly across feedback states; sampled mode integrates supplied samples",
                                           "filters", 1, 1, floating_types());
            m.parameters.push_back({"dt", "value", "Model time per step", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"initial_value", "value", "y[0]", 0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"mode", "enum", "Implicit feedback solve or supplied-sample quadrature",
                                    "implicit", std::nullopt, std::nullopt, {"implicit", "sampled"}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "SymplecticIntegrator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).is_floating()) {
                    return std::make_unique<SymplecticIntegratorT<T>>(
                        param_value<component_signal_type(T)>(ctx, "dt", component_storage_t<T>(1)),
                        param_value<T>(ctx, "initial_position", {}), param_value<T>(ctx, "initial_velocity", {}));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata(
                "SymplecticIntegrator", "Symplectic Integrator (semi-implicit Euler)",
                "Semi-implicit Euler for x'' = acceleration: v[n+1] = v[n] + dt*a[n], x[n+1] = x[n] + dt*v[n+1]; "
                "bounds a conservative oscillator's energy instead of letting it drift like EulerIntegrator",
                "filters", 1, 2, floating_types());
            m.parameters.push_back({"dt", "value", "Model time per step", 1, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"initial_position", "value", "x[0]", 0, std::nullopt, std::nullopt, {}, false});
            m.parameters.push_back({"initial_velocity", "value", "v[0]", 0, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].name = "acceleration";
            m.input_ports[0].description = "Acceleration/force input, a(x) computed by the surrounding graph; a "
                                           "velocity dependency (damping) loses the symplectic energy bound.";
            m.output_ports[0].name = "position";
            m.output_ports[0].description = "Position output x[n].";
            m.output_ports[1].name = "velocity";
            m.output_ports[1].description = "Velocity output v[n].";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Trig",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    return std::make_unique<TrigT<T>>(trig_function_param(ctx));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        [] {
            auto m = make_generic_metadata("Trig", "Trigonometric Function",
                                           "y = f(x): sin, cos, tan, asin, acos, atan, sinh, cosh or tanh of an arbitrary input",
                                           "filters", 1, 1, real_floating_types());
            m.parameters.push_back({"function", "enum", "Which function to apply", "sin", std::nullopt, std::nullopt,
                                    {"sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh"}, false});
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
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                auto taps = ctx.has_param("taps") ? param_array<A>(ctx, "taps", 1)
                                                  : std::vector<signal_storage_t<A>>{SignalOps<A>::one()};
                return std::make_unique<FirFilterT<T, A>>(std::move(taps), overflow_param(ctx));
            });
        },
        [] {
            auto m = make_generic_metadata("FirFilter", "FIR Filter", "Finite impulse response filter", "filters", 1, 1,
                                           signal_type_sets::arithmetic);
            m.parameters.push_back({"taps", "array", "Filter taps, of the element's type; at least one", boost::json::array{1}, std::nullopt, std::nullopt, {}, false});
            add_overflow_parameter(m);
            m.input_ports[0].description = "Input signal sample.";
            m.output_ports[0].description = "Filtered output sample.";
            add_accumulator_variable(m);
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


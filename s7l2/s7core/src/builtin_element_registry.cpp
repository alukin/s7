// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/builtin_element_registry.hpp"

#include "s7/convert.hpp"
#include "s7/elements.hpp"
#include "s7/schema_library.hpp"
#include "s7_schema_elements.hpp"
#include "s7/metadata_json.hpp"
#include "core_metadata.hpp"
#include "s7/signal_values.hpp"
#include "s7/type_inference.hpp"

#include <stdexcept>
#include <utility>

namespace s7 {
namespace {

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

    static const ElementMetadataLibrary metadata(resources::core_metadata);

    registry.register_element(ElementDescriptor(
        "Constant",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                return std::make_unique<ConstantT<T>>(param_value<T>(ctx, "value", signal_storage_t<T>{}));
            });
        },
        metadata.at("Constant")));

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
        metadata.at("Gain")));

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
        metadata.at("Divider")));

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
        metadata.at("MathFunction")));

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
        metadata.at("Power")));

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
        metadata.at("Saturation")));

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
        metadata.at("MinMax")));

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
        metadata.at("Sign")));

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
        metadata.at("Comparator")));

    registry.register_element(ElementDescriptor(
        "Adder",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<AdderT<T, A>>(param_values<A>(ctx, "weights"), overflow_param(ctx));
            });
        },
        metadata.at("Adder")));

    registry.register_element(ElementDescriptor(
        "Multiply",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<MultiplyT<T, A>>(param_values<A>(ctx, "factors"), overflow_param(ctx));
            });
        },
        metadata.at("Multiply")));

    for (const auto* name : {"Delay", "PreviousValue"}) {
        registry.register_element(ElementDescriptor(
            name,
            [name](const ElementCreationContext& ctx) {
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    return std::make_unique<DelayT<T>>(param_value<T>(ctx, "initial_value", signal_storage_t<T>{}), name);
                });
            },
            metadata.at(name)));
    }

    registry.register_element(ElementDescriptor(
        "Accumulator",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                return std::make_unique<AccumulatorT<T, A>>(param_value<A>(ctx, "initial_value", {}), overflow_param(ctx));
            });
        },
        metadata.at("Accumulator")));

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
        metadata.at("EulerIntegrator")));

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
        metadata.at("TrapezoidIntegrator")));

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
        metadata.at("SymplecticIntegrator")));

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
        metadata.at("Trig")));

    registry.register_element(ElementDescriptor(
        "ChirpGenerator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    using V = signal_storage_t<T>;
                    return std::make_unique<ChirpGeneratorT<T>>(
                        param_value<T>(ctx, "start_hz", V(1)), param_value<T>(ctx, "end_hz", V(100)),
                        static_cast<std::uint64_t>(ctx.get_int("sweep_samples")), ctx.get_real("sample_rate_hz"),
                        param_value<T>(ctx, "amplitude", V(1)), param_value<T>(ctx, "offset", V(0)),
                        param_value<T>(ctx, "phase", V(0)), ctx.get_string("sweep") == "linear" ? ChirpSweep::Linear : ChirpSweep::Logarithmic,
                        ctx.get_string("end_behavior") == "repeat");
                } else return unsupported_type(ctx);
            });
        },
        metadata.at("ChirpGenerator")));

    registry.register_element(ElementDescriptor(
        "PRBSGenerator",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    using V = signal_storage_t<T>;
                    return std::make_unique<PrbsGeneratorT<T>>(
                        static_cast<unsigned>(ctx.get_int("order")), static_cast<std::uint32_t>(ctx.get_int("seed")),
                        static_cast<std::uint64_t>(ctx.get_int("bit_samples")), ctx.get_real("sample_rate_hz"),
                        param_value<T>(ctx, "low", V(-1)), param_value<T>(ctx, "high", V(1)));
                } else return unsupported_type(ctx);
            });
        },
        metadata.at("PRBSGenerator")));

    for (const std::string name : {"TriangleGenerator", "PulseGenerator", "ImpulseGenerator",
                                   "StepGenerator", "ControlledOscillator"}) {
        registry.register_element(ElementDescriptor(
            name,
            [name](const ElementCreationContext& ctx) {
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                        using V = signal_storage_t<T>;
                        const double rate = ctx.get_real("sample_rate_hz");
                        if (name == "TriangleGenerator")
                            return std::make_unique<TriangleGeneratorT<T>>(
                                param_value<T>(ctx, "amplitude", V(1)), param_value<T>(ctx, "offset", V(0)),
                                param_value<T>(ctx, "frequency_hz", V(1)), rate,
                                ctx.get_real("phase"), ctx.get_real("rise_fraction"));
                        if (name == "PulseGenerator")
                            return std::make_unique<PulseGeneratorT<T>>(
                                param_value<T>(ctx, "low", V(0)), param_value<T>(ctx, "high", V(1)),
                                param_value<T>(ctx, "frequency_hz", V(1)), rate,
                                ctx.get_real("duty"), ctx.get_real("phase"));
                        if (name == "ImpulseGenerator")
                            return std::make_unique<ImpulseGeneratorT<T>>(
                                param_value<T>(ctx, "area", V(1)), rate,
                                static_cast<std::uint64_t>(ctx.get_int("start_sample")));
                        if (name == "StepGenerator")
                            return std::make_unique<StepGeneratorT<T>>(
                                param_value<T>(ctx, "initial_value", V(0)), param_value<T>(ctx, "final_value", V(1)),
                                rate, static_cast<std::uint64_t>(ctx.get_int("start_sample")));
                        return std::make_unique<ControlledOscillatorT<T>>(
                            param_value<T>(ctx, "amplitude", V(1)), param_value<T>(ctx, "offset", V(0)),
                            rate, param_value<T>(ctx, "phase", V(0)));
                    } else {
                        return unsupported_type(ctx);
                    }
                });
            },
            metadata.at(name)));
    }

    for (const std::string name : {"Switch", "HysteresisRelay", "ControlledIntegrator", "PhaseWrap", "FrequencyMeter"}) {
        registry.register_element(ElementDescriptor(name,
            [name](const ElementCreationContext& ctx) {
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    using V = signal_storage_t<T>;
                    if constexpr (!signal_type_info(T).is_complex()) {
                        if (name == "Switch") return std::make_unique<SwitchT<T>>();
                        if (name == "HysteresisRelay") return std::make_unique<HysteresisRelayT<T>>(
                            param_value<T>(ctx,"lower_threshold",V(-1)), param_value<T>(ctx,"upper_threshold",V(1)),
                            param_value<T>(ctx,"low_value",V(0)), param_value<T>(ctx,"high_value",V(1)),
                            ctx.params.at("initial_on").as_bool());
                        if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                            if (name == "ControlledIntegrator") return std::make_unique<ControlledIntegratorT<T>>(
                                param_value<T>(ctx,"dt",V(0.001)), param_value<T>(ctx,"initial_value",V(0)),
                                param_value<T>(ctx,"lower_limit",std::numeric_limits<V>::lowest()),
                                param_value<T>(ctx,"upper_limit",std::numeric_limits<V>::max()),
                                ctx.get_string("reset_mode")=="rising" ? IntegratorReset::Rising : IntegratorReset::Level);
                            if (name == "PhaseWrap") return std::make_unique<PhaseWrapT<T>>(
                                param_value<T>(ctx,"period",V(2)*math::pi_v<V>), param_value<T>(ctx,"lower",-math::pi_v<V>));
                            return std::make_unique<FrequencyMeterT<T>>(ctx.get_real("sample_rate_hz"));
                        }
                    }
                    return unsupported_type(ctx);
                });
            },
            metadata.at(name)));
    }

    registry.register_element(ElementDescriptor(
        "PWM",
        [](const ElementCreationContext& ctx) {
            return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                if constexpr (signal_type_info(T).kind == SignalKind::Float) {
                    return std::make_unique<PwmT<T>>(static_cast<std::size_t>(ctx.get_int("period_samples")),
                                                     ctx.get_real("sample_rate_hz"));
                } else {
                    return unsupported_type(ctx);
                }
            });
        },
        metadata.at("PWM")));

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
        metadata.at("AnalogOscillator")));

    registry.register_element(ElementDescriptor(
        "FirFilter",
        [](const ElementCreationContext& ctx) {
            return make_accumulating(ctx, [&]<SignalType T, SignalType A>() -> std::unique_ptr<Element> {
                auto taps = ctx.has_param("taps") ? param_array<A>(ctx, "taps", 1)
                                                  : std::vector<signal_storage_t<A>>{SignalOps<A>::one()};
                return std::make_unique<FirFilterT<T, A>>(std::move(taps), overflow_param(ctx));
            });
        },
        metadata.at("FirFilter")));

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
        metadata.at("IirFilter")));

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
        metadata.at("ReImToComplex")));

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
        metadata.at("ComplexToReIm")));

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
        metadata.at("ComplexToMagnitudePhase")));

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
        metadata.at("Convert")));
    SchemaLibraryLoader::register_library(SchemaLibraryLoader::load_from_json(
        boost::json::parse(detail::builtin_schema_resource)), registry);

}

} // namespace s7


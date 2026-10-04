#include "s7/builtin_element_registry.hpp"

#include "s7/elements.hpp"

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
        port.type = SignalType::DOUBLE;
        port.optional = false;
        metadata.input_ports.push_back(port);
    }

    for (int i = 0; i < output_count; ++i) {
        PortSpec port;
        port.name = i == 0 ? "out" : "out" + std::to_string(i);
        port.description = "Output";
        port.type = SignalType::DOUBLE;
        port.optional = false;
        metadata.output_ports.push_back(port);
    }

    return metadata;
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
            const double value = ctx.has_param("value") ? ctx.get_real("value") : 0.0;
            return std::make_unique<Constant>(value);
        },
        [] {
            auto m = make_simple_metadata("Constant", "Constant", "Constant source", "basic", 0, 1);
            m.parameters.push_back({"value", "real", "Constant output value", 0.0, std::nullopt, std::nullopt, {}, false});
            m.visual.shape = "circle";
            m.visual.color = "#66BB6A";
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Gain",
        [](const ElementCreationContext& ctx) {
            const double factor = ctx.has_param("factor") ? ctx.get_real("factor") : 1.0;
            return std::make_unique<Gain>(factor);
        },
        [] {
            auto m = make_simple_metadata("Gain", "Gain", "Multiply input by factor", "basic", 1, 1);
            m.parameters.push_back({"factor", "real", "Gain factor", 1.0, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Adder",
        [](const ElementCreationContext& ctx) {
            auto weights = parseRealArrayParam(ctx.params, "weights", {1.0, 1.0});
            return std::make_unique<Adder>(std::move(weights));
        },
        [] {
            auto m = make_simple_metadata("Adder", "Adder", "Weighted sum of N inputs", "basic", 2, 1);
            m.parameters.push_back({"weights", "array", "Per-input weights; input count follows this array length", boost::json::array{1.0, 1.0}, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].name = "lhs";
            m.input_ports[0].description = "Summation input 0 (weighted by weights[0]).";
            m.input_ports[1].name = "rhs";
            m.input_ports[1].description = "Summation input 1 (weighted by weights[1]).";
            m.output_ports[0].description = "Weighted sum output.";
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::DOUBLE};
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Multiply",
        [](const ElementCreationContext& ctx) {
            auto factors = parseRealArrayParam(ctx.params, "factors", {1.0, 1.0});
            return std::make_unique<Multiply>(std::move(factors));
        },
        [] {
            auto m = make_simple_metadata("Multiply", "Multiply", "Product of N inputs", "basic", 2, 1);
            m.parameters.push_back({"factors", "array", "Per-input factors; input count follows this array length", boost::json::array{1.0, 1.0}, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].name = "lhs";
            m.input_ports[0].description = "Multiplier input 0 (scaled by factors[0]).";
            m.input_ports[1].name = "rhs";
            m.input_ports[1].description = "Multiplier input 1 (scaled by factors[1]).";
            m.output_ports[0].description = "Product output.";
            m.variadic_inputs = {true, 2, 64, "in_{index}", SignalType::DOUBLE};
            return m;
        }()));

    registry.register_element(ElementDescriptor(
        "Delay",
        [](const ElementCreationContext& ctx) {
            const double initial_value = ctx.has_param("initial_value") ? ctx.get_real("initial_value") : 0.0;
            return std::make_unique<Delay>(initial_value);
        },
        [] {
            auto m = make_simple_metadata("Delay", "Delay", "Unit delay", "filters", 1, 1);
            m.parameters.push_back({"initial_value", "real", "Initial output value", 0.0, std::nullopt, std::nullopt, {}, false});
            return m;
        }()));

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
        "PreviousValue",
        [](const ElementCreationContext& ctx) {
            const double initial_value = ctx.has_param("initial_value") ? ctx.get_real("initial_value") : 0.0;
            return std::make_unique<PreviousValue>(initial_value);
        },
        [] {
            auto m = make_simple_metadata("PreviousValue", "Previous Value", "Unit-delay previous sample output", "filters", 1, 1);
            m.parameters.push_back({"initial_value", "real", "Initial previous value", 0.0, std::nullopt, std::nullopt, {}, false});
            m.input_ports[0].description = "Current input sample.";
            m.output_ports[0].description = "Previous input sample.";
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
            m.input_ports[0].type = SignalType::DOUBLE;
            m.input_ports[1].name = "im";
            m.input_ports[1].description = "Imaginary part.";
            m.input_ports[1].type = SignalType::DOUBLE;
            m.output_ports[0].description = "Complex output.";
            m.output_ports[0].type = SignalType::COMPLEX;
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
            m.input_ports[0].type = SignalType::COMPLEX;
            m.output_ports[0].name = "re";
            m.output_ports[0].description = "Real output.";
            m.output_ports[0].type = SignalType::DOUBLE;
            m.output_ports[1].name = "im";
            m.output_ports[1].description = "Imaginary output.";
            m.output_ports[1].type = SignalType::DOUBLE;
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
            m.input_ports[0].type = SignalType::COMPLEX;
            m.output_ports[0].name = "magnitude";
            m.output_ports[0].description = "Magnitude output.";
            m.output_ports[0].type = SignalType::DOUBLE;
            m.output_ports[1].name = "phase";
            m.output_ports[1].description = "Phase output (radians).";
            m.output_ports[1].type = SignalType::DOUBLE;
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
            m.input_ports[0].type = SignalType::INT12;
            m.output_ports[0].description = "Converted DOUBLE output.";
            m.output_ports[0].type = SignalType::DOUBLE;
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
            m.input_ports[0].type = SignalType::DOUBLE;
            m.output_ports[0].description = "Converted INT12 output.";
            m.output_ports[0].type = SignalType::INT12;
            return m;
        }()));
}

} // namespace s7


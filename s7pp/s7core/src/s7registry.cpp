//
// s7registry.cpp — registers all built-in s7stdlib elements into an
// ElementRegistry so they can be instantiated by name from JSON netlists.
//

#include "s7factory.hpp"
#include "s7probe.hpp"
#include "s7stdlib.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace s7 {

void registerBuiltins(ElementRegistry& registry)
{
    registry.registerType(
        TypeDescriptor{
            .type_name            = "Constant",
            .category             = "Sources",
            .description          = "Emits a fixed scalar value every tick",
            .default_input_count  = 0,
            .default_output_count = 1,
            .default_params       = {{"value", Real{0.0}}},
            .ports                = {{.label = "out", .direction = "output", .description = "Constant scalar output"}}
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const auto val = getParam(params, "value", ParamValue{Real{0.0}});
            if (std::holds_alternative<Real>(val))
                return std::make_shared<Constant<Real>>(id, std::get<Real>(val));
            return std::make_shared<Constant<Complex>>(id, std::get<Complex>(val));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "Integrator",
            .category             = "Math",
            .description          = "Stateful accumulator: state(n+1) = state(n) + input(n)",
            .default_input_count  = 1,
            .default_output_count = 1,
            .default_params       = {{"init_state", Real{0.0}}},
            .ports                = {{.label = "in",  .direction = "input",  .description = "Input signal"},
                                     {.label = "out", .direction = "output", .description = "Integrated output"}}
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const auto init = getParam(params, "init_state", ParamValue{Real{0.0}});
            if (std::holds_alternative<Real>(init))
                return std::make_shared<Integrator<Real>>(id, std::get<Real>(init));
            return std::make_shared<Integrator<Complex>>(id, std::get<Complex>(init));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "Sum",
            .category             = "Math",
            .description          = "Weighted sum of N real inputs",
            .default_input_count  = 2,
            .default_output_count = 1,
            .inputs_configurable  = true,
            .default_params       = {{"w0", Real{1.0}}, {"w1", Real{1.0}}},
            // Ports are auto-numbered (in0, in1, …) because inputs are identical.
            .ports                = {{.label = "out", .direction = "output", .description = "Weighted sum"}},
            .compute_port_layout  = [](const ParamMap& p) -> std::pair<int,int> {
                int maxIdx = 1; // default: w0+w1 → 2 inputs
                for (const auto& [key, val] : p) {
                    if (key.size() < 2 || key.front() != 'w') continue;
                    bool numeric = true;
                    for (std::size_t i = 1; i < key.size(); ++i)
                        if (!std::isdigit(static_cast<unsigned char>(key[i]))) { numeric = false; break; }
                    if (!numeric) continue;
                    const int idx = std::stoi(key.substr(1));
                    maxIdx = std::max(maxIdx, idx);
                }
                return {maxIdx + 1, 1};
            }
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            std::vector<ParamValue> raw_weights;
            const auto scalar_it = params.find("weight");

            std::map<std::size_t, ParamValue> indexed;
            std::size_t max_idx = 0;
            for (const auto& [key, value] : params)
            {
                if (key.size() < 2 || key.front() != 'w') continue;
                bool numeric = true;
                for (std::size_t i = 1; i < key.size(); ++i)
                    if (!std::isdigit(static_cast<unsigned char>(key[i]))) { numeric = false; break; }
                if (!numeric) continue;

                const auto idx = static_cast<std::size_t>(std::stoul(key.substr(1)));
                indexed[idx] = value;
                if (idx > max_idx) max_idx = idx;
            }

            if (scalar_it != params.end() && !indexed.empty())
                throw std::runtime_error(
                    "Sum params are ambiguous: use 'weight' or indexed 'w0','w1',... not both");

            if (scalar_it != params.end())
            {
                raw_weights.push_back(scalar_it->second);
            }
            else if (!indexed.empty())
            {
                raw_weights.reserve(max_idx + 1);
                for (std::size_t i = 0; i <= max_idx; ++i)
                {
                    const auto it = indexed.find(i);
                    if (it == indexed.end())
                        throw std::runtime_error("Sum indexed weights must be contiguous from w0");
                    raw_weights.push_back(it->second);
                }
            }
            else
            {
                raw_weights = {ParamValue{Real{1.0}}, ParamValue{Real{1.0}}};
            }

            const bool has_complex = std::ranges::any_of(raw_weights,
                [](const ParamValue& v) -> bool { return std::holds_alternative<Complex>(v); });
            const bool has_real    = std::ranges::any_of(raw_weights,
                [](const ParamValue& v) -> bool { return std::holds_alternative<Real>(v); });

            if (has_real && has_complex)
                throw std::runtime_error("Sum weights must be all Real or all Complex");

            if (has_complex)
            {
                std::vector<Complex> w;
                w.reserve(raw_weights.size());
                for (const auto& v : raw_weights) w.push_back(std::get<Complex>(v));
                return std::make_shared<Sum<Complex>>(id, std::move(w));
            }

            std::vector<Real> w;
            w.reserve(raw_weights.size());
            for (const auto& v : raw_weights) w.push_back(std::get<Real>(v));
            return std::make_shared<Sum<Real>>(id, std::move(w));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "ToComplex",
            .category             = "Math",
            .description          = "Two real inputs (Re, Im) → one Complex output",
            .default_input_count  = 2,
            .default_output_count = 1,
            .ports                = {{.label = "re",  .direction = "input",  .description = "Real part"},
                                     {.label = "im",  .direction = "input",  .description = "Imaginary part"},
                                     {.label = "out", .direction = "output", .description = "Complex output"}},
        },
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<ToComplex>(id); });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "ToReals",
            .category             = "Math",
            .description          = "One Complex input → two Real outputs (Re, Im)",
            .default_input_count  = 1,
            .default_output_count = 2,
            .ports                = {{.label = "in", .direction = "input",  .description = "Complex input"},
                                     {.label = "re", .direction = "output", .description = "Real part"},
                                     {.label = "im", .direction = "output", .description = "Imaginary part"}},
        },
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<ToReals>(id); });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "Vca",
            .category             = "Math",
            .description          = "Voltage controlled amplifier: out = signal * gain",
            .default_input_count  = 2,
            .default_output_count = 1,
            .ports                = {{.label = "signal", .direction = "input",  .description = "Input signal"},
                                     {.label = "gain",   .direction = "input",  .description = "Gain control voltage"},
                                     {.label = "out",    .direction = "output", .description = "Amplified output"}},
        },
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<Vca>(id); });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "FIRFilter",
            .category             = "Filters",
            .description          = "FIR filter: y[k] = sum_i b[i]*x[k-i]",
            .default_input_count  = 1,
            .default_output_count = 1,
            .default_params       = {{"b0", Real{1.0}}},
            .ports                = {{.label = "in",  .direction = "input",  .description = "Filter input"},
                                     {.label = "out", .direction = "output", .description = "Filter output"}},
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            std::map<std::size_t, Real> indexed_b;
            std::size_t max_b = 0;
            std::map<std::size_t, Real> indexed_x;
            std::size_t max_x = 0;

            for (const auto& [key, value] : params)
            {
                if (key.size() < 2 || (key.front() != 'b' && key.front() != 'x')) continue;
                bool numeric = true;
                for (std::size_t i = 1; i < key.size(); ++i)
                    if (!std::isdigit(static_cast<unsigned char>(key[i]))) { numeric = false; break; }
                if (!numeric) continue;

                const auto idx = static_cast<std::size_t>(std::stoul(key.substr(1)));
                if (!std::holds_alternative<Real>(value))
                    throw std::runtime_error("FIRFilter parameter '" + key + "' must be Real");

                if (key.front() == 'b') { indexed_b[idx] = std::get<Real>(value); if (idx > max_b) max_b = idx; }
                else { indexed_x[idx] = std::get<Real>(value); if (idx > max_x) max_x = idx; }
            }

            std::vector<Real> b;
            if (!indexed_b.empty())
            {
                b.reserve(max_b + 1);
                for (std::size_t i = 0; i <= max_b; ++i)
                {
                    const auto it = indexed_b.find(i);
                    if (it == indexed_b.end()) throw std::runtime_error("FIRFilter coefficients must be contiguous from b0");
                    b.push_back(it->second);
                }
            }
            else { b = {1.0}; }

            std::vector<Real> x_init;
            if (!indexed_x.empty())
            {
                x_init.reserve(max_x + 1);
                for (std::size_t i = 0; i <= max_x; ++i)
                {
                    const auto it = indexed_x.find(i);
                    if (it == indexed_x.end()) throw std::runtime_error("FIRFilter initial state must be contiguous from x0");
                    x_init.push_back(it->second);
                }
            }
            FIRFilter::InitState init_state;
            init_state.x = std::move(x_init);
            return std::make_shared<FIRFilter>(id, std::move(b), std::move(init_state));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "IIRFilter",
            .category             = "Filters",
            .description          = "IIR filter: y[k] = sum_i b[i]*x[k-i] - sum_j a[j]*y[k-j-1]",
            .default_input_count  = 1,
            .default_output_count = 1,
            .default_params       = {{"b0", Real{1.0}}},
            .ports                = {{.label = "in",  .direction = "input",  .description = "Filter input"},
                                     {.label = "out", .direction = "output", .description = "Filter output"}},
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            std::map<std::size_t, Real> indexed_b, indexed_a, indexed_x, indexed_y;
            std::size_t max_b = 0, max_a = 0, max_x = 0, max_y = 0;

            for (const auto& [key, value] : params)
            {
                if (key.size() < 2) continue;
                const auto prefix = key.front();
                if (prefix != 'a' && prefix != 'b' && prefix != 'x' && prefix != 'y') continue;
                bool numeric = true;
                for (std::size_t i = 1; i < key.size(); ++i)
                    if (!std::isdigit(static_cast<unsigned char>(key[i]))) { numeric = false; break; }
                if (!numeric) continue;
                if (!std::holds_alternative<Real>(value))
                    throw std::runtime_error("IIRFilter coefficient '" + key + "' must be Real");

                const auto idx = static_cast<std::size_t>(std::stoul(key.substr(1)));
                if (prefix == 'b') { indexed_b[idx] = std::get<Real>(value); if (idx > max_b) max_b = idx; }
                else if (prefix == 'a')
                {
                    if (idx == 0) throw std::runtime_error("IIRFilter uses a1,a2,... (a0 is assumed to be 1)");
                    indexed_a[idx] = std::get<Real>(value); if (idx > max_a) max_a = idx;
                }
                else if (prefix == 'x') { indexed_x[idx] = std::get<Real>(value); if (idx > max_x) max_x = idx; }
                else { indexed_y[idx] = std::get<Real>(value); if (idx > max_y) max_y = idx; }
            }

            auto extractVec = [](const std::map<std::size_t, Real>& m, std::size_t mx, const char* name) {
                std::vector<Real> v;
                if (m.empty()) { v = {1.0}; return v; }
                v.reserve(mx + 1);
                for (std::size_t i = 0; i <= mx; ++i) {
                    auto it = m.find(i);
                    if (it == m.end()) throw std::runtime_error(std::string(name) + " must be contiguous from 0");
                    v.push_back(it->second);
                }
                return v;
            };
            auto extractVecFrom1 = [](const std::map<std::size_t, Real>& m, std::size_t mx) {
                std::vector<Real> v;
                if (m.empty()) return v;
                v.reserve(mx);
                for (std::size_t i = 1; i <= mx; ++i) {
                    auto it = m.find(i);
                    if (it == m.end()) throw std::runtime_error("IIRFilter feedback coefficients must be contiguous from a1");
                    v.push_back(it->second);
                }
                return v;
            };

            auto b = extractVec(indexed_b, max_b, "IIRFilter b");
            auto a = extractVecFrom1(indexed_a, max_a);

            IIRFilter::InitState init_state;
            if (!indexed_x.empty())
            {
                init_state.x.reserve(max_x + 1);
                for (std::size_t i = 0; i <= max_x; ++i)
                    init_state.x.push_back(indexed_x.at(i));
            }
            if (!indexed_y.empty())
            {
                init_state.y.reserve(max_y + 1);
                for (std::size_t i = 0; i <= max_y; ++i)
                    init_state.y.push_back(indexed_y.at(i));
            }
            return std::make_shared<IIRFilter>(id, std::move(b), std::move(a), std::move(init_state));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "DigitalOscillator",
            .category             = "Sources",
            .description          = "Autonomous digital sinusoidal oscillator",
            .default_input_count  = 0,
            .default_output_count = 1,
            .default_params       = {{"frequency_hz", Real{440.0}}, {"amplitude", Real{1.0}}},
            .ports                = {{.label = "out", .direction = "output", .description = "Sinusoidal output"}},
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            DigitalOscillator::Params cfg;
            cfg.frequency_hz = getParamAs<Real>(params, "frequency_hz", cfg.frequency_hz);
            cfg.amplitude    = getParamAs<Real>(params, "amplitude",    cfg.amplitude);
            cfg.phase_rad    = getParamAs<Real>(params, "phase_rad",    cfg.phase_rad);
            cfg.radius       = getParamAs<Real>(params, "radius",       cfg.radius);
            return std::make_shared<DigitalOscillator>(id, cfg);
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "HarmonicOscillator",
            .category             = "Sources",
            .description          = "Sinusoidal source: amplitude*sin(2π*f*n*dt + φ)",
            .default_input_count  = 0,
            .default_output_count = 1,
            .default_params       = {{"frequency_hz", Real{1.0}}, {"amplitude", Real{1.0}}},
            .ports                = {{.label = "out", .direction = "output", .description = "Sinusoidal output"}},
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            HarmonicOscillator::Params cfg;
            cfg.frequency_hz = getParamAs<Real>(params, "frequency_hz", cfg.frequency_hz);
            cfg.phase_rad    = getParamAs<Real>(params, "phase_rad",    cfg.phase_rad);
            cfg.amplitude    = getParamAs<Real>(params, "amplitude",    cfg.amplitude);
            return std::make_shared<HarmonicOscillator>(id, cfg);
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "EnergyDifference",
            .category             = "Math",
            .description          = "Accumulated energy of signal difference: E += (u1-u2)²·dt",
            .default_input_count  = 2,
            .default_output_count = 1,
            .ports                = {{.label = "a",   .direction = "input",  .description = "First signal"},
                                     {.label = "b",   .direction = "input",  .description = "Second signal"},
                                     {.label = "out", .direction = "output", .description = "Accumulated energy"}},
        },
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<EnergyDifference>(id); });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "Vco",
            .category             = "Sources",
            .description          = "Voltage-controlled oscillator (sine): f = base_hz + k*cv",
            .default_input_count  = 1,
            .default_output_count = 1,
            .default_params       = {{"base_hz", Real{440.0}}},
            .ports                = {{.label = "cv",  .direction = "input",  .description = "Control voltage"},
                                     {.label = "out", .direction = "output", .description = "Sine wave output"}},
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            Vco::Params cfg;
            cfg.base_hz               = getParamAs<Real>(params, "base_hz",               cfg.base_hz);
            cfg.sensitivity_hz_per_v  = getParamAs<Real>(params, "sensitivity_hz_per_v",  cfg.sensitivity_hz_per_v);
            cfg.amplitude             = getParamAs<Real>(params, "amplitude",             cfg.amplitude);
            cfg.initial_phase_rad     = getParamAs<Real>(params, "initial_phase_rad",     cfg.initial_phase_rad);
            return std::make_shared<Vco>(id, cfg);
        });
}

void registerProbe(ElementRegistry& registry)
{
    registry.registerType(
        TypeDescriptor{
            .type_name            = "Probe",
            .category             = "Internal",
            .description          = "Data-capture probe — used internally for visual sinks",
            .default_input_count  = 2,
            .default_output_count = 0,
            .inputs_configurable  = true,
            .hidden               = true,   // not shown in the GUI palette
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const int channels = static_cast<int>(getParamAs<Real>(params, "channels", Real{2.0}));
            return std::make_shared<Probe>(id, std::max(1, channels));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "ComplexProbe",
            .category             = "Internal",
            .description          = "Complex data-capture probe",
            .default_input_count  = 2,
            .default_output_count = 0,
            .inputs_configurable  = true,
            .hidden               = true,
        },
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const int channels = static_cast<int>(getParamAs<Real>(params, "channels", Real{2.0}));
            return std::make_shared<ComplexProbe>(id, std::max(1, channels));
        });
}

} // namespace s7

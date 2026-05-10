//
// s7registry.cpp — registers all built-in s7stdlib elements into an
// ElementRegistry so they can be instantiated by name from JSON netlists.
//

#include "s7factory.hpp"
#include "s7stdlib.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace s7 {

void registerBuiltins(ElementRegistry& registry)
{
    registry.registerType("Constant",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const auto val = getParam(params, "value", ParamValue{Real{0.0}});
            if (std::holds_alternative<Real>(val))
                return std::make_shared<Constant<Real>>(id, std::get<Real>(val));
            return std::make_shared<Constant<Complex>>(id, std::get<Complex>(val));
        });

    registry.registerType("Integrator",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            const auto init = getParam(params, "init_state", ParamValue{Real{0.0}});
            if (std::holds_alternative<Real>(init))
                return std::make_shared<Integrator<Real>>(id, std::get<Real>(init));
            return std::make_shared<Integrator<Complex>>(id, std::get<Complex>(init));
        });

    registry.registerType("Sum",
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

    registry.registerType("ToComplex",
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<ToComplex>(id); });

    registry.registerType("ToReals",
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<ToReals>(id); });

    registry.registerType("Vca",
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<Vca>(id); });

    registry.registerType("FIRFilter",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            std::map<std::size_t, Real> indexed_b;
            std::size_t max_b = 0;
            std::map<std::size_t, Real> indexed_x;
            std::size_t max_x = 0;

            for (const auto& [key, value] : params)
            {
                if (key.size() < 2 || (key.front() != 'b' && key.front() != 'x'))
                {
                    continue;
                }
                bool numeric = true;
                for (std::size_t i = 1; i < key.size(); ++i)
                {
                    if (!std::isdigit(static_cast<unsigned char>(key[i])))
                    {
                        numeric = false;
                        break;
                    }
                }
                if (!numeric)
                {
                    continue;
                }

                const auto idx = static_cast<std::size_t>(std::stoul(key.substr(1)));
                if (!std::holds_alternative<Real>(value))
                {
                    throw std::runtime_error("FIRFilter parameter '" + key + "' must be Real");
                }

                if (key.front() == 'b')
                {
                    indexed_b[idx] = std::get<Real>(value);
                    if (idx > max_b) max_b = idx;
                }
                else if (key.front() == 'x')
                {
                    indexed_x[idx] = std::get<Real>(value);
                    if (idx > max_x) max_x = idx;
                }
            }

            std::vector<Real> b;
            if (!indexed_b.empty())
            {
                b.reserve(max_b + 1);
                for (std::size_t i = 0; i <= max_b; ++i)
                {
                    const auto it = indexed_b.find(i);
                    if (it == indexed_b.end())
                    {
                        throw std::runtime_error("FIRFilter coefficients must be contiguous from b0");
                    }
                    b.push_back(it->second);
                }
            }
            else
            {
                b = {1.0};
            }

            std::vector<Real> x_init;
            if (!indexed_x.empty())
            {
                x_init.reserve(max_x + 1);
                for (std::size_t i = 0; i <= max_x; ++i)
                {
                    const auto it = indexed_x.find(i);
                    if (it == indexed_x.end())
                    {
                        throw std::runtime_error("FIRFilter initial state must be contiguous from x0");
                    }
                    x_init.push_back(it->second);
                }
            }

            FIRFilter::InitState init_state;
            init_state.x = std::move(x_init);
            return std::make_shared<FIRFilter>(id, std::move(b), std::move(init_state));
        });

    registry.registerType("IIRFilter",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            std::map<std::size_t, Real> indexed_b;
            std::size_t max_b = 0;
            std::map<std::size_t, Real> indexed_a;
            std::size_t max_a = 0;
            std::map<std::size_t, Real> indexed_x;
            std::size_t max_x = 0;
            std::map<std::size_t, Real> indexed_y;
            std::size_t max_y = 0;

            for (const auto& [key, value] : params)
            {
                if (key.size() < 2)
                {
                    continue;
                }

                const auto prefix = key.front();
                if (prefix != 'a' && prefix != 'b' && prefix != 'x' && prefix != 'y')
                {
                    continue;
                }

                bool numeric = true;
                for (std::size_t i = 1; i < key.size(); ++i)
                {
                    if (!std::isdigit(static_cast<unsigned char>(key[i])))
                    {
                        numeric = false;
                        break;
                    }
                }
                if (!numeric)
                {
                    continue;
                }

                if (!std::holds_alternative<Real>(value))
                {
                    throw std::runtime_error("IIRFilter coefficient '" + key + "' must be Real");
                }

                const auto idx = static_cast<std::size_t>(std::stoul(key.substr(1)));
                if (prefix == 'b')
                {
                    indexed_b[idx] = std::get<Real>(value);
                    if (idx > max_b) max_b = idx;
                }
                else if (prefix == 'a')
                {
                    if (idx == 0)
                    {
                        throw std::runtime_error("IIRFilter uses a1,a2,... (a0 is assumed to be 1)");
                    }
                    indexed_a[idx] = std::get<Real>(value);
                    if (idx > max_a) max_a = idx;
                }
                else if (prefix == 'x')
                {
                    indexed_x[idx] = std::get<Real>(value);
                    if (idx > max_x) max_x = idx;
                }
                else if (prefix == 'y')
                {
                    indexed_y[idx] = std::get<Real>(value);
                    if (idx > max_y) max_y = idx;
                }
            }

            std::vector<Real> b;
            if (!indexed_b.empty())
            {
                b.reserve(max_b + 1);
                for (std::size_t i = 0; i <= max_b; ++i)
                {
                    const auto it = indexed_b.find(i);
                    if (it == indexed_b.end())
                    {
                        throw std::runtime_error("IIRFilter coefficients must be contiguous from b0");
                    }
                    b.push_back(it->second);
                }
            }
            else
            {
                b = {1.0};
            }

            std::vector<Real> a;
            if (!indexed_a.empty())
            {
                a.reserve(max_a);
                for (std::size_t i = 1; i <= max_a; ++i)
                {
                    const auto it = indexed_a.find(i);
                    if (it == indexed_a.end())
                    {
                        throw std::runtime_error("IIRFilter feedback coefficients must be contiguous from a1");
                    }
                    a.push_back(it->second);
                }
            }

            std::vector<Real> x_init;
            if (!indexed_x.empty())
            {
                x_init.reserve(max_x + 1);
                for (std::size_t i = 0; i <= max_x; ++i)
                {
                    const auto it = indexed_x.find(i);
                    if (it == indexed_x.end())
                    {
                        throw std::runtime_error("IIRFilter x-state must be contiguous from x0");
                    }
                    x_init.push_back(it->second);
                }
            }

            std::vector<Real> y_init;
            if (!indexed_y.empty())
            {
                y_init.reserve(max_y + 1);
                for (std::size_t i = 0; i <= max_y; ++i)
                {
                    const auto it = indexed_y.find(i);
                    if (it == indexed_y.end())
                    {
                        throw std::runtime_error("IIRFilter y-state must be contiguous from y0");
                    }
                    y_init.push_back(it->second);
                }
            }

            IIRFilter::InitState init_state;
            init_state.x = std::move(x_init);
            init_state.y = std::move(y_init);
            return std::make_shared<IIRFilter>(id, std::move(b), std::move(a), std::move(init_state));
        });

    registry.registerType("DigitalOscillator",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            DigitalOscillator::Params cfg;
            cfg.frequency_hz = getParamAs<Real>(params, "frequency_hz", cfg.frequency_hz);
            cfg.amplitude = getParamAs<Real>(params, "amplitude", cfg.amplitude);
            cfg.phase_rad = getParamAs<Real>(params, "phase_rad", cfg.phase_rad);
            cfg.radius = getParamAs<Real>(params, "radius", cfg.radius);
            return std::make_shared<DigitalOscillator>(id, cfg);
        });

    registry.registerType("EnergyDifference",
        [](const std::string& id, const ParamMap&) -> ElementPtr
        { return std::make_shared<EnergyDifference>(id); });

    registry.registerType("Vco",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            Vco::Params cfg;
            cfg.base_hz = getParamAs<Real>(params, "base_hz", cfg.base_hz);
            cfg.sensitivity_hz_per_v = getParamAs<Real>(params, "sensitivity_hz_per_v", cfg.sensitivity_hz_per_v);
            cfg.amplitude = getParamAs<Real>(params, "amplitude", cfg.amplitude);
            cfg.initial_phase_rad = getParamAs<Real>(params, "initial_phase_rad", cfg.initial_phase_rad);
            return std::make_shared<Vco>(id, cfg);
        });

    registry.registerType("HarmonicOscillator",
        [](const std::string& id, const ParamMap& params) -> ElementPtr
        {
            HarmonicOscillator::Params cfg;
            cfg.frequency_hz = getParamAs<Real>(params, "frequency_hz", cfg.frequency_hz);
            cfg.phase_rad = getParamAs<Real>(params, "phase_rad", cfg.phase_rad);
            cfg.amplitude = getParamAs<Real>(params, "amplitude", cfg.amplitude);
            return std::make_shared<HarmonicOscillator>(id, cfg);
        });
}

} // namespace s7


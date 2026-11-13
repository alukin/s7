// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit experiment command: executes fresh schema instances, never a viewer callback.
#include <boost/json.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <s7/core.hpp>
#include <s7spectral/analysis.hpp>
#include <stdexcept>
int main(int argc, char **argv) try {
    if (argc != 2) {
        std::cerr << "usage: s7analysis_lab damping_surface.csv\n";
        return 2;
    }
    auto &registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    constexpr double rate = 1024, omega = 2 * std::numbers::pi * 5;
    constexpr std::size_t count = 8192, columns = 513, rows = 9;
    std::vector<double> gains(columns * rows);
    boost::json::array runs;
    for (std::size_t row = 0; row < rows; ++row) {
        const double damping = .1 + .2 * double(row);
        s7::Schema schema("damping_sweep");
        schema.set_rate(s7::Rate::from_hz(rate));
        auto add = [&](const char *id, const char *type, boost::json::object params) {
            const auto *descriptor = registry.find(type);
            if (!descriptor)
                throw std::runtime_error("Missing schema-library element");
            schema.add_element(s7::make_element_spec(id, *descriptor, std::move(params)));
        };
        add("excitation", "ImpulseGenerator", {{"area", 1}, {"start_time", .048828125}});
        add("plant", "SecondOrderLowPass", {});
        add("omega", "Constant", {{"value", omega}});
        add("damping", "Constant", {{"value", damping}});
        schema.connect("excitation", 0, "plant", 0);
        schema.connect("omega", 0, "plant", 1);
        schema.connect("damping", 0, "plant", 2);
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        std::vector<double> input, output;
        input.reserve(count);
        output.reserve(count);
        for (std::size_t n = 0; n < count; ++n) {
            runtime.step();
            input.push_back(runtime.real_output_of("excitation"));
            output.push_back(runtime.real_output_of("plant"));
        }
        const auto response = s7spectral::impulseResponse(input, output, rate);
        for (std::size_t k = 0; k < columns; ++k) {
            gains[row * columns + k] = std::abs(response.values[k]);
            const auto z = std::polar(1.0, 2 * std::numbers::pi * response.frequency[k] / rate);
            const auto dt = 1 / rate;
            const auto reference =
                dt * dt * omega * omega /
                ((z - 1.0) * (z - 1.0 + 2 * damping * omega * dt) + dt * dt * omega * omega);
            if (std::abs(reference - response.values[k]) > 1e-6)
                throw std::runtime_error("Sweep failed independent discrete-transfer reference");
        }
        runs.push_back(
            boost::json::object{{"run", row + 1},
                                {"damping", damping},
                                {"omega0", omega},
                                {"rate_hz", rate},
                                {"samples", count},
                                {"pulse_height", rate},
                                {"plant", "SecondOrderLowPass"},
                                {"method", "independent fresh graph, explicit Euler integrators"}});
    }
    boost::json::object meta{{"kind", "surface"},      {"version", 1},
                             {"columns", columns},     {"rows", rows},
                             {"x_unit", "Hz"},         {"y_unit", "1"},
                             {"x_label", "Frequency"}, {"y_label", "Damping ratio"},
                             {"height_label", "Gain"}, {"height_unit", "output/input gain"},
                             {"runs", std::move(runs)}};
    std::ofstream out(argv[1]);
    out << std::setprecision(17) << "# " << boost::json::serialize(meta) << "\ncolumn,row,x,y,re,im,valid\n";
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < columns; ++c)
            out << c << ',' << r << ',' << double(c) * rate / 8192 << ',' << .1 + .2 * double(r) << ','
                << gains[r * columns + c] << ",0,1\n";
    out.flush();
    if (!out)
        throw std::runtime_error("Cannot write sweep result");
    std::cout << "Wrote " << columns << " x " << rows
              << " measured damping surface; all runs match independent discrete references\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

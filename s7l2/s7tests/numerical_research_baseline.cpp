// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Read-only research benchmark of the indexed serial NodeRuntime. The extra
// step publishes the last staged state (S7's normal causal-memory contract).
#include <s7/core.hpp>

#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::invalid_argument("usage: baseline oscillator|linear dt duration [euler|trapezoid|symplectic]");
        const std::string model = argv[1];
        const double h = std::stod(argv[2]);
        const double duration = std::stod(argv[3]);
        const std::string method = argc == 5 ? argv[4] : "euler";
        if (method != "euler" && method != "trapezoid" && method != "symplectic")
            throw std::invalid_argument("unknown method");
        if (!std::isfinite(h) || !std::isfinite(duration) || h <= 0 || duration <= 0)
            throw std::invalid_argument("dt and duration must be finite and positive");
        const auto steps = static_cast<std::size_t>(std::llround(duration / h));
        if (std::abs(steps*h-duration) > 1e-10) throw std::invalid_argument("nonintegral step count");

        s7::Schema schema("numerical_research_baseline");
        if (model == "oscillator") {
            if (method == "symplectic") {
                schema.add_element("integ", [=] { return std::make_unique<s7::SymplecticIntegrator>(h, 1.0, 0.0); });
                schema.add_element("minus_x", [] { return std::make_unique<s7::Gain>(-1.0); });
                schema.connect("integ", 0, "minus_x", 0);
                schema.connect("minus_x", 0, "integ", 0);
            } else {
                if (method == "euler") {
                    schema.add_element("x", [=] { return std::make_unique<s7::EulerIntegrator>(h, 1.0); });
                    schema.add_element("v", [=] { return std::make_unique<s7::EulerIntegrator>(h, 0.0); });
                } else {
                    schema.add_element("x", [=] { return std::make_unique<s7::TrapezoidIntegrator>(h, 1.0); });
                    schema.add_element("v", [=] { return std::make_unique<s7::TrapezoidIntegrator>(h, 0.0); });
                }
                schema.add_element("minus_x", [] { return std::make_unique<s7::Gain>(-1.0); });
                schema.connect("v", 0, "x", 0);
                schema.connect("x", 0, "minus_x", 0);
                schema.connect("minus_x", 0, "v", 0);
            }
        } else if (model == "linear") {
            if (method == "symplectic") throw std::invalid_argument("symplectic method requires x'=v, v'=a(x)");
            if (method == "euler")
                schema.add_element("x", [=] { return std::make_unique<s7::EulerIntegrator>(h, 0.0); });
            else
                schema.add_element("x", [=] { return std::make_unique<s7::TrapezoidIntegrator>(h, 0.0); });
            schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("minus_2x", [] { return std::make_unique<s7::Gain>(-2.0); });
            schema.add_element("rhs", [] { return std::make_unique<s7::Adder>(std::vector<double>{1,1}); });
            schema.connect("x", 0, "minus_2x", 0);
            schema.connect("one", 0, "rhs", 0);
            schema.connect("minus_2x", 0, "rhs", 1);
            schema.connect("rhs", 0, "x", 0);
        } else throw std::invalid_argument("unknown model");

        if (!s7::Validator::validate(schema).ok()) throw std::runtime_error("schema failed validation");
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        const auto start = std::chrono::steady_clock::now();
        const auto cpu_start = std::clock();
        for (std::size_t i = 0; i <= steps; ++i) runtime.step();
        const auto cpu_elapsed = std::clock() - cpu_start;
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const double x = method == "symplectic" ? runtime.real_output_of("integ", 0) : runtime.real_output_of("x");
        const double v = method == "symplectic" ? runtime.real_output_of("integ", 1)
                         : model == "oscillator" ? runtime.real_output_of("v") : 0.0;
        const double reference_x = model == "oscillator" ? std::cos(duration) : (1-std::exp(-2*duration))/2;
        const double reference_v = model == "oscillator" ? -std::sin(duration) : 0.0;
        std::cout << std::setprecision(17)
                  << "{\"model\":\"" << model << "\",\"method\":\"" << method << "\",\"h\":" << h
                  << ",\"duration\":" << duration << ",\"steps\":" << steps
                  << ",\"x\":" << x << ",\"v\":" << v
                  << ",\"final_error\":" << std::hypot(x-reference_x,v-reference_v)
                  << ",\"ns_per_step\":"
                  << std::chrono::duration<double,std::nano>(elapsed).count()/(steps+1)
                  << ",\"cpu_ns_per_step\":"
                  << (static_cast<double>(cpu_elapsed)*1e9/CLOCKS_PER_SEC)/(steps+1) << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

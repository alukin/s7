// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// examples/double_pendulum_full.s7diagram builds the nonlinear double-pendulum
// accelerations in nested subsystems. Its four states must follow an
// independent forward-Euler integration of the equations in its note
// (m1 = m2 = l1 = l2 = 1, g = 9.81), with the state after runtime step k
// equal to z[k-1]. Given a second diagram, the test instead requires both to
// produce bitwise identical states (e.g. the earlier flat layout).

#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/rate.hpp>
#include <s7/runtime.hpp>
#include <s7/schema_library.hpp>
#include <s7/validation.hpp>

#include <boost/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr std::array<const char*, 4> kStates{"theta1", "omega1", "theta2", "omega2"};

std::unique_ptr<s7::NodeRuntime> load(const std::string& path, double& dt)
{
    std::ifstream stream(path);
    std::stringstream text;
    text << stream.rdbuf();
    const auto document = boost::json::parse(text.str()).as_object();
    dt = document.at("dt").to_number<double>();
    auto schema = s7::SchemaLibraryLoader::build_schema(document.at("schema"));
    if (!schema.rate()) schema.set_rate(s7::Rate::from_seconds(dt));
    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) throw std::runtime_error(path + ": " + validation.errors.front());
    auto runtime = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_rate(s7::Rate::from_seconds(dt));
    return runtime;
}

std::array<double, 4> states(const s7::NodeRuntime& runtime)
{
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = runtime.real_output_of(kStates[i]);
    return values;
}

// One forward-Euler step of z = (theta1, omega1, theta2, omega2).
std::array<double, 4> euler(const std::array<double, 4>& z, double dt)
{
    constexpr double g = 9.81;
    const double delta = z[0] - z[2];
    const double s = std::sin(delta), c = std::cos(delta);
    const double denominator = 1.0 + s * s; // m1 + m2 sin^2(delta)
    const double alpha1 = (g * std::sin(z[2]) * c - s * (z[1] * z[1] * c + z[3] * z[3]) - 2.0 * g * std::sin(z[0])) / denominator;
    const double alpha2 = (2.0 * (z[1] * z[1] * s - g * std::sin(z[2]) + g * std::sin(z[0]) * c) + z[3] * z[3] * s * c) / denominator;
    return {z[0] + dt * z[1], z[1] + dt * alpha1, z[2] + dt * z[3], z[3] + dt * alpha2};
}

} // namespace

int main(int argc, char** argv)
try {
    if (argc < 2) throw std::invalid_argument("usage: double_pendulum_full_smoke <diagram> [<reference diagram>]");
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    double dt = 0;
    auto runtime = load(argv[1], dt);
    if (argc == 3) {
        double reference_dt = 0;
        auto reference = load(argv[2], reference_dt);
        for (int step = 1; step <= 20000; ++step) {
            runtime->step();
            reference->step();
            if (states(*runtime) != states(*reference))
                throw std::runtime_error("states differ from the reference diagram at step " + std::to_string(step));
        }
        std::cout << "bitwise identical to the reference diagram over 20000 steps\n";
        return 0;
    }
    // A chaotic system: compare over 2 s, before rounding differences in the
    // operation order grow; 1e-9 rad (rad/s) is far below any visible effect.
    std::array<double, 4> z{2.0, 0.0, 1.5, 0.0};
    double worst = 0;
    for (int step = 1; step <= 2000; ++step) {
        runtime->step();
        const auto value = states(*runtime);
        for (std::size_t i = 0; i < z.size(); ++i) worst = std::max(worst, std::abs(value[i] - z[i]));
        if (worst > 1e-9) throw std::runtime_error("state differs from the forward-Euler reference at step " + std::to_string(step));
        z = euler(z, dt);
    }
    std::cout << "matches the forward-Euler reference over 2000 steps; largest difference " << worst << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

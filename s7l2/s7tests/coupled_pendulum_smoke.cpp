// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "coupled_pendulum_cases.hpp"

#include <chrono>
#include <iostream>

int main() try
{
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    const std::filesystem::path output(S7_PENDULUM_REPORT_DIR);
    std::filesystem::create_directories(output);
    std::ofstream summary(output / "summary.csv");
    if (!summary) throw std::runtime_error("Cannot create numerical summary");
    summary << std::setprecision(12)
            << "model,frequency_hz,duration_s,max_angle_rad,rms_angle_rad,max_velocity_rad_s,max_relative_energy_error,max_discrete_error,steps_per_second\n";
    for (const auto method : s7tests::pendulum::methods) {
        for (const auto mode : s7tests::pendulum::modes) {
            double previous_error = 0;
            for (const int frequency : s7tests::pendulum::frequencies) {
                const auto model = s7tests::pendulum::name(method, mode);
                const auto start = std::chrono::steady_clock::now();
                const auto result = s7tests::pendulum::run_headless(method, mode, frequency,
                    output / (model + "_" + std::to_string(frequency) + ".csv"));
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                std::cout << model << " " << frequency << " Hz: max angle=" << result.max_angle
                          << " rms=" << result.rms_angle() << " max energy=" << result.max_energy_relative
                          << " discrete residual=" << result.max_discrete << '\n';
                summary << model << ',' << frequency << ',' << s7tests::pendulum::duration << ','
                        << result.max_angle << ',' << result.rms_angle() << ',' << result.max_velocity << ','
                        << result.max_energy_relative << ',' << result.max_discrete << ','
                        << s7tests::pendulum::duration * frequency / seconds << '\n';
                if (previous_error > 0) {
                    const double ratio = previous_error / result.max_angle;
                    // Euler/Symplectic are first order; implicit trapezoid
                    // is second order, giving ~100x improvement for 10x rate.
                    const double low = method == "trapezoid" ? 70 : 7;
                    const double high = method == "trapezoid" ? 130 : 20;
                    if (ratio < low || ratio > high) throw std::runtime_error(model + ": unexpected convergence ratio");
                }
                previous_error = result.max_angle;
            }
        }
    }
    std::cout << "Reports: " << output << '\n';
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}

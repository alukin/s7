// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Opt-in workload for two independent, per-sample heavy Element::step() calls.
// This is a scheduler prototype; it does not modify NodeRuntime.

#include "s7/elements.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::vector<double> samples(std::size_t steps) {
    std::vector<double> result(steps);
    for (std::size_t i = 0; i < steps; ++i)
        result[i] = std::sin(0.013 * static_cast<double>(i)) + 0.2 * std::cos(0.041 * static_cast<double>(i));
    return result;
}

std::vector<double> taps(std::size_t size, double phase) {
    std::vector<double> result(size);
    for (std::size_t i = 0; i < size; ++i)
        result[i] = std::exp(-0.0004 * static_cast<double>(i)) * std::cos(phase * static_cast<double>(i));
    return result;
}

struct TimedOutputs {
    double ns_per_step{};
    std::vector<std::pair<double, double>> values;
};

TimedOutputs run_serial(std::span<const double> input, const std::vector<double>& h1,
                        const std::vector<double>& h2) {
    double sample{};
    s7::FirFilter left{h1};
    s7::FirFilter right{h2};
    const std::array<s7::InputBinding, 1> binding{s7::bind_input<s7::SignalType::F64>(sample)};
    left.bind_inputs(binding);
    right.bind_inputs(binding);
    const auto* left_output = static_cast<const double*>(left.output_ports()[0].value_ptr);
    const auto* right_output = static_cast<const double*>(right.output_ports()[0].value_ptr);
    TimedOutputs result;
    result.values.reserve(input.size());
    const auto start = std::chrono::steady_clock::now();
    for (double value : input) {
        sample = value;
        left.step();
        right.step();
        result.values.emplace_back(*left_output, *right_output);
    }
    result.ns_per_step = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / input.size();
    return result;
}

TimedOutputs run_parallel(std::span<const double> input, const std::vector<double>& h1,
                          const std::vector<double>& h2) {
    double sample{};
    s7::FirFilter left{h1};
    s7::FirFilter right{h2};
    const std::array<s7::InputBinding, 1> binding{s7::bind_input<s7::SignalType::F64>(sample)};
    left.bind_inputs(binding);
    right.bind_inputs(binding);
    const auto* left_output = static_cast<const double*>(left.output_ports()[0].value_ptr);
    const auto* right_output = static_cast<const double*>(right.output_ports()[0].value_ptr);
    std::barrier start_gate{2};
    std::barrier done_gate{2};
    std::atomic<bool> stopping{false};
    std::jthread worker([&] {
        for (;;) {
            start_gate.arrive_and_wait();
            if (stopping.load()) break;
            right.step();
            done_gate.arrive_and_wait();
        }
    });
    TimedOutputs result;
    result.values.reserve(input.size());
    const auto start = std::chrono::steady_clock::now();
    for (double value : input) {
        sample = value;
        start_gate.arrive_and_wait();
        left.step();
        done_gate.arrive_and_wait();
        result.values.emplace_back(*left_output, *right_output);
    }
    result.ns_per_step = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / input.size();
    stopping.store(true);
    start_gate.arrive_and_wait();
    return result;
}

void benchmark(std::size_t tap_count, std::size_t steps) {
    const auto input = samples(steps);
    const auto h1 = taps(tap_count, 0.021);
    const auto h2 = taps(tap_count, 0.043);
    const auto serial = run_serial(input, h1, h2);
    const auto parallel = run_parallel(input, h1, h2);
    for (std::size_t i = 0; i < steps; ++i) {
        if (std::bit_cast<std::uint64_t>(serial.values[i].first) !=
                std::bit_cast<std::uint64_t>(parallel.values[i].first) ||
            std::bit_cast<std::uint64_t>(serial.values[i].second) !=
                std::bit_cast<std::uint64_t>(parallel.values[i].second)) {
            std::cerr << "FIR branches differ at step " << i << '\n';
            throw std::runtime_error("FIR parallel result differs from serial.");
        }
    }
    std::cout << "taps=" << tap_count << " steps=" << steps
              << " serial_ns_per_step=" << serial.ns_per_step
              << " parallel_ns_per_step=" << parallel.ns_per_step
              << " speedup=" << serial.ns_per_step / parallel.ns_per_step << '\n';
}

} // namespace

int main() {
    try {
        benchmark(64, 10000);
        benchmark(4096, 3000);
        benchmark(16384, 1500);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "model_run_controller.hpp"

#include <utility>
#include <charconv>
#include <cmath>
#include <string_view>
#include <limits>
#include <stdexcept>
#include <s7/rate.hpp>
#include <boost/multiprecision/cpp_int.hpp>

namespace s7studio::ui {

namespace {
long finiteTicks(std::uint64_t seconds, double dt)
{
    const auto rate = s7::Rate::from_seconds(dt);
    const boost::multiprecision::uint128_t ticks =
        boost::multiprecision::uint128_t(seconds) * rate.numerator / rate.denominator;
    if (ticks > std::numeric_limits<long>::max())
        throw std::overflow_error("Run duration exceeds the supported step count.");
    return ticks.convert_to<long>();
}
}

long ModelRunController::computeTicks(int hours, int minutes, int seconds, double dt)
{
    if (hours < 0 || minutes < 0 || seconds < 0)
        throw std::invalid_argument("Run duration components must be nonnegative.");
    const auto total = static_cast<std::uint64_t>(hours) * 3600
                     + static_cast<std::uint64_t>(minutes) * 60 + seconds;
    return finiteTicks(total, dt);
}

long ModelRunController::computeTicks(double runtimeSeconds, double dt)
{
    if (runtimeSeconds == -1) return -1;
    if (!std::isfinite(runtimeSeconds) || runtimeSeconds < 0)
        throw std::invalid_argument("Run duration must be finite and nonnegative, or -1 for continuous execution.");
    const auto rate = s7::Rate::from_seconds(dt);
    // Interpret the shortest decimal representation used to persist the input.
    // Exact rational arithmetic avoids losing a step at e.g. 0.3 s / 0.1 s,
    // without rounding genuinely sub-boundary durations up to the next step.
    if (runtimeSeconds == 0) return 0;
    char buffer[64];
    const auto encoded = std::to_chars(std::begin(buffer), std::end(buffer), runtimeSeconds);
    if (encoded.ec != std::errc{}) throw std::invalid_argument("Cannot represent run duration.");
    const std::string_view text(buffer, static_cast<std::size_t>(encoded.ptr - buffer));
    boost::multiprecision::cpp_int numerator = 0, denominator = 1;
    int decimalPlaces = 0;
    bool fraction = false;
    const auto exponentAt = text.find('e');
    for (char digit : text.substr(0, exponentAt)) {
        if (digit == '.') { fraction = true; continue; }
        numerator = numerator * 10 + (digit - '0');
        if (fraction) ++decimalPlaces;
    }
    int exponent = 0;
    if (exponentAt != std::string_view::npos) {
        auto exponentText = text.substr(exponentAt + 1);
        if (exponentText.starts_with('+')) exponentText.remove_prefix(1);
        std::from_chars(exponentText.data(), exponentText.data() + exponentText.size(), exponent);
    }
    const int power = exponent - decimalPlaces;
    for (int i = 0; i < std::abs(power); ++i) {
        if (power > 0) numerator *= 10;
        else denominator *= 10;
    }
    const boost::multiprecision::cpp_int ticks = numerator * rate.numerator / (denominator * rate.denominator);
    if (ticks > std::numeric_limits<long>::max())
        throw std::overflow_error("Run duration exceeds the supported step count.");
    return ticks.convert_to<long>();
}

ModelRunController::ModelRunController(s7studio::ISchemaRunner* runner)
    : runner_(runner)
{
}

void ModelRunController::run(const boost::json::value& runtimeJson,
                             const double dt,
                             const RunDuration& duration,
                             std::vector<RunProbe> probes) const
{
    if (!runner_) {
        return;
    }

    const long ticks = computeTicks(duration.hours, duration.minutes, duration.seconds, dt);
    runner_->run(runtimeJson, dt, ticks, std::move(probes));
}

void ModelRunController::run(const boost::json::value& runtimeJson,
                             const double dt,
                             const RunExecutionConfig& config,
                             std::vector<RunProbe> probes) const
{
    if (!runner_) {
        return;
    }

    runner_->run(runtimeJson, dt, computeTicks(config.runtimeSeconds, dt), std::move(probes));
}

void ModelRunController::pause() const
{
    if (runner_) {
        runner_->pause();
    }
}

void ModelRunController::resume() const
{
    if (runner_) {
        runner_->resume();
    }
}

void ModelRunController::stop() const
{
    if (runner_) {
        runner_->stop();
    }
}

void ModelRunController::reset() const
{
    if (runner_) {
        runner_->reset();
    }
}

auto ModelRunController::actionState() const -> RunActionState
{
    RunActionState state;
    if (!runner_) {
        return state;
    }

    const auto lifecycle = runner_->state();
    state.runEnabled = !runIsActive(lifecycle);
    state.pauseEnabled = lifecycle == RunState::Running;
    state.resumeEnabled = lifecycle == RunState::Paused;
    state.stopEnabled = lifecycle == RunState::Running || lifecycle == RunState::Pausing
        || lifecycle == RunState::Paused;
    state.resetEnabled = lifecycle != RunState::Preparing && lifecycle != RunState::Prepared;
    return state;
}

} // namespace s7studio::ui

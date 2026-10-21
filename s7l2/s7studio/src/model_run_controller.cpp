// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "model_run_controller.hpp"

#include <utility>
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

long ModelRunController::computeTicks(long runtimeSeconds, double dt)
{
    if (runtimeSeconds == -1) return -1;
    if (runtimeSeconds < 0)
        throw std::invalid_argument("Run duration must be nonnegative, or -1 for continuous execution.");
    return finiteTicks(static_cast<std::uint64_t>(runtimeSeconds), dt);
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

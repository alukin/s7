#include "model_run_controller.hpp"

#include <utility>

namespace s7studio::ui {

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

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

    const bool running = runner_->isRunning();
    const bool paused = runner_->isPaused();
    state.runEnabled = !running;
    state.pauseEnabled = running && !paused;
    state.resumeEnabled = running && paused;
    state.stopEnabled = running;
    state.resetEnabled = true;
    return state;
}

} // namespace s7studio::ui

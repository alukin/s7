#include "model_run_controller.hpp"

namespace s7studio::ui {

ModelRunController::ModelRunController(s7studio::ISchemaRunner* runner)
    : runner_(runner)
{
}

void ModelRunController::run(const boost::json::value& runtimeJson,
                             const double dt,
                             const RunDuration& duration) const
{
    if (!runner_) {
        return;
    }

    const long ticks = computeTicks(duration.hours, duration.minutes, duration.seconds, dt);
    runner_->run(runtimeJson, dt, ticks);
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
    state.resetEnabled = running;
    return state;
}

} // namespace s7studio::ui

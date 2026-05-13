#include "model_run_controller.hpp"

#include <QAction>
#include <QLabel>
#include <QSpinBox>

namespace s7ui::ui {

ModelRunController::ModelRunController(s7ui::ISchemaRunner* runner)
    : runner_(runner)
{
}

void ModelRunController::run(const boost::json::value& runtimeJson,
                              const double dt,
                              const QSpinBox* hours,
                              const QSpinBox* minutes,
                              const QSpinBox* seconds) const
{
    if (!runner_) return;

    const int h = hours   ? hours->value()   : 0;
    const int m = minutes ? minutes->value() : 1;
    const int s = seconds ? seconds->value() : 0;

    const long ticks = computeTicks(h, m, s, dt);
    runner_->run(runtimeJson, dt, ticks);
}

void ModelRunController::pause()  const { if (runner_) runner_->pause();  }
void ModelRunController::resume() const { if (runner_) runner_->resume(); }
void ModelRunController::stop()   const { if (runner_) runner_->stop();   }
void ModelRunController::reset()  const { if (runner_) runner_->reset();  }

void ModelRunController::updateActionStates(QAction* runAction,
                                             QAction* pauseAction,
                                             QAction* resumeAction,
                                             QAction* stopAction,
                                             QAction* resetAction) const
{
    if (!runner_) return;

    if (runAction)    runAction->setEnabled(!runner_->isRunning());
    if (pauseAction)  pauseAction->setEnabled(runner_->isRunning() && !runner_->isPaused());
    if (resumeAction) resumeAction->setEnabled(runner_->isRunning() &&  runner_->isPaused());
    if (stopAction)   stopAction->setEnabled(runner_->isRunning());
    if (resetAction)  resetAction->setEnabled(runner_->isRunning());
}

void ModelRunController::setStatusLabel(QLabel* label, const QString& status) const
{
    if (label) label->setText(status);
}

} // namespace s7ui::ui

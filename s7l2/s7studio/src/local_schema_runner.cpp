#include "local_schema_runner.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace s7studio {

namespace {

std::string joinValidationError(const std::exception& ex)
{
    return ex.what();
}

} // namespace

LocalSchemaRunner::LocalSchemaRunner(QObject* parent)
    : QObject(parent)
{
}

LocalSchemaRunner::~LocalSchemaRunner()
{
    stop();
    waitForCompletion();
}

bool LocalSchemaRunner::isRunning() const
{
    return running_.load();
}

bool LocalSchemaRunner::isPaused() const
{
    return paused_.load();
}

void LocalSchemaRunner::waitForCompletion()
{
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}

void LocalSchemaRunner::run(const boost::json::value& schemaJson, const double dt, const long ticks)
{
    if (isRunning()) {
        emit logMessage("[runtime] run skipped: runtime is already active");
        return;
    }

    waitForCompletion();

    try {
        schema_ = std::make_shared<s7::Schema>(runtime::buildSchema(schemaJson));
        runtime::validateSchema(*schema_);
        auto graph = s7::Compiler::compile(*schema_);
        runtime_ = std::make_unique<s7::NodeRuntime>(std::move(graph));
    } catch (const std::exception& ex) {
        emit statusChanged("error");
        emit logMessage(QString("[runtime] build failed: %1").arg(QString::fromUtf8(joinValidationError(ex))));
        emit runCompleted(false, QString::fromUtf8(joinValidationError(ex)));
        return;
    }

    totalTicks_ = ticks;
    completedTicks_.store(0);
    stopRequested_.store(false);
    paused_.store(false);
    running_.store(true);

    emit statusChanged("running");
    emit logMessage(QString("[runtime] starting local embedded run: dt=%1 ticks=%2").arg(dt).arg(ticks));

    worker_ = std::thread([this] {
        bool success = true;
        std::string error;

        try {
            for (long step = 0; step < totalTicks_; ++step) {
                if (stopRequested_.load()) {
                    break;
                }

                while (paused_.load() && !stopRequested_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                if (stopRequested_.load()) {
                    break;
                }

                runtime_->step();
                completedTicks_.store(step + 1);
                emit ticksProgress(step + 1, totalTicks_);
            }

            if (!stopRequested_.load()) {
                emitFinalSamples();
            }
        } catch (const std::exception& ex) {
            success = false;
            error = ex.what();
        }

        running_.store(false);
        paused_.store(false);

        if (stopRequested_.load() && success) {
            emit statusChanged("stopped");
            emit logMessage("[runtime] stopped");
        } else if (success) {
            emit statusChanged("completed");
            emit logMessage("[runtime] run completed");
        } else {
            emit statusChanged("error");
            emit logMessage(QString("[runtime] run failed: %1").arg(QString::fromStdString(error)));
        }

        emit runCompleted(success, QString::fromStdString(error));
    });
}

void LocalSchemaRunner::pause()
{
    if (!isRunning() || isPaused()) {
        emit logMessage("[runtime] pause skipped");
        return;
    }

    paused_.store(true);
    emit statusChanged("paused");
    emit logMessage("[runtime] paused");
}

void LocalSchemaRunner::resume()
{
    if (!isRunning() || !isPaused()) {
        emit logMessage("[runtime] resume skipped");
        return;
    }

    paused_.store(false);
    emit statusChanged("running");
    emit logMessage("[runtime] resumed");
}

void LocalSchemaRunner::stop()
{
    if (!isRunning()) {
        emit logMessage("[runtime] stop skipped");
        return;
    }

    stopRequested_.store(true);
    paused_.store(false);
}

void LocalSchemaRunner::reset()
{
    stop();
    waitForCompletion();

    schema_.reset();
    runtime_.reset();
    completedTicks_.store(0);
    totalTicks_ = 0;
    running_.store(false);
    paused_.store(false);
    stopRequested_.store(false);

    emit statusChanged("idle");
    emit logMessage("[runtime] reset");
}

void LocalSchemaRunner::emitFinalSamples()
{
    if (!schema_ || !runtime_) {
        return;
    }

    for (const auto& line : runtime::collectSamples(*schema_, *runtime_)) {
        emit logMessage(QString::fromStdString(line));
    }
}

} // namespace s7studio

#include "local_schema_runner.hpp"

#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <exception>
#include <thread>
#include <utility>

#include <algorithm>

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
    requestStop();
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

void LocalSchemaRunner::run(const boost::json::value& schemaJson, const double dt, const long ticks, std::vector<RunProbe> probes)
{
    if (isRunning()) {
        emit logMessage("[runtime] run skipped: runtime is already active");
        return;
    }

    waitForCompletion();

    dt_ = dt;
    probes_ = std::move(probes);

    if (!prepareRuntime(schemaJson)) {
        return;
    }

    totalTicks_ = ticks;
    completedTicks_.store(0);
    scopeMailbox_.clear();
    stopRequested_.store(false);
    paused_.store(false);
    running_.store(true);

    emit statusChanged("running");
    const QString ticksLabel = ticks < 0 ? QStringLiteral("continuous") : QString::number(ticks);
    emit logMessage(QString("[runtime] starting local embedded run: dt=%1 ticks=%2").arg(dt).arg(ticksLabel));

    startRunLoop();
}

auto LocalSchemaRunner::prepareRuntime(const boost::json::value& schemaJson) -> bool
{
    try {
        netEndpoint_ = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(netEndpoint_);
        schema_ = std::make_shared<s7::Schema>(runtime::buildSchema(schemaJson));
        // The run's own dt is authoritative over whatever (if anything) the
        // schema JSON's own top-level "dt" said, the same way it already
        // overrides the discretization control's previous value.
        if (dt_ > 0.0) {
            schema_->set_rate(s7::Rate::from_seconds(dt_));
        }
        runtime::validateSchema(*schema_);
        auto graph = s7::Compiler::compile(*schema_);
        runtime_ = std::make_unique<s7::NodeRuntime>(std::move(graph));
        // Model time of the embedded run: one step per discretization interval.
        const auto stepNs = static_cast<std::int64_t>(std::llround(dt_ * 1e9));
        runtime_->set_model_time_step_ns(stepNs > 0 ? std::optional<std::int64_t>{stepNs} : std::nullopt);
        unreadableProbes_.clear();
    } catch (const std::exception& ex) {
        emit statusChanged("error");
        emit logMessage(QString("[runtime] build failed: %1").arg(QString::fromUtf8(joinValidationError(ex))));
        emit runCompleted(false, QString::fromUtf8(joinValidationError(ex)));
        return false;
    }

    return true;
}

void LocalSchemaRunner::startRunLoop()
{
    worker_ = std::thread([this] { executeRunLoop(); });
}

void LocalSchemaRunner::executeRunLoop()
{
    bool success = true;
    std::string error;

    try {
        constexpr auto progressInterval = std::chrono::milliseconds(50);
        auto nextProgress = std::chrono::steady_clock::now();
        for (long step = 0; totalTicks_ < 0 || step < totalTicks_; ++step) {
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
            emitScopeSamples();
            if (const auto now = std::chrono::steady_clock::now(); now >= nextProgress) {
                emit ticksProgress(step + 1, totalTicks_);
                nextProgress = now + progressInterval;
            }
        }
        emit ticksProgress(completedTicks_.load(), totalTicks_);

        if (!stopRequested_.load()) {
            emitFinalSamples();
        }
    } catch (const std::exception& ex) {
        success = false;
        error = ex.what();
    }

    finalizeRun(success, error);
}

void LocalSchemaRunner::emitScopeSamples()
{
    if (!runtime_ || probes_.empty()) {
        return;
    }

    // Runs on the worker thread that steps the runtime (observation contract);
    // samples reach the UI through the bounded mailbox.
    s7::ObservationService observation(*runtime_);
    bool notify = false;
    for (const auto& probe : probes_) {
        try {
            const auto sample = observation.sample_output(probe.element, static_cast<std::size_t>(probe.port));
            const double timeSeconds = sample.model_time_ns
                ? static_cast<double>(*sample.model_time_ns) * 1e-9
                : static_cast<double>(sample.step) * dt_;
            notify |= scopeMailbox_.push(ScopeSample{probe.name, static_cast<long>(sample.step), timeSeconds, sample.to_display_double()});
        } catch (const std::exception& ex) {
            // Other probes keep rendering; report each unreadable one once per run.
            if (unreadableProbes_.insert(probe.name).second) {
                emit logMessage(QString("[runtime] probe %1 cannot be displayed: %2")
                                    .arg(QString::fromStdString(probe.name))
                                    .arg(QString::fromUtf8(ex.what())));
            }
        }
    }
    if (notify) {
        emit scopeSamplesAvailable();
    }
}

auto LocalSchemaRunner::takeScopeSamples() -> ScopeSampleBatch
{
    return scopeMailbox_.take();
}

void LocalSchemaRunner::finalizeRun(const bool success, const std::string& error)
{
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

    requestStop();
}

void LocalSchemaRunner::requestStop()
{
    stopRequested_.store(true);
    paused_.store(false);
}

void LocalSchemaRunner::reset()
{
    stop();
    waitForCompletion();

    schema_.reset();
    runtime_.reset();
    netEndpoint_.reset();
    s7::RuntimeNetContext::set_endpoint(nullptr);
    probes_.clear();
    scopeMailbox_.clear();
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

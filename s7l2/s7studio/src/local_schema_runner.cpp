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

void LocalSchemaRunner::run(const boost::json::value& schemaJson, const double dt, const long ticks)
{
    if (isRunning()) {
        emit logMessage("[runtime] run skipped: runtime is already active");
        return;
    }

    waitForCompletion();

    dt_ = dt;
    parseScopeConfigs(schemaJson);

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
        runtime::validateSchema(*schema_);
        auto graph = s7::Compiler::compile(*schema_);
        runtime_ = std::make_unique<s7::NodeRuntime>(std::move(graph));
        // Model time of the embedded run: one step per discretization interval.
        const auto stepNs = static_cast<std::int64_t>(std::llround(dt_ * 1e9));
        runtime_->set_model_time_step_ns(stepNs > 0 ? std::optional<std::int64_t>{stepNs} : std::nullopt);
        unreadableScopeChannels_.clear();
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

void LocalSchemaRunner::parseScopeConfigs(const boost::json::value& schemaJson)
{
    scopeConfigs_.clear();
    if (!schemaJson.is_object()) {
        return;
    }

    const auto& root = schemaJson.as_object();
    const auto elementsIt = root.find("elements");
    if (elementsIt == root.end() || !elementsIt->value().is_array()) {
        return;
    }

    for (const auto& elementValue : elementsIt->value().as_array()) {
        if (!elementValue.is_object()) {
            continue;
        }
        const auto& elementObject = elementValue.as_object();
        const auto idIt = elementObject.find("id");
        const auto typeIt = elementObject.find("type");
        if (idIt == elementObject.end() || typeIt == elementObject.end() ||
            !idIt->value().is_string() || !typeIt->value().is_string()) {
            continue;
        }

        if (typeIt->value().as_string() != "Scope") {
            continue;
        }

        std::size_t channelCount = 1;
        if (const auto paramsIt = elementObject.find("params");
            paramsIt != elementObject.end() && paramsIt->value().is_object()) {
            const auto& params = paramsIt->value().as_object();
            const auto ccIt = params.find("channel_count");
            if (ccIt != params.end()) {
                if (ccIt->value().is_uint64()) {
                    channelCount = static_cast<std::size_t>(ccIt->value().as_uint64());
                } else if (ccIt->value().is_int64() && ccIt->value().as_int64() > 0) {
                    channelCount = static_cast<std::size_t>(ccIt->value().as_int64());
                }
            }
        }

        ScopeConfig config;
        config.id = idIt->value().as_string().c_str();
        config.channelCount = std::max<std::size_t>(1, channelCount);
        scopeConfigs_.push_back(std::move(config));
    }
}

void LocalSchemaRunner::emitScopeSamples()
{
    if (!runtime_ || scopeConfigs_.empty()) {
        return;
    }

    // Runs on the worker thread that steps the runtime (observation contract);
    // samples reach the UI through the bounded mailbox.
    s7::ObservationService observation(*runtime_);
    bool notify = false;
    for (const auto& scope : scopeConfigs_) {
        for (std::size_t channel = 0; channel < scope.channelCount; ++channel) {
            try {
                const auto sample = observation.sample_output(scope.id, channel);
                const double timeSeconds = sample.model_time_ns
                    ? static_cast<double>(*sample.model_time_ns) * 1e-9
                    : static_cast<double>(sample.step) * dt_;
                notify |= scopeMailbox_.push(ScopeSample{scope.id,
                                                         static_cast<int>(channel),
                                                         static_cast<long>(sample.step),
                                                         timeSeconds,
                                                         sample.to_display_double()});
            } catch (const std::exception& ex) {
                // Other channels keep rendering; report each unreadable channel once per run.
                const auto key = scope.id + "#" + std::to_string(channel);
                if (unreadableScopeChannels_.insert(key).second) {
                    emit logMessage(QString("[runtime] scope %1 channel %2 cannot be displayed: %3")
                                        .arg(QString::fromStdString(scope.id))
                                        .arg(channel)
                                        .arg(QString::fromUtf8(ex.what())));
                }
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
    scopeConfigs_.clear();
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

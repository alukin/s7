#include "local_schema_runner.hpp"

#include <QThread>

#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <exception>
#include <thread>
#include <utility>

namespace s7studio {

LocalSchemaRunner::LocalSchemaRunner(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<RunState>();
}

LocalSchemaRunner::~LocalSchemaRunner()
{
    ++generation_;
    requestStop();
    waitForCompletion();
    releaseRuntime();
}

bool LocalSchemaRunner::isRunning() const { return runIsActive(state()); }
bool LocalSchemaRunner::isPaused() const { return state() == RunState::Paused; }

void LocalSchemaRunner::waitForCompletion()
{
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

void LocalSchemaRunner::postRunEvent(std::uint64_t id, std::function<void()> event)
{
    QMetaObject::invokeMethod(this, [this, id, event = std::move(event)] {
        if (id == generation_.load()) event();
    }, Qt::QueuedConnection);
}

void LocalSchemaRunner::publishState(RunState expected, std::uint64_t id)
{
    auto notify = [this, expected, id] {
        if (id != generation_.load() || state() != expected) return;
        emit stateChanged(expected);
        if (id == generation_.load() && state() == expected)
            emit statusChanged(QString::fromLatin1(runStateName(expected)));
    };
    if (QThread::currentThread() == thread()) notify();
    else postRunEvent(id, std::move(notify));
}

void LocalSchemaRunner::releaseRuntime()
{
    runtime_.reset();
    schema_.reset();
    if (netEndpoint_ && s7::RuntimeNetContext::endpoint() == netEndpoint_)
        s7::RuntimeNetContext::set_endpoint(nullptr);
    netEndpoint_.reset();
}

void LocalSchemaRunner::run(const boost::json::value& schemaJson, double dt, long ticks, std::vector<RunProbe> probes)
{
    if (isRunning()) {
        emit logMessage("[runtime] run skipped: runtime is already active");
        return;
    }
    waitForCompletion();
    const auto id = ++generation_;
    releaseRuntime();
    scopeMailbox_.clear(id);
    completedTicks_.store(0);
    pauseRequested_.store(false);
    stopRequested_.store(false);
    state_.store(RunState::Preparing);
    configuration_ = std::make_shared<const RunConfiguration>(RunConfiguration{id, schemaJson, dt, ticks, std::move(probes)});
    dt_ = dt;
    totalTicks_ = ticks;
    probes_ = configuration_->probes;
    publishState(RunState::Preparing, id);
    if (id != generation_.load()) return;
    try {
        if (!std::isfinite(dt) || dt <= 0.0) throw std::invalid_argument("Run dt must be finite and greater than zero.");
        if (ticks < -1) throw std::invalid_argument("Run ticks must be nonnegative, or -1 for continuous execution.");
        if (stopRequested_.load()) { finalizeRun(id, true, {}); return; }
        prepareRuntime(*configuration_);
        if (id != generation_.load()) return;
        if (stopRequested_.load()) { finalizeRun(id, true, {}); return; }
        state_.store(RunState::Prepared);
        publishState(RunState::Prepared, id);
        if (id != generation_.load()) return;
        if (stopRequested_.load()) { finalizeRun(id, true, {}); return; }
        state_.store(RunState::Running);
        publishState(RunState::Running, id);
        if (id != generation_.load()) return;
        const QString ticksLabel = ticks < 0 ? QStringLiteral("continuous") : QString::number(ticks);
        emit logMessage(QString("[runtime] starting local run %1: dt=%2 ticks=%3").arg(id).arg(dt).arg(ticksLabel));
        if (id != generation_.load()) return;
        worker_ = std::thread([this, configuration = configuration_] { executeRunLoop(configuration); });
    } catch (const std::exception& ex) {
        releaseRuntime();
        state_.store(RunState::Error);
        publishState(RunState::Error, id);
        emit logMessage(QString("[runtime] run %1 could not start: %2").arg(id).arg(QString::fromUtf8(ex.what())));
        emit runCompleted(false, QString::fromUtf8(ex.what()));
    } catch (...) {
        releaseRuntime();
        state_.store(RunState::Error);
        publishState(RunState::Error, id);
        emit logMessage(QString("[runtime] run %1 could not start: unknown exception").arg(id));
        emit runCompleted(false, QStringLiteral("Unknown exception while preparing the local run."));
    }
}

void LocalSchemaRunner::prepareRuntime(const RunConfiguration& configuration)
{
    netEndpoint_ = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    s7::RuntimeNetContext::set_endpoint(netEndpoint_);
    schema_ = std::make_shared<s7::Schema>(runtime::buildSchema(configuration.schemaJson));
    schema_->set_rate(s7::Rate::from_seconds(configuration.dt));
    runtime::validateSchema(*schema_);
    auto graph = s7::Compiler::compile(*schema_);
    runtime_ = std::make_unique<s7::NodeRuntime>(std::move(graph));
    const double ns = configuration.dt * 1e9;
    if (!std::isfinite(ns) || ns >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("Run dt exceeds the supported model-time interval.");
    const auto stepNs = static_cast<std::int64_t>(std::llround(ns));
    runtime_->set_model_time_step_ns(stepNs > 0 ? std::optional<std::int64_t>{stepNs} : std::nullopt);
    unreadableProbes_.clear();
}

void LocalSchemaRunner::executeRunLoop(std::shared_ptr<const RunConfiguration> configuration)
{
    bool success = true;
    std::string error;
    const auto id = configuration->id;
    try {
        constexpr auto progressInterval = std::chrono::milliseconds(50);
        auto nextProgress = std::chrono::steady_clock::now();
        for (long step = 0; configuration->ticks < 0 || step < configuration->ticks; ++step) {
            if (pauseRequested_.load()) {
                std::unique_lock lock(controlMutex_);
                if (pauseRequested_.load() && !stopRequested_.load()) {
                    state_.store(RunState::Paused);
                    lock.unlock();
                    publishState(RunState::Paused, id);
                    lock.lock();
                    controlChanged_.wait(lock, [this] { return !pauseRequested_.load() || stopRequested_.load(); });
                }
            }
            if (stopRequested_.load()) break;
            runtime_->step();
            completedTicks_.store(step + 1);
            emitScopeSamples();
            if (const auto now = std::chrono::steady_clock::now(); now >= nextProgress) {
                const long completed = step + 1;
                postRunEvent(id, [this, completed] { emit ticksProgress(completed, totalTicks_); });
                nextProgress = now + progressInterval;
            }
        }
        const long completed = completedTicks_.load();
        postRunEvent(id, [this, completed] { emit ticksProgress(completed, totalTicks_); });
        if (!stopRequested_.load()) emitFinalSamples();
    } catch (const std::exception& ex) {
        success = false;
        error = ex.what();
    } catch (...) {
        success = false;
        error = "Unknown exception while executing the local graph.";
    }
    finalizeRun(id, success, error);
}

void LocalSchemaRunner::emitScopeSamples()
{
    if (!runtime_ || probes_.empty()) return;
    s7::ObservationService observation(*runtime_);
    bool notify = false;
    const auto id = configuration_->id;
    for (const auto& probe : probes_) {
        try {
            const auto sample = observation.sample_output(probe.element, static_cast<std::size_t>(probe.port));
            const double timeSeconds = sample.model_time_ns
                ? static_cast<double>(*sample.model_time_ns) * 1e-9 : static_cast<double>(sample.step) * dt_;
            notify |= scopeMailbox_.push(ScopeSample{probe.name, static_cast<long>(sample.step), timeSeconds,
                                                    sample.to_display_double(), id});
        } catch (const std::exception& ex) {
            if (unreadableProbes_.insert(probe.name).second) {
                const auto message = QString("[runtime] probe %1 cannot be displayed: %2")
                    .arg(QString::fromStdString(probe.name)).arg(QString::fromUtf8(ex.what()));
                postRunEvent(id, [this, message] { emit logMessage(message); });
            }
        }
    }
    if (notify) postRunEvent(id, [this] { emit scopeSamplesAvailable(); });
}

auto LocalSchemaRunner::takeScopeSamples() -> ScopeSampleBatch { return scopeMailbox_.take(); }

void LocalSchemaRunner::finalizeRun(std::uint64_t id, bool success, const std::string& error)
{
    RunState terminal;
    {
        std::lock_guard lock(controlMutex_);
        terminal = !success ? RunState::Error : stopRequested_.load() ? RunState::Stopped : RunState::Completed;
        pauseRequested_.store(false);
        state_.store(terminal);
    }
    publishState(terminal, id);
    const auto detail = QString::fromStdString(error);
    postRunEvent(id, [this, id, terminal, success, detail] {
        emit logMessage(QString("[runtime] run %1 %2%3").arg(id)
            .arg(QString::fromLatin1(runStateName(terminal)))
            .arg(detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
        emit runCompleted(success, detail);
    });
}

void LocalSchemaRunner::pause()
{
    {
        std::lock_guard lock(controlMutex_);
        if (state() != RunState::Running) return;
        pauseRequested_.store(true);
        state_.store(RunState::Pausing);
    }
    publishState(RunState::Pausing, generation_.load());
}

void LocalSchemaRunner::resume()
{
    {
        std::lock_guard lock(controlMutex_);
        if (state() != RunState::Paused && state() != RunState::Pausing) return;
        pauseRequested_.store(false);
        state_.store(RunState::Running);
    }
    controlChanged_.notify_all();
    publishState(RunState::Running, generation_.load());
}

void LocalSchemaRunner::stop() { requestStop(); }

void LocalSchemaRunner::requestStop()
{
    {
        std::lock_guard lock(controlMutex_);
        if (!isRunning()) return;
        stopRequested_.store(true);
        pauseRequested_.store(false);
        state_.store(RunState::Stopping);
    }
    controlChanged_.notify_all();
    publishState(RunState::Stopping, generation_.load());
}

void LocalSchemaRunner::reset()
{
    ++generation_; // queued owner events from the retiring run are now invalid
    requestStop();
    waitForCompletion();
    releaseRuntime();
    configuration_.reset();
    probes_.clear();
    scopeMailbox_.clear();
    completedTicks_.store(0);
    totalTicks_ = 0;
    pauseRequested_.store(false);
    stopRequested_.store(false);
    state_.store(RunState::Idle);
    publishState(RunState::Idle, generation_.load());
    emit logMessage("[runtime] reset");
}

void LocalSchemaRunner::emitFinalSamples()
{
    if (!schema_ || !runtime_) return;
    for (const auto& line : runtime::collectSamples(*schema_, *runtime_)) {
        const auto message = QString::fromStdString(line);
        postRunEvent(configuration_->id, [this, message] { emit logMessage(message); });
    }
}

} // namespace s7studio

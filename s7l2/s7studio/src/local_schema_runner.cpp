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

LocalSchemaRunner::LocalSchemaRunner(QObject* parent, observation::StorageOptions storage)
    : QObject(parent), storage_(std::move(storage))
{
    qRegisterMetaType<RunState>();
}

LocalSchemaRunner::~LocalSchemaRunner()
{
    ++generation_;
    requestStop();
    waitForCompletion();
    if (recording_ && !recording_->status().finished) recording_->finish(true);
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
    auto storage = storage_;
    if (storage.temporaryDirectory.isEmpty()) storage.temporaryDirectory = qEnvironmentVariable("S7STUDIO_RECORDING_TEMP_DIR");
    recording_ = std::make_shared<observation::ObservationSource>(id, std::move(storage));
    probeChannels_.clear();
    completedTicks_.store(0);
    pauseRequested_.store(false);
    stopRequested_.store(false);
    state_.store(RunState::Preparing);
    configuration_ = std::make_shared<const RunConfiguration>(RunConfiguration{id, schemaJson, dt, ticks, std::move(probes)});
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
        recording_->finish(false, QString::fromUtf8(ex.what()));
        state_.store(RunState::Error);
        publishState(RunState::Error, id);
        if (id != generation_.load()) return;
        emit logMessage(QString("[runtime] run %1 could not start: %2").arg(id).arg(QString::fromUtf8(ex.what())));
        emit runCompleted(false, QString::fromUtf8(ex.what()));
    } catch (...) {
        releaseRuntime();
        recording_->finish(false, QStringLiteral("Unknown preparation exception."));
        state_.store(RunState::Error);
        publishState(RunState::Error, id);
        if (id != generation_.load()) return;
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
    runtime_->set_model_time_rate(s7::Rate::from_seconds(configuration.dt));
    s7::ObservationService observation(*runtime_);
    std::set<std::string> names;
    for (const auto& probe : configuration.probes) {
        if (probe.name.empty() || !names.insert(probe.name).second || probe.port < 0)
            throw std::invalid_argument("Probe names must be nonempty and unique, and output ports nonnegative.");
        const auto sample = observation.sample_output(probe.element, static_cast<std::size_t>(probe.port));
        if (sample.is_complex())
            throw std::invalid_argument("Probe '" + probe.name + "' has unsupported complex type " +
                std::string(s7::signal_type_name(sample.type)) + "; scalar viewers require a real-valued output.");
        probeChannels_.push_back(recording_->channel(QString::fromStdString(probe.name),
            QString::fromUtf8(s7::signal_type_name(sample.type).data(), static_cast<int>(s7::signal_type_name(sample.type).size()))));
        if (sample.type != s7::SignalType::F64) {
            const auto message = QStringLiteral("[runtime] probe %1: %2 is explicitly converted to stored binary64; wider scalar values may round.")
                .arg(QString::fromStdString(probe.name)).arg(QString::fromStdString(std::string(s7::signal_type_name(sample.type))));
            postRunEvent(configuration.id, [this, message] { emit logMessage(message); });
        }
    }
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
    for (std::size_t i = 0; i < probes_.size(); ++i) {
        const auto& probe = probes_[i];
        const auto sample = observation.sample_output(probe.element, static_cast<std::size_t>(probe.port));
        const double timeSeconds = sample.model_time_seconds.value();
        const auto value = sample.to_display_double();
        if (!recording_->append({probeChannels_[i], static_cast<std::int64_t>(sample.step), timeSeconds, value}))
            throw std::runtime_error("Observation capture failed: " + recording_->status().storageError.toStdString());
        notify |= scopeMailbox_.push(ScopeSample{probe.name, static_cast<long>(sample.step), timeSeconds, value, id});
    }
    if (notify) postRunEvent(id, [this] { emit scopeSamplesAvailable(); });
}

auto LocalSchemaRunner::takeScopeSamples() -> ScopeSampleBatch { return scopeMailbox_.take(); }

void LocalSchemaRunner::finalizeRun(std::uint64_t id, bool success, const std::string& error)
{
    QString detail = QString::fromStdString(error);
    const bool stopped = stopRequested_.load(); // latch outcome before final storage flush
    if (recording_ && !recording_->finish(stopped, detail)) {
        success = false;
        const auto status = recording_->status();
        if (detail.isEmpty()) detail = status.storageError.isEmpty() ? status.runError : status.storageError;
    }
    RunState terminal;
    {
        std::lock_guard lock(controlMutex_);
        terminal = !success ? RunState::Error : stopped ? RunState::Stopped : RunState::Completed;
        pauseRequested_.store(false);
        state_.store(terminal);
    }
    publishState(terminal, id);
    postRunEvent(id, [this, id, terminal, success, detail] {
        emit logMessage(QString("[runtime] run %1 %2%3").arg(id)
            .arg(QString::fromLatin1(runStateName(terminal)))
            .arg(detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
        if (id == generation_.load()) emit runCompleted(success, detail);
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
    if (recording_ && !recording_->status().finished) recording_->finish(true);
    releaseRuntime();
    configuration_.reset();
    recording_.reset();
    probeChannels_.clear();
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

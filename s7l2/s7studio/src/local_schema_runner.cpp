// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "local_schema_runner.hpp"
#include "run_provenance.hpp"

#include <QThread>

#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <chrono>
#include <map>
#include <algorithm>
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
    for (const auto& source : std::as_const(arraySources_)) source.mailbox->close();
    arraySources_.clear();
    arrayRoutes_.clear();
    compiledProbes_.reset(); // addresses storage owned by the runtime
    hardwareRun_.reset();
    hardwarePaced_ = false;
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
    recordingNotifyPending_.store(false);
    auto storage = storage_;
    if (storage.temporaryDirectory.isEmpty()) storage.temporaryDirectory = qEnvironmentVariable("S7STUDIO_RECORDING_TEMP_DIR");
    const bool liveOnly = !probes.empty() && std::ranges::none_of(probes, [](const auto& p) { return p.record; });
    recording_ = liveOnly ? nullptr : std::make_shared<observation::ObservationSource>(id, std::move(storage));
    liveSources_.clear(); liveRoutes_.clear(); readIndices_.clear(); distinctValues_.clear();
    probeChannels_.clear(); recordedProbes_.clear(); compiledProbes_.reset(); ring_.reset();
    throttled_ = {}; droppedFrames_ = 0;
    completedTicks_.store(0);
    pauseRequested_.store(false);
    stopRequested_.store(false);
    hardwareStop_ = std::stop_source{};
    state_.store(RunState::Preparing);
    configuration_ = std::make_shared<const RunConfiguration>(
        RunConfiguration{id, schemaJson, dt, ticks, std::move(probes), recordingOverflow_, std::exchange(keepSdrActive_, false)});
    totalTicks_ = ticks;
    probes_.clear();
    for (const auto& probe : configuration_->probes)
        if (probe.arrayObserver == diagram::ArrayObserverKind::None) probes_.push_back(probe);
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
        if (recording_) recording_->finish(false, QString::fromUtf8(ex.what()));
        for (auto& route : liveRoutes_) if (route.source) route.source->finish(QStringLiteral("Run preparation failed."));
        state_.store(RunState::Error);
        publishState(RunState::Error, id);
        if (id != generation_.load()) return;
        emit logMessage(QString("[runtime] run %1 could not start: %2").arg(id).arg(QString::fromUtf8(ex.what())));
        emit runCompleted(false, QString::fromUtf8(ex.what()));
    } catch (...) {
        releaseRuntime();
        if (recording_) recording_->finish(false, QStringLiteral("Unknown preparation exception."));
        for (auto& route : liveRoutes_) if (route.source) route.source->finish(QStringLiteral("Run preparation failed."));
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
    auto prepared = prepareRunProvenance(configuration);
    schema_ = std::make_shared<s7::Schema>(runtime::buildSchema(prepared.effectiveSchema));
    const auto rate = s7::Rate::from_seconds(configuration.dt);
    schema_->set_rate(rate);
    runtime::validateSchema(*schema_);
    auto graph = s7::Compiler::compile(*schema_);
    if (hardwareFactory_) hardwareRun_.reset(hardwareFactory_(graph, rate));
    hardwarePaced_ = static_cast<bool>(hardwareRun_);
    if (!hardwarePaced_) {
        if (std::ranges::any_of(graph.elements, [](const auto& entry) {
            const auto type = entry.second->type_name();
            return type == "AudioSource" || type == "AudioBlockSource" || type == "AudioSink" || type == "SdrSource" ||
                   type == "SdrSink" || type == "SdrBlockSource";
        })) throw std::runtime_error("Studio hardware execution requires the matching S7 hardware plugins.");
        runtime_ = std::make_unique<s7::NodeRuntime>(std::move(graph));
        runtime_->set_model_time_rate(rate);
    }
    // Hardware runs never wait for observation; nor do runs without a
    // recording, since live views always keep only the newest data (D1).
    activeOverflow_ = hardwarePaced_ || !recording_ ? RecordingOverflow::Drop : configuration.recordingOverflow;
    prepared.provenance.insert(QStringLiteral("recording_overflow"), QString::fromLatin1(recordingOverflowName(activeOverflow_)));
    if (recording_) recording_->setProvenance(prepared.provenance);
    std::map<std::string, std::size_t> groups;
    std::vector<QVector<observation::LiveChannel>> liveChannels;
    std::map<std::pair<std::string, int>, std::size_t> distinctReads;
    std::map<const s7::ArrayDescriptor*, std::size_t> distinctArrays;
    const auto& running = hardwarePaced_ ? hardwareRun_->runtime() : *runtime_;
    std::vector<s7::ProbePoint> distinct;
    std::size_t liveCount = 0;
    std::set<std::string> names;
    for (const auto& probe : configuration.probes) {
        if (probe.name.empty() || !names.insert(probe.name).second || probe.port < 0)
            throw std::invalid_argument("Probe names must be nonempty and unique, and output ports nonnegative.");
        if (probe.arrayObserver != diagram::ArrayObserverKind::None) {
            if (probe.record || !probe.liveGroup.empty())
                throw std::invalid_argument("Array viewers require a presentation-only subscription.");
            const auto port = static_cast<std::size_t>(probe.port);
            const auto* shape = running.array_output_descriptor(probe.element, port);
            if (const auto problem = diagram::arrayObserverPortProblem(probe.arrayObserver, shape); !problem.isEmpty())
                throw std::invalid_argument(probe.name + ": " + problem.toStdString());
            const auto [route, added] = distinctArrays.emplace(shape, arrayRoutes_.size());
            if (added) arrayRoutes_.push_back({{probe.element, port}, shape->byte_size()});
            auto mailbox = std::make_shared<s7::ArrayMailbox>();
            arrayRoutes_[route->second].mailboxes.push_back(mailbox);
            arraySources_.insert(QString::fromStdString(probe.name), {probe.arrayObserver, std::move(mailbox)});
            continue;
        }
        if (!probe.record && probe.liveGroup.empty()) throw std::invalid_argument("Probe has no capture subscription.");
        const auto [read, added] = distinctReads.emplace(std::pair{probe.element, probe.port}, distinct.size());
        if (added) distinct.push_back({probe.element, static_cast<std::size_t>(probe.port)});
        readIndices_.push_back(read->second);
    }
    // Resolved once: per-step reads copy from output storage directly.
    compiledProbes_.emplace(s7::ObservationService(running).compile(distinct));
    distinctValues_.resize(distinct.size());
    for (std::size_t index = 0; index < probes_.size(); ++index) {
        const auto& probe = probes_[index];
        const auto signal = compiledProbes_->point(readIndices_[index]).type;
        if (s7::is_complex_signal_type(signal))
            throw std::invalid_argument("Probe '" + probe.name + "' has unsupported complex type " +
                std::string(s7::signal_type_name(signal)) + "; scalar viewers require a real-valued output.");
        const auto type = QString::fromStdString(std::string(s7::signal_type_name(signal)));
        probeChannels_.push_back(probe.record ? recording_->channel(QString::fromStdString(probe.name), type) : 0);
        if (probe.record) recordedProbes_.push_back(index);
        if (!probe.liveGroup.empty()) {
            if (++liveCount > observation::LiveAcquisition::maxChannels)
                throw std::invalid_argument("Prototype supports at most 8 live channels across all workspaces.");
            auto [group, added] = groups.emplace(probe.liveGroup, groups.size());
            if (added) { liveChannels.emplace_back(); liveRoutes_.emplace_back(); }
            liveChannels[group->second].push_back({QString::fromStdString(probe.name), QString::fromStdString(probe.unit), type});
            liveRoutes_[group->second].probes.push_back(index);
        }
        if (signal != s7::SignalType::F64) {
            const auto message = QStringLiteral("[runtime] probe %1: %2 is explicitly converted to stored binary64; wider scalar values may round.")
                .arg(QString::fromStdString(probe.name)).arg(QString::fromStdString(std::string(s7::signal_type_name(signal))));
            postRunEvent(configuration.id, [this, message] { emit logMessage(message); });
        }
    }
    for (const auto& [name, index] : groups) {
        auto source = std::make_shared<observation::LiveAcquisition>(configuration.id, configuration.dt,
            std::move(liveChannels[index]), prepared.provenance,
            rate.numerator, rate.denominator);
        if (configuration.ticks > 0) source->setPlannedDuration(static_cast<double>(configuration.ticks)*configuration.dt);
        liveSources_.insert(QString::fromStdString(name), source);
        liveRoutes_[index].source = source;
        liveRoutes_[index].values.resize(liveRoutes_[index].probes.size());
    }
}

void LocalSchemaRunner::executeRunLoop(std::shared_ptr<const RunConfiguration> configuration)
{
    bool success = true;
    std::string error;
    const auto id = configuration->id;
    const auto started = std::chrono::steady_clock::now();
    try {
        startObservation(id);
        if (hardwarePaced_) {
            executeHardwareRun(*configuration);
        } else {
            constexpr auto progressInterval = std::chrono::milliseconds(50);
            auto nextProgress = std::chrono::steady_clock::now();
            for (long step = 0; configuration->ticks < 0 || step < configuration->ticks; ++step) {
                if (pauseRequested_.load() && ring_) {
                    // A paused run shows its latest samples as soon as it
                    // reports Paused: deliver them before pausing.
                    publishBlock(false);
                    ring_->waitDrained();
                }
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
                captureArrays(*runtime_);
                captureFrame();
                if (const auto now = std::chrono::steady_clock::now(); now >= nextProgress) {
                    const long completed = step + 1;
                    postRunEvent(id, [this, completed] { emit ticksProgress(completed, totalTicks_); });
                    nextProgress = now + progressInterval;
                }
            }
            const long completed = completedTicks_.load();
            postRunEvent(id, [this, completed] { emit ticksProgress(completed, totalTicks_); });
            if (!stopRequested_.load()) emitFinalSamples();
        }
    } catch (const std::exception& ex) {
        success = false;
        error = ex.what();
    } catch (...) {
        success = false;
        error = "Unknown exception while executing the local graph.";
    }
    // Every captured frame reaches its destinations before the recording is
    // finished; a capture failure fails the run as before.
    if (auto observation = finishObservation(); !observation.empty() && success) {
        success = false;
        error = std::move(observation);
    }
    const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (throttled_.count() > 0 && wall > 0) {
        const auto message = QStringLiteral("[runtime] run throttled by recording: %1% of wall time (%2 s)")
            .arg(100.0 * std::chrono::duration<double>(throttled_).count() / wall, 0, 'f', 1)
            .arg(std::chrono::duration<double>(throttled_).count(), 0, 'f', 3);
        postRunEvent(id, [this, message] { emit logMessage(message); });
    }
    if (droppedFrames_ > 0) {
        const auto message = QStringLiteral("[runtime] observation kept up by dropping %1 frames; the recording is incomplete")
            .arg(droppedFrames_);
        postRunEvent(id, [this, message] { emit logMessage(message); });
    }
    finalizeRun(id, success, error);
}

void LocalSchemaRunner::executeHardwareRun(const RunConfiguration& configuration)
{
    // The worker owns stream preparation, transfer and teardown. Stop uses
    // only a token so it can race safely with any of those operations.
    auto run = std::move(hardwareRun_);
    const auto stop = hardwareStop_.get_token();
    if (!configuration.ticks || stop.stop_requested()) return;
    const auto description = QString::fromStdString(run->prepare(configuration.keepSdrActive));
    const auto id = configuration.id;
    postRunEvent(id, [this, description] {
        emit logMessage(QStringLiteral("[runtime] device-paced run using %1").arg(description));
    });
    auto& runtime = run->runtime();
    const auto trackTicks = [this](std::uint64_t frames) {
        completedTicks_.store(static_cast<long>(std::min<std::uint64_t>(frames, std::numeric_limits<long>::max())));
    };
    auto nextProgress = std::chrono::steady_clock::now();
    const auto frames = configuration.ticks < 0 ? std::numeric_limits<std::uint64_t>::max()
                                               : static_cast<std::uint64_t>(configuration.ticks);
    try {
        run->execute(frames, stop, [&](std::uint64_t completed) {
            captureArrays(runtime);
            captureFrame();
            if (completed != 1 && completed % 1024 != 0) return;
            trackTicks(completed);
            const auto now = std::chrono::steady_clock::now();
            if (now < nextProgress) return;
            const auto ticks = completedTicks_.load();
            postRunEvent(id, [this, ticks] { emit ticksProgress(ticks, totalTicks_); });
            nextProgress = now + std::chrono::milliseconds(50);
        });
    } catch (...) {
        trackTicks(runtime.step_count());
        throw;
    }
    trackTicks(runtime.step_count());
    const auto completed = completedTicks_.load();
    postRunEvent(id, [this, completed] { emit ticksProgress(completed, totalTicks_); });
}

void LocalSchemaRunner::captureArrays(const s7::NodeRuntime& runtime)
{
    const s7::ObservationService observer(runtime);
    for (auto& route : arrayRoutes_) {
        const auto& [element, port] = route.probe;
        const auto step = runtime.committed_array_step(element, port);
        if (!step || step == route.lastStep) continue;
        route.lastStep = step;

        // Resolve from the committed lease even for a forwarded or delayed output.
        const auto admitted = runtime.admit_array_output(element, port, route.admission, 1, route.maxBytes);
        auto sample = admitted ? observer.try_retain_array_output(route.probe, route.admission)
                               : std::expected<s7::CommittedArraySample, s7::ArrayPoolError>(std::unexpected(admitted.error()));
        if (admitted && !sample && sample.error() == s7::ArrayPoolError::Exhausted) {
            bool released = false;
            for (const auto& mailbox : route.mailboxes) released |= mailbox->discard_pending();
            if (released) sample = observer.try_retain_array_output(route.probe, route.admission);
        }
        for (const auto& mailbox : route.mailboxes) mailbox->offer(sample);
    }
}

void LocalSchemaRunner::captureFrame()
{
    if (!ring_) return;
    compiledProbes_->read_display(distinctValues_);
    auto& block = ring_->current();
    const auto frame = block.frames;
    if (frame == 0) blockStart_ = std::chrono::steady_clock::now();
    block.steps[frame] = static_cast<std::int64_t>(compiledProbes_->step());
    block.times[frame] = compiledProbes_->model_time_seconds().value();
    const auto width = probes_.size();
    auto* values = block.values.data() + frame * width;
    for (std::size_t probe = 0; probe < width; ++probe) values[probe] = distinctValues_[readIndices_[probe]];
    block.frames = frame + 1;
    // D3: hand over at 1,024 frames or after 20 ms, checking the clock every
    // 64 frames rather than on every step.
    constexpr auto flushInterval = std::chrono::milliseconds(20);
    if (block.frames == ring_->capacity() ||
        ((block.frames & 63) == 0 && std::chrono::steady_clock::now() - blockStart_ >= flushInterval))
        publishBlock();
}

void LocalSchemaRunner::publishBlock(const bool midRun)
{
    if (!ring_ || ring_->current().frames == 0) return;
    if (ring_->failed()) throw std::runtime_error("Observation capture failed: " + ring_->error());
    if (ring_->tryPublish()) return;
    if (midRun && activeOverflow_ == RecordingOverflow::Drop) {
        // Never wait: count the loss; live views see the step gap.
        auto& block = ring_->current();
        droppedFrames_ += block.frames;
        if (recording_ && !recordedProbes_.empty()) recording_->addCaptureLoss(block.frames * recordedProbes_.size());
        block.frames = 0;
        return;
    }
    bool published = false;
    const auto waited = ring_->publishWaiting(published);
    if (midRun) throttled_ += waited;
    if (!published) throw std::runtime_error("Observation capture failed: " + ring_->error());
}

void LocalSchemaRunner::startObservation(const std::uint64_t id)
{
    if (probes_.empty()) return;
    // D3: 1,024-frame blocks; up to 64 of them, fewer when many probes would
    // make the ring larger than about 16 MiB.
    constexpr std::size_t frames = 1024, maxSlots = 64, budget = std::size_t(16) << 20;
    const auto blockBytes = frames * (sizeof(std::int64_t) + sizeof(double) * (1 + probes_.size()));
    const auto slotCount = std::clamp<std::size_t>(budget / blockBytes, 4, maxSlots);
    ring_ = std::make_unique<observation::ProbeBlockRing>(slotCount, frames, probes_.size());
    recordBatch_.reserve(frames * recordedProbes_.size());
    for (auto& route : liveRoutes_) route.values.reserve(frames * route.probes.size());
    distributor_ = std::thread([this, id] { distributeBlocks(id); });
}

std::string LocalSchemaRunner::finishObservation()
{
    if (!ring_) return {};
    std::string error;
    try {
        publishBlock(false);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    ring_->close();
    if (distributor_.joinable()) distributor_.join();
    if (error.empty() && ring_->failed()) error = "Observation capture failed: " + ring_->error();
    return error;
}

void LocalSchemaRunner::distributeBlocks(const std::uint64_t id)
{
    while (const auto* block = ring_->waitReadable()) {
        if (!ring_->failed()) {
            try {
                distribute(*block, id);
            } catch (const std::exception& ex) {
                ring_->fail(ex.what());
            } catch (...) {
                ring_->fail("unknown observation delivery error");
            }
        }
        ring_->release();
    }
}

void LocalSchemaRunner::distribute(const observation::ProbeBlock& block, const std::uint64_t id)
{
    const auto width = ring_->width();
    const auto frames = block.frames;
    if (recording_ && !recordedProbes_.empty()) {
        recordBatch_.clear();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto step = block.steps[frame];
            const auto time = block.times[frame];
            const auto* values = block.values.data() + frame * width;
            for (const auto probe : recordedProbes_) {
                recordBatch_.push_back({probeChannels_[probe], step, time, values[probe]});
            }
        }
        if (!recording_->append(std::span<const observation::Sample>(recordBatch_)))
            throw std::runtime_error(recording_->status().storageError.toStdString());
        if (frames > 0 && !recordingNotifyPending_.exchange(true)) postRunEvent(id, [this] { emit recordingAdvanced(); });
    }
    for (auto& route : liveRoutes_) {
        const auto routeWidth = route.probes.size();
        route.values.resize(frames * routeWidth);
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::size_t i = 0; i < routeWidth; ++i)
                route.values[frame * routeWidth + i] = block.values[frame * width + route.probes[i]];
        route.source->append(std::span<const std::int64_t>(block.steps.data(), frames),
                             std::span<const double>(block.times.data(), frames), route.values);
    }
}

void LocalSchemaRunner::finalizeRun(std::uint64_t id, bool success, const std::string& error)
{
    QString detail = QString::fromStdString(error);
    const bool stopped = stopRequested_.load(); // latch outcome before final storage flush
    if (recording_ && !recording_->finish(stopped, detail)) {
        const auto status = recording_->status();
        // In drop mode (D1) lost samples are expected: the recording is marked
        // incomplete and the log says so, but the run itself succeeded.
        const bool onlyDropped = activeOverflow_ == RecordingOverflow::Drop &&
            status.storageError.isEmpty() && status.runError.isEmpty();
        if (!onlyDropped) {
            success = false;
            if (detail.isEmpty()) detail = status.storageError.isEmpty() ? status.runError : status.storageError;
        }
    }
    for (auto& route : liveRoutes_) if (route.source) route.source->finish(detail);
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
        if (state() != RunState::Running || !canPause()) return;
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
    hardwareStop_.request_stop();
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
    for (auto& route : liveRoutes_) if (route.source) route.source->finish();
    liveSources_.clear(); liveRoutes_.clear(); readIndices_.clear(); distinctValues_.clear();
    probeChannels_.clear(); recordedProbes_.clear(); compiledProbes_.reset(); ring_.reset();
    probes_.clear();
    recordingNotifyPending_.store(false);
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

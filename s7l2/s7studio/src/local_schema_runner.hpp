// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "is_schema_runner.hpp"
#include "local_runtime_v2.hpp"
#include "observation/observation_source.hpp"
#include "observation/live_acquisition.hpp"
#include "observation/array_sources.hpp"
#include "observation/probe_block_ring.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <set>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <s7/transport.hpp>
#include <s7/hardware_live_run.hpp>
#include <s7/observe.hpp>

#include <chrono>
#include <optional>

namespace s7studio {

class LocalSchemaRunner : public QObject, public ISchemaRunner
{
    Q_OBJECT

public:
    explicit LocalSchemaRunner(QObject* parent = nullptr, observation::StorageOptions storage = {});
    ~LocalSchemaRunner() override;

    void run(const boost::json::value& schemaJson, double dt, long ticks, std::vector<RunProbe> probes) override;
    void pause() override;
    void resume() override;
    void stop() override;
    void reset() override;
    void setHardwareFactory(s7::hardware::CreateLiveRun factory) { hardwareFactory_ = factory; }
    void setRecordingOverflow(RecordingOverflow overflow) override { recordingOverflow_ = overflow; }

    // Commands and snapshot access belong to the QObject owner thread.
    [[nodiscard]] RunState state() const override { return state_.load(); }
    [[nodiscard]] auto activeConfiguration() const -> std::shared_ptr<const RunConfiguration> { return configuration_; }
    [[nodiscard]] auto recordedSource() const -> std::shared_ptr<const observation::ObservationSource> { return recording_; }
    [[nodiscard]] auto liveSources() const -> observation::LiveSources { return liveSources_; }
    [[nodiscard]] auto arraySources() const -> observation::ArraySources { return arraySources_; }
    [[nodiscard]] long completedTicks() const { return completedTicks_.load(); }
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] bool isPaused() const override;
    [[nodiscard]] bool canPause() const override { return !hardwarePaced_; }

    // UI thread: re-arms recordingAdvanced(). Call it before reading the
    // recording: samples appended after it notify again, and the exchange
    // synchronizes with the distributor's, so earlier appends are visible.
    void acknowledgeRecordingAdvanced() { static_cast<void>(recordingNotifyPending_.exchange(false)); }

signals:
    void stateChanged(s7studio::RunState state);
    void logMessage(const QString& message);
    void statusChanged(const QString& status);
    void runCompleted(bool success, const QString& error);
    // Throttled to at most 20 per second, plus the final count.
    void ticksProgress(long completed, long total);
    // recordedSource() has new samples. At most one is pending until
    // acknowledgeRecordingAdvanced().
    void recordingAdvanced();

private:
    // Observation (plan WP3): the stepping thread copies probe values into
    // blocks; one distributor thread delivers them to the recording and the
    // live views, and notifies the UI.
    void captureFrame();
    void captureArrays(const s7::NodeRuntime& runtime);
    // midRun: a hand-over while stepping continues. Only then may drop mode
    // discard a block when the ring is full, and only then does waiting for
    // a slot count as throttling. Pause and the end of the run hand over the
    // partial block losslessly; stepping has already stopped.
    void publishBlock(bool midRun = true);
    void startObservation(std::uint64_t id);
    [[nodiscard]] std::string finishObservation();
    void distributeBlocks(std::uint64_t id);
    void distribute(const observation::ProbeBlock& block, std::uint64_t id);
    void requestStop();
    void prepareRuntime(const RunConfiguration& configuration);
    void executeRunLoop(std::shared_ptr<const RunConfiguration> configuration);
    void executeHardwareRun(const RunConfiguration& configuration);
    void finalizeRun(std::uint64_t id, bool success, const std::string& error);
    void publishState(RunState state, std::uint64_t id);
    void postRunEvent(std::uint64_t id, std::function<void()> event);
    void releaseRuntime();
    void waitForCompletion();
    void emitFinalSamples();

    std::shared_ptr<s7::Schema> schema_;
    std::unique_ptr<s7::NodeRuntime> runtime_;
    s7::hardware::CreateLiveRun hardwareFactory_{};
    std::unique_ptr<s7::hardware::LiveRun> hardwareRun_;
    std::stop_source hardwareStop_;
    bool hardwarePaced_{};
    s7::transport::RuntimeNetEndpointPtr netEndpoint_;
    std::thread worker_;
    std::vector<RunProbe> probes_;
    long totalTicks_ = 0;
    std::atomic<long> completedTicks_{0};
    std::shared_ptr<const RunConfiguration> configuration_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<RunState> state_{RunState::Idle};
    std::atomic<bool> pauseRequested_{false};
    std::mutex controlMutex_;
    std::condition_variable controlChanged_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> recordingNotifyPending_{false};
    const observation::StorageOptions storage_;
    std::shared_ptr<observation::ObservationSource> recording_;
    std::vector<observation::ChannelId> probeChannels_;
    observation::LiveSources liveSources_;
    observation::ArraySources arraySources_;
    struct ArrayRoute {
        s7::ProbePoint probe;
        std::size_t maxBytes;
        std::optional<std::uint64_t> lastStep{};
        s7::ArrayPool::Retention admission{};
        std::vector<std::shared_ptr<s7::ArrayMailbox>> mailboxes{};
    };
    std::vector<ArrayRoute> arrayRoutes_;
    struct LiveRoute {
        std::shared_ptr<observation::LiveAcquisition> source;
        std::vector<std::size_t> probes;
        std::vector<double> values;
    };
    std::vector<LiveRoute> liveRoutes_;
    std::vector<std::size_t> readIndices_; // probe -> index of its distinct output in compiledProbes_
    std::optional<s7::CompiledProbes> compiledProbes_;
    std::vector<double> distinctValues_;
    std::vector<std::size_t> recordedProbes_;
    RecordingOverflow recordingOverflow_ = RecordingOverflow::Lossless; // next run
    RecordingOverflow activeOverflow_ = RecordingOverflow::Lossless;     // this run, after hardware/live rules
    std::unique_ptr<observation::ProbeBlockRing> ring_;
    std::thread distributor_;
    std::chrono::steady_clock::time_point blockStart_{};
    std::chrono::nanoseconds throttled_{};
    std::uint64_t droppedFrames_ = 0;
    // Distributor scratch, reused for every block.
    std::vector<observation::Sample> recordBatch_;
};

} // namespace s7studio

Q_DECLARE_METATYPE(s7studio::RunState)

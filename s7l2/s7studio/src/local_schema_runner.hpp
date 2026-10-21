#pragma once

#include "is_schema_runner.hpp"
#include "local_runtime_v2.hpp"
#include "scope_sample_mailbox.hpp"
#include "observation/observation_source.hpp"

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

    // Commands and snapshot access belong to the QObject owner thread.
    [[nodiscard]] RunState state() const override { return state_.load(); }
    [[nodiscard]] auto activeConfiguration() const -> std::shared_ptr<const RunConfiguration> { return configuration_; }
    [[nodiscard]] auto recordedSource() const -> std::shared_ptr<const observation::ObservationSource> { return recording_; }
    [[nodiscard]] long completedTicks() const { return completedTicks_.load(); }
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] bool isPaused() const override;

    // UI thread: takes the Scope samples queued since the last call.
    [[nodiscard]] auto takeScopeSamples() -> ScopeSampleBatch;

signals:
    void stateChanged(s7studio::RunState state);
    void logMessage(const QString& message);
    void statusChanged(const QString& status);
    void runCompleted(bool success, const QString& error);
    // Throttled to at most 20 per second, plus the final count.
    void ticksProgress(long completed, long total);
    // New samples are waiting in takeScopeSamples(); at most one is pending.
    void scopeSamplesAvailable();

private:
    void emitScopeSamples();
    void requestStop();
    void prepareRuntime(const RunConfiguration& configuration);
    void executeRunLoop(std::shared_ptr<const RunConfiguration> configuration);
    void finalizeRun(std::uint64_t id, bool success, const std::string& error);
    void publishState(RunState state, std::uint64_t id);
    void postRunEvent(std::uint64_t id, std::function<void()> event);
    void releaseRuntime();
    void waitForCompletion();
    void emitFinalSamples();

    std::shared_ptr<s7::Schema> schema_;
    std::unique_ptr<s7::NodeRuntime> runtime_;
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
    ScopeSampleMailbox scopeMailbox_;
    const observation::StorageOptions storage_;
    std::shared_ptr<observation::ObservationSource> recording_;
    std::vector<observation::ChannelId> probeChannels_;
};

} // namespace s7studio

Q_DECLARE_METATYPE(s7studio::RunState)

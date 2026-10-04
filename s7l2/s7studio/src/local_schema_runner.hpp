#pragma once

#include "is_schema_runner.hpp"
#include "local_runtime_v2.hpp"
#include "scope_sample_mailbox.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
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
    explicit LocalSchemaRunner(QObject* parent = nullptr);
    ~LocalSchemaRunner() override;

    void run(const boost::json::value& schemaJson, double dt, long ticks) override;
    void pause() override;
    void resume() override;
    void stop() override;
    void reset() override;

    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] bool isPaused() const override;

    // UI thread: takes the Scope samples queued since the last call.
    [[nodiscard]] auto takeScopeSamples() -> ScopeSampleBatch;

signals:
    void logMessage(const QString& message);
    void statusChanged(const QString& status);
    void runCompleted(bool success, const QString& error);
    // Throttled to at most 20 per second, plus the final count.
    void ticksProgress(long completed, long total);
    // New samples are waiting in takeScopeSamples(); at most one is pending.
    void scopeSamplesAvailable();

private:
    struct ScopeConfig {
        std::string id;
        std::size_t channelCount = 1;
    };

    void parseScopeConfigs(const boost::json::value& schemaJson);
    void emitScopeSamples(long step);
    void requestStop();
    auto prepareRuntime(const boost::json::value& schemaJson) -> bool;
    void startRunLoop();
    void executeRunLoop();
    void finalizeRun(bool success, const std::string& error);
    void waitForCompletion();
    void emitFinalSamples();

    std::shared_ptr<s7::Schema> schema_;
    std::unique_ptr<s7::NodeRuntime> runtime_;
    s7::transport::RuntimeNetEndpointPtr netEndpoint_;
    std::thread worker_;
    std::vector<ScopeConfig> scopeConfigs_;
    double dt_ = 0.001;
    long totalTicks_ = 0;
    std::atomic<long> completedTicks_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopRequested_{false};
    ScopeSampleMailbox scopeMailbox_;
};

} // namespace s7studio

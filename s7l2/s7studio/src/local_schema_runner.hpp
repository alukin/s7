#pragma once

#include "is_schema_runner.hpp"
#include "local_runtime.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

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

signals:
    void logMessage(const QString& message);
    void statusChanged(const QString& status);
    void runCompleted(bool success, const QString& error);
    void ticksProgress(long completed, long total);

private:
    void waitForCompletion();
    void emitFinalSamples();

    std::shared_ptr<s7::Schema> schema_;
    std::unique_ptr<s7::NodeRuntime> runtime_;
    std::thread worker_;
    long totalTicks_ = 0;
    std::atomic<long> completedTicks_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopRequested_{false};
};

} // namespace s7studio

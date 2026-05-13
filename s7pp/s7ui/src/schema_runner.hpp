#pragma once

#include "i_schema_runner.hpp"

#include <s7.hpp>
#include <s7factory.hpp>
#include <s7probe.hpp>
#include "schema_session.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <string>

namespace s7ui {

/// Qt-facing runner that implements ISchemaRunner.
/// Emits Qt signals for status changes and completion.
/// Does NOT register element libraries — that is the bootstrapper's job.
class SchemaRunner : public QObject, public ISchemaRunner
{
    Q_OBJECT

public:
    explicit SchemaRunner(QObject* parent = nullptr);
    ~SchemaRunner() override;

    // ISchemaRunner -----------------------------------------------------------
    void run(const boost::json::value& schemaJson, double dt, long ticks) override;
    void pause()  override;
    void resume() override;
    void stop()   override;
    void reset()  override;

    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] bool isPaused()  const override;

    // Probe access ------------------------------------------------------------
    /// Returns the real-valued probe for the given sink node id, or nullptr.
    [[nodiscard]] std::shared_ptr<s7::Probe> probe(const std::string& itemId) const;
    /// Returns the complex probe for the given sink node id, or nullptr.
    [[nodiscard]] std::shared_ptr<s7::ComplexProbe> complexProbe(const std::string& itemId) const;

signals:
    void logMessage(const QString& message);
    void statusChanged(const QString& status);
    void runCompleted(bool success, const QString& error);
    void ticksProgress(long completed, long total);

private:
    void onRunCompleted(bool success, const std::string& error);

    std::shared_ptr<SchemaSession> session_;
    bool runActive_ = false;
    bool runPaused_ = false;
};

} // namespace s7ui

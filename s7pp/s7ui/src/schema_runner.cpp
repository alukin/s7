#include "schema_runner.hpp"
#include "schema_compiler.hpp"

#include <boost/json.hpp>

namespace s7ui {

SchemaRunner::SchemaRunner(QObject* parent) : QObject(parent)
{
    s7::registerBuiltins();
    s7::registerFileIO();
    s7::registerMemIO();
    s7::registerNetIO();
    s7::registerProbe();
}

SchemaRunner::~SchemaRunner()
{
    if (session_) {
        session_->stop();
        session_->waitForCompletion();
    }
}

void SchemaRunner::run(const boost::json::value& schemaJson, double dt, long ticks)
{
    if (runActive_) {
        emit logMessage("[model] Run skipped: model is already running");
        return;
    }

    // Diagram validation applies only to diagram documents (UI payload with explicit "diagram" object).
    // Runtime execution path usually passes compiled schema JSON from scene serialization.
    const auto& schemaObj = schemaJson.as_object();
    const bool hasDiagramPayload = schemaObj.if_contains("diagram") && schemaObj.at("diagram").is_object();
    if (hasDiagramPayload) {
        const auto diagramReport = s7ui::compiler::validateDiagramConnections(schemaJson);
        for (const auto& d : diagramReport.diagnostics) {
            const QString prefix = (d.severity == s7ui::compiler::Diagnostic::Severity::Error)
                ? "[diagram:error]"
                : "[diagram:warn]";
            emit logMessage(QString("%1 %2").arg(prefix).arg(QString::fromStdString(d.message)));
        }
        if (diagramReport.hasErrors()) {
            emit statusChanged("error: invalid diagram");
            return;
        }
    }

    const auto& runtimeObj = schemaObj;
    const auto* runtimeNodes = runtimeObj.if_contains("nodes");
    if (!runtimeNodes || !runtimeNodes->is_array() || runtimeNodes->as_array().empty()) {
        emit logMessage("[model] No runtime nodes found in scene");
        emit statusChanged("error: empty runtime graph");
        return;
    }

    const auto report = s7ui::compiler::validateRuntimeGraph(schemaJson);
    for (const auto& d : report.diagnostics) {
        const QString prefix = (d.severity == s7ui::compiler::Diagnostic::Severity::Error)
            ? "[compile:error]"
            : "[compile:warn]";
        emit logMessage(QString("%1 %2").arg(prefix).arg(QString::fromStdString(d.message)));
    }
    if (report.hasErrors()) {
        emit statusChanged("error: invalid graph");
        return;
    }

    std::vector<std::string> sinkItemIds;
    if (const auto* nodes = runtimeObj.if_contains("nodes")) {
        for (const auto& node : nodes->as_array()) {
            const auto& nodeObj = node.as_object();
            if (nodeObj.contains("type")) {
                auto type = nodeObj.at("type").as_string();
                if (type == "Probe" || type == "ComplexProbe") {
                    sinkItemIds.push_back(nodeObj.at("id").as_string().c_str());
                }
            }
        }
    }

    try {
        session_ = SchemaSession::build(schemaJson, sinkItemIds);

        runActive_ = true;
        runPaused_ = false;
        emit statusChanged("running");

        std::weak_ptr<SchemaSession> weakSession = session_;
        session_->runAsync(ticks, [this, weakSession](bool succeeded, const std::string& error) {
            QMetaObject::invokeMethod(this, [this, weakSession, succeeded, error] {
                onRunCompleted(succeeded, error);
            }, Qt::QueuedConnection);
        });

        emit logMessage(QString("[model] Run started: %1 ticks (dt=%2 s)").arg(ticks).arg(dt));

    } catch (const std::exception& ex) {
        session_.reset();
        runActive_ = false;
        runPaused_ = false;
        emit statusChanged("error");
        emit logMessage(QString("[model] Run failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void SchemaRunner::pause()
{
    if (!session_ || !runActive_ || runPaused_) {
        emit logMessage("[model] Pause skipped");
        return;
    }
    session_->pause();
    runPaused_ = true;
    emit statusChanged("paused");
    emit logMessage("[model] Paused");
}

void SchemaRunner::resume()
{
    if (!session_ || !runPaused_) {
        emit logMessage("[model] Resume skipped");
        return;
    }
    session_->resume();
    runPaused_ = false;
    emit statusChanged("running");
    emit logMessage("[model] Resumed");
}

void SchemaRunner::stop()
{
    if (!session_ || !runActive_) {
        emit logMessage("[model] Stop skipped");
        return;
    }
    session_->stop();
    emit logMessage("[model] Stop requested");
}

void SchemaRunner::reset()
{
    if (session_) {
        session_->stop();
        session_->waitForCompletion();
        session_.reset();
    }
    runActive_ = false;
    runPaused_ = false;
    emit statusChanged("idle");
    emit logMessage("[model] Reset");
}

bool SchemaRunner::isRunning() const
{
    return runActive_;
}

bool SchemaRunner::isPaused() const
{
    return runPaused_;
}

void SchemaRunner::onRunCompleted(bool success, const std::string& error)
{
    runActive_ = false;
    runPaused_ = false;
    if (success) {
        emit statusChanged("completed");
        emit logMessage(QString("[model] Run completed"));
    } else {
        emit statusChanged("error");
        emit logMessage(QString("[model] Run failed: %1").arg(QString::fromStdString(error)));
    }
    emit runCompleted(success, QString::fromStdString(error));
}

std::shared_ptr<s7::Probe> SchemaRunner::probe(const std::string& itemId) const
{
    return session_ ? session_->probe(itemId) : nullptr;
}

std::shared_ptr<s7::ComplexProbe> SchemaRunner::complexProbe(const std::string& itemId) const
{
    return session_ ? session_->complexProbe(itemId) : nullptr;
}

} // namespace s7ui


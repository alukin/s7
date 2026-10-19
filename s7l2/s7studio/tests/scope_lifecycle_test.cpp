#include "nonlinear_pendulum_cases.hpp"
#include "local_schema_runner.hpp"
#include "scope_view_window.hpp"
#include "qcp.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDataStream>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
class FailingDestination : public QIODevice {
public:
    FailingDestination() { open(QIODevice::WriteOnly); }
protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char*, qint64) override { return -1; }
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
QByteArray csv(s7studio::ui::ScopeViewWindow& scope) {
    QBuffer result;
    result.open(QIODevice::ReadWrite);
    QString error;
    require(scope.writeRecordedCsv(result, &error), "CSV snapshot failed");
    return result.data();
}
QByteArray save(s7studio::ui::ScopeViewWindow& scope) {
    QBuffer result;
    result.open(QIODevice::ReadWrite);
    QString error;
    require(scope.writeRunResults(result, &error), "Save snapshot failed");
    return result.data();
}
void load(s7studio::ui::ScopeViewWindow& scope, const QByteArray& data, const QString& label) {
    QBuffer input;
    input.setData(data);
    input.open(QIODevice::ReadOnly);
    QString error;
    require(scope.openRunResults(input, label, &error), error.toUtf8().constData());
}
void verifySameValues(const QByteArray& first, const QByteArray& second) {
    const auto a = first.split('\n'), b = second.split('\n');
    require(a.size() == b.size(), "Saved result sample count changed");
    for (qsizetype i = 1; i + 1 < a.size(); ++i) {
        const auto x = a[i].split(','), y = b[i].split(',');
        require(x.size() == 4 && y.size() == 4 && x[1] == y[1] && x[2] == y[2] && x[3] == y[3],
            "Saved result changed a channel, timestamp or value");
    }
}
bool warningContains(s7studio::ui::ScopeViewWindow& scope, const QString& text) {
    for (auto* label : scope.findChildren<QLabel*>()) if (label->text().contains(text)) return true;
    return false;
}
}

int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create test directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("scope-lifecycle"));
    QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"), directory.path());

    // Exercise spill-backed save/open, snapshots followed by appends, overlay
    // independence, malformed input rollback and zooming imported history.
    {
        s7studio::ui::ScopeViewWindow source(QStringLiteral("reference"));
        for (int i = 0; i < 25000; ++i) source.appendSample(QStringLiteral("theta"), i * .001, std::sin(i * .001));
        source.setDroppedSampleCount(7);
        const auto before = csv(source);
        const auto snapshot = save(source);
        FailingDestination failure;
        QString writeError;
        require(!source.writeRunResults(failure, &writeError) && !writeError.isEmpty(),
            "Failed result writes were reported as successful");
        for (auto* check : source.findChildren<QCheckBox*>())
            if (check->text() == QStringLiteral("Fit full run")) check->setChecked(false);
        QMetaObject::invokeMethod(&source, "refreshPlot", Qt::DirectConnection);
        source.findChild<QCustomPlot*>()->xAxis->setRange(10.0, 10.1);
        source.appendSample(QStringLiteral("theta"), 25.0, std::sin(25.0));
        const auto after = csv(source);
        require(after.count('\n') == before.count('\n') + 1 && after.startsWith(before), "Saving disturbed append cursor");
        s7studio::ui::ScopeViewWindow restored(QStringLiteral("comparison"));
        load(restored, snapshot, QStringLiteral("previous"));
        verifySameValues(before, csv(restored));
        require(warningContains(restored, QStringLiteral("dropped 7")), "Saved capture loss was hidden");
        auto* plot = restored.findChild<QCustomPlot*>();
        require(plot && plot->graphCount() == 1 && plot->graph(0)->pen().style() == Qt::DashLine,
            "Comparison was not drawn as an overlay");
        for (auto* check : restored.findChildren<QCheckBox*>())
            if (check->text() == QStringLiteral("Fit full run")) check->setChecked(false);
        plot->xAxis->setRange(10.0, 10.1);
        require(plot->graph(0)->data()->size() > 90, "Imported disk-backed zoom lost full-resolution history");
        const auto unchanged = csv(restored);
        auto wrongVersion = snapshot;
        wrongVersion[8] = 2;
        auto unordered = snapshot;
        QBuffer edit(&unordered); edit.open(QIODevice::ReadWrite); edit.seek(unordered.size() - 16);
        QDataStream editStream(&edit); editStream.setByteOrder(QDataStream::LittleEndian); editStream << 0.0;
        edit.close();
        for (const auto& invalid : {snapshot.left(10), snapshot.left(snapshot.size() - 1), snapshot + 'x',
                QByteArray("wrong"), wrongVersion, unordered}) {
            QBuffer input;
            input.setData(invalid); input.open(QIODevice::ReadOnly);
            QString error;
            require(!restored.openRunResults(input, QStringLiteral("bad"), &error) && !error.isEmpty(),
                "Malformed result was accepted");
            require(csv(restored) == unchanged, "Failed import changed existing data");
        }
        restored.appendSample(QStringLiteral("theta"), .001, 1.5);
        require(plot->graphCount() == 2, "Current run collided with comparison channel");
        const auto liveOnly = save(restored);
        s7studio::ui::ScopeViewWindow live(QStringLiteral("live"));
        load(live, liveOnly, QStringLiteral("live"));
        require(csv(live).count('\n') == 2, "Save included comparison overlays");
        restored.clearSamples();
        require(csv(restored) == unchanged, "Rerun cleared comparison data");
        load(restored, snapshot, QStringLiteral("previous"));
        require(plot->graphCount() == 2, "Repeated import overwrote a comparison");
        restored.clearComparisons();
        require(plot->graphCount() == 0 && csv(restored).count('\n') == 1, "Removing comparisons left stale data");
    }
    // A temporary-storage failure stays visible after saving and reopening.
    {
        const auto badPath = directory.path() + QStringLiteral("/not-a-directory");
        QFile blocker(badPath); require(blocker.open(QIODevice::WriteOnly), "Cannot create failure fixture"); blocker.close();
        QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"), badPath);
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("storage-failure"));
        for (int i = 0; i < 25000; ++i) scope.appendSample(i * .001, double(i));
        require(warningContains(scope, QStringLiteral("WARNING")), "Storage failure was not visible");
        const auto snapshot = save(scope);
        QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"), directory.path());
        s7studio::ui::ScopeViewWindow restored(QStringLiteral("restored"));
        load(restored, snapshot, QStringLiteral("failed recording"));
        require(warningContains(restored, QStringLiteral("incomplete")), "Saved storage failure lost its warning");
    }

    namespace p = s7tests::nonlinear_pendulum;
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto root = p::diagram("symplectic", 1000);
    const auto schemaJson = root.at("schema");
    auto schema = s7tests::build_schema(root);
    auto reference = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    auto observation = std::make_unique<s7::ObservationService>(*reference);
    const std::vector<s7studio::RunProbe> probes{{"theta", "state", 0}, {"omega", "state", 1}};
    s7studio::ui::ScopeViewWindow scope(QStringLiteral("pendulum"));
    s7studio::LocalSchemaRunner runner;
    bool completed = false, success = false;
    QObject::connect(&runner, &s7studio::LocalSchemaRunner::runCompleted, &runner,
        [&](bool ok, const QString&) { completed = true; success = ok; });
    std::size_t received = 0;
    auto consume = [&] {
        auto batch = runner.takeScopeSamples();
        require(batch.dropped == 0, "Lifecycle run unexpectedly dropped samples");
        for (const auto& sample : batch.samples) {
            const auto channel = received % 2;
            if (channel == 0) reference->step();
            const auto expected = observation->sample_output("state", channel).to_display_double();
            require(sample.step == static_cast<long>(received / 2 + 1) && sample.probeName == probes[channel].name,
                "Lifecycle skipped, duplicated or mixed steps/channels");
            require(sample.value == expected && std::abs(sample.timeSeconds - sample.step * .001) < 1e-12,
                "Lifecycle changed headless values or model timestamps");
            scope.appendSample(QString::fromStdString(sample.probeName), sample.timeSeconds, sample.value);
            ++received;
        }
    };
    auto waitUntil = [&](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            app.processEvents(); consume();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(predicate(), "Lifecycle operation timed out");
    };
    runner.run(schemaJson, .001, -1, probes);
    waitUntil([&] { return received >= 6000; });
    runner.pause();
    // Pause is a request; allow the current worker step to reach its boundary.
    for (int i = 0; i < 30; ++i) { app.processEvents(); consume(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(runner.isPaused(), "Pause request was not retained");
    const auto pausedCount = received;
    scope.show(); scope.close(); scope.show();
    const auto pausedSnapshot = save(scope);
    for (int i = 0; i < 30; ++i) { app.processEvents(); consume(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(received == pausedCount, "Paused runtime kept publishing samples");
    load(scope, pausedSnapshot, QStringLiteral("paused baseline"));
    runner.resume();
    waitUntil([&] { return received >= pausedCount + 6000; });
    static_cast<void>(save(scope)); // Snapshot while the worker is producing.
    static_cast<void>(csv(scope));
    runner.pause(); runner.stop();
    waitUntil([&] { return completed; }); consume();
    require(success && received > pausedCount && received % 2 == 0, "Stop while paused lost final channel or failed");
    runner.reset();
    require(runner.takeScopeSamples().samples.empty(), "Reset retained stale mailbox data");
    app.processEvents();
    scope.clearSamples();
    observation.reset();
    reference = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    completed = false; success = false;
    runner.run(schemaJson, .001, 1000, probes);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::size_t rerunCount = 0;
    while ((!completed || runner.isRunning()) && std::chrono::steady_clock::now() < deadline) {
        app.processEvents();
        const auto batch = runner.takeScopeSamples();
        require(batch.dropped == 0, "Rerun dropped samples");
        for (const auto& sample : batch.samples) {
            if (rerunCount % 2 == 0) reference->step();
            s7::ObservationService fresh(*reference);
            require(sample.step == static_cast<long>(rerunCount / 2 + 1)
                && sample.value == fresh.sample_output("state", rerunCount % 2).to_display_double(), "Rerun did not restart cleanly");
            scope.appendSample(QString::fromStdString(sample.probeName), sample.timeSeconds, sample.value);
            ++rerunCount;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(completed && success && rerunCount == 2000, "Rerun incomplete");
    require(scope.findChild<QCustomPlot*>()->graphCount() == 4, "Rerun lost saved comparison");
    runner.reset(); app.processEvents();

    // Intentionally withhold delivery to test the real worker/mailbox limit.
    completed = false; success = false;
    runner.run(schemaJson, .001, 110000, probes);
    const auto overloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!completed && std::chrono::steady_clock::now() < overloadDeadline) {
        app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto overflow = runner.takeScopeSamples();
    require(completed && success && overflow.samples.size() == 200000 && overflow.dropped == 20000,
        "Overload was not bounded and accounted for");
    scope.setDroppedSampleCount(overflow.dropped);
    require(warningContains(scope, QStringLiteral("20000")), "Overload was hidden in Scope");
    runner.reset();
    std::cout << "Scope lifecycle, saved comparisons, pause/resume/stop/reset/rerun, overload and storage failure passed\n";
    return 0;
} catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }

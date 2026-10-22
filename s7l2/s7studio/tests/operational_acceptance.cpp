// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// Measured local Studio envelope, opt-in so CI timing is not a universal claim.
#include "main_window.hpp"
#include "diagram_editor.hpp"
#include "scope_view_window.hpp"
#include "viewers/viewer_manager.hpp"
#include "json_utils.hpp"
#include <s7/schema_library.hpp>
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSysInfo>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
qint64 rssBytes() {
#if defined(__linux__)
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly))
        for (const auto& line : status.readAll().split('\n'))
            if (line.startsWith("VmRSS:")) return line.simplified().split(' ').at(1).toLongLong() * 1024;
#endif
    return -1;
}
}
int main(int argc, char** argv) try {
    QApplication app(argc, argv);
    require(argc >= 3, "Usage: operational_acceptance <example.s7diagram> <report.json> [wall-seconds-per-rate]");
    const double seconds = argc > 3 ? QString::fromLocal8Bit(argv[3]).toDouble() : 5.;
    require(seconds >= 1 && seconds <= 300, "Wall duration must be 1..300 seconds per rate");
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create temporary settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName("s7-test"); app.setApplicationName("operational-acceptance");
    QJsonArray results;
    for (const int hz : {1000, 10000, 100000}) {
        QSettings{}.clear();
        auto model = s7studio::json_utils::parseFile(QString::fromLocal8Bit(argv[1]));
        model.as_object().erase("probes"); // four wired Scope channels; no duplicate ad-hoc probes
        model.as_object()["dt"] = 1. / hz;
        s7::configure_schema_discretization(model.as_object().at("schema"), 1. / hz);
        const auto saved = temporary.path() + "/configured.s7diagram";
        QFile fixture(saved); require(fixture.open(QIODevice::WriteOnly), "Cannot save fixture");
        const auto bytes = boost::json::serialize(model);
        require(fixture.write(bytes.data(), bytes.size()) == static_cast<qint64>(bytes.size()), "Fixture write failed"); fixture.close();
        auto window = std::make_unique<s7studio::MainWindow>(nullptr, false);
        window->openPath(saved); window->show();
        auto* runner = window->findChild<s7studio::LocalSchemaRunner*>();
        auto* manager = window->findChild<s7studio::viewers::ViewerManager*>();
        require(runner && manager, "Missing production runner/viewer manager");
        auto* viewer = manager->openObserver("Scope", "scope");
        require(viewer, "Missing Scope");
        auto action = [&](const char* name) {
            auto* a = window->findChild<QAction*>(name);
            require(a && a->isEnabled(), "Run action unavailable"); a->trigger();
        };
        bool configured = false;
        QTimer::singleShot(0, window.get(), [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !frequency || !buttons) { dialog->reject(); return; }
            duration->setValue(-1); frequency->setValue(hz);
            configured = true; buttons->button(QDialogButtonBox::Ok)->click();
        });
        QElapsedTimer clock; clock.start();
        action("action.run"); require(configured, "Run configuration did not open");
        qint64 lastHeartbeat = clock.nsecsElapsed(), maxGap = 0, peakRss = rssBytes();
        QJsonArray checkpoints;
        QTimer heartbeat; heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
            const auto now = clock.nsecsElapsed(); maxGap = std::max(maxGap, now-lastHeartbeat); lastHeartbeat = now;
            peakRss = std::max(peakRss, rssBytes());
        });
        heartbeat.start();
        auto pump = [&] { app.processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); };
        auto waitFor = [&](const auto& condition) {
            QElapsedTimer limit; limit.start();
            while (!condition() && limit.elapsed() < 10000) pump();
            require(condition(), "Lifecycle timed out");
        };
        waitFor([&] { return runner->state() == s7studio::RunState::Running; });
        const auto start = clock.nsecsElapsed();
        int nextCheckpoint = 1;
        while ((clock.nsecsElapsed()-start)/1e9 < seconds) {
            require(runner->isRunning(), "Run stopped unexpectedly"); pump();
            if ((clock.nsecsElapsed()-start)/1e9 >= nextCheckpoint * seconds / 4) {
                checkpoints.append(QJsonObject{{"wall_s", (clock.nsecsElapsed()-start)/1e9},
                    {"steps", static_cast<qint64>(runner->completedTicks())}, {"rss_bytes", rssBytes()}});
                ++nextCheckpoint;
            }
        }
        const auto runWall = (clock.nsecsElapsed()-start)/1e9;
        QElapsedTimer latency; latency.start(); action("action.pause");
        waitFor([&] { return runner->isPaused(); });
        const auto pauseMs = latency.nsecsElapsed()/1e6;
        const auto pausedSteps = runner->completedTicks();
        for (int i=0; i<20; ++i) pump();
        require(runner->completedTicks() == pausedSteps, "Paused run advanced");
        action("action.resume");
        waitFor([&] { return runner->completedTicks() > pausedSteps; });
        latency.restart(); action("action.stop"); waitFor([&] { return !runner->isRunning(); });
        const auto stopMs = latency.nsecsElapsed()/1e6;
        const auto source = runner->recordedSource();
        require(source && source->status().finished && !source->status().incomplete(), "Incomplete authoritative capture");
        const auto steps = runner->completedTicks();
        const auto channels = source->channels().size();
        require(channels == 4 && source->status().samples == static_cast<std::uint64_t>(steps)*channels,
                "Lost final steps/channels");
        const auto first = source->read(0, channels), last = source->read(source->status().samples-channels, channels);
        require(first.error.isEmpty() && last.error.isEmpty() && first.samples.front().step == 1
                && last.samples.back().step == steps && last.samples.back().timeSeconds == static_cast<double>(steps)/hz,
                "Invalid recording extent");
        const auto dropped = manager->droppedSampleCount();
        const auto sourceBytes = source->status().samples * sizeof(s7studio::observation::Sample);
        latency.restart();
        // Destroy with a running worker, exercising production stop/join teardown.
        action("action.reset");
        runner->run(model.as_object().at("schema"), 1./hz, -1, {{"close", "state", 0}});
        window.reset();
        const auto closeMs = latency.nsecsElapsed()/1e6;
        heartbeat.stop();
        results.append(QJsonObject{{"rate_hz", hz}, {"wall_s", runWall}, {"steps_before_pause", static_cast<qint64>(pausedSteps)},
            {"steps_per_wall_second", pausedSteps/runWall}, {"model_seconds", static_cast<double>(steps)/hz},
            {"channels", channels}, {"captured_samples", static_cast<qint64>(source->status().samples)},
            {"recording_bytes", static_cast<qint64>(sourceBytes)}, {"preview_skipped", static_cast<qint64>(dropped)},
            {"pause_ms", pauseMs}, {"stop_ms", stopMs}, {"reset_start_close_ms", closeMs},
            {"max_10ms_timer_gap_ms", maxGap/1e6}, {"peak_sampled_rss_bytes", peakRss}, {"checkpoints", checkpoints}});
        require(pauseMs < 2000 && stopMs < 2000 && closeMs < 2000 && maxGap/1e6 < 2000,
                "Operational latency exceeded this test's two-second acceptance budget");
        std::cout << hz << " Hz: " << pausedSteps/runWall << " steps/wall-s, pause " << pauseMs
                  << " ms, stop " << stopMs << " ms, timer gap " << maxGap/1e6 << " ms\n";
    }
    QJsonObject report{{"platform", QSysInfo::prettyProductName()}, {"cpu", QSysInfo::currentCpuArchitecture()},
        {"qt", qVersion()}, {"results", results}, {"scope", "one four-element RLC graph, four channels, visible Scope; sample disk bytes exclude metadata; no real-time guarantee"}};
    QFile output(QString::fromLocal8Bit(argv[2])); require(output.open(QIODevice::WriteOnly), "Cannot open report");
    const auto data = QJsonDocument(report).toJson(); require(output.write(data) == data.size(), "Cannot write report");
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

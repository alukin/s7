// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
// Opt-in published-source UI load/soak, not a runtime throughput benchmark.
#include "observers_panel.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtGraphs/QXYSeries>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#ifdef __linux__
#include <unistd.h>
#endif
using namespace s7studio;
namespace {
qint64 rss() {
    QFile file("/proc/self/status"); if (!file.open(QIODevice::ReadOnly)) return 0;
    for (const auto& line : file.readAll().split('\n')) if (line.startsWith("VmRSS:")) return line.simplified().split(' ')[1].toLongLong()*1024;
    return 0;
}
qint64 threadId() {
#ifdef __linux__
    return gettid();
#else
    return 0;
#endif
}
// Opt-in (S7_BENCHMARK_EVENT_PROFILE=1): times each event the GUI event loop dispatches at the outermost
// level, keyed by receiver, so plot render frames, viewer refreshes and window flushes are counted and costed.
class ProfilingApplication final : public QApplication {
public:
    struct Cost { qint64 events = 0; double totalMs = 0, maxMs = 0; };
    using QApplication::QApplication;
    const bool enabled = qEnvironmentVariableIntValue("S7_BENCHMARK_EVENT_PROFILE") != 0;
    bool recording = false; QHash<QObject*,QString> labels; QHash<QString,Cost> costs;
    // Frames per plot widget, counted from its scene's afterRendering (paced widgets may withhold a timer event without rendering),
    // and frames whose QXYSeries were replaced since the widget's previous frame.
    QSet<QObject*> pendingData; QHash<QString,qint64> frames, dataFrames, paints;
    bool notify(QObject* receiver, QEvent* event) override {
        if (!recording || QThread::currentThread() != thread()) return QApplication::notify(receiver, event);
        if (depth_ > 0) {  // nested: only count which widgets repaint, by window, class and object name
            if (event->type() == QEvent::Paint && receiver->isWidgetType())
                ++paints[QString(static_cast<QWidget*>(receiver)->window()->metaObject()->className())+" / "+receiver->metaObject()->className()+" "+receiver->objectName()];
            return QApplication::notify(receiver, event);
        }
        const auto key = labelOf(receiver, event); ++depth_; QElapsedTimer clock; clock.start();
        const bool result = QApplication::notify(receiver, event);
        --depth_; const double ms = static_cast<double>(clock.nsecsElapsed())/1e6;
        auto& cost = costs[key]; ++cost.events; cost.totalMs += ms; cost.maxMs = std::max(cost.maxMs, ms);
        return result;
    }
private:
    QString labelOf(QObject* receiver, QEvent* event) const {
        const QString type = QString::fromLatin1(QMetaEnum::fromType<QEvent::Type>().valueToKey(event->type()));
        if (const auto it = labels.constFind(receiver); it != labels.cend()) return *it+" "+(event->type() == QEvent::Timer ? QString("render") : type);
        if (qobject_cast<QTimer*>(receiver) && receiver->parent()) return QString("QTimer of ")+receiver->parent()->metaObject()->className();
        if (receiver->isWidgetType() && static_cast<QWidget*>(receiver)->isWindow()) return QString("window ")+receiver->metaObject()->className()+" "+type;
        return QString(receiver->metaObject()->className())+" "+type;
    }
    int depth_ = 0;
};
}
int main(int argc, char** argv) {
    ProfilingApplication app(argc, argv); QTemporaryDir temporary;
    if (argc < 4 || !temporary.isValid()) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]); const int seconds = QString::fromLocal8Bit(argv[2]).toInt();
    const auto mode = QString::fromLocal8Bit(argv[3]);
    const bool liveOnly = mode == "live";
    const bool fullOnly = mode == "full-only";
    const bool full = fullOnly || (argc > 4 && QString::fromLocal8Bit(argv[4]) == "full");
    bool validViewerIndex = false;
    const int fullViewerIndex = fullOnly && argc > 4 ? QString::fromLocal8Bit(argv[4]).toInt(&validViewerIndex) : -1;
    if (validViewerIndex && (fullViewerIndex < 0 || fullViewerIndex > 3)) return 2;
    if (seconds < 5 || seconds > 3600) return 2;
    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    auto recording = std::make_shared<observation::ObservationSource>(1, observation::StorageOptions{20000, temporary.path(), {}, {}});
    QVector<viewers::ObserverDescriptor> descriptors; observation::LiveSources live;
    boost::json::array selection; QVector<observation::ChannelId> recordChannels;
    for (int i = 0; i < 12; ++i) {
        const QString types[]{"Scope", "PhasePortrait", "PhaseTrajectory3D", "LiveOscilloscope"};
        const auto type = liveOnly ? QString("LiveOscilloscope") : types[i%4];
        const auto id = QString("observer_%1").arg(i); viewers::ObserverDescriptor descriptor{id,type,{}};
        const int count = type == "PhasePortrait" ? 2 : type == "PhaseTrajectory3D" ? 3 : 8;
        QVector<observation::LiveChannel> channels;
        for (int j = 0; j < count; ++j) {
            const auto role = type.startsWith("Phase") ? QStringList{"x","y","z"}[j] : QString("signal%1").arg(j);
            const auto name = id+"/"+role; descriptor.inputs.push_back({name,"V","benchmark",j}); channels.push_back({name,"V","f64"});
            if (type != "LiveOscilloscope") recordChannels.push_back(recording->channel(name));
        }
        if (type == "LiveOscilloscope") live.insert(id, std::make_shared<observation::LiveAcquisition>(1,.001,channels,QJsonObject{},1000,1));
        descriptors.push_back(descriptor); selection.push_back(boost::json::object{{"element_id",id.toStdString()},{"type",type.toStdString()},{"collapsed",false}});
    }
    manager.present(recording,0,[&](const QString& id) { for (const auto& d : descriptors) if (d.id==id) return d.type; return QString{}; }); manager.presentLive(live);
    ui::ObserversPanel panel(&manager); panel.resize(600,2400); panel.setDocument("Preview load fixture",descriptors,selection); panel.bindRun(1,descriptors);
    std::atomic<std::int64_t> steps{0};
    std::jthread producer([&](std::stop_token stop) {
        auto deadline = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            const auto step = ++steps; const double time = static_cast<double>(step)*.001;
            double values[8]; for (int j=0;j<8;++j) values[j] = std::sin(static_cast<double>(step)*.01+j*.5);
            for (auto channel : recordChannels) recording->append({channel,step,time,values[channel%8]});
            for (const auto& source : live) source->append(step,time,values);
            deadline += std::chrono::milliseconds(1); std::this_thread::sleep_until(deadline);
        }
        recording->finish(true); for (const auto& source : live) source->finish();
    });
    // Monotonic start and GUI thread id let external profilers (perf -k monotonic) align with gap timestamps.
    QElapsedTimer clock; clock.start(); const auto startNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    qint64 last = 0, baselineGap = 0, visibleGap = 0, initialRss = 0, maximumRss = 0;
    qint64 visibleTicks = 0, gapsOver100Ms = 0, gapsOver250Ms = 0;
    QJsonArray checkpoints, longUiGaps, slowUiGaps, scrollEvents;
    int nextCheckpoint = 30, scrollPeriod = 4; bool shown = false, opened = false; int maximumActive = 0;
    QVector<QPair<QString,QPointer<QWidget>>> fullViewers; qint64 nextLabelScan = 0; QHash<QString,double> instanceSeconds; QSet<QObject*> watchedSeries, watchedWindows;
    QObject seriesWatch;  // destroyed before watchedSeries, so plot teardown at exit cannot reach it
    const auto watchPlot = [&](QQuickWidget* quick) {  // QLineSeries are children of the plot widget that owns the QQuickWidget.
        if (auto* window = quick->quickWindow(); window && !watchedWindows.contains(window)) {
            watchedWindows.insert(window); QPointer<QQuickWidget> target(quick);
            QObject::connect(window,&QObject::destroyed,&seriesWatch,[&,window] { watchedWindows.remove(window); });
            QObject::connect(window,&QQuickWindow::afterRendering,&seriesWatch,[&app,target] {
                if (!app.recording || !target) return;
                const auto label = app.labels.value(target); ++app.frames[label]; if (app.pendingData.remove(target)) ++app.dataFrames[label];
            });
        }
        if (!quick->parentWidget()) return;
        for (auto* series : quick->parentWidget()->findChildren<QXYSeries*>(Qt::FindDirectChildrenOnly)) {
            if (watchedSeries.contains(series)) continue;
            watchedSeries.insert(series); QPointer<QQuickWidget> target(quick);
            QObject::connect(series,&QObject::destroyed,&seriesWatch,[&,series] { watchedSeries.remove(series); });
            QObject::connect(series,&QXYSeries::pointsReplaced,&seriesWatch,[&app,target] { if (target) app.pendingData.insert(target); });
        }
    };
    const auto scanQuickWidgets = [&] {  // QQuickWidgets come and go as dock cards scroll in and out of view.
        app.labels.clear();
        for (auto* quick : panel.findChildren<QQuickWidget*>()) {
            QString id; for (auto* p = quick->parentWidget(); p && id.isEmpty(); p = p->parentWidget()) if (p->objectName().startsWith("observer.card.")) id = p->objectName().mid(14);
            QString type; for (const auto& d : descriptors) if (d.id == id) type = d.type;
            app.labels.insert(quick, "dock "+id+" "+type+" "+quick->source().fileName()); watchPlot(quick);
        }
        for (const auto& [type, widget] : fullViewers) if (widget) for (auto* quick : widget->findChildren<QQuickWidget*>()) { app.labels.insert(quick, "full "+type+" "+quick->source().fileName()); watchPlot(quick); }
        for (const auto& label : std::as_const(app.labels)) instanceSeconds[label] += .5;
    };
    QTimer timer; timer.setInterval(10);
    QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        const auto elapsed = clock.elapsed(), gap = elapsed-last; last = elapsed;
        if (elapsed < 10000) baselineGap = std::max(baselineGap,gap);
        else if (elapsed > 20000) {
            visibleGap = std::max(visibleGap,gap); ++visibleTicks;
            gapsOver100Ms += gap > 100; gapsOver250Ms += gap > 250;
            if (gap > 250) longUiGaps.append(QJsonObject{{"wall_ms", elapsed}, {"gap_ms", gap}});
            if (gap > 100) slowUiGaps.append(QJsonObject{{"wall_ms", elapsed}, {"gap_ms", gap}});
        }
        if (elapsed >= 10000 && !shown && !fullOnly) { panel.show(); shown = true; }
        if (full && !opened && elapsed >= 15000) {
            // Four full viewers share the same data, each with independent state.
            for (int i=0;i<4;++i) {
                if (validViewerIndex && i != fullViewerIndex) continue;
                auto* viewer = manager.openObserver(descriptors[i].type,descriptors[i].id);
                if (viewer) { viewer->widget()->move(650, i*560); fullViewers.push_back({descriptors[i].type, viewer->widget()}); }
            }
            opened = true;
        }
        if (elapsed >= 20000 && !initialRss) initialRss = rss();
        if (app.enabled && elapsed >= 20000 && elapsed >= nextLabelScan) {
            if (!app.recording) { app.recording = true; nextLabelScan = elapsed; }
            // Rescan on a fixed 500 ms schedule; each scan adds 0.5 s of life to every live plot widget.
            while (nextLabelScan <= elapsed) { scanQuickWidgets(); nextLabelScan += 500; }
        }
        if (shown && !liveOnly && elapsed/5000 > scrollPeriod) {
            scrollPeriod = static_cast<int>(elapsed/5000); auto* scroll = panel.findChild<QScrollArea*>("observers.scroll"); auto* bar = scroll->verticalScrollBar();
            scrollEvents.append(QJsonObject{{"wall_ms", elapsed}, {"before", bar->value()}});
            bar->setValue(bar->value()+700 > bar->maximum() ? 0 : bar->value()+700);
        }
        maximumActive = std::max(maximumActive,panel.activePresentations()); maximumRss = std::max(maximumRss,rss());
        if (elapsed/1000 >= nextCheckpoint) {
            checkpoints.append(QJsonObject{{"wall_s",elapsed/1000},{"rss_bytes",rss()},{"steps",QString::number(steps.load())},{"recorded_samples",QString::number(recording->status().samples)},
                {"active_plots",panel.activePresentations()},{"submitted",QString::number(panel.submittedRequests())}}); nextCheckpoint += 10;
            std::cout << "checkpoint " << elapsed/1000 << "s rss=" << rss() << " plots=" << panel.activePresentations() << std::endl;
        }
        if (elapsed >= static_cast<qint64>(seconds+20)*1000) {
            producer.request_stop(); producer.join();
            app.recording = false; QJsonArray eventProfile; double busyMs = 0;
            auto costs = app.costs.asKeyValueRange(); QVector<QPair<QString,ProfilingApplication::Cost>> sorted(costs.begin(), costs.end());
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.totalMs > b.second.totalMs; });
            for (const auto& [label, cost] : sorted) {
                busyMs += cost.totalMs; if (eventProfile.size() >= 60) continue;
                QJsonObject entry{{"label",label},{"events",cost.events},{"total_ms",cost.totalMs},{"max_ms",cost.maxMs}};
                if (const auto instance = label.left(label.lastIndexOf(' ')); instanceSeconds.contains(instance)) entry.insert("instance_seconds", instanceSeconds.value(instance));
                if (const auto instance = label.left(label.lastIndexOf(' ')); label.endsWith(" render") && app.frames.contains(instance)) {
                    entry.insert("frames", app.frames.value(instance)); entry.insert("frames_with_new_series_data", app.dataFrames.value(instance));
                }
                eventProfile.append(entry);
            }
            QJsonObject paintCounts; for (auto it = app.paints.cbegin(); it != app.paints.cend(); ++it) paintCounts.insert(it.key(), it.value());
            QJsonObject result{{"workload",liveOnly?"live-only published sources":"mixed published sources"},{"full_viewers",full},
                {"panel_visible",!fullOnly},{"full_viewer_index",validViewerIndex ? fullViewerIndex : -1},{"measurement_seconds",seconds},
                {"baseline_ui_max_gap_ms",baselineGap},{"visible_ui_max_gap_ms",visibleGap},
                {"visible_ui_ticks",visibleTicks},{"ui_gaps_over_100_ms",gapsOver100Ms},{"ui_gaps_over_250_ms",gapsOver250Ms},
                {"long_ui_gaps",longUiGaps},{"slow_ui_gaps",slowUiGaps},{"scroll_events",scrollEvents},
                {"clock_start_monotonic_ns",QString::number(startNs)},{"gui_thread_id",threadId()},
                {"gui_event_profile",eventProfile},{"gui_widget_paints",paintCounts},{"gui_event_profile_busy_ms",busyMs},
                {"warm_rss_bytes",initialRss},{"final_rss_bytes",rss()},{"peak_rss_bytes",maximumRss},
                {"maximum_active_plots",maximumActive},{"steps",QString::number(steps.load())},{"achieved_source_steps_per_wall_second",steps.load()/(static_cast<double>(elapsed)/1000.)},
                {"capture_error", recording->status().storageError},
                {"recorded_samples",QString::number(recording->status().samples)},{"journal_payload_bytes",QString::number(recording->status().samples*sizeof(observation::Sample))},{"checkpoints",checkpoints}};
            QFile file(output); const auto bytes = QJsonDocument(result).toJson();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) app.exit(2);
            else app.exit(maximumActive > 4 || recording->status().incomplete() ? 1 : 0);
        }
    });
    timer.start(); const auto result = app.exec(); producer.request_stop(); if (producer.joinable()) producer.join(); return result;
}

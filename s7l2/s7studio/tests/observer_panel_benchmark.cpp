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
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace s7studio;
namespace {
qint64 rss() {
    QFile file("/proc/self/status"); if (!file.open(QIODevice::ReadOnly)) return 0;
    for (const auto& line : file.readAll().split('\n')) if (line.startsWith("VmRSS:")) return line.simplified().split(' ')[1].toLongLong()*1024;
    return 0;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QTemporaryDir temporary;
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
    QElapsedTimer clock; clock.start(); qint64 last = 0, baselineGap = 0, visibleGap = 0, initialRss = 0, maximumRss = 0;
    qint64 visibleTicks = 0, gapsOver100Ms = 0, gapsOver250Ms = 0;
    QJsonArray checkpoints, longUiGaps, scrollEvents;
    int nextCheckpoint = 30, scrollPeriod = 4; bool shown = false, opened = false; int maximumActive = 0;
    QTimer timer; timer.setInterval(10);
    QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        const auto elapsed = clock.elapsed(), gap = elapsed-last; last = elapsed;
        if (elapsed < 10000) baselineGap = std::max(baselineGap,gap);
        else if (elapsed > 20000) {
            visibleGap = std::max(visibleGap,gap); ++visibleTicks;
            gapsOver100Ms += gap > 100; gapsOver250Ms += gap > 250;
            if (gap > 250) longUiGaps.append(QJsonObject{{"wall_ms", elapsed}, {"gap_ms", gap}});
        }
        if (elapsed >= 10000 && !shown && !fullOnly) { panel.show(); shown = true; }
        if (full && !opened && elapsed >= 15000) {
            // Four full viewers share the same data, each with independent state.
            for (int i=0;i<4;++i) {
                if (validViewerIndex && i != fullViewerIndex) continue;
                auto* viewer = manager.openObserver(descriptors[i].type,descriptors[i].id);
                if (viewer) viewer->widget()->move(650, i*560);
            }
            opened = true;
        }
        if (elapsed >= 20000 && !initialRss) initialRss = rss();
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
            QJsonObject result{{"workload",liveOnly?"live-only published sources":"mixed published sources"},{"full_viewers",full},
                {"panel_visible",!fullOnly},{"full_viewer_index",validViewerIndex ? fullViewerIndex : -1},{"measurement_seconds",seconds},
                {"baseline_ui_max_gap_ms",baselineGap},{"visible_ui_max_gap_ms",visibleGap},
                {"visible_ui_ticks",visibleTicks},{"ui_gaps_over_100_ms",gapsOver100Ms},{"ui_gaps_over_250_ms",gapsOver250Ms},
                {"long_ui_gaps",longUiGaps},{"scroll_events",scrollEvents},
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

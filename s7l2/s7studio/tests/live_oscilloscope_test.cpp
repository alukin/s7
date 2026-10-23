// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "lifecycle_test_support.hpp"
#include "live_oscilloscope_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QtGraphs/QLineSeries>
#include <QQuickWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTemporaryDir>
#include <QDir>
#include <cmath>
#include <thread>

using namespace s7studio;
using observation::LiveAcquisition;
class LiveOscilloscopeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { registerLifecycleElements(); }
    void wrapsResizeAndExactSamples() {
        LiveAcquisition source(7, .001, {{"x", "V", "f64"}, {"y", "A", "f64"}}, {}, 1000, 1);
        const auto allocated = source.allocatedBytes();
        QVERIFY(source.setCapacity(32));
        for (int i = 1; i <= 200000; ++i) { const double values[]{static_cast<double>(i), -static_cast<double>(i)}; source.append(i, i*.001, values); }
        auto frame = source.snapshot(); QVERIFY(frame);
        QCOMPARE(frame->times.size(), std::size_t(32)); QCOMPARE(frame->received, std::uint64_t(200000));
        QCOMPARE(frame->steps.front(), std::int64_t(199969)); QCOMPARE(frame->steps.back(), std::int64_t(200000));
        for (std::size_t i = 0; i < frame->steps.size(); ++i) {
            QCOMPARE(frame->values[i*2], static_cast<double>(frame->steps[i]));
            QCOMPARE(frame->values[i*2+1], -static_cast<double>(frame->steps[i]));
        }
        const auto frozen = *frame;
        QVERIFY(source.setCapacity(8)); QCOMPARE(source.snapshot()->steps.size(), std::size_t(8));
        QVERIFY(source.setCapacity(256)); QCOMPARE(source.snapshot()->steps.size(), std::size_t(8)); // no resurrected history
        const double next[]{200001, -200001}; source.append(200001, 200.001, next);
        QCOMPARE(source.snapshot()->steps.size(), std::size_t(9));
        QCOMPARE(frozen.steps.size(), std::size_t(32)); QCOMPARE(frozen.steps.back(), std::int64_t(200000));
        QCOMPARE(source.allocatedBytes(), allocated);
        QVERIFY(!source.setCapacity(0)); QVERIFY(!source.setCapacity(LiveAcquisition::maxPositions+1));
        QString error;
        QVERIFY(source.setDuration("0.125", &error)); QCOMPARE(source.capacity(), std::size_t(126));
        QVERIFY(source.setDuration("1.25e-1", &error)); QCOMPARE(source.capacity(), std::size_t(126));
        QVERIFY(source.setDuration("0.125000000000000001", &error)); QCOMPARE(source.capacity(), std::size_t(127));
        QVERIFY(!source.setDuration("1000", &error)); QVERIFY(!error.isEmpty()); QCOMPARE(source.capacity(), std::size_t(127));
        QVERIFY(!source.setDuration("nan", &error)); QVERIFY(!source.setDuration("-1", &error));
        LiveAcquisition rational(8, 1.0/3, {{"x", {}, "f64"}}, {}, 3, 1);
        QVERIFY(rational.setDuration("1", &error)); QCOMPARE(rational.capacity(), std::size_t(4));
        QVERIFY(rational.setDuration("1.000000000000000001", &error)); QCOMPARE(rational.capacity(), std::size_t(5));
    }
    void snapshotsRemainCoherentDuringConcurrentWrapAndResize() {
        LiveAcquisition source(1, .001, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        QVERIFY(source.setCapacity(1024));
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (int i = 1; i <= 500000; ++i) { const double v[]{static_cast<double>(i), -static_cast<double>(i)}; source.append(i, i*.001, v); }
            done = true;
        });
        bool coherent = true;
        std::size_t snapshots = 0;
        while (!done.load()) {
            source.setCapacity(snapshots % 2 ? 512 : 2048);
            if (auto frame = source.snapshot()) {
                ++snapshots;
                for (std::size_t i = 0; i < frame->steps.size(); ++i)
                    if (frame->values[i*2] != static_cast<double>(frame->steps[i]) || frame->values[i*2+1] != -frame->values[i*2]) coherent = false;
            }
        }
        producer.join(); QVERIFY(coherent); QVERIFY(snapshots > 0);
        auto last = source.snapshot(); QVERIFY(last); QCOMPARE(last->steps.back(), std::int64_t(500000));
    }
    void envelopePreservesSpikesAndCursorUsesRawValues() {
        LiveAcquisition source(3, .001, {{"x", "V", "f64"}});
        const double exact = std::nextafter(1., 2.);
        for (int i = 0; i < 20000; ++i) { const double v[]{i == 19998 ? exact : i == 19997 ? -20. : .5}; source.append(i, i*.001, v); }
        auto frame = source.snapshot(); QVERIFY(frame);
        const auto points = frame->envelope(0, 10);
        bool high = false, low = false;
        double previous = -1;
        for (const auto& p : points) { high |= p.y() == exact; low |= p.y() == -20; QVERIFY(p.x() >= previous); previous = p.x(); }
        QVERIFY(high && low);
        const auto zoomed = frame->envelope(0, 100, 19.99, 19.999);
        QVERIFY(zoomed.size() >= 10);
        for (qsizetype i = 1; i < zoomed.size(); ++i) QVERIFY(zoomed[i].x() - zoomed[i-1].x() < .00101);
        auto raw = frame->nearest(19.998); QVERIFY(raw); QCOMPARE(frame->values[*raw], exact);
        observation::LiveFrame tie; tie.times = {0, 2}; QCOMPARE(*tie.nearest(1), std::size_t(0));
        QVERIFY(!tie.nearest(-1));
    }
    void liveOnlyRunnerNeverCreatesJournalAndMixedRecordingStillComplete() {
        currentGate = std::make_shared<Gate>(); currentGate->releaseAll();
        QTemporaryDir storageDir; QVERIFY(storageDir.isValid());
        int opened = 0;
        observation::StorageOptions storage; storage.memoryLimit = 2; storage.temporaryDirectory = storageDir.path();
        storage.createStorage = [&]() -> std::unique_ptr<QIODevice> { ++opened; throw std::runtime_error("unexpected recording"); };
        LocalSchemaRunner runner(nullptr, storage);
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        runner.run(schema(std::nextafter(1., 2.)), .001, 200000, {{"live/x", "source", 0, "V", false, "live"}});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 15000);
        QCOMPARE(runner.state(), RunState::Completed); QVERIFY(!runner.recordedSource()); QCOMPARE(opened, 0);
        QVERIFY(QDir(storageDir.path()).entryList(QDir::Files|QDir::NoDotAndDotDot).isEmpty());
        auto live = runner.liveSources().value("live"); QVERIFY(live);
        auto frame = live->snapshot(); QVERIFY(frame); QCOMPARE(frame->received, std::uint64_t(200000));
        QCOMPARE(frame->steps.size(), std::size_t(4096)); QVERIFY(frame->finished);
        QCOMPARE(frame->values.back(), std::nextafter(1., 2.));
        QCOMPARE(runner.takeScopeSamples().samples.size(), std::size_t(0));
        runner.reset(); QCOMPARE(live->snapshot()->received, std::uint64_t(200000));
        LocalSchemaRunner mixed;
        QSignalSpy finished(&mixed, &LocalSchemaRunner::runCompleted);
        mixed.run(schema(9), .001, 10000, {{"live/x", "source", 0, {}, false, "live"}, {"scope/x", "source", 0}});
        QTRY_COMPARE(finished.count(), 1); QCOMPARE(mixed.state(), RunState::Completed);
        QCOMPARE(mixed.recordedSource()->status().samples, std::uint64_t(10000));
        QVERIFY(!mixed.recordedSource()->status().incomplete());
        QCOMPARE(mixed.liveSources()["live"]->snapshot()->steps.back(), std::int64_t(10000));
    }
    void continuousPauseResizeStopAndRerun() {
        currentGate = std::make_shared<Gate>(); currentGate->releaseAll();
        LocalSchemaRunner runner; QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        const std::vector<RunProbe> probes{{"live/x", "source", 0, {}, false, "live"}};
        runner.run(schema(), .001, -1, probes);
        QTRY_VERIFY(runner.completedTicks() > 20000);
        auto source = runner.liveSources()["live"]; QVERIFY(source);
        runner.pause(); QTRY_COMPARE(runner.state(), RunState::Paused);
        const auto ticks = runner.completedTicks();
        source->setCapacity(16); auto frame = source->snapshot(); QVERIFY(frame);
        QCOMPARE(frame->steps.size(), std::size_t(16)); QCOMPARE(frame->steps.back(), static_cast<std::int64_t>(ticks));
        source->setCapacity(32); QCOMPARE(source->snapshot()->steps.size(), std::size_t(16));
        runner.resume(); QTRY_VERIFY(runner.completedTicks() > ticks+32);
        runner.stop(); QTRY_COMPARE(complete.count(), 1); QCOMPARE(runner.state(), RunState::Stopped);
        QVERIFY(source->status().finished); QVERIFY(!runner.recordedSource());
        const auto run = source->snapshot()->runId;
        runner.run(schema(), .001, 10, probes); QTRY_COMPARE(complete.count(), 2);
        auto next = runner.liveSources()["live"]->snapshot(); QVERIFY(next); QVERIFY(next->runId != run);
        QCOMPARE(next->steps.size(), std::size_t(10)); QCOMPARE(next->steps.front(), std::int64_t(1));
        QVERIFY(source->status().finished);
    }
    void aggregateChannelLimitAcrossWorkspaces() {
        currentGate = std::make_shared<Gate>(); currentGate->releaseAll();
        std::vector<RunProbe> probes;
        for (int i = 0; i < 8; ++i)
            probes.push_back({"channel" + std::to_string(i), "source", 0, "V", false, i < 4 ? "a" : "b"});
        LocalSchemaRunner runner; QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        runner.run(schema(9), .001, 10, probes);
        QTRY_COMPARE(complete.count(), 1); QCOMPARE(runner.state(), RunState::Completed);
        QCOMPARE(runner.liveSources().size(), 2); QVERIFY(!runner.recordedSource());
        for (const auto& source : runner.liveSources()) {
            auto frame = source->snapshot(); QVERIFY(frame);
            QCOMPARE(frame->channels.size(), 4); QCOMPARE(frame->steps.size(), std::size_t(10));
            for (const auto value : frame->values) QCOMPARE(value, 9.);
        }
        probes.push_back({"ninth", "source", 0, "V", false, "c"});
        runner.run(schema(), .001, 10, probes);
        QCOMPARE(complete.count(), 2); QCOMPARE(runner.state(), RunState::Error);
        QVERIFY(complete.last()[1].toString().contains("8 live channels"));
        QCOMPARE(runner.completedTicks(), 0L); QVERIFY(runner.liveSources().isEmpty());
        QVERIFY(!runner.recordedSource());
    }
    void viewerFreezeRawHoverResizeAndReopen() {
        auto source = std::make_shared<LiveAcquisition>(12, .001, QVector<observation::LiveChannel>{{"live/x", "V", "f64"}}, QJsonObject{}, 1000, 1);
        const double exact = std::nextafter(1., 2.);
        auto append = [&](int first, int last) { for (int i = first; i <= last; ++i) { const double v[]{exact}; source->append(i, i*.001, v); } };
        append(1, 5000);
        viewers::ViewerManager manager(viewers::ViewerRegistry::instance()); manager.presentLive({{"live", source}});
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager.openObserver("LiveOscilloscope", "live")); QVERIFY(viewer);
        QTRY_VERIFY(!viewer->displayedFrame().times.empty());
        QVERIFY(viewer->plotView()->quickWidget()); QTRY_COMPARE(viewer->plotView()->quickWidget()->status(), QQuickWidget::Ready);
        const auto text = viewer->inspect(5);
        QVERIFY(text.contains("1.0000000000000002 V")); QVERIFY(text.contains("step 5000"));
        auto* freeze = viewer->findChild<QPushButton*>("live.freeze"); QVERIFY(freeze); freeze->setChecked(true);
        append(5001, 10000); manager.refreshViews(); QCOMPARE(viewer->displayedFrame().steps.back(), std::int64_t(5000));
        auto* count = viewer->findChild<QSpinBox*>("live.samples"); count->setValue(128); QCOMPARE(source->capacity(), std::size_t(128));
        QCOMPARE(viewer->displayedFrame().steps.back(), std::int64_t(5000)); // resizing never mutates frozen data
        viewer->inspect(0, false);
        viewer->findChild<QPushButton*>("live.follow")->click();
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(10000));
        QCOMPARE(viewer->displayedFrame().steps.size(), std::size_t(128));
        delete viewer;
        append(10001, 11000);
        viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager.openObserver("LiveOscilloscope", "live")); QVERIFY(viewer);
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(11000));
        QCOMPARE(source->capacity(), std::size_t(128));
        source->finish(); QTRY_VERIFY(viewer->findChild<QLabel*>("live.status")->text().contains("Run finished"));
        manager.beginRun("old", true); QVERIFY(viewer->displayedFrame().steps.empty());
    }
    void displayOverflowPreservesRawSample() {
        auto source = std::make_shared<LiveAcquisition>(13, .001,
            QVector<observation::LiveChannel>{{"live/x", "V", "f64"}});
        const double values[]{1e303};
        source->append(1, .001, values); source->append(2, .002, values);
        ui::LiveOscilloscopeWindow viewer("live");
        viewer.attachLiveSource(source); viewer.show();
        viewer.refreshSource();
        QTRY_COMPARE(viewer.displayedFrame().steps.size(), std::size_t(2));
        QTRY_COMPARE(viewer.plotView()->series().front()->points().size(), 2);
        auto* gain = viewer.findChild<QDoubleSpinBox*>("live.channel.0.gain"); QVERIFY(gain);
        auto* freeze = viewer.findChild<QPushButton*>("live.freeze"); QVERIFY(freeze);
        freeze->setChecked(true); gain->setValue(1e6);
        QTRY_VERIFY(viewer.plotView()->series().front()->points().isEmpty());
        QTRY_VERIFY(viewer.findChild<QLabel*>("live.status")->text().contains("overflow"));
        QCOMPARE(viewer.displayedFrame().values.back(), values[0]);
        QVERIFY(viewer.inspect(.002).contains(QString::number(values[0], 'g', 17)));
        gain->setValue(1);
        QTRY_COMPARE(viewer.plotView()->series().front()->points().size(), 2);
    }
};
QTEST_MAIN(LiveOscilloscopeTest)
#include "live_oscilloscope_test.moc"

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "lifecycle_test_support.hpp"
#include "live_oscilloscope_window.hpp"
#include "instrument_controls.hpp"
#include "viewers/window_inspection_panel.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QtGraphs/QLineSeries>
#include <QQuickWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDial>
#include <QSlider>
#include <QScrollArea>
#include <QToolButton>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSignalSpy>
#include <QCheckBox>
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
    void rotaryControlsPreserveExactValuesAndRelativePosition() {
        ui::RotaryValueControl gain(ui::RotaryValueControl::Mapping::LogGain, "gain"); gain.show();
        auto* number = gain.number(); auto* dial = gain.findChild<QDial*>("gain.dial"); QVERIFY(dial);
        const double exact = std::nextafter(.123456789012345, 1.);
        QSignalSpy changed(number, &QDoubleSpinBox::valueChanged);
        number->setValue(exact); QCOMPARE(number->value(), exact); QCOMPARE(changed.count(), 1);
        number->setValue(999999.123456); QCOMPARE(dial->value(), 1200);
        dial->setFocus(); QTest::keyClick(dial, Qt::Key_End); QCOMPARE(number->value(), 1e6);
        QTest::keyClick(dial, Qt::Key_Home); QCOMPARE(number->value(), 1e-6);
        auto* edit = number->findChild<QLineEdit*>(); QVERIFY(edit); edit->setFocus(); edit->selectAll();
        QTest::keyClicks(edit, "1.2345678901234567e-5"); QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(number->value(), QString("1.2345678901234567e-5").toDouble());
        std::unique_ptr<QDoubleSpinBox> frequency(ui::compactNumber(nullptr));
        frequency->setRange(-1000, 1000); frequency->setSuffix(" Hz"); frequency->show();
        auto* frequencyEdit = frequency->findChild<QLineEdit*>(); frequencyEdit->setFocus(); frequencyEdit->selectAll();
        QTest::keyClicks(frequencyEdit, "123.456789012345"); QTest::keyClick(frequencyEdit, Qt::Key_Return);
        QCOMPARE(frequency->value(), QString("123.456789012345").toDouble());
        ui::RotaryValueControl offset(ui::RotaryValueControl::Mapping::RelativeOffset, "offset"); offset.show();
        auto* offsetDial = offset.findChild<QDial*>("offset.dial"); QVERIFY(offsetDial);
        QVERIFY(!offset.findChild<QDoubleSpinBox*>("offset.span"));
        offset.setOffsetStep(.02);
        offset.number()->setValue(100.125);
        QCOMPARE(offsetDial->value(), 500);
        offsetDial->setValue(510); QCOMPARE(offset.number()->value(), 100.325);
        offsetDial->setValue(500); QCOMPARE(offset.number()->value(), 100.125);
        // Crossing the circular dial's seam moves a single increment.
        offsetDial->setValue(999); const double before = offset.number()->value();
        offsetDial->setValue(0); QCOMPARE(offset.number()->value(), before+.02);
        offsetDial->setValue(999); QCOMPARE(offset.number()->value(), before);
        offset.number()->setValue(1e12); offsetDial->setValue(0); QCOMPARE(offset.number()->value(), 1e12);

    }
    void triggerControlsKeepPresetAndSingleChangeSemantics() {
        ui::LiveOscilloscopeWindow viewer("controls");
        observation::TriggerSettings settings; settings.mode = observation::TriggerMode::Single; settings.rising = false; settings.pretriggerPercent = 37;
        QSignalSpy changes(&viewer, &ui::LiveOscilloscopeWindow::triggerSettingsEdited);
        viewer.setTriggerSettings(settings); QCOMPARE(changes.count(), 0);
        auto* slider = viewer.findChild<QSlider*>("trigger.pre.slider"); auto* spin = viewer.findChild<QSpinBox*>("trigger.pre"); QVERIFY(slider && spin);
        QCOMPARE(slider->value(), 37); QCOMPARE(spin->value(), 37);
        slider->setValue(61); QCOMPARE(spin->value(), 61); QCOMPARE(changes.count(), 1); QCOMPARE(viewer.triggerSettings().pretriggerPercent, 61);
        viewer.findChild<QToolButton*>("trigger.edge.0")->click(); QCOMPARE(changes.count(), 2); QVERIFY(viewer.triggerSettings().rising);
        const auto before = viewer.triggerSettings(); auto* level = viewer.findChild<QLineEdit*>("trigger.level");
        level->setText("nan"); QMetaObject::invokeMethod(level, "editingFinished"); QCOMPARE(viewer.triggerSettings(), before); QCOMPARE(changes.count(), 2);
    }
    void cursorSlidersKeepIdentityWhenCrossing() {
        auto frame = std::make_shared<observation::LiveFrame>(); frame->dt = .001; frame->channels = {{"x", "V", "f64"}};
        for (int i = 0; i < 20; ++i) { frame->times.push_back(i*.001); frame->steps.push_back(i); frame->values.push_back(i); }
        QWidget owner;
        viewers::WindowInspectionPanel panel([frame] { return std::optional{analysis::timeWindow(frame)}; }, &owner);
        QVERIFY(panel.pin());
        auto* a = panel.findChild<QSpinBox*>("inspection.a"); auto* b = panel.findChild<QSpinBox*>("inspection.b");
        auto* as = panel.findChild<QSlider*>("inspection.a.slider"); auto* bs = panel.findChild<QSlider*>("inspection.b.slider"); QVERIFY(a && b && as && bs);
        as->setValue(17); bs->setValue(3); QCOMPARE(a->value(), 17); QCOMPARE(b->value(), 3);
        a->setValue(19); QCOMPARE(as->value(), 19); QCOMPARE(bs->value(), 3);
        QVERIFY(panel.readout().contains("A")); QVERIFY(panel.readout().contains("B"));
        panel.clear(); QVERIFY(!as->isEnabled()); QVERIFY(!bs->isEnabled());
    }
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
        const double exact = std::nextafter(1.23456789012345, 2.);
        auto append = [&](int first, int last) { for (int i = first; i <= last; ++i) { const double v[]{exact}; source->append(i, i*.001, v); } };
        append(1, 5000);
        viewers::ViewerManager manager(viewers::ViewerRegistry::instance()); manager.presentLive({{"live", source}});
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager.openObserver("LiveOscilloscope", "live")); QVERIFY(viewer);
        QTRY_VERIFY(!viewer->displayedFrame().times.empty());
        QVERIFY(viewer->plotView()->quickWidget()); QTRY_COMPARE(viewer->plotView()->quickWidget()->status(), QQuickWidget::Ready);
        const auto text = viewer->inspect(5);
        QVERIFY(text.contains("live/x = 1.2346 V")); QVERIFY(text.contains("step 5000"));
        QVERIFY(!text.contains("1.234567890")); QCOMPARE(viewer->displayedFrame().values.back(), exact);
        auto* freeze = viewer->findChild<QPushButton*>("live.freeze"); QVERIFY(freeze); freeze->setChecked(true);
        append(5001, 10000); manager.refreshViews(); QCOMPARE(viewer->displayedFrame().steps.back(), std::int64_t(5000));
        auto* count = viewer->findChild<QSpinBox*>("live.samples"); count->setValue(128); QCOMPARE(source->capacity(), std::size_t(128));
        QCOMPARE(viewer->displayedFrame().steps.back(), std::int64_t(5000)); // resizing never mutates frozen data
        viewer->inspect(0, false);
        viewer->findChild<QPushButton*>("live.follow")->click();
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(10000));
        QCOMPARE(source->snapshot()->steps.size(), std::size_t(128));
        QVERIFY(viewer->displayedFrame().steps.size() <= 4096);
        delete viewer;
        append(10001, 11000);
        viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager.openObserver("LiveOscilloscope", "live")); QVERIFY(viewer);
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(11000));
        QCOMPARE(source->capacity(), std::size_t(128));
        source->finish(); QTRY_VERIFY(viewer->findChild<QLabel*>("live.status")->text().contains("Run finished"));
        manager.beginRun("old", true); QVERIFY(viewer->displayedFrame().steps.empty());
    }
    void timeBaseRetainsSeveralPeriodsAtDifferentModelRates() {
        for (const int rate : {100, 1000, 10000}) {
            auto source = std::make_shared<LiveAcquisition>(30+rate, 1./rate,
                QVector<observation::LiveChannel>{{"position", "V", "f64"}, {"velocity", "V", "f64"}}, QJsonObject{}, rate, 1);
            const int spike = 50*rate+rate/7;
            for (int i = 1; i <= 60*rate; ++i) {
                const double t = double(i)/rate, values[]{i == spike ? 3. : std::sin(t), std::cos(t)};
                source->append(i, t, values);
            }
            source->finish();
            const auto raw = source->snapshot(); QVERIFY(raw); QVERIFY(!raw->timeEnvelope);
            QCOMPARE(raw->steps.size(), std::size_t(4096)); QCOMPARE(raw->dt, 1./rate);
            const auto frame = source->timeSnapshot(); QVERIFY(frame.timeEnvelope);
            QVERIFY(frame.steps.size() <= 4096); QVERIFY(frame.times.front() <= 40.1); QCOMPARE(frame.times.back(), 60.);
            bool sawSpike = false; int rising = 0;
            for (std::size_t i = 0; i < frame.steps.size(); ++i) {
                sawSpike |= frame.steps[i] == spike && frame.values[2*i] == 3.;
                QCOMPARE(frame.values[2*i+1], std::cos(frame.times[i]));
                if (i && frame.values[2*(i-1)+1] < 0 && frame.values[2*i+1] >= 0) ++rising;
            }
            QVERIFY(sawSpike); QVERIFY(rising >= 3);
            ui::LiveOscilloscopeWindow viewer("time base"); viewer.attachLiveSource(source); viewer.show();
            QTRY_COMPARE(viewer.plotView()->xMax(), 60.); QCOMPARE(viewer.plotView()->xMin(), 40.);
            auto* scale = viewer.findChild<QDoubleSpinBox*>("live.timeScale"); QVERIFY(scale); QCOMPARE(scale->value(), 2.);
            viewer.findChild<QPushButton*>("live.freeze")->setChecked(true); scale->setValue(.5);
            QTRY_COMPARE(viewer.plotView()->xMin(), 55.); QCOMPARE(viewer.plotView()->xMax(), 60.);
            QCOMPARE(source->snapshot()->steps.size(), std::size_t(4096));
            QVERIFY(source->setTimeSpan(40));
            const auto wider = source->timeSnapshot(); QVERIFY(!wider.steps.empty()); QCOMPARE(wider.times.back(), 60.);
            bool keptSpike = false; for (std::size_t i = 0; i < wider.steps.size(); ++i) keptSpike |= wider.steps[i] == spike && wider.values[2*i] == 3.;
            QVERIFY(keptSpike); QVERIFY(wider.steps.size() <= 4096);
            analysis::SpectrumAnalyzer fft; analysis::PairAnalyzer pair;
            QCOMPARE(fft.analyze(frame, {}).state, analysis::SpectrumResult::State::Unavailable);
            QCOMPARE(pair.analyze(analysis::PairKind::Correlation, frame, {}).state, analysis::PairResult::State::Unavailable);
            const auto window = analysis::timeWindow(std::make_shared<observation::LiveFrame>(frame));
            QVERIFY(analysis::measurementReadout(window, 0, window.axis.size()-1).contains("not full-rate signal measurements"));
        }
    }
    void timeEnvelopeDoesNotBridgeGapsOrNonfiniteSamples() {
        LiveAcquisition source(90, .001, {{"x", "V", "f64"}}, {}, 1000, 1);
        const double finite[]{1}, invalid[]{std::numeric_limits<double>::quiet_NaN()};
        source.append(1, .001, finite); source.append(3, .003, invalid); source.append(4, .004, finite);
        const auto frame = source.timeSnapshot(); QVERIFY(frame.envelopeGaps); QVERIFY(frame.envelopeInvalid[0]);
        QVERIFY(frame.envelope(0, 100).empty());
        QVERIFY(!source.setTimeSpan(0)); QVERIFY(!source.setTimeSpan(std::numeric_limits<double>::infinity()));
        QVERIFY(source.setTimeSpan(40)); const auto wider = source.timeSnapshot();
        QVERIFY(wider.envelopeGaps); QVERIFY(wider.envelopeInvalid[0]); QVERIFY(wider.envelope(0, 100).empty());
    }
    void timeWorkerUsesRetainedRawDetailWithoutLosingOverview() {
        auto source = std::make_shared<LiveAcquisition>(95, .001, QVector<observation::LiveChannel>{{"sine", "V", "f64"}});
        for (int i = 1; i <= 60000; ++i) { const double value[]{std::sin(i*.1)}; source->append(i, i*.001, value); }
        source->finish();
        analysis::TimeViewWorker worker;
        analysis::TimeViewSettings settings; settings.prepareTime = settings.fitX = true;
        settings.timeSpan = .05; settings.buckets = 100; settings.visible[0] = true; settings.gain[0] = 1;
        worker.submitSource(source, false, settings, 1);
        QTRY_VERIFY(!worker.busy()); auto detail = worker.takeResult(); QVERIFY(detail && detail->frame && detail->historyFrame);
        QVERIFY(!detail->frame->timeEnvelope); QVERIFY(detail->historyFrame->timeEnvelope);
        QCOMPARE(detail->xMin, 59.95); QCOMPARE(detail->xMax, 60.);
        QVERIFY(detail->points[0].size() >= 50); QVERIFY(detail->warning.isEmpty());
        for (const auto& point : detail->points[0]) QVERIFY(std::abs(point.y()-std::sin(point.x()*100)) < 1e-11);
        settings.timeSpan = 20;
        worker.submitFrame(detail->historyFrame, settings, 2, detail->rawFrame);
        QTRY_VERIFY(!worker.busy()); auto overview = worker.takeResult(); QVERIFY(overview && overview->frame->timeEnvelope);
        QCOMPARE(overview->xMin, 40.); QCOMPARE(overview->xMax, 60.); QVERIFY(!overview->points[0].empty());
        settings.fitX = false; settings.lower = 40.; settings.upper = 40.05;
        worker.submitFrame(detail->historyFrame, settings, 3, detail->rawFrame);
        QTRY_VERIFY(!worker.busy()); auto expired = worker.takeResult(); QVERIFY(expired && expired->frame->timeEnvelope);
        QVERIFY(expired->warning.contains("not retained"));
        // A genuine gap in the retained raw interval must still be reported,
        // rather than being joined into a plausible continuous waveform.
        auto broken = std::make_shared<LiveAcquisition>(96, .001, QVector<observation::LiveChannel>{{"sine", "V", "f64"}});
        for (int i = 1; i <= 1000; ++i) if (i != 980) { const double value[]{std::sin(i*.1)}; broken->append(i, i*.001, value); }
        settings.fitX = true; settings.timeSpan = .05;
        worker.submitSource(broken, false, settings, 4);
        QTRY_VERIFY(!worker.busy()); auto gap = worker.takeResult(); QVERIFY(gap); QVERIFY(gap->points[0].empty());
        QVERIFY(gap->warning.contains("Discontinuous"));
        QCOMPARE(source->dt(), .001); QCOMPARE(source->capacity(), std::size_t(4096));
    }
    void independentChannelControlsKeepAxesAndReadoutStable() {
        auto source = std::make_shared<LiveAcquisition>(20, .001,
            QVector<observation::LiveChannel>{{"live/v", "V", "f64"}, {"live/u", "V", "f64"}});
        for (int i = 1; i <= 600000; ++i) {
            const double t = i*.001, values[]{std::sin(t), std::cos(t)}; source->append(i, t, values);
        }
        source->finish();
        ui::LiveOscilloscopeWindow viewer("two channels"); viewer.attachLiveSource(source); viewer.show();
        QTRY_COMPARE(viewer.plotView()->series().size(), 2);
        QTRY_VERIFY(viewer.plotView()->series()[1]->points().size() > 100);
        auto* plot = viewer.plotView();
        QCOMPARE(plot->xMax(), 600.); QCOMPARE(plot->xMin(), 580.);
        QVERIFY(std::abs(plot->series()[0]->points().front().x()-580.) < .1);
        QCOMPARE(plot->series()[0]->points().back().x(), 600.);
        QVERIFY(viewer.findChild<QLabel*>("live.timeRange")->text().contains("displayed span 20 s"));
        auto* freeze = viewer.findChild<QPushButton*>("live.freeze"); freeze->setChecked(true);
        const auto unchanged = plot->series()[1]->points(); const auto ymin = plot->yMin(), ymax = plot->yMax();
        viewer.findChild<QDoubleSpinBox*>("live.channel.0.offset")->setValue(.5);
        QTRY_COMPARE(plot->series()[0]->points().back().y(), std::sin(600.)+.5);
        QVERIFY(!viewer.findChild<QCheckBox*>("live.autoY")->isChecked());
        QCOMPARE(plot->series()[1]->points(), unchanged); QCOMPARE(plot->yMin(), ymin); QCOMPARE(plot->yMax(), ymax);
        viewer.findChild<QDoubleSpinBox*>("live.channel.1.gain")->setValue(2);
        QTRY_COMPARE(plot->series()[1]->points().back().y(), 2*std::cos(600.));
        QCOMPARE(plot->yMin(), ymin); QCOMPARE(plot->yMax(), ymax);
        // The legend checkbox and the channel-panel checkbox share one state.
        plot->bridge()->toggleLegend(1, false);
        QTRY_VERIFY(plot->series()[1]->points().isEmpty());
        QVERIFY(!viewer.findChild<QCheckBox*>("live.channel.1.visible")->isChecked());
        QCOMPARE(plot->bridge()->legend().size(), 2);
        QVERIFY(!plot->bridge()->legend()[1].toMap()["checked"].toBool());
        const auto plotGeometry = plot->geometry();
        auto* readout = viewer.findChild<QLabel*>("live.readout");
        const auto areaGeometry = readout->parentWidget()->parentWidget()->geometry();
        const auto* hoverArea = viewer.findChild<QScrollArea*>("live.readoutScroll");
        const auto* statusArea = viewer.findChild<QScrollArea*>("live.statusScroll");
        QVERIFY(hoverArea && statusArea); QVERIFY(hoverArea->height() > statusArea->height()*2);
        QVERIFY(statusArea->parentWidget()->height()-statusArea->geometry().bottom() < 12);
        viewer.inspect(600.); QCoreApplication::processEvents();
        viewer.inspect(0, false); QCoreApplication::processEvents();
        QCOMPARE(plot->geometry(), plotGeometry);
        QCOMPARE(readout->parentWidget()->parentWidget()->geometry(), areaGeometry);
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
        QVERIFY(viewer.inspect(.002).contains(QString::number(values[0], 'g', 5)));
        gain->setValue(1);
        QTRY_COMPARE(viewer.plotView()->series().front()->points().size(), 2);
    }
    void triggerPresetIsAppliedButNotReportedAsEdit() {
        ui::LiveOscilloscopeWindow viewer("live");
        QSignalSpy edited(&viewer, &ui::LiveOscilloscopeWindow::triggerSettingsEdited);
        observation::TriggerSettings preset; preset.mode = observation::TriggerMode::Auto; preset.channel = 1; preset.hysteresis = 1;
        viewer.setTriggerSettings(preset);
        QCOMPARE(edited.count(), 0); QVERIFY(viewer.triggerSettings() == preset);
        QCOMPARE(viewer.findChild<QComboBox*>("trigger.mode")->currentText(), QString("Auto"));
        QCOMPARE(viewer.findChild<QLineEdit*>("trigger.hysteresis")->text(), QString("1"));
        // Applied before the first sample once a run attaches, including the source channel.
        auto source = std::make_shared<LiveAcquisition>(15, .001,
            QVector<observation::LiveChannel>{{"live/a", "V", "f64"}, {"live/b", "V", "f64"}});
        viewer.attachLiveSource(source);
        QCOMPARE(source->triggerStatus().state, observation::TriggerState::Filling);
        QCOMPARE(viewer.findChild<QComboBox*>("trigger.channel")->currentIndex(), 1);
        QCOMPARE(edited.count(), 0);
        auto* level = viewer.findChild<QLineEdit*>("trigger.level");
        level->setText("0.5"); QVERIFY(QMetaObject::invokeMethod(level, "editingFinished", Qt::DirectConnection));
        QCOMPARE(edited.count(), 1); QCOMPARE(viewer.triggerSettings().level, .5);
    }
    void hoverTipFollowsDrawnTraces() {
        auto source = std::make_shared<LiveAcquisition>(14, .001,
            QVector<observation::LiveChannel>{{"live/up", "V", "f64"}, {"live/flat", "", "f64"}});
        QVERIFY(source->setTimeSpan(.1));
        for (int i = 1; i <= 100; ++i) { const double v[]{i*.01, -.5}; source->append(i, i*.001, v); }
        ui::LiveOscilloscopeWindow viewer("live");
        viewer.attachLiveSource(source); viewer.resize(1100, 740); viewer.show();
        QTRY_COMPARE(viewer.displayedFrame().steps.size(), std::size_t(100));
        QTRY_COMPARE(viewer.plotView()->series().front()->points().size(), 100);
        auto* bridge = viewer.plotView()->bridge();
        QTRY_VERIFY(bridge->plotAreaSize.width() > 100 && bridge->plotAreaSize.height() > 100);
        auto* freeze = viewer.findChild<QPushButton*>("live.freeze"); QVERIFY(freeze); freeze->setChecked(true);
        viewer.plotView()->setXRange(0, .1); viewer.plotView()->setYRange(-1, 1.1);
        auto text = [](const QVariantList& rows) { QStringList lines; for (const auto& row : rows) lines << row.toMap()["text"].toString(); return lines.join('\n'); };
        // On a stored sample: header with its step, value with unit, and a marker on it.
        auto rows = viewer.hoverTip(.05, .5);
        QCOMPARE(rows.size(), 2); QVERIFY(text(rows).contains("step 50")); QVERIFY(text(rows).contains("live/up = 0.5 V"));
        QCOMPARE(bridge->hoverMarkers.size(), 1);
        QCOMPARE(bridge->hoverMarkers.front().toMap()["y"].toDouble(), .5);
        // Away from every trace: nothing to show.
        QVERIFY(viewer.hoverTip(.05, .2).isEmpty()); QVERIFY(bridge->hoverTip.isEmpty());
        // Zoomed in so samples are far apart: a point on the drawn segment still counts.
        viewer.plotView()->setXRange(.0495, .0515); viewer.plotView()->setYRange(.48, .53);
        rows = viewer.hoverTip(.05075, .5075);
        QVERIFY(text(rows).contains("live/up = 0.51 V")); QVERIFY(text(rows).contains("step 51"));
        // A unitless channel; hidden channels are never reported.
        viewer.plotView()->setXRange(0, .1); viewer.plotView()->setYRange(-1, 1.1);
        QVERIFY(text(viewer.hoverTip(.02, -.5)).contains("live/flat = -0.5"));
        viewer.findChild<QCheckBox*>("live.channel.1.visible")->setChecked(false);
        QTRY_VERIFY(viewer.plotView()->series()[1]->points().isEmpty());
        QVERIFY(viewer.hoverTip(.02, -.5).isEmpty());
        // Display gain moves the trace; raw stays primary, the shown value is labelled.
        constexpr double gain = 1.97530864202469;
        viewer.findChild<QDoubleSpinBox*>("live.channel.0.gain")->setValue(gain);
        QTRY_COMPARE(viewer.plotView()->series().front()->points().at(49).y(), .5*gain);
        QVERIFY(text(viewer.hoverTip(.05, .5*gain)).contains("live/up = 0.5 V (shown 0.98765)"));
        QCOMPARE(bridge->hoverMarkers.front().toMap()["y"].toDouble(), .5*gain);
        QVERIFY(viewer.hoverTip(.05, .5, false).isEmpty()); QVERIFY(bridge->hoverMarkers.isEmpty());
    }
};
QTEST_MAIN(LiveOscilloscopeTest)
#include "live_oscilloscope_test.moc"

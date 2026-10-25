// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/observer_presentation.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include "viewers/line_plot_view.hpp"
#include "live_oscilloscope_window.hpp"
#include "analysis/worker_pool.hpp"
#include "diagram/diagram_scene.hpp"
#include "scene_serializer.hpp"
#include "deployment_support.hpp"
#include <QBuffer>
#include <QSemaphore>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QDoubleSpinBox>
#include <QSignalSpy>
#include <QComboBox>
#include <QUndoStack>
#include <QtTest>
#include <QtGraphs/QLineSeries>
#include <cmath>
using namespace s7studio;
using namespace viewers;
namespace {
class CountedBuffer : public QBuffer {
public:
    int reads = 0;
    qint64 readData(char* bytes, qint64 count) override { ++reads; return QBuffer::readData(bytes, count); }
};
ObserverDescriptor scope(int count = 1) {
    ObserverDescriptor descriptor{"scope", "Scope", {}};
    for (int i = 0; i < count; ++i) descriptor.inputs.push_back({QString("scope/c%1").arg(i), i == 0 ? "V" : "", "source", i});
    return descriptor;
}
}
class ObserverPresentationsTest : public QObject {
    Q_OBJECT
private slots:
    void sparseTailsDoNotReadTheJournal() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); CountedBuffer* storage = nullptr;
        observation::StorageOptions options{3, temporary.path(), [&] { auto buffer = std::make_unique<CountedBuffer>(); storage = buffer.get(); buffer->open(QIODevice::ReadWrite); return buffer; }, {}};
        auto source = std::make_shared<observation::ObservationSource>(7, options);
        const auto dense = source->channel("scope/c0"), sparse = source->channel("scope/c1");
        QVERIFY(source->append({sparse, 1, .001, 42}));
        for (int i = 1; i <= 40000; ++i) QVERIFY(source->append({dense, i, i*.001, static_cast<double>(i)}));
        QVERIFY(storage); QCOMPARE(storage->reads, 0);
        const auto tail = source->tail({dense, sparse}); QCOMPARE(tail.channels.size(), 2);
        QCOMPARE(tail.channels[0].samples.size(), 4096); QCOMPARE(tail.channels[0].samples.front().step, std::int64_t(35905));
        QCOMPARE(tail.channels[1].samples.size(), 1); QCOMPARE(tail.channels[1].samples.front().step, std::int64_t(1));
        QCOMPARE(storage->reads, 0); QCOMPARE(source->status().samples, std::uint64_t(40001));
        PreviewRequest request{scope(2), {0,1}, source}; const auto prepared = preparePreview(request);
        QVERIFY(prepared.details[1].contains("step 1")); QVERIFY(prepared.details[1].contains("unit unspecified"));
        QCOMPARE(prepared.rawPositions, std::size_t(4097)); QVERIFY(prepared.vertices <= 2048); QCOMPARE(storage->reads, 0);
        QVERIFY(source->finish()); QCOMPARE(source->read(0, 1).samples.front().value, 42.);
    }
    void reductionBoundsAndDiscontinuities() {
        auto source = std::make_shared<observation::ObservationSource>(9);
        const auto channel = source->channel("scope/c0");
        for (int i = 1; i <= 6000; ++i) QVERIFY(source->append({channel, i + (i > 4000 ? 20 : 0), i*.001, i == 5000 ? 1e6 : std::sin(i*.1)}));
        auto result = preparePreview({scope(), {0}, source}); QCOMPARE(result.rawPositions, std::size_t(4096)); QVERIFY(result.vertices <= 1024);
        QCOMPARE(result.traces.front().segments.size(), 2); bool peak = false;
        for (const auto& segment : result.traces.front().segments) for (const auto& point : segment) peak |= point.y() == 1e6;
        QVERIFY(peak); QVERIFY(result.details.front().contains("step 6020"));
        const auto unavailable = preparePreview({scope(), {0,7}, source}); QVERIFY(unavailable.status.contains("Unavailable selected channel indices: 7"));
        QVERIFY(unavailable.summary.startsWith("Unavailable channels"));
    }
    void livePreviewKeepsModelTimestampsAfter600Seconds() {
        auto descriptor = scope(2); descriptor.id = "live"; descriptor.type = "LiveOscilloscope";
        auto live = std::make_shared<observation::LiveAcquisition>(15, .001,
            QVector<observation::LiveChannel>{{"scope/c0", "V", "f64"}, {"scope/c1", "", "f64"}});
        for (int step = 1; step <= 600000; ++step) {
            const double t = step*.001, values[]{std::sin(t), std::cos(t)}; live->append(step, t, values);
        }
        const auto data = preparePreview({descriptor, {0,1}, {}, live});
        QVERIFY(data.summary.contains("600 s"));
        QVERIFY(data.rawPositions <= 8192);
        QCOMPARE(data.traces.size(), 2);
        ObserverPresentation presentation("LiveOscilloscope"); presentation.present(data);
        QCOMPARE(presentation.linePlot()->xMin(), 580.); QCOMPARE(presentation.linePlot()->xMax(), 600.);
        for (const auto* series : presentation.linePlot()->series()) {
            QVERIFY(std::abs(series->points().front().x()-580.) < .1); QCOMPARE(series->points().back().x(), 600.);
        }
    }
    void coordinatesJoinOnlyEqualSteps() {
        auto source = std::make_shared<observation::ObservationSource>(11);
        const auto x = source->channel("phase/x"), y = source->channel("phase/y");
        for (int step : {1,3,4,6}) QVERIFY(source->append({x, step, step*.01, static_cast<double>(step)}));
        for (int step : {2,3,4,5}) QVERIFY(source->append({y, step, step*.01, -static_cast<double>(step)}));
        ObserverDescriptor descriptor{"phase", "PhasePortrait", {{"phase/x", "m", "x", 0}, {"phase/y", "m/s", "y", 0}}};
        const auto result = preparePreview({descriptor, {0,1}, source});
        const auto points = result.traces.front().segments.front(); QCOMPARE(points.size(), 2); QCOMPARE(points.front(), QPointF(3,-3)); QCOMPARE(points.back(), QPointF(4,-4));
        QVERIFY(result.details[0].contains("step 6")); QVERIFY(result.details[1].contains("step 5"));
    }
    void triggeredPreviewKeepsModelTimeSpanAcrossRates() {
        auto descriptor = scope(); descriptor.id = "live"; descriptor.type = "LiveOscilloscope";
        observation::TriggerSettings trigger; trigger.mode = observation::TriggerMode::Single; trigger.level = .5;
        for (const int rate : {100, 1000, 10000}) {
            auto live = std::make_shared<observation::LiveAcquisition>(rate, 1./rate,
                QVector<observation::LiveChannel>{{"scope/c0", "V", "f64"}});
            QVERIFY(live->setCapacity(101)); QVERIFY(live->configureTrigger(trigger));
            for (int i = 1; i <= 1000; ++i) { const double value[]{i < 100 ? 0. : 1.}; live->append(i, double(i)/rate, value); }
            live->finish();
            const auto data = preparePreview({descriptor, {0}, {}, live, trigger});
            QVERIFY(data.timeRange); QCOMPARE(data.timeRange->first, -5.); QCOMPARE(data.timeRange->second, 15.);
            QVERIFY(data.status.contains("partial display history")); QVERIFY(data.timeAxis.contains("sampled trigger"));
            QCOMPARE(live->capacity(), std::size_t(101));
        }
    }
    void sharedSettingsAndIndependentPresentations() {
        ViewerManager manager(ViewerRegistry::instance()); observation::TriggerSettings settings;
        settings.mode = observation::TriggerMode::Single; settings.pretriggerPercent = 25; settings.level = .2;
        QVERIFY(manager.setObserverTrigger("live", settings));
        auto source = std::make_shared<observation::LiveAcquisition>(13, .001, QVector<observation::LiveChannel>{{"live/c0", "V", "f64"}});
        source->setCapacity(64); manager.presentLive({{"live", source}});
        for (int step = 1; step <= 512; ++step) { const double value[]{std::sin(step*.1)}; source->append(step, step*.001, value); }
        const auto before = source->triggerSnapshot(); QVERIFY(before && before->trigger && before->trigger->triggered);
        auto descriptor = scope(); descriptor.id = "live"; descriptor.type = "LiveOscilloscope"; descriptor.inputs[0].name = "live/c0";
        const auto data = preparePreview({descriptor, {0}, {}, source, manager.observerTrigger("live"), 1});
        QCOMPARE(data.run, std::uint64_t(13)); QVERIFY(data.vertices > 0);
        ObserverPresentation first("LiveOscilloscope"), second("LiveOscilloscope"); first.present(data); second.present(data);
        first.linePlot()->setXRange(0, .01); QVERIFY(second.linePlot()->xMax() != first.linePlot()->xMax());
        auto* full = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager.ensureObserver("LiveOscilloscope", "live")); QVERIFY(full);
        QCOMPARE(full->triggerSettings(), settings); QCOMPARE(source->capacity(), std::size_t(64));
        QCOMPARE(source->triggerSnapshot()->trigger->id, before->trigger->id); QCOMPARE(source->triggerSnapshot()->steps, before->steps);
        QSignalSpy edits(full, &ui::LiveOscilloscopeWindow::triggerSettingsEdited);
        full->findChild<QComboBox*>("trigger.mode")->setCurrentIndex(0); QCOMPARE(edits.count(), 1); QCOMPARE(manager.observerTrigger("live").mode, observation::TriggerMode::Rolling);
    }
    void cancelledWorkerRejectsItsOldCompletion() {
        QSemaphore entered, release;
        for (int i=0;i<2;++i) analysis::analysisPool().start([&] { entered.release(); release.acquire(); });
        entered.acquire(2);
        PreviewWorker worker; PreviewRequest first{scope(), {0}}; first.revision = 1; worker.submit(first); worker.cancel(); first.revision = 2; worker.submit(first);
        release.release(2); QVERIFY(analysis::analysisPool().waitForDone(5000));
        const auto result = worker.takeResult(); QVERIFY(result); QCOMPARE(result->revision, std::uint64_t(2));
        worker.cancel(); QVERIFY(!worker.takeResult());
    }
    void selectionRoundTripsUndoAndDeploymentProjection() {
        diagram::DiagramScene scene; auto node = scene.addElement({"scope", "Scope", {}, {}, {}, {}}, {});
        const auto baseline = diagram::SceneSerializer{}.serialize(scene, {});
        const boost::json::array selection{boost::json::object{{"element_id", "scope"}, {"type", "Scope"}, {"collapsed", false}, {"plotted_channels", boost::json::array{0,2}}}};
        scene.setObserversPanel(selection, true);
        const auto document = diagram::SceneSerializer{}.serialize(scene, {});
        QCOMPARE(document.diagram.at("observers_panel").as_array(), selection);
        QVERIFY(diagram::deploymentDiagram(document,{}) == diagram::deploymentDiagram(baseline,{}));
        diagram::DiagramScene restored; diagram::SceneSerializer{}.deserialize(diagram::DiagramDocument::fromJson(document.toJson()), restored);
        QVERIFY(restored.observersPanel() == selection);
        scene.clearSelection(); node->setSelected(true); scene.deleteSelection(); QVERIFY(scene.observersPanel().empty());
        scene.undoStack()->undo(); QVERIFY(scene.observersPanel() == selection);
        auto element = node->element(); element.type = "PhasePortrait"; scene.changeElement(node, element); QVERIFY(scene.observersPanel().empty());
        scene.undoStack()->undo(); QVERIFY(scene.observersPanel() == selection);
        scene.undoStack()->undo(); QVERIFY(scene.observersPanel().empty());
        scene.undoStack()->redo(); QVERIFY(scene.observersPanel() == selection);
        auto invalid = document.toJson(); invalid.as_object()["diagram"].as_object()["observers_panel"] = boost::json::object{};
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, diagram::DiagramDocument::fromJson(invalid));
    }
};
QTEST_MAIN(ObserverPresentationsTest)
#include "observer_presentations_test.moc"

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// Observation-viewer framework: registry, manager routing and lifecycle,
// step alignment, Qt Graphs plot state and the Qt Quick 3D trajectory.
#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "scope_sample_mailbox.hpp"
#include "scope_view_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "observation/step_aligner.hpp"
#include "viewers/trajectory_geometry.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"

#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QBuffer>
#include "observation/scope_results.hpp"
#include <QLabel>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWidget>
#include <QtGraphs/QLineSeries>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using namespace s7studio;

void require(const bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

auto sample(const char* probe, const long step, const double value) -> ScopeSample
{
    return {probe, step, step * 0.001, value};
}

auto everythingIsScope(const QString&) -> QString { return QStringLiteral("Scope"); }

// A viewer type that is not built in, registered without touching the host.
class CountingViewer : public QWidget, public viewers::ObservationViewer {
public:
    explicit CountingViewer(QString probe) : probe_(std::move(probe)) {}
    auto widget() -> QWidget* override { return this; }
    void appendObservedSample(const viewers::ObservedSample& sample) override { sum += sample.value; ++count; }
    void clearSamples() override { sum = 0; count = 0; }
    void setDroppedSampleCount(const std::uint64_t value) override { dropped = value; }
    double sum = 0;
    int count = 0;
    std::uint64_t dropped = 0;
private:
    QString probe_;
};

void testRegistry()
{
    auto& registry = viewers::ViewerRegistry::instance();
    const auto* scope = registry.find(QStringLiteral("Scope"));
    const auto* phase = registry.find(QStringLiteral("PhasePortrait"));
    const auto* trajectory = registry.find(QStringLiteral("PhaseTrajectory3D"));
    require(scope && phase && trajectory && registry.find("LiveOscilloscope") && registry.types().size() == 4, "Built-in viewer types missing");
    require(scope->inputSuffixes.isEmpty() && phase->inputSuffixes == QStringList{"x", "y"}
        && trajectory->inputSuffixes == QStringList{"x", "y", "z"}, "Observer input mapping changed");
    require(!registry.find(QStringLiteral("Nope")), "Unknown viewer type resolved");

    viewers::ViewerRegistry local;
    viewers::registerBuiltinViewers(local);
    local.add({QStringLiteral("Counter"), QStringLiteral("Counter"), {}, {}, {QStringLiteral("in")},
               [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* {
                   return new CountingViewer(spec.probeNames.value(0));
               }});
    require(local.types().size() == 5, "Registering a new viewer type failed");
    local.add({QStringLiteral("Counter"), QStringLiteral("Counter v2"), {}, {}, {QStringLiteral("in")},
               local.find(QStringLiteral("Counter"))->create});
    require(local.types().size() == 5 && local.find(QStringLiteral("Counter"))->displayName == "Counter v2",
        "Re-registering a type did not replace it");

    // A new type is routed by the unchanged manager.
    viewers::ViewerManager manager(local);
    auto* counter = dynamic_cast<CountingViewer*>(manager.ensureObserver(QStringLiteral("Counter"), QStringLiteral("c1")));
    require(counter, "Custom viewer was not created");
    ScopeSampleBatch batch;
    batch.samples = {sample("c1/in", 1, 2.0), sample("c1/other", 1, 100.0), sample("c1/in", 2, 3.0)};
    manager.deliver(batch, [](const QString& id) {
        return id == QStringLiteral("c1") ? QStringLiteral("Counter") : QStringLiteral("Scope");
    });
    require(counter->count == 2 && counter->sum == 5.0, "Custom viewer got the wrong samples");
}

void testManagerRoutingAndAlignment()
{
    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    // Two observers of each type: independent instances.
    auto* p1 = dynamic_cast<ui::PhasePortraitViewWindow*>(manager.ensureObserver("PhasePortrait", "pp1"));
    auto* p2 = dynamic_cast<ui::PhasePortraitViewWindow*>(manager.ensureObserver("PhasePortrait", "pp2"));
    auto* t1 = dynamic_cast<ui::PhaseTrajectory3DViewWindow*>(manager.ensureObserver("PhaseTrajectory3D", "t1"));
    require(p1 && p2 && t1 && p1 != p2, "Observer instances were not created independently");
    require(manager.ensureObserver("PhasePortrait", "pp1") == p1, "ensure() duplicated an instance");
    require(p1->xProbeName() == "pp1/x" && p1->yProbeName() == "pp1/y" && t1->zProbeName() == "t1/z",
        "Observer probes do not follow the type's inputs");
    require(!manager.ensure("PhasePortrait", {"bad", {"a", "a"}}), "Degenerate portrait accepted");
    require(!manager.ensure("PhaseTrajectory3D", {"bad", {"a", "b"}}), "3D viewer accepted two probes");
    require(!manager.ensure("Unknown", {"u", {}}), "Unknown type created a viewer");

    // Coordinates of one step arrive in any order and interleaved with other
    // observers; each point must come from one step. Step 3 of pp1 lacks y.
    ScopeSampleBatch batch;
    batch.samples = {sample("pp1/y", 1, 10), sample("t1/z", 1, 300), sample("pp2/x", 1, -1), sample("pp1/x", 1, 1),
                     sample("t1/x", 1, 100), sample("pp2/y", 1, -10), sample("t1/y", 1, 200),
                     sample("pp1/x", 2, 2), sample("scope_a/v", 2, 0.5), sample("pp1/y", 2, 20),
                     sample("pp1/x", 3, 3), sample("pp2/x", 3, -3), sample("pp2/y", 3, -30), sample("scope_a/w", 3, 0.75)};
    manager.deliver(batch, [](const QString& id) {
        if (id.startsWith("pp")) return QStringLiteral("PhasePortrait");
        if (id.startsWith("t")) return QStringLiteral("PhaseTrajectory3D");
        return QStringLiteral("Scope");
    });
    require(p1->points() == QList<QPointF>{{1, 10}, {2, 20}}, "Portrait 1 misaligned X/Y samples");
    require(p2->points() == QList<QPointF>{{-1, -10}, {-3, -30}}, "Portrait 2 misaligned X/Y samples");
    require(t1->points().size() == 1 && t1->points().front() == viewers::TrajectoryPoint{100, 200, 300},
        "3D viewer misaligned X/Y/Z samples");
    auto* scope = dynamic_cast<ui::ScopeViewWindow*>(manager.openObserver("Scope", "scope_a"));
    require(scope && scope->recording().series().size() == 2
        && scope->recording().series().contains("live:v") && scope->recording().series().contains("live:w"),
        "Scope did not read retained channels on open");
    require(manager.viewers().size() == 4, "Unexpected viewer instances");

    // Loss accounting reaches existing and new viewers; clearing resets it.
    ScopeSampleBatch lossy;
    lossy.dropped = 5;
    manager.deliver(lossy, everythingIsScope);
    require(manager.droppedSampleCount() == 5, "Dropped samples not accumulated");
    auto* late = dynamic_cast<ui::PhasePortraitViewWindow*>(manager.ensureObserver("PhasePortrait", "late"));
    for (auto* window : {static_cast<QWidget*>(p1), static_cast<QWidget*>(late)}) {
        bool shown = false;
        for (auto* label : window->findChildren<QLabel*>()) shown |= label->text().contains("display dropped 5");
        require(shown, "Capture loss hidden in a viewer");
    }
    manager.clearSamples();
    require(manager.droppedSampleCount() == 5 && manager.source()->status().samples == 14 && p1->points().isEmpty() && t1->points().isEmpty()
        && scope->recording().series().isEmpty(), "Clear erased source data or left presentation points behind");
    // A step half-delivered before the clear must not complete afterwards.
    ScopeSampleBatch afterClear;
    afterClear.samples = {sample("pp1/y", 3, 30)};
    manager.deliver(afterClear, everythingIsScope);
    require(p1->points().isEmpty(), "Clear kept a pending half point");
}

void testLifecycle()
{
    auto manager = std::make_unique<viewers::ViewerManager>(viewers::ViewerRegistry::instance());
    const auto portraits = [](const QString& id) {
        return id == QStringLiteral("pp") ? QStringLiteral("PhasePortrait") : QStringLiteral("Scope");
    };
    auto* viewer = manager->openObserver("PhasePortrait", "pp");
    QPointer<QWidget> window = viewer->widget();
    require(window && window->isVisible(), "open() did not show the viewer");
    ScopeSampleBatch batch;
    batch.samples = {sample("pp/x", 1, 1), sample("pp/y", 1, 2)};
    manager->deliver(batch, portraits);
    // Closing hides; the recording survives and reopening shows the same instance.
    window->close();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(window && !window->isVisible(), "Closing destroyed the viewer");
    manager->deliver(batch, portraits); // same step again: a new (duplicate) point
    require(manager->openObserver("PhasePortrait", "pp") == viewer && window->isVisible(), "Reopen created a new viewer");
    require(static_cast<ui::PhasePortraitViewWindow*>(viewer)->points().size() == 2, "Hidden viewer stopped recording");
    require(manager->viewers().size() == 1, "Delivery created extra viewers");

    // A viewer destroyed elsewhere is forgotten and never receives samples;
    // the next sample of its observer records into a fresh instance.
    delete window.data();
    require(!manager->find("PhasePortrait", "pp") && manager->viewers().isEmpty(), "Destroyed viewer still registered");
    manager->deliver(batch, portraits);
    auto* recreated = dynamic_cast<ui::PhasePortraitViewWindow*>(manager->openObserver("PhasePortrait", "pp"));
    require(recreated && recreated->points() == QList<QPointF>{{1, 2}, {1, 2}, {1, 2}}, "Recreated viewer lost source history");

    // Diagram switch: closeAll destroys every viewer.
    auto* scope = manager->openObserver("Scope", "s");
    QPointer<QWidget> scopeWindow = scope->widget();
    manager->closeAll();
    require(!scopeWindow && manager->viewers().isEmpty(), "closeAll left viewers behind");

    // Shutdown with viewers open: destroying the manager destroys them.
    QPointer<QWidget> open3d = manager->openObserver("PhaseTrajectory3D", "t")->widget();
    QPointer<QWidget> openScope = manager->openObserver("Scope", "s2")->widget();
    QApplication::processEvents();
    manager.reset();
    require(!open3d && !openScope, "Viewer windows outlived their manager");
}

void testRetainedComparisonWithoutWindows()
{
    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    ScopeSampleBatch first; first.runId = 11;
    first.samples = {{"scope/v", 1, .001, 12, 11}, {"scope/v", 2, .002, 24, 11}};
    manager.deliver(first, everythingIsScope);
    require(manager.viewers().isEmpty(), "First run created a recording window");
    manager.beginRun("Run 11", true);
    ScopeSampleBatch second; second.runId = 12;
    second.samples = {{"scope/v", 1, .001, 99, 12}};
    manager.deliver(second, everythingIsScope);
    require(manager.viewers().isEmpty(), "Second run created a recording window");
    auto* scope = dynamic_cast<ui::ScopeViewWindow*>(manager.openObserver("Scope", "scope"));
    require(scope && scope->recording().series().size() == 2, "Unopened previous run lost its comparison");
    int comparisons = 0;
    for (const auto& series : scope->recording().series()) if (series.isComparison()) {
        ++comparisons;
        require(series.receivedSamples == 2 && series.lastValue == 24 && series.comparisonLabel.startsWith("Run 11"),
            "Late comparison has wrong source data or identity");
    }
    require(comparisons == 1, "Previous run was duplicated");
    delete scope;
    scope = dynamic_cast<ui::ScopeViewWindow*>(manager.openObserver("Scope", "scope"));
    require(scope->recording().series().size() == 2, "Recreation lost session-owned comparison");
}

void testWindowIndependentResults()
{
    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    ScopeSampleBatch batch;
    for (long i = 1; i <= 25000; ++i) {
        batch.samples.push_back(sample("scope/v", i, i * .25));
        batch.samples.push_back(sample("pp/x", i, i));
        batch.samples.push_back(sample("pp/y", i, 2 * i));
        batch.samples.push_back(sample("t/x", i, i));
        batch.samples.push_back(sample("t/y", i, 2 * i));
        batch.samples.push_back(sample("t/z", i, 3 * i));
    }
    const auto types = [](const QString& id) {
        return id == "pp" ? QStringLiteral("PhasePortrait") : id == "t" ? QStringLiteral("PhaseTrajectory3D") : QStringLiteral("Scope");
    };
    manager.deliver(batch, types);
    require(manager.viewers().isEmpty(), "Data delivery created hidden windows");
    const auto source = manager.source();
    require(source->status().samples == 150000, "No-window delivery lost samples");
    auto* scope = dynamic_cast<ui::ScopeViewWindow*>(manager.openObserver("Scope", "scope"));
    require(scope, "Cannot open retained Scope");
    // Export before bounded replay catches up: full source, not plotted prefix.
    QBuffer output; output.open(QIODevice::WriteOnly); QString error;
    require(scope->writeRunResults(output, &error), "Source-backed save failed");
    QBuffer input(&output.buffer()); input.open(QIODevice::ReadOnly);
    ui::ScopeRecording restored("saved");
    require(restored.openRunResults(input, "saved", &error), "Saved source cannot reopen");
    require(restored.series().first().receivedSamples == 25000, "Save used incomplete viewer replay");
    auto* phase = dynamic_cast<ui::PhasePortraitViewWindow*>(manager.openObserver("PhasePortrait", "pp"));
    auto* trajectory = dynamic_cast<ui::PhaseTrajectory3DViewWindow*>(manager.openObserver("PhaseTrajectory3D", "t"));
    require(phase && trajectory, "Cannot open retained phase data");
    for (int turn = 0; turn < 40; ++turn) manager.refreshViews();
    require(phase->points().size() == 25000 && trajectory->points().size() == 25000, "Phase history was window-owned");
    require(phase->points().last() == QPointF(25000, 50000), "Replayed coordinates misaligned");
    scope->clearSamples(); phase->clearSamples(); trajectory->clearSamples();
    require(source->status().samples == 150000, "Presentation clear deleted captured data");
    manager.refreshViews(); require(phase->points().isEmpty(), "Clear replayed old history again");
    delete phase;
    phase = dynamic_cast<ui::PhasePortraitViewWindow*>(manager.openObserver("PhasePortrait", "pp"));
    for (int turn = 0; turn < 40; ++turn) manager.refreshViews();
    require(phase->points().size() == 25000, "Viewer deletion destroyed recorded history");
    manager.beginRun("old", false);
    require(manager.source() != source && source->status().samples == 150000, "New run mutated previous source");
    require(phase->points().isEmpty(), "New source retained old phase alignment/history");
    manager.present(source, 100, types);
    for (int turn = 0; turn < 40; ++turn) manager.refreshViews();
    require(source->status().samples == 150000 && source->status().captureDropped == 0,
        "Presentation duplicated authoritative samples or changed completeness");
    require(phase->points().size() == 25000, "Authoritative source did not drive phase replay");
    bool previewWarning = false;
    for (auto* label : scope->findChildren<QLabel*>()) previewWarning |= label->text().contains("preview skipped 100");
    require(previewWarning, "Preview loss is not distinguished from capture loss");
    QBuffer full; full.open(QIODevice::WriteOnly);
    require(scope->writeRunResults(full, &error), "Preview loss prevented complete source-backed save");
    require(!source->status().incomplete(), "Preview loss marked authoritative capture incomplete");
}

void testRunDetails()
{
    const QString model = R"({"name":"saved-model","elements":[],"connections":[]})";
    const QJsonObject provenance{{"format_version", 1}, {"run_uuid", "12345678-1234-1234-1234-123456789abc"},
        {"effective_model_json", model}, {"requested_model_json", model},
        {"model_sha256", QString::fromLatin1(QCryptographicHash::hash(model.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {"model_name", "saved-model"}, {"software", QJsonObject{{"project_version", "test-version"}, {"revision", "test-revision"}}},
        {"requested_steps", "1"}, {"dt_seconds", .001}, {"rate", QJsonObject{{"numerator", "1000"}, {"denominator", "1"}}},
        {"probes", QJsonArray{QJsonObject{{"name", "scope/v"}, {"element", "source"}, {"port", 0}, {"unit", "V"}}, QJsonObject{{"name", "scope/i"}, {"element", "source"}, {"port", 1}, {"unit", "A"}}}}, {"seeds", QJsonArray{}}, {"initial_conditions", QJsonArray{}}, {"element_versions", QJsonArray{}}};
    auto source = std::make_shared<observation::ObservationSource>(42);
    source->setProvenance(provenance); source->channel("scope/v"); source->channel("scope/i");
    require(source->append({0, 1, .001, 2}), "Cannot append details fixture");
    require(source->append({1, 1, .001, 3}), "Cannot append current fixture");
    require(source->finish(), "Cannot finish details fixture");
    ui::ScopeViewWindow live("scope"); live.attachSource(source, {{"v", 0}, {"i", 1}}); live.refreshSource();
    require(live.recording().series().value("live:v").displayName() == "v (V)", "Live legend dropped voltage units");
    require(live.recording().series().value("live:i").displayName() == "i (A)", "Live legend dropped current units");
    require(live.plotView()->yTitle().contains("mixed units"), "Mixed-unit axis is misleading");
    require(live.findChild<QLabel*>("viewer.status")->text().contains("Run #42 — completed"), "Run identity/state missing");
    ui::PhasePortraitViewWindow phase("v", "i"); phase.attachSource(source, {{"x", 0}, {"y", 1}}); phase.refreshSource();
    require(phase.findChild<viewers::LinePlotView*>()->xTitle() == "v (V)"
        && phase.findChild<viewers::LinePlotView*>()->yTitle() == "i (A)", "Phase axes lost units");
    auto* button = live.findChild<QPushButton*>("scope.runDetails");
    require(button, "Missing Run details action"); button->click();
    auto* dialog = live.findChild<QDialog*>("scope.runDetailsDialog");
    auto* summary = dialog ? dialog->findChild<QPlainTextEdit*>("scope.detailsSummary") : nullptr;
    require(summary && summary->isReadOnly() && summary->toPlainText().contains("saved-model")
        && summary->toPlainText().contains("test-revision"), "Live provenance summary missing");
    require(dialog->findChild<QPlainTextEdit*>("scope.detailsEffectiveModel")->toPlainText() == model,
        "Details did not display the frozen effective model");
    QBuffer saved; saved.open(QIODevice::ReadWrite); QString error;
    require(live.writeRunResults(saved, &error), "Cannot save details fixture"); saved.seek(0);
    ui::ScopeViewWindow imported("import");
    require(imported.openRunResults(saved, "saved comparison", &error), "Cannot import details fixture");
    bool importedUnits = false;
    for (const auto& series : imported.recording().series())
        importedUnits |= series.channelName == "v" && series.channelUnit == "V" && series.displayName().contains("(V)");
    require(importedUnits, "Saved comparison lost channel units");
    auto* analysisRun = imported.findChild<QComboBox*>("scope.analysisRun");
    auto* fullInterval = imported.findChild<QPushButton*>("scope.fullAnalysisRange");
    auto* analyze = imported.findChild<QPushButton*>("scope.analyze");
    auto* analysisStatus = imported.findChild<QLabel*>("scope.analysisStatus");
    require(analysisRun && analysisRun->count() == 1 && fullInterval && analyze && analysisStatus,
        "Imported Scope run is not available for analysis");
    fullInterval->click(); analyze->click();
    require(analysisStatus->text().contains("1 exact positions") &&
        !analysisStatus->text().contains("rate"), "Imported Scope analysis could not read its retained samples");
    imported.findChild<QPushButton*>("scope.runDetails")->click();
    auto* importedDialog = imported.findChild<QDialog*>("scope.runDetailsDialog");
    auto* choices = importedDialog->findChild<QComboBox*>("scope.detailsRuns");
    require(choices && choices->count() == 1 && choices->itemText(0).contains("saved comparison"),
        "Imported run is missing from details selector");
    require(importedDialog->findChild<QPlainTextEdit*>("scope.detailsSummary")->toPlainText().contains("saved-model"),
        "Imported provenance did not survive saving and reopening");
    auto incomplete = std::make_shared<observation::ObservationSource>(43);
    incomplete->channel("v"); incomplete->append({0, 1, .001, 1}); incomplete->addCaptureLoss(3); incomplete->finish(false, "test failure");
    live.attachSource(incomplete, {{"v", 0}}); live.refreshSource();
    const auto status = live.findChild<QLabel*>("viewer.status")->text();
    require(status.contains("INCOMPLETE") && status.contains("capture lost 3") && status.contains("test failure"), "Capture failure lacks actionable warning");
    auto stopped = std::make_shared<observation::ObservationSource>(44);
    stopped->channel("v"); stopped->append({0, 1, .001, 1}); stopped->finish(true);
    live.attachSource(stopped, {{"v", 0}}); live.refreshSource();
    require(live.findChild<QLabel*>("viewer.status")->text().contains("stopped"), "Stopped run mislabelled completed");
    require(live.plotView()->yTitle().contains("unit unspecified"), "Unknown units were invented");
    ui::ScopeViewWindow empty("empty"); empty.findChild<QPushButton*>("scope.runDetails")->click();
    require(empty.findChild<QPlainTextEdit*>("scope.detailsSummary")->toPlainText().contains("unavailable"),
        "Missing provenance was not explicit");
}

void testLinePlot()
{
    viewers::LinePlotView plot;
    plot.setXRange(5, 5);
    require(plot.xMin() < 5 && plot.xMax() > 5, "Degenerate X range not widened");
    plot.setXRange(3, 1);
    require(plot.xMin() == 1 && plot.xMax() == 3, "Inverted X range not normalized");
    plot.setYRange(std::nan(""), 1);
    require(std::isfinite(plot.yMin()) && std::isfinite(plot.yMax()), "Non-finite Y range accepted");
    auto* a = plot.addSeries("a", {QColor("red"), 1.6, false});
    auto* b = plot.addSeries("b", {QColor("blue"), 1.2, true});
    viewers::LinePlotView::setPoints(a, {{0, 0}, {1, 10}, {2, -10}});
    viewers::LinePlotView::setPoints(b, {{0.5, 3}, {1.5, 4}});
    require(!plot.isOverlay(a) && plot.isOverlay(b) && b->opacity() < 1, "Overlay styling lost");
    plot.setXRange(1.4, 2.5);
    require(plot.fitYToVisibleData() && plot.yMin() < -10 && plot.yMax() > 4 && plot.yMax() < 10,
        "Y fit ignored the visible X window");
    require(plot.fitAll() && plot.xMin() < 0 && plot.xMax() > 2 && plot.yMax() > 10, "Fit all clipped data");
    const auto legend = plot.bridge()->legend();
    require(legend.size() == 2 && legend[1].toMap().value("overlay").toBool(), "Legend entries wrong");
    plot.removeSeries(a);
    require(plot.series() == QList<QLineSeries*>{b}, "Series removal failed");

    // The QML scene loads (Qt Graphs available) and shows the series.
    plot.resize(400, 300);
    plot.show();
    QApplication::processEvents();
    auto* quick = plot.quickWidget();
    require(quick && quick->status() == QQuickWidget::Ready && quick->errors().isEmpty(), "LinePlot.qml failed to load");
    viewers::LinePlotView::setPoints(plot.addSeries("c", {QColor("green"), 1.6, false}), {{0, 1}, {1, 2}});
    QApplication::processEvents();
}

void testTrajectory()
{
    using viewers::TrajectoryPoint;
    QList<TrajectoryPoint> points;
    for (int i = 0; i < 1001; ++i) points.push_back({std::cos(i * .01), std::sin(i * .01), i * 1e-3});
    const auto bounds = viewers::TrajectoryBounds::of(points);
    require(bounds.min[2] == 0 && bounds.max[2] == 1, "Trajectory bounds wrong");
    constexpr float e = viewers::kTrajectoryHalfExtent;
    // Data X right, data Z up, data Y into the screen.
    const auto corner = viewers::toScene({bounds.max[0], bounds.max[1], bounds.max[2]}, bounds);
    require(std::abs(corner.x() - e) < 1e-3f && std::abs(corner.y() - e) < 1e-3f && std::abs(corner.z() + e) < 1e-3f,
        "Data axes mapped to the wrong scene axes");
    const auto flat = viewers::TrajectoryBounds::of({{1, 2, 3}});
    require(flat.min[0] < 1 && flat.max[0] > 1, "Degenerate trajectory axis not widened");

    viewers::TrajectoryGeometry geometry;
    geometry.setTrajectory(points, bounds, 0);
    require(geometry.vertexCount() == 1001 && geometry.primitiveType() == QQuick3DGeometry::PrimitiveType::LineStrip,
        "Trajectory is not one line strip through every point");
    geometry.setTrajectory(points, bounds, 100);
    require(geometry.vertexCount() <= 102 && geometry.vertexAt(geometry.vertexCount() - 1) == viewers::toScene(points.back(), bounds)
        && geometry.vertexAt(0) == viewers::toScene(points.front(), bounds), "Thinning lost the trajectory ends");

    // The 3D scene loads; the window draws in its own instance.
    ui::PhaseTrajectory3DViewWindow first("a/x", "a/y", "a/z");
    ui::PhaseTrajectory3DViewWindow second("b/x", "b/y", "b/z");
    first.show();
    second.show();
    QApplication::processEvents();
    auto* quick = first.findChild<QQuickWidget*>();
    require(quick && quick->status() == QQuickWidget::Ready && quick->errors().isEmpty(), "Trajectory3D.qml failed to load");
    for (long step = 0; step < 50; ++step) {
        first.appendXSample(step, step);
        first.appendYSample(step, 2.0 * step);
        first.appendZSample(step, 3.0 * step);
    }
    QMetaObject::invokeMethod(&first, "refreshPlots", Qt::DirectConnection);
    QMetaObject::invokeMethod(&second, "refreshPlots", Qt::DirectConnection);
    require(first.bridge()->trajectoryGeometry().vertexCount() == 50 && second.bridge()->trajectoryGeometry().vertexCount() == 0,
        "3D viewer instances share data");
    require(first.bridge()->hasHead() && !second.bridge()->hasHead(), "Newest-point marker wrong");
    first.setViewPreset(ui::PhaseTrajectory3DViewWindow::ViewPreset::XY);
    require(first.bridge()->viewRotation() == QVector3D(-90, 0, 0), "XY preset does not look down the Z axis");
    QApplication::processEvents();
}

} // namespace

int main(int argc, char** argv) try
{
    QApplication app(argc, argv);
    testRegistry();
    testManagerRoutingAndAlignment();
    testLifecycle();
    testRetainedComparisonWithoutWindows();
    testWindowIndependentResults();
    testRunDetails();
    testLinePlot();
    testTrajectory();
    std::cout << "Viewer registry, routing, alignment, lifecycle, plot and 3D trajectory checks passed\n";
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}

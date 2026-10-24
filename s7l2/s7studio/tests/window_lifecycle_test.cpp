// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "lifecycle_test_support.hpp"
#include "main_window.hpp"
#include "atomic_file.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "scene_serializer.hpp"
#include "scope_view_window.hpp"
#include "phase_portrait_view_window.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>

class Window : public s7studio::MainWindow {
public:
    Window() : MainWindow(nullptr, false) {}
    using MainWindow::currentDocument;
    using MainWindow::documentModified;
};
class WindowLifecycleTest : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    std::shared_ptr<Gate> gate_;
    std::unique_ptr<Window> window_;
    QString path_;
    auto editor() { return window_->findChild<s7studio::diagram::DiagramEditor*>(); }
    auto scene() { return editor()->scene(); }
    auto runner() { return window_->findChild<s7studio::LocalSchemaRunner*>(); }
    auto manager() { return window_->findChild<s7studio::viewers::ViewerManager*>(); }
    auto action(const char* name) { return window_->findChild<QAction*>(name); }
    auto scope() { return dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager()->openObserver("Scope", "scope")); }
    bool start(int seconds = -1, bool preserve = false) {
        bool accepted = false;
        QTimer::singleShot(0, window_.get(), [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* clear = dialog->findChild<QCheckBox*>("run.keepScopeTraces");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !clear || !buttons) { dialog->reject(); return; }
            duration->setValue(seconds); clear->setChecked(preserve);
            accepted = true;
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        auto* run = action("action.run");
        if (!run || !run->isEnabled()) return false;
        run->trigger();
        return accepted;
    }
    bool openObserver(const QString& type = "Scope", const QString& id = "scope") {
        return QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, id), Q_ARG(QString, type), Q_ARG(int, 1));
    }
private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName("s7-test"); QCoreApplication::setApplicationName("window-lifecycle");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        registerLifecycleElements();
    }
    void init() {
        gate_ = currentGate = std::make_shared<Gate>();
        QSettings{}.clear();
        s7studio::diagram::DiagramScene fixture;
        s7studio::diagram::ElementNode::Element sourceElement;
        sourceElement.id = "source"; sourceElement.type = "LifecycleGate"; sourceElement.params = {{"value", 1.0}};
        auto source = fixture.addElement(sourceElement, {40, 40});
        s7studio::diagram::ElementNode::Element scopeElement;
        scopeElement.id = "scope"; scopeElement.type = "Scope";
        scopeElement.params = {{"channel_count", 1}, {"channel_names", {"value"}}};
        auto observer = fixture.addElement(scopeElement, {400, 40});
        using namespace s7studio::diagram;
        QVERIFY(fixture.connectEnds(WireEnd::onPort(source, PortDirection::Output, 0),
                                    WireEnd::onPort(observer, PortDirection::Input, 0)).isEmpty());
        SerializationOptions options; options.schemaName = "lifecycle"; options.dt = .001;
        auto document = SceneSerializer{}.serialize(fixture, options);
        document.runConfiguration = {{"runtime_seconds", -1}, {"clear_scope_traces", true}};
        path_ = temporary_.filePath("lifecycle.s7diagram"); QString error;
        QVERIFY(s7studio::writeFileAtomically(path_, QByteArray::fromStdString(boost::json::serialize(document.toJson())), error));
        window_ = std::make_unique<Window>();
        window_->openPath(path_);
        QVERIFY(openObserver());
        QVERIFY(scope());
    }
    void cleanup() {
        gate_->releaseAll();
        window_.reset();
        QVERIFY(!gate_->destroyedInStep.load());
        QCoreApplication::sendPostedEvents();
    }
    void zeroDurationDoesNotStep() {
        QSignalSpy completed(runner(), &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(start(0));
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(runner()->state(), s7studio::RunState::Completed);
        QCOMPARE(gate_->entered.load(), 0);
        QCOMPARE(runner()->recordedSource()->status().samples, std::uint64_t(0));
    }
    void editsObserversAndRerunUseFrozenConfiguration() {
        QVERIFY(start());
        const auto snapshot = runner()->activeConfiguration();
        QTRY_COMPARE(gate_->entered.load(), 1);
        runner()->pause(); gate_->allow();
        QTRY_VERIFY(runner()->isPaused());
        QTRY_VERIFY(!scope()->recording().series().empty());
        auto source = scene()->elementNode("source");
        auto edited = source->element(); edited.params["value"] = 9.0; scene()->changeElement(source, edited);
        auto observer = scene()->elementNode("scope");
        const auto originalObserver = observer->element();
        auto replacement = originalObserver; replacement.type = "PhasePortrait"; scene()->changeElement(observer, replacement);
        QVERIFY(openObserver("PhasePortrait"));
        QVERIFY(!manager()->find("PhasePortrait", "scope"));
        bool configured = false;
        QTimer::singleShot(0, window_.get(), [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            if (!frequency || !duration) { dialog->reject(); return; }
            frequency->setValue(2000); duration->setValue(1);
            auto* notice = dialog->findChild<QLabel*>("run.editNotice");
            configured = notice && notice->text().contains("next run"); dialog->accept();
        });
        action("action.runConfiguration")->trigger();
        QVERIFY(configured);
        QCOMPARE(snapshot->dt, .001);
        QCOMPARE(snapshot->schemaJson.as_object().at("elements").as_array()[0].as_object().at("params").as_object().at("value").to_number<double>(), 1.0);
        auto* label = window_->findChild<QLabel*>("run.activeConfiguration");
        QVERIFY(label && label->text().contains(QString::number(snapshot->id)) && label->text().contains("next run"));
        runner()->resume();
        QTRY_COMPARE(gate_->entered.load(), 2);
        runner()->pause(); gate_->allow();
        QTRY_VERIFY(runner()->isPaused());
        QTRY_COMPARE(scope()->recording().series().first().receivedSamples, std::uint64_t(2));
        QCOMPARE(scope()->recording().series().first().lastValue, 1.0);
        // A new observer is editable but does not subscribe retroactively.
        auto added = originalObserver; added.id = "new_scope";
        scene()->addElement(added, {400, 200});
        QVERIFY(openObserver("Scope", "new_scope"));
        QVERIFY(!manager()->find("Scope", "new_scope"));
        // Restore the next-run observer and remove the unwired new observer.
        auto fresh = scene()->elementNode("new_scope");
        scene()->clearSelection(); fresh->setSelected(true); scene()->deleteSelection();
        scene()->changeElement(observer, originalObserver);
        runner()->stop();
        QTRY_VERIFY(action("action.run")->isEnabled());
        gate_->releaseAll();
        QVERIFY(start(1, true));
        const auto next = runner()->activeConfiguration();
        QVERIFY(next->id > snapshot->id);
        QCOMPARE(next->dt, .0005);
        QCOMPARE(next->ticks, 2000L);
        QTRY_COMPARE(runner()->state(), s7studio::RunState::Completed);
        QTRY_VERIFY(scope()->recording().series().contains("live:value"));
        QTRY_COMPARE(scope()->recording().series().value("live:value").receivedSamples, std::uint64_t(2000));
        QCOMPARE(scope()->recording().series().value("live:value").lastValue, 9.0);
        int comparisons = 0;
        for (const auto& series : scope()->recording().series()) if (series.isComparison()) {
            ++comparisons; QCOMPARE(series.receivedSamples, std::uint64_t(2)); QCOMPARE(series.lastValue, 1.0);
            QVERIFY(series.comparisonLabel.contains(QString::number(snapshot->id)));
        }
        QCOMPARE(comparisons, 1);
    }
    void closeAndDestruct_data() {
        QTest::addColumn<bool>("paused"); QTest::addColumn<bool>("closeEvent");
        QTest::newRow("close-running") << false << true;
        QTest::newRow("close-paused") << true << true;
        QTest::newRow("destruct-running") << false << false;
        QTest::newRow("destruct-paused") << true << false;
    }
    void closeAndDestruct() {
        QFETCH(bool, paused); QFETCH(bool, closeEvent);
        QVERIFY(start());
        auto* active = runner();
        QTRY_COMPARE(gate_->entered.load(), 1);
        if (paused) { active->pause(); gate_->allow(); QTRY_VERIFY(active->isPaused()); }
        bool windowDestroyed = false;
        bool stoppedBeforeDestruction = false;
        QObject lifetime;
        QObject::connect(window_.get(), &QObject::destroyed, &lifetime, [&] { windowDestroyed = true; });
        QObject::connect(active, &s7studio::LocalSchemaRunner::stateChanged, &lifetime, [&](s7studio::RunState state) {
            if (state == s7studio::RunState::Stopping) { stoppedBeforeDestruction = !windowDestroyed; gate_->releaseAll(); }
        }, Qt::DirectConnection);
        QPointer<QWidget> viewer(scope());
        std::thread retired([active] {
            emit active->logMessage("SHUTDOWN_SENTINEL");
            emit active->runCompleted(false, "SHUTDOWN_SENTINEL");
            emit active->scopeSamplesAvailable();
        }); retired.join();
        if (closeEvent) {
            QVERIFY(!window_->documentModified());
            QVERIFY(window_->close());
            QVERIFY(!active->isRunning());
            QVERIFY(manager()->viewers().empty());
            QCoreApplication::sendPostedEvents();
            QVERIFY(manager()->viewers().empty());
            for (auto* text : window_->findChildren<QPlainTextEdit*>()) QVERIFY(!text->toPlainText().contains("SHUTDOWN_SENTINEL"));
        } else window_.reset();
        QVERIFY(stoppedBeforeDestruction);
        QVERIFY(viewer.isNull());
    }
    void replaceWhileActive_data() {
        QTest::addColumn<bool>("paused");
        QTest::newRow("running") << false;
        QTest::newRow("paused") << true;
    }
    void replaceWhileActive() {
        QFETCH(bool, paused);
        QVERIFY(start());
        const auto id = runner()->activeConfiguration()->id;
        QTRY_COMPARE(gate_->entered.load(), 1);
        if (paused) { runner()->pause(); gate_->allow(); QTRY_VERIFY(runner()->isPaused()); }
        QObject lifetime;
        QObject::connect(runner(), &s7studio::LocalSchemaRunner::stateChanged, &lifetime,
            [gate = gate_](s7studio::RunState state) { if (state == s7studio::RunState::Stopping) gate->releaseAll(); }, Qt::DirectConnection);
        QPointer<QWidget> retired(scope());
        window_->openPath(path_);
        QCOMPARE(runner()->state(), s7studio::RunState::Idle);
        QVERIFY(!runner()->activeConfiguration());
        QVERIFY(retired.isNull() && manager()->viewers().empty());
        QCoreApplication::sendPostedEvents();
        QVERIFY(manager()->viewers().empty());
        // A paused worker can reach Stopped before the transient Stopping
        // notification is published. The replacement run must not rely on
        // that notification to open this reused test gate.
        gate_->releaseAll();
        QVERIFY(start(1));
        QVERIFY(runner()->activeConfiguration()->id > id);
        QTRY_COMPARE(runner()->state(), s7studio::RunState::Completed);
        QTRY_VERIFY(scope());
        QTRY_COMPARE(scope()->recording().series().value("live:value").receivedSamples, std::uint64_t(1000));
    }
    void workerFailureAndUiRecovery() {
        gate_->fail.store(true);
        QVERIFY(start());
        QTRY_COMPARE(gate_->entered.load(), 1);
        gate_->allow();
        QTRY_COMPARE(runner()->state(), s7studio::RunState::Error);
        QTRY_VERIFY(action("action.run")->isEnabled());
        QTRY_VERIFY(([&] {
            for (auto* text : window_->findChildren<QPlainTextEdit*>())
                if (text->toPlainText().contains("injected worker step failure")) return true;
            return false;
        })());
        gate_->fail.store(false); gate_->releaseAll();
        QVERIFY(start(1));
        QTRY_COMPARE(runner()->state(), s7studio::RunState::Completed);
        QTRY_VERIFY(action("action.run")->isEnabled());
        QTRY_COMPARE(scope()->recording().series().value("live:value").receivedSamples, std::uint64_t(1000));
    }
    void runBoundariesArchiveScopesAndClearPhaseAlignment() {
        s7studio::ui::ScopeViewWindow recording("history");
        for (int i = 0; i < 25000; ++i) recording.appendSample("value", i * .001, double(i));
        recording.setDroppedSampleCount(7);
        const auto spill = recording.recording().series().first().spillFile;
        QVERIFY(spill);
        recording.beginRun("Run 10", true);
        QCOMPARE(recording.recording().series().size(), 1);
        const auto& old = recording.recording().series().first();
        QVERIFY(old.isComparison() && old.comparisonLabel.contains("Run 10"));
        QVERIFY(old.recordingWarning.contains("7"));
        QCOMPARE(old.spillFile, spill); // move the disk history, never rerecord or copy all samples
        recording.appendSample("value", .001, 99);
        QCOMPARE(recording.recording().series().value("live:value").receivedSamples, std::uint64_t(1));
        QBuffer csv; QVERIFY(csv.open(QIODevice::ReadWrite)); QString error;
        QVERIFY(recording.writeRecordedCsv(csv, &error));
        QCOMPARE(csv.data().count('\n'), 25002);
        s7studio::viewers::ViewerManager views(s7studio::viewers::ViewerRegistry::instance());
        auto* phase = dynamic_cast<s7studio::ui::PhasePortraitViewWindow*>(views.ensureObserver("PhasePortrait", "p"));
        QVERIFY(phase);
        auto type = [](const QString&) { return QStringLiteral("PhasePortrait"); };
        s7studio::ScopeSampleBatch batch;
        batch.samples = {{"p/x", 1, .001, 11}}; views.deliver(batch, type);
        views.beginRun("Run 10", true);
        batch.samples = {{"p/y", 1, .001, 22}}; views.deliver(batch, type);
        QVERIFY(phase->points().isEmpty());
        batch.samples = {{"p/x", 1, .001, 33}}; views.deliver(batch, type);
        QCOMPARE(phase->points(), QList<QPointF>{QPointF(33,22)});
    }
    void observerDeletionAndRecreationDuringRun() {
        QVERIFY(start());
        QTRY_COMPARE(gate_->entered.load(), 1);
        runner()->pause(); gate_->allow();
        QTRY_VERIFY(runner()->isPaused());
        QTRY_VERIFY(!scope()->recording().series().empty());
        auto* old = scope(); delete old;
        QVERIFY(!manager()->find("Scope", "scope"));
        QVERIFY(openObserver()); QVERIFY(scope());
        runner()->resume();
        QTRY_COMPARE(gate_->entered.load(), 2);
        runner()->pause(); gate_->allow();
        QTRY_VERIFY(runner()->isPaused());
        QTRY_COMPARE(scope()->recording().series().value("live:value").receivedSamples, std::uint64_t(2));
        runner()->stop();
        QTRY_COMPARE(runner()->state(), s7studio::RunState::Stopped);
        action("action.reset")->trigger();
        QVERIFY(scope()->recording().series().empty());
    }
};
QTEST_MAIN(WindowLifecycleTest)
#include "window_lifecycle_test.moc"

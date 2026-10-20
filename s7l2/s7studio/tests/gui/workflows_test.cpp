#include "main_window.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "element_picker_panel.hpp"
#include "scope_view_window.hpp"
#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "viewers/viewer_manager.hpp"
#include <s7/builtin_element_registry.hpp>
#include <QtTest>
#include <QAction>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickWidget>
#include <QSettings>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTextStream>
#include <QUndoStack>
#include <functional>
#include <memory>

class TestWindow : public s7studio::MainWindow {
public:
    TestWindow() : MainWindow(nullptr, false) {}
    using MainWindow::currentDocument;
    using MainWindow::documentModified;
};

class Workflows : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    std::unique_ptr<TestWindow> window_;
    QString artifacts_;
    bool graphicsAvailable_ = false;

    QAction* action(const char* name) { return window_->findChild<QAction*>(QString::fromLatin1(name)); }
    s7studio::diagram::DiagramEditor* editor() { return window_->findChild<s7studio::diagram::DiagramEditor*>(); }
    s7studio::diagram::DiagramScene* scene() { return editor()->scene(); }

    bool modalWorkflow(const std::function<void()>& trigger, const std::function<bool(QDialog*)>& handle) {
        bool handled = false;
        QTimer driver;
        driver.setInterval(10);
        connect(&driver, &QTimer::timeout, this, [&] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                driver.stop();
                handled = handle(dialog);
                if (!handled) dialog->reject();
            }
        });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, this, [&] {
            qWarning("Modal workflow watchdog expired");
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        driver.start(); watchdog.start(10000);
        trigger();
        return handled;
    }
    bool dialogAction(const char* name, const std::function<bool(QDialog*)>& handle) {
        auto* command = action(name);
        return command && command->isEnabled() && modalWorkflow([command] { command->trigger(); }, handle);
    }
    static bool selectFile(QDialog* dialog, const QString& path) {
        auto* file = qobject_cast<QFileDialog*>(dialog);
        if (!file) return false;
        file->setDirectory(QFileInfo(path).absolutePath());
        file->selectFile(QFileInfo(path).fileName());
        auto* filename = file->findChild<QLineEdit*>("fileNameEdit");
        if (!filename) return false;
        filename->setText(path);
        QTimer::singleShot(0, file, [file] { QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection); });
        return true;
    }
    bool fileAction(const char* name, const QString& path) {
        return dialogAction(name, [path](QDialog* dialog) { return selectFile(dialog, path); });
    }
    bool configure(const char* name, int seconds) {
        return dialogAction(name, [seconds](QDialog* dialog) {
            auto* duration = dialog->findChild<QSpinBox*>("run.duration");
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !frequency || !buttons) return false;
            duration->setValue(seconds);
            frequency->setValue(1000);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        });
    }
private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        artifacts_ = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR", temporary_.path());
        QDir{}.mkpath(artifacts_);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        s7::register_builtin_elements(s7::ElementRegistry::instance());
        QOpenGLContext context;
        if (context.create()) {
            QOffscreenSurface surface;
            surface.setFormat(context.format()); surface.create();
            graphicsAvailable_ = surface.isValid() && context.makeCurrent(&surface);
            if (graphicsAvailable_) context.doneCurrent();
        }
        qInfo() << "Qt" << qVersion() << "platform" << QGuiApplication::platformName()
                << "display" << qEnvironmentVariable("DISPLAY");
    }
    void init() {
        QSettings{}.clear();
        window_ = std::make_unique<TestWindow>();
        window_->resize(1100, 750);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
    }
    void cleanup() {
        if (QTest::currentTestFailed() && window_) {
            const auto base = artifacts_ + '/' + QString::fromLatin1(QTest::currentTestFunction())
                + '-' + QString::fromLatin1(QTest::currentDataTag() ? QTest::currentDataTag() : "default");
            window_->grab().save(base + "-studio.png");
            QFile objects(base + "-objects.txt");
            if (objects.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream text(&objects);
                std::function<void(QObject*, int)> dump = [&](QObject* object, int depth) {
                    text << QString(depth * 2, ' ') << object->metaObject()->className()
                         << " name=" << object->objectName();
                    if (auto* widget = qobject_cast<QWidget*>(object))
                        text << " visible=" << widget->isVisible() << " enabled=" << widget->isEnabled();
                    text << '\n';
                    for (auto* child : object->children()) dump(child, depth + 1);
                };
                dump(window_.get(), 0);
            }
            int n = 0;
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (widget->isVisible()) widget->grab().save(base + QString("-window-%1.png").arg(n++));
                for (auto* quick : widget->findChildren<QQuickWidget*>())
                    quick->grabFramebuffer().save(base + QString("-quick-%1.png").arg(n++));
            }
            QFile document(base + "-document.json");
            if (document.open(QIODevice::WriteOnly)) document.write(QByteArray::fromStdString(boost::json::serialize(window_->currentDocument())));
        }
        window_.reset();
    }
    void editParametersSaveOpen() {
        auto* panel = window_->findChild<s7studio::ui::ElementPickerPanel*>();
        QVERIFY(panel);
        for (QWidget* parent = panel; parent && parent != window_.get(); parent = parent->parentWidget()) parent->show();
        auto* tree = panel->findChild<QTreeWidget*>("elements.tree");
        QVERIFY(tree);
        QTreeWidgetItem* constant = nullptr;
        QTreeWidgetItemIterator it(tree);
        while (*it) { if ((*it)->data(0, Qt::UserRole).toString() == "Constant") { constant = *it; break; } ++it; }
        QVERIFY(constant);
        tree->expandItem(constant->parent());
        tree->scrollToItem(constant);
        QTRY_VERIFY(!tree->visualItemRect(constant).isEmpty());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, tree->visualItemRect(constant).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, tree->visualItemRect(constant).center());
        QTRY_VERIFY(scene()->elementNode("constant_1") != nullptr);
        QVERIFY(window_->documentModified());
        // Undo/redo are the same actions as the Edit menu and toolbar.
        auto* undo = action("action.undo"); auto* redo = action("action.redo");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->trigger(); QVERIFY(!scene()->elementNode("constant_1"));
        redo->trigger(); QVERIFY(scene()->elementNode("constant_1"));
        auto node = scene()->elementNode("constant_1");
        auto* view = editor()->findChild<QGraphicsView*>();
        QVERIFY(view);
        view->ensureVisible(node.get());
        bool parametersEdited = false;
        QTimer editDriver;
        connect(&editDriver, &QTimer::timeout, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* text = dialog->findChild<QPlainTextEdit*>("element.parameters");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!text || !buttons) { dialog->reject(); editDriver.stop(); return; }
            text->setFocus(); QTest::keyClick(text, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(text, "{\"value\":2.5}");
            parametersEdited = true; editDriver.stop();
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        });
        editDriver.start(10);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, view->mapFromScene(node->sceneBoundingRect().center()));
        QTRY_VERIFY(parametersEdited);
        QCOMPARE(node->element().params.at("value").to_number<double>(), 2.5);
        QVERIFY(configure("action.runConfiguration", 7));
        const auto path = temporary_.filePath("edited.s7diagram");
        QVERIFY(fileAction("action.saveAs", path));
        QVERIFY(QFile::exists(path)); QVERIFY(!window_->documentModified());
        const auto saved = window_->currentDocument();
        QVERIFY(action("action.closeDiagram")); action("action.closeDiagram")->trigger();
        QVERIFY(!scene()->elementNode("constant_1"));
        QVERIFY(fileAction("action.open", path));
        QCOMPARE(window_->currentDocument(), saved);
        QCOMPARE(scene()->elementNode("constant_1")->element().params.at("value").to_number<double>(), 2.5);
    }
    void simulationAndViewers_data() {
        QTest::addColumn<QString>("diagram");
        QTest::addColumn<QString>("observerType");
        QTest::newRow("scope") << "2int_osc.s7diagram" << "Scope";
        QTest::newRow("phase") << "2int_osc_phase_portrait.s7diagram" << "PhasePortrait";
        QTest::newRow("trajectory3d") << "3int_helix_phase_trajectory.s7diagram" << "PhaseTrajectory3D";
    }
    void simulationAndViewers() {
        QFETCH(QString, diagram); QFETCH(QString, observerType);
        if (observerType == "PhaseTrajectory3D" && !graphicsAvailable_)
            QSKIP("OpenGL unavailable: actual Qt Quick 3D viewer workflow was not tested");
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + '/' + diagram));
        QString observerId;
        for (const auto& node : scene()->elementNodes())
            if (QString::fromStdString(node->element().type) == observerType) observerId = QString::fromStdString(node->element().id);
        QVERIFY(!observerId.isEmpty());
        // Exercise the editor's object-routed double-click command, including
        // viewer selection/creation; no screen-coordinate lookup is involved.
        QVERIFY(QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, observerId), Q_ARG(QString, observerType), Q_ARG(int, 1)));
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>();
        QVERIFY(manager);
        auto* viewer = manager->find(observerType, observerId);
        QVERIFY(viewer); QTRY_VERIFY(viewer->widget()->isVisible());
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>();
        QVERIFY(runner);
        QSignalSpy samples(runner, &s7studio::LocalSchemaRunner::scopeSamplesAvailable);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", -1));
        QTRY_VERIFY(runner->isRunning());
        QTRY_VERIFY(samples.count() > 0);
        QVERIFY(action("action.pause")->isEnabled()); action("action.pause")->trigger();
        QTRY_VERIFY(runner->isPaused());
        QTRY_VERIFY(action("action.resume")->isEnabled());
        viewer->widget()->close(); QVERIFY(!viewer->widget()->isVisible());
        QVERIFY(QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, observerId), Q_ARG(QString, observerType), Q_ARG(int, 1)));
        QTRY_VERIFY(viewer->widget()->isVisible());
        action("action.resume")->trigger(); QTRY_VERIFY(!runner->isPaused());
        action("action.stop")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), 10000);
        QTRY_VERIFY(completed.count() > 0); QVERIFY(completed.last().at(0).toBool());
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) QVERIFY(!scope->recording().series().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhasePortraitViewWindow*>(viewer)) QVERIFY(!phase->points().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer)) QVERIFY(!phase->points().isEmpty());
        for (auto* quick : viewer->widget()->findChildren<QQuickWidget*>()) QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        action("action.reset")->trigger(); QVERIFY(!runner->isRunning());
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) QVERIFY(scope->recording().series().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhasePortraitViewWindow*>(viewer)) QVERIFY(phase->points().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer)) QVERIFY(phase->points().isEmpty());
        completed.clear();
        QVERIFY(configure("action.run", 1));
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() > 0, 10000);
        QVERIFY(completed.last().at(0).toBool());
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) {
            const auto path = temporary_.filePath("recording.s7scope");
            auto* save = scope->findChild<QPushButton*>("scope.saveRun");
            auto* open = scope->findChild<QPushButton*>("scope.openRun");
            auto* remove = scope->findChild<QPushButton*>("scope.removeComparisons");
            QVERIFY(save && open && remove);
            QVERIFY(modalWorkflow([save] { QTest::mouseClick(save, Qt::LeftButton); },
                [path](QDialog* dialog) { return selectFile(dialog, path); }));
            QVERIFY(QFileInfo(path).size() > 0);
            const auto liveCount = scope->recording().series().size();
            QVERIFY(modalWorkflow([open] { QTest::mouseClick(open, Qt::LeftButton); },
                [path](QDialog* dialog) { return selectFile(dialog, path); }));
            QTRY_COMPARE(scope->recording().series().size(), liveCount * 2);
            QTest::mouseClick(remove, Qt::LeftButton);
            QTRY_COMPARE(scope->recording().series().size(), liveCount);
        }
    }
    void unsavedCloseChoices_data() {
        QTest::addColumn<int>("choice");
        QTest::newRow("save") << int(QMessageBox::Save);
        QTest::newRow("discard") << int(QMessageBox::Discard);
        QTest::newRow("cancel") << int(QMessageBox::Cancel);
    }
    void unsavedCloseChoices() {
        QFETCH(int, choice);
        const auto path = temporary_.filePath(QString("unsaved-%1.s7diagram").arg(choice));
        QVERIFY(QFile::copy(QString(S7_GUI_EXAMPLES_DIR) + "/2int_osc.s7diagram", path));
        QVERIFY(fileAction("action.open", path));
        const auto original = window_->currentDocument();
        auto nodes = scene()->elementNodes();
        QVERIFY(!nodes.empty());
        scene()->clearSelection();
        nodes.front()->setSelected(true);
        auto* view = editor()->findChild<QGraphicsView*>();
        QVERIFY(view);
        view->setFocus();
        QTest::keyClick(view->viewport(), Qt::Key_Delete);
        QTRY_VERIFY(window_->documentModified());
        const auto edited = window_->currentDocument();
        QVERIFY(dialogAction("action.closeDiagram", [choice](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog);
            if (!box || !box->button(QMessageBox::StandardButton(choice))) return false;
            QTest::mouseClick(box->button(QMessageBox::StandardButton(choice)), Qt::LeftButton);
            return true;
        }));
        if (choice == QMessageBox::Cancel) {
            QCOMPARE(window_->currentDocument(), edited);
            QVERIFY(window_->documentModified());
        } else {
            QVERIFY(scene()->elementNodes().empty());
            QVERIFY(fileAction("action.open", path));
            QCOMPARE(window_->currentDocument(), choice == QMessageBox::Save ? edited : original);
        }
    }
    void failureArtifactProbe() {
        QVERIFY2(!qEnvironmentVariableIsSet("S7_GUI_INJECT_FAILURE"), "Requested failure to verify GUI artifacts");
    }
};

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName("s7-test"); app.setApplicationName("studio-gui-workflows");
    Workflows tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "workflows_test.moc"

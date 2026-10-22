// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

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
#include <QMenuBar>
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
#include <QTabWidget>
#include <QLabel>
#include <QComboBox>
#include "parameter_editor.hpp"
#include "viewers/line_plot_view.hpp"
#include "tool_panels.hpp"
#include <QTextStream>
#include <QTextBrowser>
#include <QListWidget>
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

    static void capture(QWidget* widget, const QString& name) {
        const auto directory = qEnvironmentVariable("S7_USABILITY_CAPTURE_DIR");
        if (directory.isEmpty()) return;
        QDir{}.mkpath(directory);
        QTest::qWait(150);
        QVERIFY(widget->grab().save(directory + '/' + name + ".png"));
    }
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
    bool configure(const char* name, double seconds) {
        return dialogAction(name, [seconds](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !frequency || !buttons) return false;
            duration->setValue(seconds);
            frequency->setValue(1000);
            capture(dialog, "run-configuration");
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
    void helpAndAbout() {
        const auto before = window_->currentDocument();
        QVERIFY(action("action.help"));
        QVERIFY(action("action.about"));
        QCOMPARE(action("action.help")->shortcut(), QKeySequence(QKeySequence::HelpContents));
        window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* view = editor()->findChild<QGraphicsView*>();
        QVERIFY(view); view->setFocus();
        QTest::keyClick(view->viewport(), Qt::Key_F1);
        QTRY_VERIFY(window_->findChild<QDialog*>("dialog.help"));
        auto* help = window_->findChild<QDialog*>("dialog.help");
        QTRY_VERIFY(help && help->isVisible());
        auto* browser = help->findChild<QTextBrowser*>("help.browser");
        auto* search = help->findChild<QLineEdit*>("help.search");
        auto* contents = help->findChild<QListWidget*>("help.contents");
        QVERIFY(browser && search && contents);
        QVERIFY(browser->toPlainText().contains("First simulation"));
        for (int i = 0; i < contents->count(); ++i) {
            contents->setCurrentRow(i);
            QVERIFY2(browser->toPlainText().size() > 100, qPrintable(contents->item(i)->text()));
        }
        search->setText("backpressure");
        int matches = 0;
        for (int i = 0; i < contents->count(); ++i) matches += !contents->item(i)->isHidden();
        QVERIFY(matches > 0 && matches < contents->count());
        search->setText("no-help-topic-matches-this-string");
        for (int i = 0; i < contents->count(); ++i) QVERIFY(contents->item(i)->isHidden());
        search->clear();
        browser->setSource(QUrl("qrc:/s7studio/help/docs/STUDIO_USER_GUIDE.md"), QTextDocument::MarkdownResource);
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("qrc:/s7studio/help/docs/SCOPE_RESULTS.md"))));
        QVERIFY(browser->toPlainText().contains("Saving and comparing Scope results"));
        browser->backward();
        QVERIFY(browser->toPlainText().contains("First simulation"));
        capture(help, "help");
        help->close();
        action("action.help")->trigger();
        QCOMPARE(window_->findChildren<QDialog*>("dialog.help").size(), 1);
        QVERIFY(help->isVisible());
        help->close();
        action("action.about")->trigger();
        auto* about = window_->findChild<QDialog*>("dialog.about");
        QVERIFY(about && about->isVisible());
        auto* credits = about->findChild<QTextBrowser*>("about.credits");
        auto* build = about->findChild<QTextBrowser*>("about.build");
        QVERIFY(credits && build);
        QVERIFY(credits->toPlainText().contains("Oleksiy Lukin"));
        QVERIFY(credits->toPlainText().contains("teren-ukr"));
        QVERIFY(build->toPlainText().contains("S7 Studio 0.1.0"));
        for (auto* text : about->findChildren<QTextBrowser*>()) QVERIFY(text->toPlainText().size() > 100);
        capture(about, "about");
        about->close();
        QCOMPARE(boost::json::serialize(window_->currentDocument()), boost::json::serialize(before));
        QVERIFY(!window_->documentModified());
    }
    void fractionalRunDuration() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/linear/rc_charge_euler.s7diagram"));
        QVERIFY(dialogAction("action.runConfiguration", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* notice = dialog->findChild<QLabel*>("run.durationNotice");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !frequency || !notice || !buttons) return false;
            const auto ok = buttons->button(QDialogButtonBox::Ok);
            frequency->setValue(1000);
            duration->setValue(.099);
            if (!notice->isVisible() || !notice->text().contains("99 steps") || !ok->isEnabled()) return false;
            duration->setValue(.1);
            if (notice->isVisible()) return false;
            frequency->setValue(500);
            if (!notice->isVisible() || !notice->text().contains("50 steps")) return false;
            duration->setValue(-.5);
            if (ok->isEnabled() || !notice->text().contains("Invalid duration")) return false;
            duration->setValue(-1);
            if (notice->isVisible() || !ok->isEnabled()) return false;
            duration->setValue(0);
            if (!notice->isVisible() || !notice->text().contains("0 steps")) return false;
            frequency->setValue(1000);
            duration->setValue(.125);
            capture(dialog, "fractional-run-configuration");
            ok->click();
            return true;
        }));
        QCOMPARE(window_->currentDocument().as_object().at("studio_run").as_object()
            .at("runtime_seconds").to_number<double>(), .125);
        const auto saved = temporary_.filePath("fractional-duration.s7diagram");
        QVERIFY(fileAction("action.saveAs", saved));
        QVERIFY(fileAction("action.open", saved));
        QVERIFY(configure("action.run", .125));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>();
        QVERIFY(runner);
        QTRY_COMPARE(runner->state(), s7studio::RunState::Completed);
        QCOMPARE(runner->completedTicks(), 125L);
        QTRY_VERIFY(action("action.run")->isEnabled());
        QVERIFY(configure("action.run", .099)); // Warning remains non-blocking.
        QTRY_COMPARE(runner->state(), s7studio::RunState::Completed);
        QCOMPARE(runner->completedTicks(), 99L);
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
            auto* tabs = dialog->findChild<QTabWidget*>("element.parameterTabs");
            if (tabs) tabs->setCurrentIndex(1);
            text->setFocus(); QTest::keyClick(text, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(text, "{\"value\":2.5}");
            parametersEdited = true; editDriver.stop();
            capture(dialog, "parameters");
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
    void typedParametersAndKeyboard() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/linear/rlc_step_symplectic.s7diagram"));
        auto node = scene()->elementNode("state"); QVERIFY(node);
        scene()->clearSelection(); node->setSelected(true);
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view); view->setFocus();
        bool checked = false;
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F4); }, [&](QDialog* dialog) {
            auto* initial = dialog->findChild<QLineEdit*>("parameter.initial_position");
            auto* tabs = dialog->findChild<QTabWidget*>("element.parameterTabs");
            auto* error = dialog->findChild<QLabel*>("element.parameterError");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!initial || !tabs || !error || !buttons) return false;
            if (initial->accessibleName() != "initial_position") return false;
            initial->setFocus(); QTest::keyClick(initial, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(initial, "invalid");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            if (!dialog->isVisible() || error->text().isEmpty()) return false;
            capture(dialog, "parameter-error");
            initial->setText("0.25");
            tabs->setCurrentIndex(1);
            auto* json = dialog->findChild<QPlainTextEdit*>("element.parameters");
            if (!json || !json->toPlainText().contains("2.5E-1")) return false;
            tabs->setCurrentIndex(0); capture(dialog, "integrator-parameters");
            QTest::keyClick(initial, Qt::Key_Tab);
            if (QApplication::focusWidget() == initial) return false;
            checked = true;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QVERIFY(checked); QCOMPARE(node->element().params.at("initial_position").to_number<double>(), .25);
        view->setFocus();
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F4); }, [](QDialog* dialog) {
            auto* initial = dialog->findChild<QLineEdit*>("parameter.initial_position");
            if (!initial) return false;
            initial->setText("0.75"); QTest::keyClick(initial, Qt::Key_Escape); return true;
        }));
        QCOMPARE(node->element().params.at("initial_position").to_number<double>(), .25);
        action("action.undo")->trigger(); QCOMPARE(node->element().params.at("initial_position").to_number<double>(), 0.);
        // Reusable editor preserves exact integer/complex values and metadata bounds.
        s7::ElementMetadata metadata; metadata.type_name = "test";
        metadata.parameters = {{"seed", "int", "Exact seed", 0, 0, {}, {}, false},
            {"mode", "enum", "Mode", "a", {}, {}, {"a", "b"}, false},
            {"enabled", "bool", "Enabled", false, {}, {}, {}, false}};
        s7studio::ui::ParameterEditor parameters(metadata, {{"seed", std::uint64_t(18446744073709551615ULL)}});
        boost::json::object values; QString error;
        QVERIFY(parameters.readParameters(values, error));
        QCOMPARE(values.at("seed").as_uint64(), std::uint64_t(18446744073709551615ULL));
        parameters.findChild<QLineEdit*>("parameter.seed")->setText("-1");
        QVERIFY(!parameters.readParameters(values, error)); QVERIFY(error.contains("at least"));
        parameters.findChild<QLineEdit*>("parameter.seed")->setText("1");
        parameters.findChild<QComboBox*>("parameter.mode")->setCurrentText("b");
        parameters.findChild<QComboBox*>("parameter.enabled")->setCurrentText("true");
        QVERIFY(parameters.readParameters(values, error));
        QCOMPARE(values.at("mode").as_string(), boost::json::string("b")); QVERIFY(values.at("enabled").as_bool());
        auto* advanced = parameters.findChild<QTabWidget*>("element.parameterTabs"); advanced->setCurrentIndex(1);
        parameters.findChild<QPlainTextEdit*>("element.parameters")->setPlainText("{");
        QVERIFY(!parameters.readParameters(values, error));
        parameters.findChild<QPlainTextEdit*>("element.parameters")->setPlainText("{\"unknown\":true}");
        QVERIFY(!parameters.readParameters(values, error)); QVERIFY(error.contains("unknown parameter"));
        parameters.findChild<QPlainTextEdit*>("element.parameters")->setPlainText("{}");
        advanced->setCurrentIndex(0);
        QCOMPARE(advanced->currentIndex(), 0);
        QVERIFY(parameters.readParameters(values, error));
        QCOMPARE(values.at("seed").to_number<int>(), 0); // defaults restored when returning to form
        s7::ElementMetadata strings; strings.type_name = "strings";
        strings.parameters = {{"label", "string", "Label", "default", {}, {}, {}, false}};
        s7studio::ui::ParameterEditor invalid(strings, {{"label", 123}});
        QCOMPARE(invalid.findChild<QTabWidget*>("element.parameterTabs")->currentIndex(), 1);
        QVERIFY(!invalid.readParameters(values, error)); // never coerced to the string "123"
    }
    void editingIntegrity() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/linear/rlc_step_symplectic.s7diagram"));
        capture(window_.get(), "diagram");
        qInfo() << "Menu palette" << window_->menuBar()->palette().color(QPalette::WindowText)
                << window_->menuBar()->palette().color(QPalette::Text)
                << window_->menuBar()->palette().color(QPalette::ButtonText)
                << window_->menuBar()->palette().color(QPalette::Window);
        const auto probes = scene()->probes();
        QVERIFY(!probes.empty());
        const auto probe = probes.front();
        auto node = scene()->elementNode(probe.point.element); QVERIFY(node);
        scene()->clearSelection(); node->setSelected(true);
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view); view->setFocus();
        QTest::keyClick(view->viewport(), Qt::Key_Delete);
        QVERIFY(!scene()->elementNode(probe.point.element));
        for (const auto& remaining : scene()->probes()) QVERIFY(remaining.point.element != probe.point.element);
        action("action.undo")->trigger();
        QCOMPARE(scene()->probes(), probes);
        QVERIFY(scene()->elementNode(probe.point.element));
        action("action.redo")->trigger();
        QVERIFY(!scene()->elementNode(probe.point.element));
        action("action.undo")->trigger();
        QVERIFY(scene()->renameProbe(probe.point, "renamed"));
        action("action.undo")->trigger(); QCOMPARE(scene()->probes(), probes);
        scene()->removeProbe(probe.point);
        action("action.undo")->trigger(); QCOMPARE(scene()->probes(), probes);
        // Real keyboard clipboard operations. Everything internal, including
        // observer links and annotation frames, must survive one undo step.
        const auto nodeCount = scene()->elementNodes().size();
        const auto wireCount = scene()->signalWires().size();
        const auto frameCount = scene()->textFrames().size();
        view->setFocus(); QTest::keyClick(view->viewport(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(view->viewport(), Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(view->viewport(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(scene()->elementNodes().size(), nodeCount * 2);
        QCOMPARE(scene()->signalWires().size(), wireCount * 2);
        QCOMPARE(scene()->textFrames().size(), frameCount * 2);
        QCOMPARE(scene()->probes().size(), probes.size() * 2);
        auto copied = scene()->elementNode("state_copy1"); QVERIFY(copied);
        QCOMPARE(copied->element().params, scene()->elementNode("state")->element().params);
        QCOMPARE(copied->element().title, scene()->elementNode("state")->element().title);
        action("action.undo")->trigger();
        QCOMPARE(scene()->elementNodes().size(), nodeCount);
        QCOMPARE(scene()->probes(), probes);
        action("action.redo")->trigger();
        QCOMPARE(scene()->elementNodes().size(), nodeCount * 2);
        const auto path = temporary_.filePath("clipboard.s7diagram");
        QVERIFY(fileAction("action.saveAs", path));
        const auto saved = window_->currentDocument();
        action("action.closeDiagram")->trigger();
        QVERIFY(fileAction("action.open", path));
        if (window_->currentDocument() != saved) {
            QFile expected(artifacts_ + "/clipboard-expected.json");
            if (expected.open(QIODevice::WriteOnly)) expected.write(QByteArray::fromStdString(boost::json::serialize(saved)));
        }
        QCOMPARE(window_->currentDocument(), saved);
        // Cutting only one selected element never duplicates outside links.
        scene()->clearSelection(); scene()->elementNode("source")->setSelected(true);
        view->setFocus(); QTest::keyClick(view->viewport(), Qt::Key_X, Qt::ControlModifier);
        QVERIFY(!scene()->elementNode("source"));
        action("action.undo")->trigger(); QVERIFY(scene()->elementNode("source"));
        QTest::keyClick(view->viewport(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(scene()->elementNodes().size(), nodeCount * 2 + 1);
        QCOMPARE(scene()->signalWires().size(), wireCount * 2);
        capture(window_.get(), "clipboard");
    }
    void clipboardNestedAndBends() {
        using namespace s7studio::diagram;
        DiagramScene graph;
        auto c = graph.addElement({"c", "Constant", {{"value", 2.5}}, {}, {}, "Input"}, {0, 0});
        auto g = graph.addElement({"g", "Gain", {{"factor", .5}}, {}, {}, "Gain"}, {500, 0});
        auto outside = graph.addElement({"outside", "Gain", {{"factor", 1.}}, {}, {}, {}}, {500, 500});
        const QVector<QPointF> bends{{240, 200}, {400, 200}};
        QVERIFY(graph.connectEnds(WireEnd::onPort(c, PortDirection::Output, 0), WireEnd::onPort(g, PortDirection::Input, 0), bends).isEmpty());
        QVERIFY(graph.connectEnds(WireEnd::onPort(c, PortDirection::Output, 0), WireEnd::onPort(outside, PortDirection::Input, 0)).isEmpty());
        const auto* descriptor = s7::ElementRegistry::instance().find("FirstOrderLag");
        QVERIFY(descriptor && descriptor->metadata().schema_definition);
        const auto definition = *descriptor->metadata().schema_definition;
        auto nested = graph.addElement({"nested", "FirstOrderLag", {}, definition, {}, "Plant"}, {1000, 0});
        auto frame = graph.addTextFrame("note", TextFrameNode::defaultParams(), {1000, 500});
        c->setSelected(true); g->setSelected(true); nested->setSelected(true); frame->setSelected(true);
        QVERIFY(graph.addProbe({"c", 0}, "voltage", "V"));
        QVERIFY(graph.copySelection()); QVERIFY(graph.pasteSelection({0, 1000}));
        auto clone = graph.elementNode("nested_copy1"); QVERIFY(clone);
        QCOMPARE(clone->element().schema, definition);
        auto gCopy = graph.elementNode("g_copy1"); QVERIFY(gCopy);
        QCOMPARE(gCopy->element().params, g->element().params);
        QCOMPARE(graph.signalWires().size(), std::size_t(3)); // two original, only one internal copied
        std::shared_ptr<SignalWire> pasted;
        for (const auto& wire : graph.signalWires()) if (wire->downstream.node.lock() == gCopy) pasted = wire;
        QVERIFY(pasted); QCOMPARE(pasted->userBends.size(), bends.size());
        QCOMPARE(pasted->userBends[1] - pasted->userBends[0], bends[1] - bends[0]);
        QCOMPARE(graph.probes().back().unit, QString("V"));
        graph.undoStack()->undo(); QVERIFY(!graph.elementNode("nested_copy1"));
        QCOMPARE(graph.signalWires().size(), std::size_t(2));
        graph.undoStack()->redo(); QVERIFY(graph.elementNode("nested_copy1"));
    }
    void simulationAndViewers_data() {
        QTest::addColumn<QString>("diagram");
        QTest::addColumn<QString>("observerType");
        QTest::newRow("scope") << "2int_osc.s7diagram" << "Scope";
        QTest::newRow("scope-linear") << "linear/rlc_step_symplectic.s7diagram" << "Scope";
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
        capture(viewer->widget(), observerType + "-stopped");
        action("action.reset")->trigger(); QVERIFY(!runner->isRunning());
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) QVERIFY(scope->recording().series().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhasePortraitViewWindow*>(viewer)) QVERIFY(phase->points().isEmpty());
        if (auto* phase = dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer)) QVERIFY(phase->points().isEmpty());
        completed.clear();
        QVERIFY(configure("action.run", diagram.startsWith("linear/") ? 10 : 1));
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() > 0, 10000);
        QVERIFY(completed.last().at(0).toBool());
        auto* viewerStatus = viewer->widget()->findChild<QLabel*>("viewer.status");
        QVERIFY(viewerStatus);
        QTRY_VERIFY(viewerStatus->text().contains("completed"));
        QTRY_VERIFY(!viewerStatus->text().contains("loading history"));
        if (auto* plot = viewer->widget()->findChild<s7studio::viewers::LinePlotView*>()) {
            const auto width = plot->xMax() - plot->xMin();
            viewer->widget()->raise(); viewer->widget()->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(viewer->widget()));
            plot->setFocus(); QTRY_VERIFY(plot->hasFocus());
            QTest::keyClick(plot, Qt::Key_Equal);
            QVERIFY(plot->xMax() - plot->xMin() < width);
            QTest::keyClick(plot, Qt::Key_Home);
        }
        capture(viewer->widget(), observerType + "-completed");
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) {
            const auto path = temporary_.filePath(QString("recording-%1.s7scope").arg(QString::fromLatin1(QTest::currentDataTag())));
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
            capture(viewer->widget(), "Scope-comparison");
            QTest::mouseClick(remove, Qt::LeftButton);
            QTRY_COMPARE(scope->recording().series().size(), liveCount);
        }
    }
    void keyboardRunShortcuts() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/linear/rlc_step_symplectic.s7diagram"));
        window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view); view->setFocus();
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F5); }, [](QDialog* dialog) {
            if (!dialog->findChild<QDoubleSpinBox*>("run.duration")) return false;
            dialog->reject(); return true;
        }));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QVERIFY(!runner->isRunning());
        QVERIFY(configure("action.run", -1)); QTRY_VERIFY(runner->isRunning());
        window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        view->setFocus(); QTest::keyClick(view->viewport(), Qt::Key_F5, Qt::ShiftModifier);
        QTRY_VERIFY(!runner->isRunning());
    }
    void engineeringErrorFeedback() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/linear/rlc_step_symplectic.s7diagram"));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        auto node = scene()->elementNode("state"); QVERIFY(node);
        auto broken = node->element(); broken.type = "UnavailableEngineeringPlugin";
        scene()->changeElement(node, broken);
        QVERIFY(configure("action.run", 1));
        QTRY_VERIFY(!runner->isRunning());
        auto* problems = window_->findChild<s7studio::ui::ProblemsPanel*>(); QVERIFY(problems);
        QTRY_VERIFY(problems->errorCount() > 0);
        bool actionable = false;
        for (const auto& problem : problems->problems()) actionable |= problem.message.contains("UnavailableEngineeringPlugin");
        QVERIFY(actionable); capture(window_.get(), "missing-element-error");
        action("action.undo")->trigger();
        QVERIFY(scene()->elementNode("state")->element().type == "SymplecticIntegrator");
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

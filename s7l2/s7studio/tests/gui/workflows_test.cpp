// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "element_picker_panel.hpp"
#include "element_documentation.hpp"
#include "library_element.hpp"
#include "instrument_controls.hpp"
#include "tool_panels.hpp"
#include "observers_panel.hpp"
#include "tool_windows.hpp"
#include "viewers/viewer_registry.hpp"
#include "scope_view_window.hpp"
#include "live_oscilloscope_window.hpp"
#include "viewers/spectrum_view.hpp"
#include "viewers/pair_analysis_view.hpp"
#include "viewers/window_inspection_panel.hpp"
#include "analysis/worker_pool.hpp"
#include <QSemaphore>
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonArray>
#include <QClipboard>
#include <QtGraphs/QLineSeries>
#include <QQuickItem>
#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "viewers/viewer_manager.hpp"
#include <s7/builtin_element_registry.hpp>
#include <QtTest>
#include <QAction>
#include <QDialogButtonBox>
#include <QDockWidget>
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
#include <QDial>
#include <QSlider>
#include <QProgressBar>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTabWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include "parameter_editor.hpp"
#include "schema_tabs.hpp"
#include "ui_icons.hpp"
#include "viewers/line_plot_view.hpp"
#include "tool_panels.hpp"
#include <QAccessible>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <QDirIterator>
#include <QScopeGuard>
#include <QTextStream>
#include <QTextBrowser>
#include <QListWidget>
#include <QScrollBar>
#include <QSplitter>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QUndoStack>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>

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

    static QQuickItem* visualItem(QQuickItem* root, const QString& name) {
        if (root->objectName() == name) return root;
        // Repeater delegates have visual parents which need not be their
        // QObject ownership parents.
        for (auto* child : root->childItems()) if (auto* found = visualItem(child, name)) return found;
        return nullptr;
    }

    static void capture(QWidget* widget, const QString& name, bool retainArtifact = false) {
        auto directory = qEnvironmentVariable("S7_USABILITY_CAPTURE_DIR");
        if (directory.isEmpty() && retainArtifact) directory = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR");
        if (directory.isEmpty()) return;
        QDir{}.mkpath(directory);
        QTest::qWait(150);
        QVERIFY(widget->grab().save(directory + '/' + name + ".png"));
    }
    QAction* action(const char* name) { return window_->findChild<QAction*>(QString::fromLatin1(name)); }
    s7studio::diagram::DiagramEditor* editor() { return window_->findChild<s7studio::diagram::DiagramEditor*>(); }
    s7studio::diagram::DiagramScene* scene() { return editor()->scene(); }
    QTreeWidgetItem* libraryItem(QTreeWidget* tree, const QString& type) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                auto* item = tree->topLevelItem(i)->child(j);
                if (item->data(0, Qt::UserRole).toString() == type) return item;
            }
        return nullptr;
    }
    // XTest events exercise Qt's native QDrag/XDND path, including its nested
    // event loop. Coordinates come from current widgets, never the desktop.
    bool nativeLibraryDrag(QTreeWidget* tree, QTreeWidgetItem* item, const QPoint destination, bool cancel = false) {
        tree->setCurrentItem(item); tree->scrollToItem(item);
        QCoreApplication::processEvents();
        const auto logicalStart = tree->viewport()->mapToGlobal(tree->visualItemRect(item).center());
        const auto logicalThreshold = logicalStart - QPoint(std::max(24, QApplication::startDragDistance()*2), 0);
        // The wrapper's single Xvfb screen starts at (0,0). XTest positions
        // are physical pixels; Qt widget/global coordinates are logical.
        const auto pixels = [&](QPoint point) { return (QPointF(point)*window_->devicePixelRatioF()).toPoint(); };
        const auto start = pixels(logicalStart), threshold = pixels(logicalThreshold), end = pixels(destination);
        QStringList commands{"mousemove", QString::number(start.x()), QString::number(start.y()), "mousedown", "1", "sleep", "0.08",
            "mousemove", QString::number(threshold.x()), QString::number(threshold.y()), "sleep", "0.12",
            "mousemove", QString::number(end.x()), QString::number(end.y()), "sleep", "0.2"};
        if (cancel) commands << "key" << "Escape";
        commands << "mouseup" << "1";
        QProcess driver;
        QSignalSpy done(&driver, &QProcess::finished);
        driver.start(QStandardPaths::findExecutable("xdotool"), commands);
        if (!driver.waitForStarted(2000)) return false;
        QElapsedTimer clock; clock.start();
        while (done.empty() && clock.elapsed() < 5000) QTest::qWait(10);
        return !done.empty() && driver.exitStatus() == QProcess::NormalExit && driver.exitCode() == 0;
    }

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
    void activateMenu(const char* menuName, QAction* command) {
        auto* menu = window_->findChild<QMenu*>(QString::fromLatin1(menuName));
        QVERIFY(menu && command);
        window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        QTest::mouseClick(window_->menuBar(), Qt::LeftButton, Qt::NoModifier,
            window_->menuBar()->actionGeometry(menu->menuAction()).center());
        QTRY_VERIFY(menu->isVisible());
        menu->setActiveAction(command);
        QTest::keyClick(menu, Qt::Key_Return);
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
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("qrc:/s7studio/help/docs/LIVE_OSCILLOSCOPE_PROTOTYPE.md"))));
        QVERIFY(browser->toPlainText().contains("Raw samples / channel"));
        QVERIFY(browser->toPlainText().contains("fm_frequency_detection_live.s7diagram"));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("STUDIO_SPECTRUM.md"))));
        QVERIFY(browser->toPlainText().contains("Windows and normalization"));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("STUDIO_PAIR_ANALYSIS.md"))));
        QVERIFY(browser->toPlainText().contains("positive lag means B occurs later than A"));
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
        auto* tabs = about->findChild<QTabWidget*>(); QVERIFY(tabs);
        int legalTabs = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            const auto title = tabs->tabText(i);
            if (title != "Licensing" && title != "Third-party notices") continue;
            auto* text = qobject_cast<QTextBrowser*>(tabs->widget(i)); QVERIFY(text);
            QCOMPARE(text->source(), QUrl(title == "Licensing" ?
                "qrc:/s7studio/legal/LICENSING.md" : "qrc:/s7studio/legal/THIRD_PARTY_NOTICES.md"));
            ++legalTabs;
        }
        QCOMPARE(legalTabs, 2);
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
    void libraryDragPlacement_data() {
        QTest::addColumn<QString>("type"); QTest::addColumn<bool>("filtered");
        QTest::addColumn<bool>("nested"); QTest::addColumn<int>("zoomSteps");
        QTest::newRow("builtin-unfiltered") << QString("Gain") << false << false << 0;
        QTest::newRow("builtin-filtered-zoom-pan") << QString("Gain") << true << false << 3;
        QTest::newRow("schema-root") << QString("PIController") << true << false << -2;
        QTest::newRow("plugin-nested") << QString("WhiteNoiseGenerator") << true << true << 2;
        QTest::newRow("schema-nested") << QString("FirstOrderLag") << false << true << -1;
        QTest::newRow("scope-root") << QString("StudioObserver:Scope") << true << false << 1;
        QTest::newRow("live-nested") << QString("StudioObserver:LiveOscilloscope") << true << true << 0;
        QTest::newRow("phase-root") << QString("StudioObserver:PhasePortrait") << true << false << -1;
        QTest::newRow("trajectory-nested") << QString("StudioObserver:PhaseTrajectory3D") << true << true << 1;
    }
    void libraryDragPlacement() {
        using namespace s7studio;
        if (QStandardPaths::findExecutable("xdotool").isEmpty()) QSKIP("xdotool is required for native X11 drag/drop checks");
        QFETCH(QString, type); QFETCH(bool, filtered); QFETCH(bool, nested); QFETCH(int, zoomSteps);
        window_->raise(); window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* target = editor();
        if (nested) {
            QVERIFY(target->addLibraryElement("PIController", QPointF(80,80)));
            auto* tabs = window_->findChild<SchemaTabs*>(); QVERIFY(tabs);
            target = tabs->open(target, "picontroller_1"); QVERIFY(target);
        }
        auto* graph = target->scene();
        if (type == "Gain") {
            graph->addElement({"gain_1", "Gain", {}, {}, {}, {}}, {40,40});
            graph->addTextFrame("gain_2", diagram::TextFrameNode::defaultParams(), {40,200});
        }
        auto* tree = window_->findChild<QTreeWidget*>("elements.tree"); QVERIFY(tree);
        auto* search = window_->findChild<QLineEdit*>("elements.search"); QVERIFY(search);
        search->setText(filtered ? type.section(':', -1) : QString());
        auto* item = libraryItem(tree, type); QVERIFY(item && !item->isHidden());
        QCoreApplication::processEvents();
        auto* view = target->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto canvasWidth = view->viewport()->width();
        tree->setCurrentItem(item); tree->scrollToItem(item);
        QCoreApplication::processEvents();
        QCOMPARE(view->viewport()->width(), canvasWidth);
        target->resetZoom();
        for (int i = 0; i < std::abs(zoomSteps); ++i) { if (zoomSteps > 0) target->zoomIn(); else target->zoomOut(); }
        view->centerOn(QPointF(460,350));
        QCoreApplication::processEvents();
        const QPoint location = view->viewport()->rect().center() + QPoint(31,-23);
        const auto desired = view->mapToScene(location);
        const auto snapped = QPointF(std::round(desired.x()/diagram::kGridStep)*diagram::kGridStep,
                                     std::round(desired.y()/diagram::kGridStep)*diagram::kGridStep);
        const auto beforeCount = graph->elementNodes().size();
        const auto rootCount = scene()->elementNodes().size();
        const auto beforeIndex = graph->undoStack()->index();
        QSignalSpy added(target, &diagram::DiagramEditor::libraryElementAdded);
        QVERIFY(nativeLibraryDrag(tree, item, view->viewport()->mapToGlobal(location)));
        QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
        QCOMPARE(graph->elementNodes().size(), beforeCount+1);
        QCOMPARE(graph->undoStack()->index(), beforeIndex+1);
        const auto id = added.first()[0].toString();
        auto node = graph->elementNode(id.toStdString()); QVERIFY(node);
        QCOMPARE(node->pos(), snapped); QVERIFY(node->isSelected());
        QVERIFY(view->hasFocus() || view->viewport()->hasFocus());
        const auto actualType = type.startsWith("StudioObserver:") ? type.mid(15) : type;
        QCOMPARE(QString::fromStdString(node->element().type), actualType);
        if (type == "Gain") QCOMPARE(id, QString("gain_3"));
        if (actualType == "Scope" || actualType == "LiveOscilloscope") QCOMPARE(node->element().params.at("channel_count").to_number<int>(), 0);
        if (const auto* descriptor = s7::ElementRegistry::instance().find(actualType.toStdString()); descriptor && descriptor->metadata().schema_definition)
            QCOMPARE(node->element().schema, *descriptor->metadata().schema_definition);
        if (nested) QCOMPARE(scene()->elementNodes().size(), rootCount);
        QCOMPARE(item->data(0, Qt::UserRole).toString(), type); // copy, never remove the library row
        const auto element = node->element();
        graph->undoStack()->undo(); QVERIFY(!graph->elementNode(id.toStdString()));
        QCOMPARE(graph->elementNodes().size(), beforeCount);
        graph->undoStack()->redo(); QCOMPARE(graph->elementNode(id.toStdString())->element(), element);
        // The existing double-click route uses the same defaults and definition.
        tree->scrollToItem(item);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(item).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(item).center());
        QTest::mouseRelease(tree->viewport(), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(added.count(), 2, 5000);
        const auto otherId = added.last()[0].toString(); QVERIFY(otherId != id);
        const auto other = graph->elementNode(otherId.toStdString()); QVERIFY(other);
        QCOMPARE(other->element().params, element.params); QCOMPARE(other->element().schema, element.schema);
        const auto path = temporary_.filePath(QString("drag-%1.s7diagram").arg(QString::fromLatin1(QTest::currentDataTag())));
        QVERIFY(fileAction("action.saveAs", path));
        const auto saved = window_->currentDocument();
        action("action.closeDiagram")->trigger(); QVERIFY(fileAction("action.open", path));
        QCOMPARE(window_->currentDocument(), saved);
        capture(window_.get(), "library-drag-" + QString::fromLatin1(QTest::currentDataTag()), true);
    }
    void libraryDragCancellationAndSourceRules() {
        using namespace s7studio;
        if (QStandardPaths::findExecutable("xdotool").isEmpty()) QSKIP("xdotool is required for native X11 drag/drop checks");
        window_->raise(); window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* tree = window_->findChild<QTreeWidget*>("elements.tree"); QVERIFY(tree);
        auto* search = window_->findChild<QLineEdit*>("elements.search"); QVERIFY(search);
        auto* item = libraryItem(tree, "Gain"); QVERIFY(item);
        auto* category = item->parent(); QVERIFY(category);
        QVERIFY(!(category->flags() & Qt::ItemIsDragEnabled));
        QVERIFY(item->flags() & Qt::ItemIsDragEnabled);
        std::unique_ptr<QMimeData> categoryData(tree->model()->mimeData({tree->indexFromItem(category)}));
        QVERIFY(!categoryData || !categoryData->hasFormat(ui::libraryElementMimeType));
        std::unique_ptr<QMimeData> leafData(tree->model()->mimeData({tree->indexFromItem(item)})); QVERIFY(leafData);
        QCOMPARE(leafData->formats(), QStringList{QString::fromLatin1(ui::libraryElementMimeType)});
        QCOMPARE(ui::libraryElementFromMimeData(leafData.get()), QString("Gain"));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto original = window_->currentDocument(); const auto index = scene()->undoStack()->index();
        QVERIFY(nativeLibraryDrag(tree, item, view->viewport()->mapToGlobal(view->viewport()->rect().center()), true));
        QCOMPARE(window_->currentDocument(), original); QCOMPARE(scene()->undoStack()->index(), index);
        QVERIFY(nativeLibraryDrag(tree, item, search->mapToGlobal(search->rect().center())));
        QCOMPARE(window_->currentDocument(), original); QCOMPARE(scene()->undoStack()->index(), index);
        QVERIFY(nativeLibraryDrag(tree, category, view->viewport()->mapToGlobal(view->viewport()->rect().center())));
        QCOMPARE(window_->currentDocument(), original); QCOMPARE(scene()->undoStack()->index(), index);
    }
    void libraryDropRejectsInvalidPayloads() {
        using namespace s7studio;
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto original = window_->currentDocument(); const auto index = scene()->undoStack()->index();
        const auto location = view->viewport()->rect().center();
        const auto refused = [&](const QByteArray& format, const QByteArray& bytes, Qt::DropActions actions = Qt::CopyAction) {
            QMimeData data; data.setData(QString::fromLatin1(format), bytes);
            QDragEnterEvent enter(location, actions, &data, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &enter);
            QDragMoveEvent move(location, actions, &data, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &move);
            QDropEvent drop(location, actions, &data, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &drop);
            QDragLeaveEvent leave; QApplication::sendEvent(view->viewport(), &leave);
            return !enter.isAccepted() && !move.isAccepted() && !drop.isAccepted();
        };
        const QByteArray valid = R"({"version":1,"type":"Gain"})";
        QVERIFY(refused("text/plain", "Gain"));
        QVERIFY(refused("application/x-qschematic-item", valid));
        for (const auto& bytes : {QByteArray{}, QByteArray("{"), QByteArray("[]"), QByteArray(R"({"version":2,"type":"Gain"})"),
                                 QByteArray(R"({"version":1})"), QByteArray(R"({"version":1,"type":"UnavailablePlugin"})"),
                                 QByteArray(R"({"version":1,"type":"Gain","node":{}})"), QByteArray(ui::maximumLibraryElementPayload+1,'x')})
            QVERIFY(refused(ui::libraryElementMimeType, bytes));
        QVERIFY(refused(ui::libraryElementMimeType, valid, Qt::MoveAction));
        std::unique_ptr<QMimeData> data(ui::libraryElementMimeData("Gain")); QVERIFY(data);
        QDragEnterEvent enter(location, Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter); QVERIFY(enter.isAccepted());
        QDropEvent outside(QPointF(-20,-20), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &outside); QVERIFY(!outside.isAccepted());
        QDragLeaveEvent leave; QApplication::sendEvent(view->viewport(), &leave);
        // An entry can disappear after drag-enter (for example after a catalog
        // refresh). Recheck availability at drop rather than accepting old data.
        auto& registry = s7::ElementRegistry::instance();
        auto metadata = registry.find("Gain")->metadata(); metadata.type_name = "TemporaryDragElement";
        registry.register_element(s7::ElementDescriptor(metadata.type_name,
            [](const s7::ElementCreationContext&) -> std::unique_ptr<s7::Element> { return {}; }, metadata));
        const auto unregister = qScopeGuard([&] { registry.unregister({metadata.type_name}); });
        std::unique_ptr<QMimeData> stale(ui::libraryElementMimeData("TemporaryDragElement")); QVERIFY(stale);
        QDragEnterEvent available(location, Qt::CopyAction, stale.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &available); QVERIFY(available.isAccepted());
        registry.unregister({metadata.type_name});
        QDropEvent missing(location, Qt::CopyAction, stale.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &missing); QVERIFY(!missing.isAccepted());
        QApplication::sendEvent(view->viewport(), &leave);
        QCOMPARE(window_->currentDocument(), original); QCOMPARE(scene()->undoStack()->index(), index);
        QVERIFY(!editor()->addLibraryElement("UnavailablePlugin"));
        QVERIFY(!editor()->addLibraryElement("Gain", QPointF(std::numeric_limits<double>::infinity(),0)));
        QCOMPARE(window_->currentDocument(), original);
    }
    void elementDocumentationCoverage() {
        using namespace s7studio;
        s7::ElementCatalogService catalog(s7::ElementRegistry::instance());
        for (const auto& entry : catalog.all_elements()) {
            const auto& metadata = s7::ElementRegistry::instance().find(entry.type_name)->metadata();
            const auto documentation = ui::elementDocumentation(metadata);
            QVERIFY2(!metadata.description.empty(), entry.type_name.c_str());
            for (const auto& parameter : metadata.parameters) {
                QVERIFY2(!parameter.description.empty(), parameter.name.c_str());
                QVERIFY(documentation.contains(QString::fromStdString(parameter.name) + QStringLiteral(" — ")));
            }
        }
        const auto docs = [&](const char* type) { return ui::elementDocumentation(s7::ElementRegistry::instance(), type); };
        QVERIFY(docs("Adder").contains("2..64"));
        QVERIFY(docs("Adder").contains("accumulator_type"));
        QVERIFY(docs("Accumulator").contains("cannot alone close a feedback loop"));
        QVERIFY(docs("TrapezoidIntegrator").contains("Sampled mode cannot alone close a feedback loop"));
        QVERIFY(docs("SymplecticIntegrator").contains("not exactly conserved"));
        QVERIFY(docs("PIController").contains("Gains are signals, not JSON parameters"));
        QVERIFY(docs("PIController").contains("Internal element settings"));
        QVERIFY(docs("Convert").contains("required; must be supplied"));
        QVERIFY(docs("RecordedSource").contains("no resampling"));
        QVERIFY(docs("NetSink").contains("[numerator, denominator]"));
        const auto live = docs("LiveOscilloscope");
        for (const auto* key : {"channel_count", "channel_names", "channel_units", "trigger", "mode", "channel", "edge", "level",
                                "hysteresis", "pretrigger_percent", "holdoff_steps", "auto_timeout_steps"})
            QVERIFY2(live.contains(key), key);
        const auto& trigger = viewers::ViewerRegistry::instance().find("LiveOscilloscope")->documentation->parameters.back();
        QCOMPARE(trigger.default_value.at("pretrigger_percent").to_number<int>(), observation::TriggerSettings{}.pretriggerPercent);
        QVERIFY(docs("UnavailablePlugin").contains("Documentation unavailable"));
    }
    void elementInformationAccess() {
        using namespace s7studio;
        auto* tree = window_->findChild<QTreeWidget*>("elements.tree"); QVERIFY(tree);
        QTreeWidgetItem* gain = nullptr; QTreeWidgetItem* delay = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                auto* item = tree->topLevelItem(i)->child(j);
                if (item->data(0, Qt::UserRole).toString() == "Gain") gain = item;
                if (item->data(0, Qt::UserRole).toString() == "Delay") delay = item;
            }
        QVERIFY(gain && delay);
        tree->setCurrentItem(gain); tree->scrollToItem(delay);
        const auto document = window_->currentDocument();
        // Right-click targets Delay even though Gain remains the selected row.
        bool menuSeen = false;
        QTimer menuDriver;
        menuDriver.setInterval(10);
        connect(&menuDriver, &QTimer::timeout, this, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu || menu->objectName() != "elements.contextMenu") return;
            menuSeen = true; menuDriver.stop();
            menu->setActiveAction(menu->actions().first());
            QTest::keyClick(menu, Qt::Key_Return);
        });
        const auto inspect = [&](QDialog* dialog) {
            auto* view = dialog->findChild<QPlainTextEdit*>("element.documentation");
            if (!view || !view->isReadOnly() || !view->toPlainText().contains("Type: Delay")) {
                qWarning() << "Context target" << (view ? view->toPlainText().left(100) : QString("No view"));
                return false;
            }
            capture(dialog, "element-information", true);
            dialog->reject(); return true;
        };
        const auto point = tree->visualItemRect(delay).center();
        menuDriver.start();
        QVERIFY(modalWorkflow([&] {
            // Dispatch the platform context-menu event with a different row
            // selected; Qt Test mouse events alone do not synthesize it.
            QContextMenuEvent context(QContextMenuEvent::Mouse, point, tree->viewport()->mapToGlobal(point));
            QApplication::sendEvent(tree->viewport(), &context);
        }, inspect));
        QVERIFY(menuSeen);
        QCOMPARE(window_->currentDocument(), document);
        // Category and empty-space context requests have no element action.
        tree->scrollToItem(tree->topLevelItem(0));
        auto categoryPoint = tree->visualItemRect(tree->topLevelItem(0)).center();
        QContextMenuEvent category(QContextMenuEvent::Mouse, categoryPoint, tree->viewport()->mapToGlobal(categoryPoint));
        QApplication::sendEvent(tree->viewport(), &category);
        QVERIFY(!QApplication::activePopupWidget());
        QContextMenuEvent blank(QContextMenuEvent::Mouse, {-10,-10}, tree->viewport()->mapToGlobal(QPoint(-10,-10)));
        QApplication::sendEvent(tree->viewport(), &blank);
        QVERIFY(!QApplication::activePopupWidget());
        tree->setCurrentItem(delay); tree->scrollToItem(delay); tree->setFocus();
        menuDriver.start();
        QVERIFY(modalWorkflow([&] {
            QTest::keyClick(tree, Qt::Key_Menu);
        }, inspect));
        menuDriver.start();
        QVERIFY(modalWorkflow([&] { QTest::keyClick(tree, Qt::Key_F10, Qt::ShiftModifier); }, inspect));
        // Edit menu reads the active canvas selection, including schema/observer types.
        for (const auto* type : {"Gain", "PIController", "Scope"}) {
            scene()->clearSelection();
            auto node = scene()->addElement({std::string("info_") + type, type, {}, {}, {}, {}}, {0,0});
            node->setSelected(true);
            QVERIFY(dialogAction("action.elementInformation", [&](QDialog* dialog) {
                auto* view = dialog->findChild<QPlainTextEdit*>("element.documentation");
                if (!view || !view->toPlainText().contains(QString("Type: ") + type)) return false;
                dialog->reject(); return true;
            }));
        }
        auto* tabs = window_->findChild<SchemaTabs*>(); QVERIFY(tabs);
        auto* nested = tabs->open(editor(), "info_PIController"); QVERIFY(nested);
        nested->scene()->clearSelection();
        auto state = nested->scene()->elementNode("i_state"); QVERIFY(state); state->setSelected(true);
        QVERIFY(dialogAction("action.elementInformation", [&](QDialog* dialog) {
            auto* view = dialog->findChild<QPlainTextEdit*>("element.documentation");
            if (!view || !view->toPlainText().contains("Type: EulerIntegrator")) return false;
            dialog->reject(); return true;
        }));
    }
    void longElementDocumentation() {
        using namespace s7studio;
        auto& registry = s7::ElementRegistry::instance();
        s7::ElementMetadata metadata;
        metadata.type_name = "DocumentationFixture"; metadata.display_name = "Long plugin documentation";
        metadata.category = "Documentation test";
        metadata.description = "<img src=\"https://invalid.example/never-load\">\n";
        for (int i = 0; i < 200; ++i) metadata.description += "Plugin paragraph " + std::to_string(i) + ": selectable documentation with a long explanation.\n";
        metadata.description += "LAST PLUGIN PARAGRAPH";
        metadata.parameters.push_back({"value", "string", "Fixture setting", "example", {}, {}, {}, false});
        registry.register_element(s7::ElementDescriptor(metadata.type_name,
            [](const s7::ElementCreationContext&) -> std::unique_ptr<s7::Element> { return {}; }, metadata));
        scene()->clearSelection();
        auto node = scene()->addElement({"long_docs", metadata.type_name, {}, {}, {}, {}}, {0,0});
        node->setSelected(true);
        const auto expected = ui::elementDocumentation(metadata);
        const auto check = [&](QDialog* dialog, bool properties) {
            dialog->resize(560, 460);
            dialog->raise(); dialog->activateWindow();
            if (!QTest::qWaitForWindowActive(dialog)) return false;
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!buttons || !buttons->isVisible() || !dialog->rect().contains(buttons->geometry())) return false;
            auto* view = dialog->findChild<QPlainTextEdit*>("element.documentation");
            if (!view || view->toPlainText() != expected || !view->isReadOnly()) {
                qWarning() << "Documentation mismatch" << bool(view) << (view ? view->toPlainText().size() : 0) << expected.size();
                return false;
            }
            if (properties) {
                auto* splitter = dialog->findChild<QSplitter*>("element.propertiesSplitter");
                if (!splitter || splitter->count() != 2) return false;
                auto* parameter = dialog->findChild<QWidget*>("parameter.value");
                if (!parameter || !parameter->isVisible()) return false;
                capture(dialog, "long-element-properties", true);
            }
            view->setFocus(); QTest::keyClick(view, Qt::Key_End, Qt::ControlModifier);
            QTest::qWait(20);
            if (view->verticalScrollBar()->value() <= 0 || !view->toPlainText().contains("LAST PLUGIN PARAGRAPH")) {
                qWarning() << "Documentation scroll" << view->verticalScrollBar()->value() << view->verticalScrollBar()->maximum() << view->textCursor().position();
                return false;
            }
            view->selectAll(); view->copy();
            if (QApplication::clipboard()->text() != expected) {
                qWarning() << "Clipboard mismatch" << QApplication::clipboard()->text().size() << expected.size();
                return false;
            }
            dialog->reject(); return true;
        };
        const auto before = window_->currentDocument();
        QVERIFY(dialogAction("action.elementInformation", [&](QDialog* d) { return check(d, false); }));
        QVERIFY(dialogAction("action.properties", [&](QDialog* d) { return check(d, true); }));
        QCOMPARE(window_->currentDocument(), before);
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
    void liveOscilloscopePrototype() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/2int_osc_live.s7diagram"));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        QVERIFY(QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, "live_1"), Q_ARG(QString, "LiveOscilloscope"), Q_ARG(int, 1)));
        auto* viewer = dynamic_cast<s7studio::ui::LiveOscilloscopeWindow*>(manager->find("LiveOscilloscope", "live_1")); QVERIFY(viewer);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", 2)); QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last()[0].toBool()); QVERIFY(!runner->recordedSource());
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(2000));
        auto* quick = viewer->plotView()->quickWidget(); QVERIFY(quick); QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        auto* count = viewer->findChild<QSpinBox*>("live.samples"); QVERIFY(count);
        // A pointer left over the plot by an earlier test holds the frame for inspection.
        QTest::mouseMove(count, count->rect().center());
        count->setValue(1001); QCOMPARE(runner->liveSources()["live_1"]->capacity(), std::size_t(1001));
        auto* seconds = viewer->findChild<QLineEdit*>("live.seconds"); QVERIFY(seconds);
        seconds->setFocus(); seconds->selectAll(); QTest::keyClicks(seconds, "0.5"); QTest::keyClick(seconds, Qt::Key_Return);
        QTRY_COMPARE(count->value(), 501);
        viewer->findChild<QDoubleSpinBox*>("live.timeScale")->setValue(.05);
        QTRY_COMPARE(viewer->plotView()->xMin(), 1.5);
        auto* graph = quick->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        const auto area = graph->property("plotArea").toRectF(); QVERIFY(area.width() > 0);
        const auto center = graph->mapToScene(area.center()).toPoint();
        QTest::mouseMove(quick, center); QTRY_VERIFY(viewer->plotView()->bridge()->cursorVisible);
        auto* readout = viewer->findChild<QLabel*>("live.readout"); QVERIFY(readout);
        QTRY_VERIFY(readout->text().contains("Stored sample")); QVERIFY(readout->text().contains("step"));
        const auto index = viewer->displayedFrame().nearest(viewer->plotView()->bridge()->cursorX); QVERIFY(index);
        QVERIFY(readout->text().contains(QString::number(viewer->displayedFrame().values[*index], 'g', 5)));
        capture(viewer, "live-oscilloscope-hover");
        // Pointer on the trace: a translucent value box beside it and a ring on the sample.
        const auto& shown = viewer->displayedFrame(); auto* view = viewer->plotView();
        const auto sample = *shown.nearest((view->xMin()+view->xMax())/2);
        const QPointF onTrace(area.left()+(shown.plottedTime(sample)-view->xMin())/(view->xMax()-view->xMin())*area.width(),
            area.top()+(view->yMax()-shown.values[sample*static_cast<std::size_t>(shown.channels.size())])/(view->yMax()-view->yMin())*area.height());
        QTest::mouseMove(quick, graph->mapToScene(onTrace).toPoint());
        auto* tip = quick->rootObject()->findChild<QQuickItem*>("viewer.hoverTip"); QVERIFY(tip);
        QTRY_VERIFY(tip->isVisible()); QVERIFY(!view->bridge()->hoverMarkers.isEmpty());
        QVERIFY(view->bridge()->hoverTip.last().toMap()["text"].toString().contains(shown.channels.front().name));
        capture(viewer, "live-oscilloscope-hover-tip", true);
        {   // On the trace itself the translucent tooltip and an on-curve marker appear.
            auto* tip = quick->rootObject()->findChild<QQuickItem*>("viewer.hoverTip"); QVERIFY(tip);
            const auto& frame = viewer->displayedFrame(); auto* plot = viewer->plotView();
            const auto i = *frame.nearest((plot->xMin()+plot->xMax())/2);
            const QPointF onCurve(area.left()+(frame.plottedTime(i)-plot->xMin())/(plot->xMax()-plot->xMin())*area.width(),
                                  area.top()+(plot->yMax()-frame.values[i*frame.channels.size()])/(plot->yMax()-plot->yMin())*area.height());
            QTest::mouseMove(quick, graph->mapToScene(onCurve).toPoint());
            QTRY_VERIFY(tip->isVisible());
            QVERIFY(plot->bridge()->hoverTip.size() >= 2); QCOMPARE(plot->bridge()->hoverMarkers.size(), 1);
            QVERIFY(plot->bridge()->hoverTip[1].toMap()["text"].toString().startsWith(frame.channels.front().name + " = "));
            capture(viewer, "live-oscilloscope-hover-tip", true);
            QTest::mouseMove(quick, center);
        }
        auto* cursor = quick->rootObject()->findChild<QQuickItem*>("viewer.sampleCursor"); QVERIFY(cursor);
        QVERIFY(cursor->isVisible());
        const auto lower = viewer->plotView()->xMin(), upper = viewer->plotView()->xMax();
        viewer->plotView()->setXRange(upper + .1, upper + .2);
        QTRY_VERIFY(!cursor->isVisible());
        viewer->plotView()->setXRange(lower, upper);
        QTRY_VERIFY(cursor->isVisible());
        auto* freeze = viewer->findChild<QPushButton*>("live.freeze"); QTest::mouseClick(freeze, Qt::LeftButton); QVERIFY(freeze->isChecked());
        const auto frameEnd = viewer->displayedFrame().steps.back();
        count->setValue(128); manager->refreshViews(); QCOMPARE(viewer->displayedFrame().steps.back(), frameEnd);
        capture(viewer, "live-oscilloscope-frozen");
        auto* follow = viewer->findChild<QPushButton*>("live.follow"); QVERIFY(follow);
        QTest::mouseMove(follow, follow->rect().center()); QTest::mouseClick(follow, Qt::LeftButton);
        QTRY_COMPARE(runner->liveSources()["live_1"]->capacity(), std::size_t(128));
        viewer->close(); QVERIFY(!viewer->isVisible());
        manager->openObserver("LiveOscilloscope", "live_1"); QTRY_VERIFY(viewer->isVisible());
        action("action.reset")->trigger(); QVERIFY(viewer->displayedFrame().steps.empty());
    }
    void liveOscilloscopeTwoChannels600Seconds() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/symplectic_osc_live.s7diagram"));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner);
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "live_1")); QVERIFY(viewer);
        QSignalSpy complete(runner, &LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", 600)); QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 30000);
        QVERIFY(complete.last()[0].toBool());
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        QTRY_COMPARE(viewer->displayedFrame().steps.back(), std::int64_t(600000));
        QCOMPARE(viewer->displayedFrame().channels.size(), 2);
        auto* plot = viewer->plotView();
        QTRY_COMPARE(plot->xMax(), 600.); QCOMPARE(plot->xMin(), 580.);
        QTRY_VERIFY(!plot->series()[0]->points().isEmpty() && !plot->series()[1]->points().isEmpty());
        const auto frame = viewer->displayedFrame();
        // Independent closed form for the symplectic update, observed one causal step later.
        const double theta = 2*std::asin(.001/2), n = 599999;
        const double position = std::sin(n*theta)/std::cos(theta/2);
        const double velocity = std::cos((n-.5)*theta)/std::cos(theta/2);
        QVERIFY(std::abs(frame.values.back()-velocity) < 1e-9);
        QVERIFY(std::abs(frame.values[frame.values.size()-2]-position) < 1e-9);
        capture(viewer, "live-two-channels-600s", true);
        viewer->findChild<QPushButton*>("live.freeze")->setChecked(true);
        viewer->findChild<QDoubleSpinBox*>("live.channel.0.offset")->setValue(.4);
        QTRY_COMPARE(plot->series()[0]->points().back().y(), frame.values[frame.values.size()-2]+.4);
        const auto geometry = plot->geometry();
        auto* graph = plot->quickWidget()->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        auto* entry = visualItem(plot->quickWidget()->rootObject(), "viewer.legend.channel.1"); QVERIFY(entry);
        QTest::mouseClick(plot->quickWidget(), Qt::LeftButton, Qt::NoModifier, entry->mapToScene(QPointF(7,7)).toPoint());
        QTRY_VERIFY(!viewer->findChild<QCheckBox*>("live.channel.1.visible")->isChecked());
        entry = visualItem(plot->quickWidget()->rootObject(), "viewer.legend.channel.1"); QVERIFY(entry);
        QTest::mouseClick(plot->quickWidget(), Qt::LeftButton, Qt::NoModifier, entry->mapToScene(QPointF(7,7)).toPoint());
        QTRY_VERIFY(viewer->findChild<QCheckBox*>("live.channel.1.visible")->isChecked());
        for (const auto t : {595.905, 598., 600.}) { viewer->inspect(t); QCoreApplication::processEvents(); QCOMPARE(plot->geometry(), geometry); }
        viewer->inspect(0, false); QCoreApplication::processEvents(); QCOMPARE(plot->geometry(), geometry);
        capture(viewer, "live-two-channels-adjusted-600s", true);
    }
    void liveMeasurementExamples_data() {
        QTest::addColumn<QString>("filename"); QTest::addColumn<QString>("observer"); QTest::addColumn<QString>("unit");
        QTest::newRow("am") << "am_coherent_detection" << "carrier_scope" << "V";
        QTest::newRow("fm") << "fm_frequency_detection" << "frequency_scope" << "Hz";
    }
    void liveSpectrumAnalysis() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/am_coherent_detection_live.s7diagram"));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy complete(runner, &LocalSchemaRunner::runCompleted);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !rate || !buttons) return false;
            duration->setValue(2); rate->setValue(12800);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton); return true;
        }));
        QTRY_COMPARE(complete.count(), 1); QVERIFY(complete.last()[0].toBool()); QVERIFY(!runner->recordedSource());
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "carrier_scope")); QVERIFY(viewer);
        viewer->resize(1280, 720); viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        auto* tabs = viewer->findChild<QTabWidget*>("live.views"); QVERIFY(tabs);
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier, tabs->tabBar()->tabRect(1).center());
        auto* spectrum = viewer->findChild<viewers::SpectrumView*>(); QVERIFY(spectrum);
        auto* source = spectrum->controlPanel()->findChild<QComboBox*>("spectrum.channel"); QVERIFY(source);
        QTRY_VERIFY(source->count() == 3);
        const auto carrier = source->findText("/carrier [", Qt::MatchContains); QVERIFY(carrier >= 0); source->setCurrentIndex(carrier);
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Ready);
        const auto& initial = *spectrum->result();
        const auto peak = static_cast<std::size_t>(std::max_element(initial.values.begin(), initial.values.end())-initial.values.begin());
        QCOMPARE(initial.frequencies[peak], 100.); QVERIFY(std::abs(initial.values[peak]-1) < 1e-10);
        QCOMPARE(initial.lastStep, std::int64_t(25600)); QCOMPARE(initial.size, std::size_t(4096));
        auto* quick = spectrum->plotView()->quickWidget(); QVERIFY(quick); QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        auto* graph = quick->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        // Zoom so a pixel is much narrower than the 3.125 Hz bin spacing at any plot width.
        spectrum->plotView()->setXRange(50, 150);
        QTRY_VERIFY(graph->property("plotArea").toRectF().width() > 100);
        const auto area = graph->property("plotArea").toRectF();
        const auto position = graph->mapToScene(area.center()).toPoint();
        QTest::mouseMove(quick, position);
        QTRY_VERIFY(spectrum->controlPanel()->findChild<QLabel*>("spectrum.readout")->text().contains("Computed spectrum"));
        QVERIFY(spectrum->controlPanel()->findChild<QLabel*>("spectrum.readout")->text().contains("100 Hz baseband"));
        const auto spectrumGeometry = spectrum->plotView()->geometry();
        spectrum->inspect(-1); QCoreApplication::processEvents();
        spectrum->inspect(100); QCoreApplication::processEvents();
        QCOMPARE(spectrum->plotView()->geometry(), spectrumGeometry);
        QVERIFY(spectrum->controlPanel()->findChild<QScrollArea*>("spectrum.readoutScroll")->height() >
                spectrum->controlPanel()->findChild<QScrollArea*>("spectrum.statusScroll")->height()*2);
        capture(viewer, "live-spectrum-am-carrier", true);
        viewer->findChild<QPushButton*>("live.follow")->click(); QTRY_VERIFY(spectrum->plotView()->xMax() > 150);
        QTest::mouseMove(tabs->tabBar(), tabs->tabBar()->tabRect(1).center());
        auto* size = spectrum->controlPanel()->findChild<QComboBox*>("spectrum.size"); QVERIFY(size); size->setCurrentIndex(size->findData(65536));
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Filling);
        QVERIFY(spectrum->plotView()->series().first()->points().isEmpty());
        size->setCurrentIndex(size->findData(1024));
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Ready);
        auto* scale = spectrum->controlPanel()->findChild<QComboBox*>("spectrum.scale"); scale->setCurrentIndex(1);
        QVERIFY(spectrum->plotView()->yTitle().contains("dB re 1"));
        capture(viewer, "live-spectrum-db", true);
        auto* freeze = viewer->findChild<QPushButton*>("live.freeze"); freeze->setChecked(true);
        const auto end = spectrum->result()->lastStep;
        viewer->findChild<QSpinBox*>("live.samples")->setValue(128);
        QCOMPARE(spectrum->result()->lastStep, end);
        spectrum->controlPanel()->findChild<QComboBox*>("spectrum.window")->setCurrentIndex(0);
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Ready);
        QCOMPARE(spectrum->result()->lastStep, end); QCOMPARE(spectrum->result()->size, std::size_t(1024));
        action("action.reset")->trigger(); QVERIFY(!spectrum->result());
        QVERIFY(spectrum->plotView()->series().first()->points().isEmpty());
    }
    void liveConstellationCorrelationAndInactiveViews() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/iq_correlation_live.s7diagram"));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &LocalSchemaRunner::runCompleted);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !rate || !buttons) return false;
            duration->setValue(8); rate->setValue(1024);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton); return true;
        }));
        QTRY_COMPARE(completed.count(), 1); QVERIFY(completed.last()[0].toBool()); QVERIFY(!runner->recordedSource());
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "iq_scope")); QVERIFY(viewer);
        viewer->resize(1280, 800); viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        auto* tabs = viewer->findChild<QTabWidget*>("live.views"); QVERIFY(tabs);
        auto* spectrum = viewer->findChild<viewers::SpectrumView*>(); QVERIFY(spectrum);
        auto* xy = viewer->findChild<viewers::PairAnalysisView*>("live.constellation"); QVERIFY(xy);
        auto* correlation = viewer->findChild<viewers::PairAnalysisView*>("live.correlation"); QVERIFY(correlation);
        QCOMPARE(spectrum->analysisRequestCount(), std::uint64_t(0));
        QCOMPARE(xy->analysisRequestCount(), std::uint64_t(0)); QCOMPARE(correlation->analysisRequestCount(), std::uint64_t(0));
        tabs->setCurrentIndex(2);
        QTRY_VERIFY(xy->result() && xy->result()->state == analysis::PairResult::State::Ready);
        QCOMPARE(xy->result()->points.size(), std::size_t(4096));
        for (const auto& p : xy->result()->points) QVERIFY(std::abs(p.x*p.x+p.y*p.y-1) < 1e-12);
        auto* quick = xy->plotView()->quickWidget(); QVERIFY(quick); QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        auto* graph = quick->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        QTRY_VERIFY(graph->property("plotArea").toRectF().width() > 100);
        QTest::mouseClick(xy->controlPanel()->findChild<QPushButton*>("pair.fit"), Qt::LeftButton);
        const auto area = graph->property("plotArea").toRectF();
        const double sx = (xy->plotView()->xMax()-xy->plotView()->xMin())/area.width();
        const double sy = (xy->plotView()->yMax()-xy->plotView()->yMin())/area.height();
        QVERIFY(std::abs(sx/sy-1) < .05);
        const auto point = xy->result()->points.back();
        // Fit and wrapped controls can change geometry on the next Quick frame.
        // Derive the gesture again while the real layout settles.
        QTRY_VERIFY(([&] {
            const auto currentArea = graph->property("plotArea").toRectF();
            const auto position = graph->mapToScene(QPointF(currentArea.left()+(point.x-xy->plotView()->xMin())/(xy->plotView()->xMax()-xy->plotView()->xMin())*currentArea.width(),
                currentArea.bottom()-(point.y-xy->plotView()->yMin())/(xy->plotView()->yMax()-xy->plotView()->yMin())*currentArea.height())).toPoint();
            viewer->raise(); viewer->activateWindow();
            QTest::mouseMove(tabs->tabBar(), tabs->tabBar()->tabRect(2).center());
            QTest::mouseMove(quick, position);
            return xy->controlPanel()->findChild<QLabel*>("pair.readout")->text().contains("Raw pair");
        })());
        capture(viewer, "live-constellation-circle", true);
        xy->inspect(0, 0, false);
        xy->controlPanel()->findChild<QComboBox*>("pair.mode")->setCurrentIndex(1);
        QVERIFY(!xy->plotView()->series().first()->points().empty());
        xy->controlPanel()->findChild<QComboBox*>("pair.mode")->setCurrentIndex(2);
        QVERIFY(xy->plotView()->bridge()->densityVisible);
        auto* image = quick->rootObject()->findChild<QQuickItem*>("viewer.density"); QVERIFY(image);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready, decoded by QML image provider.
        QVERIFY(xy->inspect(point.x, point.y).contains("Density aggregate")); xy->inspect(0, 0, false);
        capture(viewer, "live-constellation-density", true);
        tabs->setCurrentIndex(3);
        correlation->controlPanel()->findChild<QComboBox*>("pair.second")->setCurrentIndex(2);
        correlation->controlPanel()->findChild<QSpinBox*>("pair.maxLag")->setValue(16);
        QTRY_VERIFY(correlation->result() && correlation->result()->state == analysis::PairResult::State::Ready);
        const auto& result = *correlation->result(); QCOMPARE(result.lags[result.peakIndex], 6);
        QVERIFY(correlation->inspect(6./1024, 0).contains("Lag 6 samples"));
        QVERIFY(correlation->controlPanel()->findChild<QLabel*>("pair.marker")->text().contains("overlap 4090/4096"));
        correlation->inspect(0, 0, false); capture(viewer, "live-correlation-delay", true);
        const auto hiddenXY = xy->analysisRequestCount();
        xy->controlPanel()->findChild<QSpinBox*>("pair.persistence")->setValue(8192); // Hidden settings must not queue work.
        QCOMPARE(xy->analysisRequestCount(), hiddenXY);
        QCOMPARE(spectrum->analysisRequestCount(), std::uint64_t(0));
        viewer->showMinimized(); QTRY_VERIFY(viewer->isMinimized());
        const auto minimized = correlation->analysisRequestCount();
        correlation->controlPanel()->findChild<QCheckBox*>("pair.removeMean")->setChecked(true);
        viewer->refreshSource(); QCOMPARE(correlation->analysisRequestCount(), minimized);
        viewer->showNormal(); viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        QTRY_VERIFY(correlation->result() && correlation->result()->state == analysis::PairResult::State::Ready);
        auto* freeze = viewer->findChild<QPushButton*>("live.freeze"); freeze->setChecked(true);
        const auto end = correlation->result()->lastStep;
        viewer->findChild<QSpinBox*>("live.samples")->setValue(128);
        correlation->controlPanel()->findChild<QComboBox*>("pair.size")->setCurrentIndex(0);
        QTRY_VERIFY(correlation->result() && correlation->result()->settings.size == 64);
        QCOMPARE(correlation->result()->lastStep, end);
        tabs->setCurrentIndex(1); QTRY_VERIFY(spectrum->analysisRequestCount() > 0);
        tabs->setCurrentIndex(0);
        const auto hiddenFFT = spectrum->analysisRequestCount(); const auto hiddenCorrelation = correlation->analysisRequestCount();
        spectrum->controlPanel()->findChild<QComboBox*>("spectrum.window")->setCurrentIndex(0);
        correlation->controlPanel()->findChild<QSpinBox*>("pair.maxLag")->setValue(8);
        QCOMPARE(spectrum->analysisRequestCount(), hiddenFFT); QCOMPARE(correlation->analysisRequestCount(), hiddenCorrelation);
        viewer->close(); QVERIFY(!viewer->isVisible());
        manager->openObserver("LiveOscilloscope", "iq_scope"); QTRY_VERIFY(viewer->isVisible());
        tabs->setCurrentIndex(2); QTRY_VERIFY(xy->result());
        action("action.reset")->trigger(); QVERIFY(!xy->result()); QVERIFY(!correlation->result());
    }
    void observersDefaultToFirstThreeAndRememberManualChoice() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/symplectic_osc_live.s7diagram"));
        auto* panel = window_->findChild<ui::ObserversPanel*>(); QVERIFY(panel);
        QCOMPARE(panel->selection().size(), std::size_t(1));
        for (int i = 1; i <= 5; ++i) scene()->addElement({"auto_"+std::to_string(i), "Scope", {}, {}}, {i*100., 0});
        QCOMPARE(panel->selection().size(), std::size_t(3));
        for (int i = 0; i < 3; ++i) QCOMPARE(std::string(panel->selection()[i].as_object().at("element_id").as_string()), "auto_"+std::to_string(i+1));
        scene()->setObserversPanel({boost::json::object{{"element_id","auto_5"},{"type","Scope"},{"collapsed",false}}}, true);
        scene()->addElement({"auto_0", "Scope", {}, {}}, {0,0});
        QCOMPARE(panel->selection().size(), std::size_t(1));
        QCOMPARE(std::string(panel->selection()[0].as_object().at("element_id").as_string()), std::string("auto_5"));
        scene()->setObserversPanel({}, true); QVERIFY(panel->selection().empty());
        const auto path = temporary_.filePath("manual-empty-previews.s7diagram"); QVERIFY(fileAction("action.saveAs", path));
        QVERIFY(fileAction("action.open", path)); QVERIFY(panel->selection().empty()); QVERIFY(scene()->observersPanelManual());
    }
    void observersDockSelectionLifecycleAndPersistence() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/iq_correlation_live.s7diagram"));
        auto* panel = window_->findChild<ui::ObserversPanel*>("observers.panel"); QVERIFY(panel);
        auto* dock = window_->findChild<QDockWidget*>("toolWindow.observers"); QVERIFY(dock);
        auto* tools = window_->findChild<ui::ToolWindows*>(); QVERIFY(tools); tools->show(dock);
        QCOMPARE(panel->selection().size(), std::size_t(1)); QVERIFY(!panel->findChild<QLabel*>("observers.empty")->isVisible());
        QVERIFY(modalWorkflow([&] { panel->findChild<QPushButton*>("observers.choose")->click(); }, [&](QDialog* dialog) {
            auto* list = dialog->findChild<QListWidget*>("observers.choices"); if (!list || list->count()!=1) return false;
            list->item(0)->setCheckState(Qt::Checked); dialog->reject(); return true;
        }));
        QCOMPARE(scene()->observersPanel().size(), std::size_t(1));
        QVERIFY(modalWorkflow([&] { panel->findChild<QPushButton*>("observers.choose")->click(); }, [&](QDialog* dialog) {
            auto* list = dialog->findChild<QListWidget*>("observers.choices"); if (!list) return false;
            list->item(0)->setCheckState(Qt::Checked); dialog->accept(); return true;
        }));
        QCOMPARE(scene()->observersPanel().size(), std::size_t(1)); QCOMPARE(panel->activePresentations(), 0);
        auto* runner = window_->findChild<LocalSchemaRunner*>(); auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(runner && manager);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>("run.duration")->setValue(-1); dialog->findChild<QDoubleSpinBox*>("run.frequency")->setValue(1024);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); return true;
        }));
        QTRY_COMPARE(runner->state(), RunState::Running); QTRY_VERIFY(panel->activePresentations() == 1);
        QTRY_VERIFY(panel->findChild<QLabel*>("observer.status.iq_scope")->text().contains("Rolling"));
        QVERIFY(!manager->find("LiveOscilloscope", "iq_scope")); // Rendering a card never opens a full window.
        auto* preview = panel->findChild<viewers::ObserverPresentation*>(); QVERIFY(preview);
        QTRY_VERIFY(!preview->linePlot()->series().empty() && !preview->linePlot()->series().front()->points().empty());
        auto* previewCard = panel->findChild<QWidget*>("observer.card.iq_scope"); QVERIFY(previewCard);
        auto* previewGraph = previewCard->findChild<QWidget*>("observer.graph.iq_scope"); QVERIFY(previewGraph);
        QVERIFY(previewGraph->height() > previewCard->height()*.6);
        auto* previewStatus = previewCard->findChild<QLabel*>("observer.status.iq_scope");
        QVERIFY(previewStatus->height() <= previewStatus->fontMetrics().lineSpacing()+4);
        capture(panel, "observers-compact-live", true);
        const auto capacity = runner->liveSources()["iq_scope"]->capacity();
        panel->findChild<QPushButton*>("observer.open.iq_scope")->click();
        auto* full = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->find("LiveOscilloscope", "iq_scope")); QVERIFY(full && full->isVisible());
        QCOMPARE(runner->liveSources()["iq_scope"]->capacity(), capacity);
        full->findChild<QPushButton*>("live.freeze")->setChecked(true); QVERIFY(!panel->findChild<QToolButton*>("observer.freeze.iq_scope")->isChecked());
        full->close(); window_->raise(); window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        action("action.pause")->trigger(); QTRY_COMPARE(runner->state(), RunState::Paused);
        QTRY_VERIFY(panel->findChild<QLabel*>("observers.state")->text().contains("paused"));
        auto* libraryDock = window_->findChild<QDockWidget*>("toolWindow.elementLibrary");
        QVERIFY(libraryDock); tools->show(libraryDock); QTRY_COMPARE(panel->activePresentations(), 0);
        const auto submitted = panel->submittedRequests(); QTest::qWait(250); QCOMPARE(panel->submittedRequests(), submitted); QVERIFY(!dock->isVisible());
        action("action.resume")->trigger(); QTRY_COMPARE(runner->state(), RunState::Running); QTest::qWait(150); QVERIFY(!dock->isVisible());
        tools->show(dock); QTRY_VERIFY(panel->activePresentations() == 1);
        // Editing the next run must not replace this card's running source.
        auto node = scene()->elementNode("iq_scope"); QVERIFY(node); auto element = node->element(); element.params["channel_names"] = boost::json::array{"new_I", "Q", "I_delayed_6"}; scene()->changeElement(node, element);
        const auto details = panel->findChild<QPlainTextEdit*>("observer.details.iq_scope");
        panel->findChild<QAction*>("observer.showDetails.iq_scope")->setChecked(true);
        QTRY_VERIFY(details->toPlainText().contains("iq_scope/I")); QVERIFY(!details->toPlainText().contains("new_I"));
        auto* collapse = panel->findChild<QToolButton*>("observer.collapse.iq_scope"); collapse->setChecked(false); QTRY_COMPARE(panel->activePresentations(), 0);
        QVERIFY(scene()->observersPanel()[0].as_object().at("collapsed").as_bool());
        action("action.stop")->trigger(); QTRY_COMPARE(runner->state(), RunState::Stopped); QVERIFY(dock->isVisible());
        const auto saved = temporary_.filePath("observer-panel.s7diagram"); QVERIFY(fileAction("action.saveAs", saved));
        action("action.reset")->trigger(); QCOMPARE(panel->activePresentations(), 0);
        QVERIFY(fileAction("action.open", saved)); QVERIFY(scene()->observersPanel()[0].as_object().at("collapsed").as_bool());
        capture(window_.get(), "observers-dock-selected-collapsed", true);
        panel->findChild<QPushButton*>("observers.recordedSignals")->click(); auto* signalsDialog = window_->findChild<QDialog*>("observers.recordedSignalsDialog"); QVERIFY(signalsDialog && signalsDialog->isVisible()); signalsDialog->close();
    }
    void observersDockAllPresentationTypes() {
        using namespace s7studio; using namespace diagram;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/iq_correlation_live.s7diagram"));
        const auto source = scene()->elementNode("i"), quadrature = scene()->elementNode("q"); QVERIFY(source && quadrature);
        auto liveNode = scene()->elementNode("iq_scope"); auto configuredLive = liveNode->element();
        configuredLive.params["trigger"] = boost::json::object{{"mode", "single"}, {"level", .2}, {"pretrigger_percent", 0}}; scene()->changeElement(liveNode, configuredLive);
        boost::json::array selection;
        for (const auto& type : {"Scope", "PhasePortrait", "PhaseTrajectory3D"}) {
            const std::string id = std::string("preview_")+type;
            auto observer = scene()->addElement({id, type, {}, {}}, {100,100});
            const int count = std::string(type)=="Scope" ? 1 : std::string(type)=="PhasePortrait" ? 2 : 3;
            for (int axis=0;axis<count;++axis) QVERIFY(scene()->connectEnds(WireEnd::onPort(axis==1 ? quadrature : source, PortDirection::Output,0), WireEnd::onPort(observer,PortDirection::Input,axis)).isEmpty());
            selection.push_back(boost::json::object{{"element_id",id},{"type",type},{"collapsed",false}});
        }
        selection.push_back(boost::json::object{{"element_id","iq_scope"},{"type","LiveOscilloscope"},{"collapsed",false}}); scene()->setObserversPanel(selection,true);
        auto* panel = window_->findChild<ui::ObserversPanel*>(); auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(panel && runner);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>("run.duration")->setValue(1); dialog->findChild<QDoubleSpinBox*>("run.frequency")->setValue(1024); dialog->accept(); return true;
        }));
        QTRY_COMPARE(runner->state(), RunState::Completed);
        const auto held = runner->liveSources()["iq_scope"]->triggerSnapshot(); QVERIFY(held && held->trigger && held->trigger->triggered);
        QVERIFY(held->trigger->step < 100); QCOMPARE(held->trigger->settings.mode, observation::TriggerMode::Single);
        auto* scroll = panel->findChild<QScrollArea*>("observers.scroll"); QVERIFY(scroll);
        for (const auto* id : {"preview_Scope", "preview_PhasePortrait", "preview_PhaseTrajectory3D", "iq_scope"}) {
            auto* card = panel->findChild<QWidget*>(QString("observer.card.%1").arg(id)); QVERIFY(card); scroll->ensureWidgetVisible(card);
            QTRY_VERIFY(card->findChild<viewers::ObserverPresentation*>());
            auto* status = card->findChild<QLabel*>(QString("observer.status.%1").arg(id)); QTRY_VERIFY(status->toolTip().contains("Recent"));
            QVERIFY(panel->activePresentations() <= 4); capture(card, QString("observers-preview-")+id,true);
        }
        // Deletion keeps the active binding/card; undo restores the selection.
        auto live = scene()->elementNode("iq_scope"); scene()->clearSelection(); live->setSelected(true); scene()->deleteSelection();
        QVERIFY(panel->findChild<QWidget*>("observer.card.iq_scope"));
        action("action.undo")->trigger(); QVERIFY(scene()->elementNode("iq_scope"));
        action("action.reset")->trigger(); QTRY_COMPARE(panel->activePresentations(), 0);
    }
    void instrumentControls_data() {
        QTest::addColumn<bool>("dark"); QTest::newRow("light") << false; QTest::newRow("dark") << true;
    }
    void instrumentControls() {
        using namespace s7studio;
        QFETCH(bool, dark);
        const auto previous = QApplication::palette(); const auto restore = qScopeGuard([previous] { QApplication::setPalette(previous); });
        auto palette = previous;
        for (auto role : {QPalette::Window, QPalette::Button}) palette.setColor(role, QColor(dark ? "#242424" : "#eeeeee"));
        palette.setColor(QPalette::Base, QColor(dark ? "#202020" : "#ffffff"));
        for (auto role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text}) {
            palette.setColor(role, QColor(dark ? "#eeeeee" : "#101010"));
            palette.setColor(QPalette::Disabled, role, QColor(dark ? "#888888" : "#777777"));
        }
        QApplication::setPalette(palette);
        QVector<observation::LiveChannel> channels;
        for (int i = 0; i < 8; ++i) channels.push_back({QStringLiteral("Signal %1 — a long descriptive channel name").arg(i+1), "V", "f64"});
        auto source = std::make_shared<observation::LiveAcquisition>(1, .001, channels);
        for (int step = 1; step <= 1024; ++step) {
            double values[8]; for (int i = 0; i < 8; ++i) values[i] = std::sin(step*.02+i)*.1+i;
            source->append(step, step*.001, values);
        }
        ui::LiveOscilloscopeWindow viewer("instrument controls"); viewer.attachLiveSource(source); viewer.resize(1024, 768); viewer.show(); viewer.raise(); viewer.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&viewer)); QTRY_VERIFY(!viewer.displayedFrame().steps.empty());
        QCOMPARE(viewer.size(), QSize(1024, 768));
        auto* scroll = viewer.findChild<QScrollArea*>("live.controlsScroll"); QVERIFY(scroll);
        auto* selected = viewer.findChild<QToolButton*>("live.channel.7.select"); QVERIFY(selected && selected->isVisible());
        QTest::mouseClick(selected, Qt::LeftButton); QVERIFY(selected->isChecked());
        auto* gain = viewer.findChild<QDoubleSpinBox*>("live.channel.7.gain"); auto* dial = viewer.findChild<QDial*>("live.channel.7.gain.dial"); QVERIFY(gain && dial && dial->isVisible());
        const auto before = dial->value(); dial->setFocus(); QTest::keyClick(dial, Qt::Key_Right); QVERIFY(dial->value() != before); QVERIFY(gain->value() != 1.);
        auto* gainEdit = gain->findChild<QLineEdit*>(); QVERIFY(gainEdit);
        QVERIFY(gain->width() < 150); // Compact editor scrolls for full precision; tooltip retains the value.
        QCOMPARE(source->snapshot()->values.back(), std::sin(1024*.02+7)*.1+7); // Display adjustment is non-invasive.
        capture(&viewer, dark ? "instrument-controls-dark-1024" : "instrument-controls-light-1024");
        for (const auto* name : {"acquisition", "trigger", "inspection"}) {
            auto* toggle = viewer.findChild<QToolButton*>(QString("live.section.%1.toggle").arg(name)); QVERIFY(toggle);
            scroll->ensureWidgetVisible(toggle); if (!toggle->isChecked()) QTest::mouseClick(toggle, Qt::LeftButton); QVERIFY(toggle->isChecked());
        }
        auto* pre = viewer.findChild<QSlider*>("trigger.pre.slider"); scroll->ensureWidgetVisible(pre); QTest::mouseClick(pre, Qt::LeftButton, Qt::NoModifier, QPoint(pre->width()*3/4, pre->height()/2));
        QCOMPARE(pre->value(), viewer.findChild<QSpinBox*>("trigger.pre")->value());
        auto* falling = viewer.findChild<QToolButton*>("trigger.edge.1"); scroll->ensureWidgetVisible(falling); QTest::mouseClick(falling, Qt::LeftButton); QVERIFY(!viewer.triggerSettings().rising);
        auto* panel = viewer.findChild<viewers::WindowInspectionPanel*>(); QTRY_VERIFY(panel->pin());
        auto* a = panel->findChild<QSlider*>("inspection.a.slider"); auto* b = panel->findChild<QSlider*>("inspection.b.slider"); const int targetA = std::min(1000, a->maximum()); a->setValue(targetA); b->setValue(3);
        panel->findChild<QToolButton*>("inspection.active.1")->click(); QCoreApplication::processEvents(); scroll->ensureWidgetVisible(b); b->setFocus(); QTest::keyClick(b, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(b->value(), 4); QCOMPARE(a->value(), targetA);
        for (auto* field : viewer.findChildren<QAbstractSpinBox*>()) {
            if (!field->isVisible() || !scroll->widget()->isAncestorOf(field)) continue;
            const auto rect = QRect(field->mapTo(scroll->widget(), QPoint()), field->size());
            QVERIFY2(rect.left() >= 0 && rect.right() < scroll->widget()->width(), qPrintable(field->objectName()));
        }
        scroll->ensureWidgetVisible(panel->findChild<QToolButton*>("inspection.export"));
        capture(&viewer, dark ? "instrument-inspection-dark-1024" : "instrument-inspection-light-1024");
        viewer.resize(1440, 900); QCOMPARE(viewer.size(), QSize(1440, 900));
        auto* tabs = viewer.findChild<QTabWidget*>("live.views");
        for (int i = 1; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i); QCoreApplication::processEvents(); QCOMPARE(viewer.width(), 1440);
            viewer.resize(1024, 768); QCoreApplication::processEvents(); QCOMPARE(viewer.size(), QSize(1024, 768));
            auto* plot = tabs->currentWidget()->findChild<viewers::LinePlotView*>(); QVERIFY(plot); QVERIFY(plot->height() > viewer.height()*.75);
            QVERIFY(plot->bridge()->instrumentTheme);
            const int widthWithControls = plot->width();
            auto* toggle = viewer.findChild<QToolButton*>("live.controlsToggle"); toggle->click(); QCoreApplication::processEvents();
            QVERIFY(plot->width() > widthWithControls); toggle->click(); QCoreApplication::processEvents();
            capture(&viewer, QString("instrument-analysis-%1-%2").arg(i).arg(dark ? "dark" : "light")); viewer.resize(1440, 900);
        }
    }
    void finiteAndContinuousProgress() {
        s7studio::ui::RunPanel panel({});
        auto* progress = panel.findChild<QProgressBar*>("run.progress"); QVERIFY(progress);
        panel.setProgress(25, 100, .025); QVERIFY(!progress->isHidden()); QCOMPARE(progress->value(), 250);
        panel.setProgress(100, 100, .1); QCOMPARE(progress->value(), 1000);
        panel.setProgress(12345, -1, 12.345); QVERIFY(progress->isHidden());
        const auto text = panel.findChild<QLabel*>("run.steps")->text(); QVERIFY(text.contains("continuous")); QVERIFY(text.contains("12.345 s")); QVERIFY(!text.contains('%'));
    }
    void liveCursorsMeasurementsAndWindowExport() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/iq_correlation_live.s7diagram"));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &LocalSchemaRunner::runCompleted);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !rate || !buttons) return false;
            duration->setValue(8); rate->setValue(1024); buttons->button(QDialogButtonBox::Ok)->click(); return true;
        }));
        QTRY_COMPARE(completed.count(), 1); QVERIFY(completed.last()[0].toBool());
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "iq_scope")); QVERIFY(viewer);
        viewer->resize(1450, 1000); viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        auto* panel = viewer->findChild<viewers::WindowInspectionPanel*>(); QVERIFY(panel);
        viewer->findChild<QToolButton*>("live.section.inspection.toggle")->setChecked(true);
        auto* tabs = viewer->findChild<QTabWidget*>("live.views"); QVERIFY(tabs);
        auto* a = panel->findChild<QSpinBox*>("inspection.a"); auto* b = panel->findChild<QSpinBox*>("inspection.b"); QVERIFY(a && b);
        auto* active = panel->findChild<ui::ChoiceButtons*>("inspection.active"); QVERIFY(active);
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty() && viewer->displayedFrame().steps.size() <= 4096);
        viewer->findChild<QDoubleSpinBox*>("live.timeScale")->setValue(.8);
        const auto selectedCount = viewer->displayedFrame().steps.size();
        auto* quick = viewer->plotView()->quickWidget(); QVERIFY(quick);
        QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        auto* graph = quick->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        QTRY_VERIFY(graph->property("plotArea").toRectF().width() > 100);
        auto area = graph->property("plotArea").toRectF();
        QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, graph->mapToScene(QPointF(area.left()+area.width()*.4, area.center().y())).toPoint());
        QTRY_VERIFY(panel->snapshot()); QVERIFY(viewer->findChild<QPushButton*>("live.freeze")->isChecked());
        QVERIFY(a->value() > 0); QCOMPARE(viewer->plotView()->bridge()->selectionMarkers.size(), 2);
        const auto oldA = a->value(); a->setFocus(); QTest::keyClick(a, Qt::Key_Right, Qt::AltModifier); QCOMPARE(a->value(), oldA+1);
        a->setValue(0); b->setValue(b->maximum());
        QTRY_VERIFY(visualItem(quick->rootObject(), "viewer.cursor.B"));
        const auto* rightCursor = visualItem(quick->rootObject(), "viewer.cursor.B");
        QVERIFY(rightCursor->property("px").toDouble() < rightCursor->width());
        viewer->findChild<QDoubleSpinBox*>("live.channel.0.gain")->setValue(10);
        viewer->findChild<QDoubleSpinBox*>("live.channel.0.offset")->setValue(500);
        QTest::mouseClick(panel->findChild<QPushButton*>("inspection.measure"), Qt::LeftButton);
        QVERIFY(panel->readout().contains("not full-rate signal measurements"));
        QVERIFY(panel->readout().contains("step 1")); QVERIFY(panel->readout().contains("step 8192"));
        QTest::mouseClick(panel->findChild<QPushButton*>("inspection.copy"), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), panel->readout());
        capture(viewer, "live-time-cursors-measurements");
        const auto saved = temporary_.filePath("frozen-window.s7window");
        QSignalSpy exported(panel, &viewers::WindowInspectionPanel::exportFinished);
        viewer->findChild<QSpinBox*>("live.samples")->setValue(128);
        QVERIFY(modalWorkflow([&] { panel->findChild<QToolButton*>("inspection.export")->click(); },
            [&](QDialog* dialog) { return selectFile(dialog, saved); }));
        QTRY_COMPARE(exported.count(), 1); QVERIFY(exported.last()[0].toBool());
        QCOMPARE(panel->findChild<QProgressBar*>("inspection.progress")->value(), 100);
        QVERIFY(!panel->findChild<QPushButton*>("inspection.cancel")->isEnabled());
        auto* exportButton = panel->findChild<QToolButton*>("inspection.export"); QVERIFY(exportButton->isEnabled());
        exportButton->menu()->actions()[2]->trigger(); QVERIFY(exportButton->text().contains("Displayed data CSV"));
        exportButton->menu()->actions()[0]->trigger(); QVERIFY(exportButton->text().contains("Window JSON"));
        QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly)); const auto document = QJsonDocument::fromJson(file.readAll());
        QCOMPARE(document["raw_samples"].toArray().size(), static_cast<qsizetype>(selectedCount));
        QVERIFY(document["metadata"].toObject()["raw"].toObject()["time_envelope_selection"].toBool());
        QVERIFY(!document["metadata"].toObject()["complete_run"].toBool());
        QCOMPARE(document["metadata"].toObject()["raw"].toObject()["channels"].toArray()[0].toObject()["unit"].toString(), QString("V"));
        viewer->findChild<QPushButton*>("live.follow")->click(); QVERIFY(!panel->snapshot());
        QTRY_VERIFY(viewer->displayedFrame().timeEnvelope);
        QCOMPARE(runner->liveSources()["iq_scope"]->capacity(), std::size_t(128));
        tabs->setCurrentIndex(1);
        auto* spectrum = viewer->findChild<viewers::SpectrumView*>(); QVERIFY(spectrum);
        spectrum->controlPanel()->findChild<QComboBox*>("spectrum.size")->setCurrentIndex(0);
        spectrum->controlPanel()->findChild<QComboBox*>("spectrum.scale")->setCurrentIndex(1);
        spectrum->controlPanel()->findChild<QDoubleSpinBox*>("spectrum.frequencyOffset")->setValue(1000);
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Ready);
        QVERIFY(panel->pin()); a->setValue(1); b->setValue(2);
        QCOMPARE(spectrum->plotView()->bridge()->selectionMarkers[0].toMap()["x"].toDouble(), 1016.);
        QVERIFY(panel->readout().contains("Δfrequency=16 Hz"));
        QVERIFY(panel->readout().contains("frequency_axis=1016 Hz"));
        panel->findChild<QPushButton*>("inspection.measure")->click(); QVERIFY(panel->readout().contains("not time-domain signal RMS"));
        QVERIFY(panel->readout().contains("Raw latest-transform input")); capture(viewer, "live-spectrum-cursors");
        const auto csv = temporary_.filePath("spectrum.csv"); exported.clear();
        QVERIFY(panel->exportTo(csv, analysis::WindowExportFormat::DisplayedCsv)); QTRY_COMPARE(exported.count(), 1); QVERIFY(exported.last()[0].toBool());
        QFile csvFile(csv); QVERIFY(csvFile.open(QIODevice::ReadOnly)); const auto bytes = csvFile.readAll();
        QVERIFY(bytes.startsWith("# {")); QVERIFY(bytes.contains("frequency_offset_hz")); QVERIFY(bytes.contains("\n1,,,16,"));
        const auto* pinnedValues = spectrum->result()->values.data();
        tabs->setCurrentIndex(0);
        QSemaphore entered, release;
        for (int i = 0; i < 2; ++i) analysis::analysisPool().start([&] { entered.release(); release.acquire(); });
        entered.acquire(2);
        tabs->setCurrentIndex(1); // Queue reanalysis behind both held workers.
        const bool processingVisible = !spectrum->controlPanel()->findChild<QProgressBar*>("spectrum.processing")->isHidden();
        const bool repinned = panel->pin(); // Freeze is already checked.
        release.release(2);
        QVERIFY(repinned); QVERIFY(processingVisible); QVERIFY(analysis::analysisPool().waitForDone(5000));
        QTest::qWait(50);
        QTRY_VERIFY(spectrum->controlPanel()->findChild<QProgressBar*>("spectrum.processing")->isHidden());
        QCOMPARE(spectrum->result()->values.data(), pinnedValues); // Queued result cannot replace the cursor owner.
        spectrum->controlPanel()->findChild<QComboBox*>("spectrum.window")->setCurrentIndex(0); QVERIFY(!panel->snapshot());
        tabs->setCurrentIndex(2);
        auto* xy = viewer->findChild<viewers::PairAnalysisView*>("live.constellation"); QVERIFY(xy);
        QTRY_VERIFY(xy->result() && xy->result()->state == analysis::PairResult::State::Ready);
        xy->controlPanel()->findChild<QComboBox*>("pair.mode")->setCurrentIndex(2); QVERIFY(panel->pin());
        const auto pair = panel->snapshot()->columns[1].values[20]; panel->pick(panel->snapshot()->displayX[20], pair);
        QVERIFY(panel->readout().contains("step ")); QVERIFY(!panel->readout().contains("Density aggregate"));
        QCOMPARE(xy->plotView()->bridge()->selectionMarkers[0].toMap()["vertical"].toBool(), false);
        b->setValue(100); panel->findChild<QPushButton*>("inspection.measure")->click(); capture(viewer, "live-xy-cursors");
        tabs->setCurrentIndex(3);
        auto* correlation = viewer->findChild<viewers::PairAnalysisView*>("live.correlation"); QVERIFY(correlation);
        correlation->controlPanel()->findChild<QComboBox*>("pair.size")->setCurrentIndex(0);
        correlation->controlPanel()->findChild<QComboBox*>("pair.second")->setCurrentIndex(2);
        correlation->controlPanel()->findChild<QSpinBox*>("pair.maxLag")->setValue(16);
        QTRY_VERIFY(correlation->result() && correlation->result()->state == analysis::PairResult::State::Ready);
        QVERIFY(panel->pin()); a->setValue(16); b->setValue(22);
        QVERIFY(panel->readout().contains("Δlag_time=0.005859375 s"));
        panel->findChild<QPushButton*>("inspection.measure")->click(); capture(viewer, "live-correlation-cursors");
        action("action.reset")->trigger(); QVERIFY(!panel->snapshot()); QVERIFY(correlation->plotView()->bridge()->selectionMarkers.isEmpty());
        viewer->close();
    }
    void liveEdgeTriggerSynchronization() {
        using namespace s7studio;
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/iq_correlation_live.s7diagram"));
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "iq_scope")); QVERIFY(viewer);
        viewer->resize(1400, 1050); viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        viewer->findChild<QSpinBox*>("live.samples")->setValue(128);
        auto* mode = viewer->findChild<QComboBox*>("trigger.mode"); QVERIFY(mode); mode->setCurrentIndex(3);
        QTest::mouseMove(mode, mode->rect().center()); // keep a stray pointer from holding the frame for inspection
        auto* level = viewer->findChild<QLineEdit*>("trigger.level"); auto* hyst = viewer->findChild<QLineEdit*>("trigger.hysteresis"); QVERIFY(level && hyst);
        level->setText("0.25"); QVERIFY(QMetaObject::invokeMethod(level, "editingFinished", Qt::DirectConnection));
        hyst->setText("0.02"); QVERIFY(QMetaObject::invokeMethod(hyst, "editingFinished", Qt::DirectConnection));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner); QSignalSpy completed(runner, &LocalSchemaRunner::runCompleted);
        auto run = [&] { return dialogAction("action.run", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration"); auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>(); if (!duration || !rate || !buttons) return false;
            duration->setValue(8); rate->setValue(1024); buttons->button(QDialogButtonBox::Ok)->click(); return true;
        }); };
        QVERIFY(run()); QTRY_COMPARE(completed.count(), 1); QVERIFY(completed.last()[0].toBool());
        QTRY_VERIFY(viewer->displayedFrame().trigger.has_value());
        const auto first = viewer->displayedFrame();
        QVERIFY(first.trigger->triggered); QVERIFY(!first.trigger->partial); QCOMPARE(first.steps.size(), std::size_t(128));
        QCOMPARE(first.steps[31], first.trigger->step); QCOMPARE(first.plottedTime(31), 0.);
        QVERIFY(first.trigger->step < 200); // Configured before stepping, rather than after the finite run finished.
        QVERIFY(viewer->plotView()->bridge()->triggerVisible); QVERIFY(viewer->plotView()->xTitle().contains("Time from sampled trigger"));
        QVERIFY(viewer->findChild<QLabel*>("trigger.status")->text().contains("Held single capture"));
        QVERIFY(viewer->inspect(0).contains("Trigger-relative time = 0 s")); viewer->inspect(0, false);
        capture(viewer, "live-single-trigger", true);
        mode->setCurrentIndex(2); QTRY_VERIFY(action("action.run")->isEnabled()); QVERIFY(run());
        QTRY_COMPARE(completed.count(), 2); QVERIFY(completed.last()[0].toBool());
        QTRY_VERIFY(viewer->displayedFrame().trigger && viewer->displayedFrame().runId != first.runId && viewer->displayedFrame().finished);
        const auto normal = viewer->displayedFrame(); QVERIFY(normal.trigger->triggered); QVERIFY(normal.trigger->partial);
        QVERIFY(normal.trigger->step > first.trigger->step+1000); QCOMPARE(normal.plottedTime(31), 0.);
        QCOMPARE(normal.trigger->step%64, first.trigger->step%64);
        for (std::size_t i = 0; i < normal.steps.size(); ++i) {
            QCOMPARE(normal.plottedTime(i), first.plottedTime(i));
            QVERIFY(std::abs(normal.values[i*3]-first.values[i*3]) < 1e-10);
        }
        QVERIFY(viewer->findChild<QLabel*>("trigger.status")->text().contains("PARTIAL"));
        auto* panel = viewer->findChild<viewers::WindowInspectionPanel*>(); QVERIFY(panel); QVERIFY(panel->pin());
        QCOMPARE(panel->snapshot()->displayX[31], 0.); QCOMPARE(panel->snapshot()->axis[31], normal.times[31]);
        viewer->findChild<QSpinBox*>("inspection.a")->setValue(31);
        QVERIFY(panel->readout().contains("trigger_time=0 s"));
        const auto path = temporary_.filePath("triggered.s7window"); QSignalSpy exported(panel, &viewers::WindowInspectionPanel::exportFinished);
        QVERIFY(panel->exportTo(path, analysis::WindowExportFormat::WindowJson)); QTRY_COMPARE(exported.count(), 1); QVERIFY(exported.last()[0].toBool());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto json = QJsonDocument::fromJson(file.readAll());
        const auto trigger = json["metadata"].toObject()["raw"].toObject()["trigger"].toObject();
        QVERIFY(trigger["triggered"].toBool()); QVERIFY(trigger["partial"].toBool());
        QCOMPARE(trigger["sampled_trigger_step"].toString(), QString::number(normal.trigger->step));
        QCOMPARE(json["displayed_rows"].toArray()[31].toObject()["display_axis"].toString(), QString("0"));
        capture(viewer, "live-normal-trigger-cursors", true);
        mode->setCurrentIndex(0); QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        QVERIFY(!viewer->plotView()->bridge()->triggerVisible); QVERIFY(!viewer->displayedFrame().trigger);
        level->setText("nan"); QVERIFY(QMetaObject::invokeMethod(level, "editingFinished", Qt::DirectConnection));
        viewer->refreshSource(); QVERIFY(viewer->findChild<QLabel*>("trigger.status")->text().contains("Invalid trigger"));
        action("action.reset")->trigger(); QVERIFY(viewer->displayedFrame().steps.empty());
    }
    void liveTriggerPresetFromDiagram() {
        using namespace s7studio;
        // A plugin-free copy of the two-integrator oscillator with a saved trigger preset.
        QFile source(QString(S7_GUI_EXAMPLES_DIR) + "/2int_osc_live.s7diagram"); QVERIFY(source.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(source.readAll()).object();
        auto diagram = document["diagram"].toObject(); auto nodes = diagram["nodes"].toArray();
        for (auto&& value : nodes) {
            auto node = value.toObject(); if (node["id"] != "live_1") continue;
            auto params = node["params"].toObject();
            params["trigger"] = QJsonObject{{"mode", "normal"}, {"edge", "rising"}, {"level", 0}, {"hysteresis", .2}};
            node["params"] = params; value = node;
        }
        diagram["nodes"] = nodes; document["diagram"] = diagram;
        const auto path = temporary_.filePath("triggered_oscillator.s7diagram");
        QFile copy(path); QVERIFY(copy.open(QIODevice::WriteOnly)); copy.write(QJsonDocument(document).toJson()); copy.close();
        QVERIFY(fileAction("action.open", path));
        auto* manager = window_->findChild<viewers::ViewerManager*>(); QVERIFY(manager);
        QVERIFY(QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, "live_1"), Q_ARG(QString, "LiveOscilloscope"), Q_ARG(int, 1)));
        auto* viewer = dynamic_cast<ui::LiveOscilloscopeWindow*>(manager->find("LiveOscilloscope", "live_1")); QVERIFY(viewer);
        // The saved preset is active before the run; omitted keys keep their defaults.
        QCOMPARE(viewer->triggerSettings().mode, observation::TriggerMode::Normal); QCOMPARE(viewer->triggerSettings().hysteresis, .2);
        QCOMPARE(viewer->triggerSettings().pretriggerPercent, 25);
        auto* hysteresis = viewer->findChild<QLineEdit*>("trigger.hysteresis"); QVERIFY(hysteresis);
        QCOMPARE(hysteresis->text(), QString("0.2"));
        auto* runner = window_->findChild<LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", 20)); QTRY_COMPARE(completed.count(), 1); QVERIFY(completed.last()[0].toBool());
        QTRY_VERIFY(viewer->displayedFrame().trigger && viewer->displayedFrame().trigger->triggered);
        // Fired on the rising flank: first sample at/above level+H/2 after one below it.
        const auto& frame = viewer->displayedFrame();
        const auto k = static_cast<std::size_t>(frame.trigger->step-frame.steps.front()); QVERIFY(k > 0);
        QVERIFY(frame.values[k] >= .1); QVERIFY(frame.values[k-1] < .1); QCOMPARE(frame.plottedTime(k), 0.);
        capture(viewer, "live-trigger-preset", true);
        // Applying a change in the window updates the diagram parameter, undoably.
        auto stored = [&] { return scene()->elementNode("live_1")->element().params.at("trigger").at("hysteresis").to_number<double>(); };
        const bool modifiedBefore = window_->documentModified();
        hysteresis->setText("0.3"); QVERIFY(QMetaObject::invokeMethod(hysteresis, "editingFinished", Qt::DirectConnection));
        QCOMPARE(stored(), .3); QTRY_VERIFY(window_->documentModified());
        scene()->undoStack()->undo();
        QCOMPARE(stored(), .2); QCOMPARE(viewer->triggerSettings().hysteresis, .2); QCOMPARE(hysteresis->text(), QString("0.2"));
        QTRY_COMPARE(window_->documentModified(), modifiedBefore);
        viewer->close();
    }
    void liveOscilloscopeWorkerKeepsUiResponsive() {
        using namespace s7studio;
        QVector<observation::LiveChannel> channels;
        for (int i = 0; i < 8; ++i) channels.push_back({QStringLiteral("channel_%1").arg(i), "V", "f64"});
        auto source = std::make_shared<observation::LiveAcquisition>(77, 1e-5, channels, QJsonObject{}, 100000, 1);
        ui::LiveOscilloscopeWindow viewer("worker-stress"); viewer.resize(1400, 900); viewer.attachLiveSource(source); viewer.show();
        auto* triggerMode = viewer.findChild<QComboBox*>("trigger.mode"); QVERIFY(triggerMode); triggerMode->setCurrentIndex(2);
        std::unique_ptr<viewers::ViewerManager> previewManager;
        std::unique_ptr<ui::ObserversPanel> previewPanel;
        if (qEnvironmentVariableIsSet("S7_OBSERVER_STRESS_PREVIEW")) {
            previewManager = std::make_unique<viewers::ViewerManager>(viewers::ViewerRegistry::instance());
            QVERIFY(previewManager->setObserverTrigger("worker-stress", viewer.triggerSettings())); previewManager->presentLive({{"worker-stress", source}});
            viewers::ObserverDescriptor descriptor{"worker-stress", "LiveOscilloscope", {}};
            for (int i=0;i<8;++i) descriptor.inputs.push_back({channels[i].name, "V", "stress", i});
            previewPanel = std::make_unique<ui::ObserversPanel>(previewManager.get()); previewPanel->resize(500,800);
            previewPanel->setDocument("Eight-channel stress", {descriptor}, {boost::json::object{{"element_id","worker-stress"},{"type","LiveOscilloscope"},{"collapsed",false}}});
            previewPanel->bindRun(77,{descriptor}); previewPanel->show(); QTRY_COMPARE(previewPanel->activePresentations(),1);
        }
        QElapsedTimer pulseClock; pulseClock.start();
        qint64 previousPulse = 0, maximumGapMs = 0; int pulses = 0;
        QTimer pulse; pulse.setInterval(10); connect(&pulse, &QTimer::timeout, &viewer, [&] {
            const auto now = pulseClock.elapsed(); if (previousPulse) maximumGapMs = std::max(maximumGapMs, now-previousPulse);
            previousPulse = now; ++pulses;
        }); pulse.start();
        std::atomic<bool> done{false};
        std::jthread producer([&] {
            const auto start = std::chrono::steady_clock::now();
            std::array<double, 8> values{};
            auto appendStep = [&](std::int64_t step) {
                values[0] = static_cast<double>(step%100-50)/50.0;
                for (std::size_t channel = 1; channel < values.size(); ++channel)
                    values[channel] = static_cast<double>((static_cast<std::uint64_t>(step)*(channel+3))%2048)/1024.0-1.0;
                source->append(step, static_cast<double>(step)*1e-5, values);
            };
            for (std::int64_t step = 1; step <= 3000000; ++step) {
                appendStep(step);
                if (step%1000 == 0) std::this_thread::sleep_until(start+std::chrono::microseconds(step*10));
            }
            const auto overloadUntil = std::chrono::steady_clock::now()+std::chrono::seconds(5);
            for (std::int64_t step = 3000001; std::chrono::steady_clock::now() < overloadUntil; ++step) appendStep(step);
            source->finish(); done.store(true, std::memory_order_release);
        });
        QTRY_VERIFY_WITH_TIMEOUT(done.load(std::memory_order_acquire), 45000);
        producer.join();
        QTRY_VERIFY_WITH_TIMEOUT(viewer.displayedFrame().trigger.has_value(), 2000);
        QTest::qWait(100); pulse.stop();
        qInfo() << "Live Time stress: 8 channels, 100 ksample/s paced plus 5 s unthrottled; preview" << bool(previewPanel) << "UI timer pulses" << pulses << "max gap" << maximumGapMs << "ms";
        QVERIFY(pulses > 0); QVERIFY2(maximumGapMs < 250, "UI event loop stalled for at least 250 ms during live Time updates");
    }
    void liveMeasurementExamples() {
        QFETCH(QString, filename); QFETCH(QString, observer); QFETCH(QString, unit);
        const auto directory = QString(S7_GUI_EXAMPLES_DIR) + "/signal_measurement/";
        QVERIFY(fileAction("action.open", directory + filename + "_live.s7diagram"));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* summary = dialog->findChild<QLabel*>("run.captureSummary");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !rate || !summary || !buttons) return false;
            if (!summary->text().contains("Live acquisition: 7 channels") || !summary->text().contains("Full-run recording: 0 channels")) return false;
            duration->setValue(2); rate->setValue(10000);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton); return true;
        }));
        QTRY_COMPARE(completed.count(), 1); QVERIFY(completed.last()[0].toBool());
        QVERIFY(!runner->recordedSource());
        int channels = 0;
        for (const auto& source : runner->liveSources()) {
            const auto frame = source->snapshot(); QVERIFY(frame); QVERIFY(frame->finished);
            QCOMPARE(frame->steps.back(), std::int64_t(20000)); QCOMPARE(frame->steps.size(), std::size_t(4096));
            channels += static_cast<int>(frame->channels.size());
            for (const auto& channel : frame->channels) QVERIFY(!channel.unit.isEmpty());
        }
        QCOMPARE(channels, 7);
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        auto* viewer = dynamic_cast<s7studio::ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", observer)); QVERIFY(viewer);
        QTRY_VERIFY(!viewer->displayedFrame().steps.empty());
        const auto readout = viewer->inspect(viewer->displayedFrame().times.back()); QVERIFY(readout.contains(unit));
        capture(viewer, filename + "-live");
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
    void menuOrganizationAndTracePolicy() {
        QStringList names;
        for (auto* entry : window_->menuBar()->actions()) {
            QVERIFY(entry->menu()); names.append(entry->menu()->objectName());
        }
        QCOMPARE(names, QStringList({"menu.file", "menu.edit", "menu.view", "menu.diagram",
                                    "menu.run", "menu.nodes", "menu.tools", "menu.settings", "menu.help"}));
        auto* view = window_->findChild<QMenu*>("menu.view");
        auto* diagram = window_->findChild<QMenu*>("menu.diagram");
        auto* run = window_->findChild<QMenu*>("menu.run");
        auto* nodes = window_->findChild<QMenu*>("menu.nodes");
        auto* page = window_->findChild<QMenu*>("menu.page");
        QVERIFY(view && diagram && run && nodes && page);
        QVERIFY(view->actions().contains(action("action.fitDiagram")));
        QVERIFY(!diagram->actions().contains(action("action.fitDiagram")));
        QVERIFY(diagram->actions().contains(page->menuAction()));
        QVERIFY(nodes->actions().contains(action("action.deploy")));
        QVERIFY(!run->actions().contains(action("action.deploy")));
        QVERIFY(!window_->findChild<QAction*>("action.trustedNetworks"));
        auto* keep = action("action.keepScopeTraces"); QVERIFY(keep);
        QVERIFY(run->actions().contains(keep)); QVERIFY(!view->actions().contains(keep));
        activateMenu("menu.run", keep);
        QVERIFY(keep->isChecked());
        QVERIFY(!window_->currentDocument().as_object().at("studio_run").as_object().at("clear_scope_traces").as_bool());
        QVERIFY(dialogAction("action.runConfiguration", [](QDialog* dialog) {
            auto* check = dialog->findChild<QCheckBox*>("run.keepScopeTraces");
            if (!check || !check->isChecked() || check->text() != "Keep Previous Scope Traces") return false;
            check->setChecked(false);
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QVERIFY(!keep->isChecked());
        QVERIFY(window_->currentDocument().as_object().at("studio_run").as_object().at("clear_scope_traces").as_bool());
        activateMenu("menu.diagram", page->menuAction());
        QTRY_VERIFY(page->isVisible());
        page->setActiveAction(action("action.paperA5")); QTest::keyClick(page, Qt::Key_Return);
        QCOMPARE(std::min(editor()->pageLayout().widthMm, editor()->pageLayout().heightMm), 148.);
        activateMenu("menu.diagram", action("action.drawWire"));
        QCOMPARE(scene()->mode(), s7studio::diagram::DiagramScene::DrawMode);
        activateMenu("menu.diagram", action("action.selectMove"));
        QCOMPARE(scene()->mode(), QSchematic::Scene::NormalMode);
        QVERIFY(action("action.selectMove")->isChecked());
    }
    void editingMenuTextFocus() {
        window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node = scene()->addElement({"source", "Constant", {{"value", 1.}}, {}, {}, "Source"}, {0, 0});
        QVERIFY(node); node->setSelected(true);
        auto* picker = window_->findChild<s7studio::ui::ElementPickerPanel*>(); QVERIFY(picker);
        auto* search = picker->findChild<QLineEdit*>(); QVERIFY(search);
        search->setFocus(); QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "text");
        activateMenu("menu.edit", action("action.selectAll"));
        QCOMPARE(search->selectedText(), QStringLiteral("text"));
        activateMenu("menu.edit", action("action.copy"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("text"));
        activateMenu("menu.edit", action("action.delete"));
        QVERIFY(search->text().isEmpty()); QVERIFY(scene()->elementNode("source"));
        activateMenu("menu.edit", action("action.undo"));
        QCOMPARE(search->text(), QStringLiteral("text"));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        view->setFocus(); QTRY_VERIFY(view->hasFocus());
        QTest::keyClick(view->viewport(), Qt::Key_Delete);
        QVERIFY(!scene()->elementNode("source"));
        QTest::keyClick(view->viewport(), Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(scene()->elementNode("source"));
        QCOMPARE(action("action.copy")->shortcut(), QKeySequence(QKeySequence::Copy));
        QVERIFY(!action("action.copy")->text().contains('\t'));
    }
    void exampleFileChooser() {
        const auto before = window_->currentDocument();
        const QDir app(QCoreApplication::applicationDirPath());
        QString expected = QString(S7_GUI_EXAMPLES_DIR);
        for (const auto& base : {QStringLiteral("../share/s7"), QStringLiteral("share/s7")}) {
            if (QFileInfo(app.filePath(base)).isDir()) { expected = QDir::cleanPath(app.filePath(base + "/examples")); break; }
        }
        QVERIFY(modalWorkflow([this] { activateMenu("menu.file", action("action.examples")); }, [&](QDialog* dialog) {
            auto* file = qobject_cast<QFileDialog*>(dialog);
            if (!file || file->directory().absolutePath() != expected || !file->nameFilters().front().contains("*.s7diagram")) return false;
            capture(file, "example-file-chooser", true);
            file->reject(); return true;
        }));
        QCOMPARE(window_->currentDocument(), before);
        QVERIFY(dialogAction("action.examples", [&](QDialog* dialog) {
            return selectFile(dialog, expected + "/linear/rc_charge_euler.s7diagram");
        }));
        QVERIFY(scene()->elementNode("state"));
        // Save uses a new destination, never the bundled source file.
        const auto copy = temporary_.filePath("example-copy.s7diagram");
        QVERIFY(fileAction("action.save", copy)); QVERIFY(QFileInfo::exists(copy));
    }
    void exampleReplacementSafety() {
        const auto input = temporary_.filePath("existing.s7diagram");
        QVERIFY(QFile::copy(QString(S7_GUI_EXAMPLES_DIR) + "/linear/rc_charge_euler.s7diagram", input));
        QVERIFY(fileAction("action.open", input));
        QVERIFY(scene()->addElement({"unsaved", "Constant", {}, {}, {}, "Unsaved"}, {0, 0}));
        const auto pending = window_->currentDocument();
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        runner->run(pending.as_object().at("schema"), .001, -1, {});
        QTRY_VERIFY(runner->isRunning()); runner->pause(); QTRY_VERIFY(runner->isPaused());
        const auto example = QString(S7_GUI_EXAMPLES_DIR) + "/linear/rc_discharge_euler.s7diagram";
        const auto openExample = [&](const QString& path) {
            QVERIFY(QMetaObject::invokeMethod(window_.get(), "openExampleDiagram", Qt::DirectConnection, Q_ARG(QString, path)));
        };
        QVERIFY(modalWorkflow([&] { openExample(example); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box) return false;
            QTest::mouseClick(box->button(QMessageBox::Cancel), Qt::LeftButton); return true;
        }));
        QCOMPARE(window_->currentDocument(), pending);
        QVERIFY(runner->isPaused());
        const auto invalid = temporary_.filePath("bad-example.s7diagram");
        QFile broken(invalid); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{\"document_kind\":\"s7.diagram\",\"schema\":{\"elements\":[{\"id\":\"x\",\"type\":\"Constant\"},{\"id\":\"x\",\"type\":\"Constant\"}],\"connections\":[]}}"); broken.close();
        QVERIFY(modalWorkflow([&] { openExample(invalid); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box || !box->text().contains("twice")) return false;
            QTest::mouseClick(box->button(QMessageBox::Ok), Qt::LeftButton); return true;
        }));
        QCOMPARE(window_->currentDocument(), pending);
        QVERIFY(runner->isPaused());
        QVERIFY(modalWorkflow([&] { openExample(example); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box) return false;
            QTest::mouseClick(box->button(QMessageBox::Save), Qt::LeftButton); return true;
        }));
        QFile saved(input); QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("unsaved"));
        saved.close();
        QVERIFY(!scene()->elementNode("unsaved"));
        QCOMPARE(runner->state(), s7studio::RunState::Idle);
        window_->openPath(input);
        QVERIFY(scene()->addElement({"second_unsaved", "Constant", {}, {}, {}, "Unsaved"}, {0, 0}));
        const auto failedSaveDocument = window_->currentDocument();
        QVERIFY(QFile::rename(input, input + ".preserved")); QVERIFY(QDir{}.mkdir(input));
        bool saveFailed = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            if (box->windowTitle() == "Unsaved changes") QTest::mouseClick(box->button(QMessageBox::Save), Qt::LeftButton);
            else if (box->windowTitle() == "Save failed") { saveFailed = true; QTest::mouseClick(box->button(QMessageBox::Ok), Qt::LeftButton); }
        });
        responder.start(10); openExample(example); responder.stop();
        QVERIFY(saveFailed); QCOMPARE(window_->currentDocument(), failedSaveDocument);
        QVERIFY(modalWorkflow([&] { openExample(example); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box) return false;
            QTest::mouseClick(box->button(QMessageBox::Discard), Qt::LeftButton); return true;
        }));
        QVERIFY(!scene()->elementNode("second_unsaved"));
    }
    void textFrameFontDefaultsAndExamples() {
        auto* size = window_->findChild<QSpinBox*>("frame.toolbarFontSize"); QVERIFY(size);
        QCOMPARE(size->value(), 10); QCOMPARE(size->minimum(), 1);
        s7studio::diagram::TextFrameNode defaults("default", {});
        QCOMPARE(defaults.params().at("font_size").as_int64(), 10);
        s7studio::diagram::TextFrameNode small("small", {{"font_size", 4}});
        QCOMPARE(small.params().at("font_size").as_int64(), 4);
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/2int_osc_phase_portrait.s7diagram"));
        auto frames = scene()->textFrames(); QVERIFY(!frames.empty());
        scene()->clearSelection(); frames.front()->setSelected(true);
        QVERIFY(dialogAction("action.textFrameFont", [](QDialog* dialog) {
            auto* points = dialog->findChild<QSpinBox*>("frame.fontSize"); if (!points) return false;
            points->setValue(4);
            QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok), Qt::LeftButton); return true;
        }));
        QCOMPARE(frames.front()->params().at("font_size").as_int64(), 4);
        int framesChecked = 0;
        QDirIterator files(QString(S7_GUI_EXAMPLES_DIR), {"*.s7diagram"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next()); QVERIFY(file.open(QIODevice::ReadOnly));
            const auto root = QJsonDocument::fromJson(file.readAll()).object();
            for (const auto& value : root["diagram"].toObject()["text_frames"].toArray()) {
                ++framesChecked;
                QVERIFY2(value.toObject()["params"].toObject()["font_size"].toInt() >= 10, qPrintable(file.fileName()));
            }
        }
        QVERIFY(framesChecked >= 62);
    }
    void toolbarPaletteChanges() {
        const auto previous = QApplication::palette();
        const auto restore = qScopeGuard([previous] { QApplication::setPalette(previous); });
        const auto icon = action("action.runConfiguration")->icon();
        auto* toolbar = window_->findChild<QToolBar*>("toolbar.run"); QVERIFY(toolbar);
        for (const bool dark : {false, true}) {
            const QColor background = dark ? QColor("#202020") : QColor("#eeeeee");
            const QColor foreground = dark ? QColor("#ffffff") : QColor("#101010");
            const QColor disabled = dark ? QColor("#999999") : QColor("#666666");
            auto palette = previous;
            palette.setColor(QPalette::Button, background); palette.setColor(QPalette::ButtonText, foreground);
            palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
            QApplication::setPalette(palette); QCoreApplication::processEvents();
            for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
                const auto image = icon.pixmap(QSize(24, 24), mode).toImage();
                int pixels = 0;
                const auto expected = mode == QIcon::Normal ? foreground : disabled;
                for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    if (color.alpha() > 200) {
                        ++pixels;
                        // Premultiplied alpha round-trips can change a channel
                        // by one level at antialiased SVG edges.
                        QVERIFY(std::abs(color.red() - expected.red()) <= 2);
                        QVERIFY(std::abs(color.green() - expected.green()) <= 2);
                        QVERIFY(std::abs(color.blue() - expected.blue()) <= 2);
                    }
                }
                QVERIFY(pixels > 0);
            }
            const auto image = toolbar->grab().toImage(); int matching = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
                matching += image.pixelColor(x, y).rgb() == background.rgb();
            QVERIFY(matching > image.width() * image.height() / 3);
            capture(toolbar, dark ? "run-toolbar-dark" : "run-toolbar-light", true);
        }
    }
    void diagramZoomShortcuts_data() {
        QTest::addColumn<int>("key");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("equal") << int(Qt::Key_Equal) << Qt::KeyboardModifiers(Qt::ControlModifier);
        QTest::newRow("plus") << int(Qt::Key_Plus) << Qt::KeyboardModifiers(Qt::ControlModifier);
        QTest::newRow("shift-plus") << int(Qt::Key_Plus) << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier);
        QTest::newRow("keypad-plus") << int(Qt::Key_Plus) << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::KeypadModifier);
    }
    void diagramZoomShortcuts() {
        QFETCH(int, key);
        QFETCH(Qt::KeyboardModifiers, modifiers);
        window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        editor()->resetZoom();
        QSignalSpy zoomed(view, SIGNAL(zoomChanged(qreal))); QVERIFY(zoomed.isValid());
        QTest::keyClick(view->viewport(), Qt::Key(key), modifiers);
        QCOMPARE(zoomed.count(), 1);
        QVERIFY(std::abs(view->transform().m11() - 1.15) < 1e-6);
        zoomed.clear();
        QTest::keyClick(view->viewport(), Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(zoomed.count(), 1);
        QVERIFY(std::abs(view->transform().m11() - 1.0) < 1e-6);
    }
    void diagramZoomFocusAndOtherInputs() {
        window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        editor()->resetZoom();
        // Canvas zoom shortcuts must leave focused library text entry alone.
        auto* picker = window_->findChild<s7studio::ui::ElementPickerPanel*>(); QVERIFY(picker);
        auto* search = picker->findChild<QLineEdit*>(); QVERIFY(search);
        search->setFocus(); QTRY_VERIFY(search->hasFocus());
        const auto before = view->transform();
        QTest::keyClick(search, Qt::Key_Plus, Qt::ControlModifier);
        QCOMPARE(view->transform(), before);
        QTest::keyClick(search, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(view->transform(), before);
        QTest::keyClick(search, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(view->transform(), before);
        QTest::keyClicks(search, "Gain"); QCOMPARE(search->text(), QStringLiteral("Gain"));

        auto* zoomIn = action("action.zoomIn"); auto* zoomOut = action("action.zoomOut");
        QVERIFY(zoomIn && zoomOut);
        // Activate the visible View menu through keyboard dispatch.
        QMenu* viewMenu = nullptr;
        for (auto* menu : window_->menuBar()->findChildren<QMenu*>())
            if (menu->actions().contains(zoomIn)) viewMenu = menu;
        QVERIFY(viewMenu);
        viewMenu->popup(window_->menuBar()->mapToGlobal(QPoint(0, window_->menuBar()->height())));
        QTRY_VERIFY(viewMenu->isVisible());
        viewMenu->setActiveAction(zoomIn);
        QTest::keyClick(viewMenu, Qt::Key_Return);
        QTRY_VERIFY(!viewMenu->isVisible());
        QVERIFY(std::abs(view->transform().m11() - 1.15) < 1e-6);
        view->setFocus(); QTRY_VERIFY(view->hasFocus());
        const QPoint position = view->viewport()->rect().center();
        QWheelEvent wheel(QPointF(position), QPointF(view->viewport()->mapToGlobal(position)),
            QPoint(), QPoint(0, -120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
        QVERIFY(std::abs(view->transform().m11() - 1.0) < 1e-6);

        // A nested editor receives zoom; its inactive root keeps its scale.
        auto nested = scene()->addElement({"nested", "Schema", {}, boost::json::object{
            {"name", "nested"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}}, {}, "Plant"}, {0, 0});
        QVERIFY(nested);
        auto* schemaTabs = window_->findChild<s7studio::SchemaTabs*>(); QVERIFY(schemaTabs);
        auto* child = schemaTabs->open(editor(), "nested"); QVERIFY(child);
        auto* childView = child->findChild<QGraphicsView*>(); QVERIFY(childView);
        QCoreApplication::processEvents();
        child->resetZoom(); childView->setFocus(); QTRY_VERIFY(childView->hasFocus());
        QTest::keyClick(childView->viewport(), Qt::Key_Equal, Qt::ControlModifier);
        QVERIFY(std::abs(childView->transform().m11() - 1.15) < 1e-6);
        QVERIFY(std::abs(view->transform().m11() - 1.0) < 1e-6);
    }
    void runToolbarPresentation() {
        auto* toolbar = window_->findChild<QToolBar*>("toolbar.run"); QVERIFY(toolbar);
        QCOMPARE(toolbar->actions().size(), 6);
        // Real hover must show descriptions even when a button is disabled.
        const auto checkTooltip = [&](QAction* command) {
            auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(command));
            QVERIFY(button);
            window_->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(window_.get()));
            QTest::mouseMove(window_->menuBar(), window_->menuBar()->rect().center());
            QToolTip::hideText();
            QTRY_VERIFY(!QToolTip::isVisible());
            QTest::mouseMove(button, button->rect().center());
            QTRY_VERIFY2_WITH_TIMEOUT(QToolTip::isVisible(), qPrintable(command->text()), 3000);
            QTRY_COMPARE(QToolTip::text(), command->toolTip());
        };
        window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        QVERIFY(!action("action.pause")->isEnabled());
        for (auto* command : toolbar->actions()) {
            auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(command)); QVERIFY(button);
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!command->icon().isNull());
            QVERIFY(!command->toolTip().isEmpty());
            QVERIFY(command->toolTip() != command->text());
            auto* accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
            // Qt omits a dialog-opening ellipsis from the accessible name.
            QCOMPARE(accessible->text(QAccessible::Name), QString(command->text()).remove("...").remove(QChar(0x2026)));
            checkTooltip(command);
        }
        capture(toolbar, "run-toolbar-idle", true);
        // Hover needs a sustained run, without unrelated full-run recording I/O.
        QVERIFY(scene()->addElement({"source", "Constant", {{"value", 1.0}}, {}, {}, "Source"}, {0, 0}));
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QVERIFY(configure("action.run", -1)); QTRY_VERIFY(runner->isRunning());
        QTRY_VERIFY(!action("action.run")->isEnabled());
        checkTooltip(action("action.run"));
        capture(toolbar, "run-toolbar-running", true);
        QTest::mouseClick(toolbar->widgetForAction(action("action.pause")), Qt::LeftButton);
        QTRY_VERIFY(runner->isPaused());
        QTRY_VERIFY(action("action.resume")->isEnabled());
        capture(toolbar, "run-toolbar-paused", true);
        QTest::mouseClick(toolbar->widgetForAction(action("action.resume")), Qt::LeftButton);
        QTRY_VERIFY(!runner->isPaused());
        QTest::mouseClick(toolbar->widgetForAction(action("action.stop")), Qt::LeftButton);
        QTRY_VERIFY(!runner->isRunning());
        QTRY_VERIFY(action("action.run")->isEnabled());
        capture(toolbar, "run-toolbar-stopped", true);
        QTest::mouseClick(toolbar->widgetForAction(action("action.reset")), Qt::LeftButton);
        QCOMPARE(runner->state(), s7studio::RunState::Idle);
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

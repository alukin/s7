// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window.hpp"
#include "help_dialogs.hpp"
#include "education_linear_reference.hpp"
#include "education_assets.hpp"
#include "education_signals_reference.hpp"
#include "education_lti_reference.hpp"
#include "education_spectral_reference.hpp"
#include "education_convolution_reference.hpp"
#include "education_stft_reference.hpp"
#include "education_control_reference.hpp"
#include "education_loop_reference.hpp"
#include "education_compensator_reference.hpp"
#include "education_am_reference.hpp"
#include <tuple>
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/element_ports.hpp"
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
#include "analysis/result_file.hpp"
#include <QElapsedTimer>
#include <QImage>
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
#include "viewers/surface_window.hpp"
#include "viewers/response_window.hpp"
#include "viewers/time_frequency_window.hpp"
#include "viewers/surface_plot.hpp"
#include <s7/builtin_element_registry.hpp>
#include <QtTest>
#include <QAction>
#include <QDialogButtonBox>
#include <QDesktopServices>
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
#include <QRadioButton>
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
#include <QScreen>
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

class BrowserUrlCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl& url) { urls.push_back(url); }
};

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
    // Live views compute frames and axis ranges asynchronously, so one hover
    // can land before the data or range it targets. Hover again until done()
    // holds, recomputing the target and nudging by a pixel so each attempt is
    // a pointer move the plot reacts to.
    template <typename Target, typename Done>
    static bool hoverUntil(QWidget* widget, Target target, Done done, int timeout_ms = 5000) {
        QElapsedTimer timer; timer.start();
        for (int nudge = 0; timer.elapsed() < timeout_ms; nudge ^= 1) {
            QTest::mouseMove(widget, target() + QPoint(nudge, 0));
            QTest::qWait(50);
            if (done()) return true;
        }
        return false;
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
    bool configure(const char* name, double seconds, double frequencyHz = 1000) {
        return dialogAction(name, [seconds, frequencyHz](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* frequency = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !frequency || !buttons) return false;
            duration->setValue(seconds);
            frequency->setValue(frequencyHz);
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
    using TeachingBindings=QMap<QString,std::tuple<std::string,std::size_t,QString>>;
    using TeachingScopes=QMap<QString,QPointer<s7studio::ui::ScopeViewWindow>>;
    void openTeachingGuide(const QString& lab,const QString& slug) {
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front();QSet<QString> links;QPoint point(-1,-1);
        for(int y=8;y<frame->height()-8;y+=3) for(int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if(!url.isEmpty()) links.insert(url.toString());
            if(url.path().contains("/labs/") && point.x()<0) point=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));QVERIFY(view->viewport()->rect().contains(point));QTest::mouseClick(view->viewport(),Qt::LeftButton,{},point);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");QVERIFY(browser && browser->source().path().contains(slug));
        QVERIFY(browser->toPlainText().contains("Predict",Qt::CaseInsensitive));capture(help,lab+"-student-guide",true);help->close();capture(window_.get(),lab+"-diagram",true);
    }
    void openTeachingScopes(const QStringList& ids,TeachingScopes& scopes) {
        scopes.clear();auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
        for(const auto& id:ids) {
            auto* scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope",id));QVERIFY(scope);scopes[id]=scope;
        }
    }
    void runTeachingProfile(double seconds,double rate) {
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);QSignalSpy complete(runner,&s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(dialogAction("action.run",[&](QDialog* dialog) {
            auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");auto* frequency=dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!duration || !frequency || !buttons || duration->value()!=seconds || frequency->value()!=rate) return false;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
        QTRY_VERIFY_WITH_TIMEOUT(!complete.empty(),10000);QVERIFY2(complete.last()[0].toBool(),qPrintable(complete.last()[1].toString()));
        QCOMPARE(runner->completedTicks(),long(seconds*rate));
    }
    void checkTeachingCapture(const TeachingBindings& bindings,const std::function<double(const std::string&,std::size_t,int)>& expected,
                              int count,double rate,const TeachingScopes& scopes,const QString& stem,double displayEnd=0) {
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
        const auto source=runner->recordedSource();QVERIFY(source && source->status().finished && !source->status().incomplete());
        const auto config=runner->activeConfiguration();QVERIFY(config);QCOMPARE(config->probes.size(),std::size_t(bindings.size()));
        for(const auto& probe:config->probes) {
            const auto name=QString::fromStdString(probe.name);QVERIFY(bindings.contains(name));
            const auto& [element,port,unit]=bindings[name];QCOMPARE(probe.element,element);QCOMPARE(probe.port,port);QCOMPARE(QString::fromStdString(probe.unit),unit);
        }
        const auto channels=source->channels();QCOMPARE(channels.size(),bindings.size());
        QMap<s7studio::observation::ChannelId,int> counts;std::uint64_t cursor=0;
        while(true) {
            const auto page=source->read(cursor,4096);QVERIFY(page.error.isEmpty());if(page.samples.isEmpty()) break;
            for(const auto& sample:page.samples) {
                QString name;for(const auto& c:channels) if(c.id==sample.channel) name=c.name;QVERIFY(bindings.contains(name));
                const auto& [id,port,unit]=bindings[name];(void)unit;QCOMPARE(sample.step,qint64(++counts[sample.channel]));
                QVERIFY(std::abs(sample.timeSeconds-sample.step/rate)<1e-12);
                const auto value=expected(id,port,int(sample.step)-1);QVERIFY(std::abs(sample.value-value)<1e-10*std::max(1.,std::abs(value)));
            }
            QVERIFY(page.next>cursor);cursor=page.next;
        }
        QCOMPARE(counts.size(),channels.size());for(auto n:counts) QCOMPARE(n,count);
        for(auto it=scopes.cbegin();it!=scopes.cend();++it) {
            auto* scope=it.value().data();QVERIFY(scope);scope->show();scope->resize(1100,750);
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);while(scope->refreshSource()) {}
            int size=0;QStringList units;
            for(auto b=bindings.cbegin();b!=bindings.cend();++b) if(b.key().startsWith(it.key()+"/")) {++size;if(!units.contains(std::get<2>(b.value()))) units.append(std::get<2>(b.value()));}
            QCOMPARE(units.size(),qsizetype(1));QTRY_COMPARE(scope->recording().series().size(),qsizetype(size));
            for(auto s=scope->recording().series().cbegin();s!=scope->recording().series().cend();++s) {
                QVERIFY(!s->isComparison());QCOMPARE(s->receivedSamples,std::uint64_t(count));
                const auto key=it.key()+"/"+s->channelName;QVERIFY(bindings.contains(key));const auto& [id,port,unit]=bindings[key];
                QCOMPARE(s->channelUnit,unit);QCOMPARE(s->time.size(),qsizetype(count));QCOMPARE(s->value.size(),qsizetype(count));
                for(int n=0;n<count;++n) {
                    QVERIFY(std::abs(s->time[n]-(n+1)/rate)<1e-12);const auto value=expected(id,port,n);
                    QVERIFY(std::abs(s->value[n]-value)<1e-10*std::max(1.,std::abs(value)));
                }
                auto* line=scope->plottedSeries(s.key());QVERIFY(line);QTRY_VERIFY(!line->points().empty());QTRY_COMPARE(line->points().back().x(),count/rate);
                QCOMPARE(line->name(),s->displayName());QVERIFY(line->isVisible());const auto points=line->points();
                QCOMPARE(points.front().x(),1/rate);QVERIFY(points.size()<=count);double previous=0,lo=points.front().y(),hi=lo;
                for(const auto& point:points) {
                    const int n=int(std::llround(point.x()*rate))-1;QVERIFY(n>=0 && n<count);QVERIFY(point.x()>previous);previous=point.x();
                    QCOMPARE(point.x(),s->time[n]);QCOMPARE(point.y(),s->value[n]);lo=std::min(lo,point.y());hi=std::max(hi,point.y());
                }
                QCOMPARE(lo,*std::min_element(s->value.cbegin(),s->value.cend()));QCOMPARE(hi,*std::max_element(s->value.cbegin(),s->value.cend()));
            }
            QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),"value ("+units.front()+")");
            QCOMPARE(scope->plotView()->xMin(),1/rate);QCOMPARE(scope->plotView()->xMax(),count/rate);
            if(displayEnd>0) {
                QCheckBox* full=nullptr;for(auto* box:scope->findChildren<QCheckBox*>()) if(box->text()=="Fit full run") full=box;
                QVERIFY(full);full->setChecked(false);scope->plotView()->setXRange(0,displayEnd);
            }
            capture(scope,stem+"-"+it.key(),true);scope->hide();
        }
    }
    void editTeachingNumber(const std::string& id,const QString& parameter,double value) {
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);auto node=scene()->elementNode(id);QVERIFY(node);editor()->showItem(node.get());view->setFocus();
        QVERIFY(modalWorkflow([&] {QTest::keyClick(view->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>("control."+parameter);auto* buttons=dialog->findChild<QDialogButtonBox*>();
            if(!field || !buttons) return false;field->setText(QString::number(value,'g',17));QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
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
        window_->resize(qEnvironmentVariableIsSet("S7_README_SCREENSHOTS") ? QSize(2048, 1152) : QSize(1100, 750));
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
        search->setText("Siebert");
        matches = 0;
        for (int i = 0; i < contents->count(); ++i) matches += !contents->item(i)->isHidden();
        QVERIFY(matches >= 2);
        search->clear();
        for (int i = 0; i < contents->count(); ++i)
            if (contents->item(i)->text().startsWith("Integrators, delays")) contents->setCurrentRow(i);
        browser->setSource(QUrl("qrc:/s7studio/help/docs/INTEGRATORS.md"), QTextDocument::MarkdownResource);
        if (qEnvironmentVariableIsSet("S7_README_SCREENSHOTS")) help->resize(1120, 1100);
        QVERIFY(browser->toPlainText().contains("BackwardEulerIntegrator"));
        QVERIFY2(browser->toPlainText().contains("frequency warping", Qt::CaseInsensitive),
                 qPrintable(browser->toPlainText().right(4000)));
        capture(help, "integrators", true);
        const auto mapping = browser->document()->find("Exact sampling and numerical s-to-z maps");
        QVERIFY(!mapping.isNull());
        auto position = mapping; position.clearSelection();
        browser->setTextCursor(position);
        browser->ensureCursorVisible();
        browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->value() + browser->cursorRect().top() - 12);
        capture(help, "integrator-mappings", true);
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("INTEGRATOR_REFERENCES.md"))));
        QVERIFY(browser->toPlainText().contains("Problem 8.7"));
        for (const auto* name : {"euler_delay_realizations", "integrator_mappings", "integrator_oscillators",
                                 "basic_symbols", "basic_symbols_compact"}) {
            QImage image(QStringLiteral(":/s7studio/help/docs/images/%1.png").arg(QString::fromUtf8(name)));
            QVERIFY2(!image.isNull(), name);
        }
        for (int i = 0; i < contents->count(); ++i)
            if (contents->item(i)->text() == "Basic block symbols") contents->setCurrentRow(i);
        browser->setSource(QUrl("qrc:/s7studio/help/docs/DIAGRAM_SYMBOLS.md"), QTextDocument::MarkdownResource);
        QVERIFY(browser->toPlainText().contains("current input"));
        QVERIFY(browser->toPlainText().contains("R/(1−R)"));
        QVERIFY(browser->toPlainText().contains("Editing the blocks"));
        capture(help, "basic-symbols", true);
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
        for (const auto* name : {"ChatGPT", "Codex", "Claude Code", "GitHub Copilot"})
            QVERIFY(credits->toPlainText().contains(QString::fromUtf8(name)));
        QVERIFY(build->toPlainText().contains("S7 Studio 0.1.2"));
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
        QTest::addColumn<bool>("byLibrary");
        QTest::newRow("builtin-unfiltered") << QString("Gain") << false << false << 0 << false;
        QTest::newRow("builtin-filtered-zoom-pan") << QString("Gain") << true << false << 3 << false;
        QTest::newRow("schema-root") << QString("PIController") << true << false << -2 << false;
        QTest::newRow("plugin-nested") << QString("WhiteNoiseGenerator") << true << true << 2 << false;
        QTest::newRow("schema-nested") << QString("FirstOrderLag") << false << true << -1 << false;
        QTest::newRow("scope-root") << QString("StudioObserver:Scope") << true << false << 1 << false;
        QTest::newRow("live-nested") << QString("StudioObserver:LiveOscilloscope") << true << true << 0 << false;
        QTest::newRow("phase-root") << QString("StudioObserver:PhasePortrait") << true << false << -1 << false;
        QTest::newRow("trajectory-nested") << QString("StudioObserver:PhaseTrajectory3D") << true << true << 1 << false;
        QTest::newRow("library-builtin") << QString("Gain") << false << false << 0 << true;
        QTest::newRow("library-schema") << QString("PIDController") << true << true << 0 << true;
        QTest::newRow("library-plugin") << QString("WhiteNoiseGenerator") << true << false << 0 << true;
        QTest::newRow("library-observer") << QString("StudioObserver:Scope") << true << false << 0 << true;
    }
    void libraryDragPlacement() {
        using namespace s7studio;
        if (QStandardPaths::findExecutable("xdotool").isEmpty()) QSKIP("xdotool is required for native X11 drag/drop checks");
        QFETCH(QString, type); QFETCH(bool, filtered); QFETCH(bool, nested); QFETCH(int, zoomSteps); QFETCH(bool, byLibrary);
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
        auto* grouping = window_->findChild<QRadioButton*>(byLibrary ? "elements.byLibrary" : "elements.byCategory"); QVERIFY(grouping);
        QTest::mouseClick(grouping, Qt::LeftButton);
        search->setText(filtered ? type.section(':', -1) : QString());
        auto* item = libraryItem(tree, type); QVERIFY(item && !item->isHidden());
        QCoreApplication::processEvents();
        auto* view = target->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto canvasWidth = view->viewport()->width();
        tree->setCurrentItem(item); tree->scrollToItem(item);
        QCoreApplication::processEvents();
        QCOMPARE(view->viewport()->width(), canvasWidth);
        if (type == "Gain" && !filtered) capture(window_->findChild<QDockWidget*>("toolWindow.elementLibrary"), byLibrary ? "picker-by-library" : "picker-by-category", true);
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
    void frameElementsLibraryAndPorts() {
        using namespace s7studio;
        auto* tree = window_->findChild<QTreeWidget*>("elements.tree"); QVERIFY(tree);
        auto* search = window_->findChild<QLineEdit*>("elements.search"); QVERIFY(search); search->setText("NetFrame");
        int index = 0;
        for (const auto* type : {"NetFrameSink", "NetFrameSource"}) {
            const bool sink = std::string_view(type) == "NetFrameSink";
            QVERIFY(libraryItem(tree, type));
            QCOMPARE(libraryItem(tree, type)->text(1), sink ? QString("2→0") : QString("0→2"));
            QVERIFY(editor()->addLibraryElement(type, QPointF(80 + 240 * index++,80)));
            const auto ports = diagram::elementPorts(type, {}, {});
            const auto& names = std::string_view(type) == "NetFrameSink" ? ports.inputs : ports.outputs;
            QCOMPARE(names.size(), std::size_t{2}); QCOMPARE(names[0], QString("L")); QCOMPARE(names[1], QString("R"));
            const auto custom = diagram::elementPorts(type, {{"channels", boost::json::array{"front","rear","sub"}}}, {});
            const auto& channels = std::string_view(type) == "NetFrameSink" ? custom.inputs : custom.outputs;
            QCOMPARE(channels.size(), std::size_t{3}); QCOMPARE(channels[2], QString("sub"));
            auto node = scene()->elementNode(sink ? "netframesink_1" : "netframesource_1"); QVERIFY(node);
            auto configured = node->element();
            configured.params["stream_id"] = "197488c5-1100-4000-8000-000000000003";
            configured.params["channels"] = boost::json::array{"front","rear","sub"};
            node->setElement(configured);
            QCOMPARE(sink ? node->inputs().size() : node->outputs().size(), std::size_t{3});
            const auto documentation = ui::elementDocumentation(s7::ElementRegistry::instance(), type);
            for (const auto* field : {"frame_rate", "channels", "encoding", "lz4", "zstd", "latches"})
                QVERIFY2(documentation.contains(field), field);
        }
        const auto saved = window_->currentDocument();
        const auto loaded = diagram::DiagramDocument::fromJson(saved);
        QVERIFY(loaded.toJson() == saved);
        capture(window_.get(), "frame-elements-library", true);
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
                auto* parameter = dialog->findChild<QWidget*>("control.value");
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
            if (tabs) tabs->setCurrentWidget(text);
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
            auto* initial = dialog->findChild<QLineEdit*>("control.initial_position");
            auto* tabs = dialog->findChild<QTabWidget*>("element.parameterTabs");
            auto* error = dialog->findChild<QLabel*>("element.parameterError");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!initial || !tabs || !error || !buttons) return false;
            if (tabs->currentIndex() != 0 || !initial->isVisible() || initial->accessibleName() != "initial_position") return false;
            initial->setFocus(); QTest::keyClick(initial, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(initial, "invalid");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            if (!dialog->isVisible() || error->text().isEmpty()) return false;
            capture(dialog, "parameter-error");
            initial->setText("0.25");
            tabs->setCurrentIndex(1);
            auto* formValue = tabs->currentWidget()->findChild<QLineEdit*>("parameter.initial_position");
            if (!formValue || formValue->text().toDouble() != .25) return false;
            tabs->setCurrentIndex(0);
            auto* control = tabs->currentWidget()->findChild<QLineEdit*>("control.initial_position");
            auto* dial = tabs->currentWidget()->findChild<QDial*>("control.dial.initial_position");
            if (!control || !dial || !dial->isVisible() || !dial->isEnabled() || control->text().toDouble() != .25) return false;
            QTest::keyClick(dial, Qt::Key_Right);
            if (control->text().toDouble() <= .25) return false;
            control->setText("0.5");
            capture(dialog, "integrator-controls");
            auto* json = dialog->findChild<QPlainTextEdit*>("element.parameters");
            if (!json) return false;
            tabs->setCurrentWidget(json);
            if (!json->toPlainText().contains("5E-1")) return false;
            json->setPlainText("{\"initial_position\":0.25}");
            tabs->setCurrentIndex(0);
            if (initial->text().toDouble() != .25) return false;
            QTest::keyClick(initial, Qt::Key_Tab);
            if (QApplication::focusWidget() == initial) return false;
            checked = true;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QVERIFY(checked); QCOMPARE(node->element().params.at("initial_position").to_number<double>(), .25);
        view->setFocus();
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F4); }, [](QDialog* dialog) {
            auto* initial = dialog->findChild<QLineEdit*>("control.initial_position");
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
        auto* advanced = parameters.findChild<QTabWidget*>("element.parameterTabs");
        QCOMPARE(advanced->currentIndex(), 0);
        QVERIFY(parameters.readParameters(values, error));
        QCOMPARE(values.at("seed").as_uint64(), std::uint64_t(18446744073709551615ULL));
        QVERIFY(!parameters.findChild<QDial*>("control.dial.seed"));
        parameters.findChild<QLineEdit*>("control.seed")->setText("-1");
        QVERIFY(!parameters.readParameters(values, error)); QVERIFY(error.contains("at least"));
        parameters.findChild<QLineEdit*>("control.seed")->setText("1");
        parameters.findChild<QComboBox*>("control.mode")->setCurrentText("b");
        parameters.findChild<QComboBox*>("control.enabled")->setCurrentText("true");
        QVERIFY(parameters.readParameters(values, error));
        QCOMPARE(values.at("mode").as_string(), boost::json::string("b")); QVERIFY(values.at("enabled").as_bool());
        auto* json = parameters.findChild<QPlainTextEdit*>("element.parameters");
        advanced->setCurrentIndex(1);
        QCOMPARE(parameters.findChild<QLineEdit*>("parameter.seed")->text(), QStringLiteral("1"));
        advanced->setCurrentWidget(json);
        json->setPlainText("{");
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
        QCOMPARE(invalid.findChild<QTabWidget*>("element.parameterTabs")->currentWidget(),
                 invalid.findChild<QPlainTextEdit*>("element.parameters"));
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
    void scopeHoverShowsStoredSample() {
        s7studio::ui::ScopeViewWindow viewer(QStringLiteral("hover-check"));
        for (int i = 0; i <= 100; ++i) viewer.appendSample(i * .01, .5);
        viewer.show(); viewer.raise(); viewer.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&viewer));
        auto* plot = viewer.plotView();
        auto* quick = plot->quickWidget(); QVERIFY(quick);
        QTRY_COMPARE(quick->status(), QQuickWidget::Ready);
        QMetaObject::invokeMethod(&viewer, "refreshPlot", Qt::DirectConnection);
        auto* graph = quick->rootObject()->findChild<QQuickItem*>("viewer.graphs"); QVERIFY(graph);
        QTRY_VERIFY(graph->property("plotArea").toRectF().width() > 0);
        const auto area = graph->property("plotArea").toRectF();
        const QPointF onTrace(area.left() + (.5 - plot->xMin()) / (plot->xMax() - plot->xMin()) * area.width(),
            area.top() + (plot->yMax() - .5) / (plot->yMax() - plot->yMin()) * area.height());
        QTest::mouseMove(quick, graph->mapToScene(onTrace).toPoint());
        // Values appear in the readout and in the value box beside the pointer.
        auto* tip = quick->rootObject()->findChild<QQuickItem*>("viewer.hoverTip"); QVERIFY(tip);
        auto* readout = viewer.findChild<QLabel*>(QStringLiteral("scope.hoverReadout")); QVERIFY(readout);
        QTRY_VERIFY(readout->text().contains(QStringLiteral("hover-check = 0.5")));
        QTRY_VERIFY(tip->isVisible());
        QVERIFY(plot->bridge()->hoverTip.front().toMap()["text"].toString().contains(QStringLiteral("hover-check = 0.5")));
        QVERIFY(plot->toolTip().isEmpty());
        QCOMPARE(plot->bridge()->hoverMarkers.size(), 1);
        QVERIFY(plot->bridge()->cursorVisible);
        QTest::mouseMove(quick, QPoint(2, 2));
        QTRY_VERIFY(!tip->isVisible());
        QVERIFY(plot->bridge()->hoverMarkers.isEmpty());
        QVERIFY(readout->text().isEmpty());
    }

    void scopeAnalysisWorkspace() {
        using namespace s7studio;
        auto source = std::make_shared<observation::ObservationSource>(42);
        source->setProvenance({{"dt_seconds", 1./1024},
            {"rate", QJsonObject{{"numerator", "1024"}, {"denominator", "1"}}}});
        const auto a = source->channel(QStringLiteral("analysis/a"));
        const auto b = source->channel(QStringLiteral("analysis/b"));
        const auto c = source->channel(QStringLiteral("analysis/c"));
        for (int step = 0; step < 1024; ++step) {
            const double time = static_cast<double>(step)/1024;
            const double phase = 2*std::numbers::pi*25*time;
            QVERIFY(source->append({a, step, time, std::sin(phase)}));
            QVERIFY(source->append({b, step, time, std::cos(phase)}));
            QVERIFY(source->append({c, step, time, .5*std::sin(3*phase)}));
        }
        QVERIFY(source->finish());
        ui::ScopeViewWindow viewer(QStringLiteral("analysis"));
        viewer.attachSource(source, {{QStringLiteral("a"), a}, {QStringLiteral("b"), b}, {QStringLiteral("c"), c}});
        while (viewer.refreshSource()) {}
        viewer.resize(1250, 720);
        viewer.show(); viewer.raise(); viewer.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&viewer));
        auto* tabs = viewer.findChild<QTabWidget*>(QStringLiteral("scope.analysisTabs")); QVERIFY(tabs);
        auto* save = viewer.findChild<QPushButton*>(QStringLiteral("scope.saveRun")); QVERIFY(save);
        QVERIFY(save->width() <= 30 && !save->icon().isNull() && !save->toolTip().isEmpty());
        QVERIFY(tabs->y() < viewer.height()/4);
        auto* full = viewer.findChild<QPushButton*>(QStringLiteral("scope.fullAnalysisRange")); QVERIFY(full);
        auto* analyze = viewer.findChild<QPushButton*>(QStringLiteral("scope.analyze")); QVERIFY(analyze);
        QVERIFY(!full->icon().isNull() && !full->toolTip().isEmpty());
        auto* intervalStart = viewer.findChild<QDoubleSpinBox*>(QStringLiteral("scope.analysisStart")); QVERIFY(intervalStart);
        auto* intervalEnd = viewer.findChild<QDoubleSpinBox*>(QStringLiteral("scope.analysisEnd")); QVERIFY(intervalEnd);
        QCOMPARE(intervalStart->value(), 0.);
        QVERIFY(std::abs(intervalEnd->value() - (1023./1024)/4) < 1e-12);
        QCOMPARE(intervalEnd->findChild<QLabel*>("control.numberReadout")->text(),
            QString::number(intervalEnd->value(), 'g', 5) + QStringLiteral(" s"));
        intervalEnd->setValue(.5);
        viewer.refreshSource();
        QCOMPARE(intervalEnd->value(), .5);
        full->click(); analyze->click();
        auto* status = viewer.findChild<QLabel*>(QStringLiteral("scope.analysisStatus")); QVERIFY(status);
        QTRY_VERIFY(status->text().contains(QStringLiteral("1024 exact positions")));
        tabs->setCurrentIndex(1);
        auto* spectrum = viewer.findChild<viewers::SpectrumView*>(); QVERIFY(spectrum);
        QTRY_VERIFY(spectrum->result().has_value());
        QCOMPARE(spectrum->result()->state, analysis::SpectrumResult::State::Ready);
        QTRY_COMPARE(spectrum->result()->traces.size(), std::size_t(3));
        auto* allInputs = spectrum->findChild<QCheckBox*>(QStringLiteral("spectrum.allInputs"));
        QVERIFY(allInputs && allInputs->isChecked());
        QVERIFY(!spectrum->plotView()->bridge()->instrumentTheme);
        QVERIFY(std::abs(spectrum->result()->traces[0].values[25]-1.) < 1e-10);
        QVERIFY(std::abs(spectrum->result()->traces[2].values[75]-.5) < 1e-10);
        auto* frequencyRange = spectrum->findChild<QComboBox*>(QStringLiteral("spectrum.frequencyRange")); QVERIFY(frequencyRange);
        QCOMPARE(frequencyRange->currentIndex(), 0);
        QVERIFY(spectrum->plotView()->xMax() > 75 && spectrum->plotView()->xMax() < 150);
        frequencyRange->setCurrentIndex(1);
        QCOMPARE(spectrum->plotView()->xMax(), 512.);
        frequencyRange->setCurrentIndex(0);
        QVERIFY(spectrum->plotView()->series().size() >= 6);
        for (int i = 0; i < 3; ++i) {
            auto* line = spectrum->plotView()->series()[i == 0 ? 0 : i*2];
            QVERIFY(line->isVisible() && !line->points().isEmpty());
            QVERIFY(line->name().startsWith(QString(QChar('a' + i))));
        }
        allInputs->setChecked(false);
        QTRY_VERIFY(spectrum->result().has_value() && spectrum->result()->state == analysis::SpectrumResult::State::Ready
            && spectrum->result()->traces.empty());
        allInputs->setChecked(true);
        QTRY_VERIFY(spectrum->result().has_value() && spectrum->result()->traces.size() == 3);
        QVERIFY(spectrum->controlPanel()->width() <= 275);
        QVERIFY(spectrum->plotView()->width() >= tabs->width()*2/3);
        capture(&viewer, "scope-analysis-spectrum", true);
        auto* toggleControls = viewer.findChild<QToolButton*>(QStringLiteral("scope.toggleAnalysisControls"));
        QVERIFY(toggleControls && toggleControls->isVisible() && !toggleControls->toolTip().isEmpty());
        const int plotWidth = spectrum->plotView()->width();
        toggleControls->click();
        QTRY_VERIFY(!spectrum->controlPanel()->isVisible());
        QTRY_VERIFY(spectrum->plotView()->width() > plotWidth + 200);
        capture(&viewer, "scope-analysis-spectrum-full-width", true);
        toggleControls->click();
        QTRY_VERIFY(spectrum->controlPanel()->isVisible());
        tabs->setCurrentIndex(2);
        auto* correlation = tabs->widget(2)->findChild<viewers::PairAnalysisView*>();
        if (!correlation) correlation = qobject_cast<viewers::PairAnalysisView*>(tabs->widget(2));
        QVERIFY(correlation);
        QTRY_VERIFY(correlation->result().has_value());
        QCOMPARE(correlation->result()->state, analysis::PairResult::State::Ready);
        QVERIFY(!correlation->plotView()->bridge()->instrumentTheme);
        QVERIFY(correlation->controlPanel()->width() <= 275);
        QVERIFY(correlation->plotView()->width() >= tabs->width()*2/3);
        capture(&viewer, "scope-analysis-correlation", true);
        auto* correlationMode = correlation->findChild<QComboBox*>(QStringLiteral("pair.mode")); QVERIFY(correlationMode);
        correlationMode->setCurrentIndex(1);
        QTRY_VERIFY(correlation->result().has_value());
        QCOMPARE(correlation->result()->state, analysis::PairResult::State::Ready);
        QCOMPARE(correlation->result()->lags[correlation->result()->peakIndex], 0);
        capture(&viewer, "scope-analysis-autocorrelation", true);
        tabs->setCurrentIndex(3);
        auto* trajectory = qobject_cast<viewers::PairAnalysisView*>(tabs->widget(3)); QVERIFY(trajectory);
        QTRY_VERIFY(trajectory->result().has_value());
        QCOMPARE(trajectory->result()->state, analysis::PairResult::State::Ready);
        QVERIFY(!trajectory->plotView()->bridge()->instrumentTheme);
        QVERIFY(trajectory->controlPanel()->width() <= 275);
        QVERIFY(trajectory->plotView()->width() >= tabs->width()*2/3);
        capture(&viewer, "scope-analysis-trajectory", true);
        tabs->setCurrentIndex(0);
        capture(&viewer, "scope-analysis-time", true);
        auto* setting = window_->findChild<QAction*>(QStringLiteral("settings.scopeAnalysisLimit")); QVERIFY(setting);
        QVERIFY(modalWorkflow([&] { setting->trigger(); }, [](QDialog* dialog) {
            auto* limit = dialog->findChild<QComboBox*>(QStringLiteral("scope.analysisSampleLimit"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!limit || !buttons) return false;
            limit->setCurrentIndex(limit->findData(4096));
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QCOMPARE(QSettings{}.value(QStringLiteral("scope/analysisSampleLimit")).toInt(), 4096);
        QSettings{}.setValue(QStringLiteral("scope/analysisSampleLimit"), 64);
        analyze->click();
        QTRY_VERIFY(status->text().contains(QStringLiteral("64 exact positions"))
            && status->text().contains(QStringLiteral("newest samples")));
        tabs->setCurrentIndex(1);
        QTRY_VERIFY(spectrum->result().has_value());
        QCOMPARE(spectrum->result()->firstStep, std::int64_t(960));
        viewer.resize(900, 520);
        QTRY_VERIFY(spectrum->plotView()->width() >= 550);
        capture(&viewer, "scope-analysis-compact-900", true);
        auto nextRun = std::make_shared<observation::ObservationSource>(43);
        const auto nextChannel = nextRun->channel(QStringLiteral("analysis/a"));
        for (int step = 0; step <= 1024; ++step)
            QVERIFY(nextRun->append({nextChannel, step, step/1024., std::sin(2*std::numbers::pi*32*step/1024.)}));
        QVERIFY(nextRun->finish());
        viewer.attachSource(nextRun, {{QStringLiteral("a"), nextChannel}});
        while (viewer.refreshSource()) {}
        QCOMPARE(intervalStart->value(), 0.);
        QCOMPARE(intervalEnd->value(), .25);
    }

    void spectrumReferenceExamples_data() {
        QTest::addColumn<QString>("example");
        QTest::newRow("three-bin-tones") << QStringLiteral("three_bin_tones");
        QTest::newRow("two-tone-sum") << QStringLiteral("two_tone_sum");
        QTest::newRow("off-bin-tone") << QStringLiteral("off_bin_tone");
    }
    void spectrumReferenceExamples() {
        QFETCH(QString, example);
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/spectrum/" + example + ".s7diagram"));
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope", "scope")); QVERIFY(scope);
        auto* live = dynamic_cast<s7studio::ui::LiveOscilloscopeWindow*>(manager->openObserver("LiveOscilloscope", "live")); QVERIFY(live);
        scope->resize(1250, 720); live->resize(1250, 720);
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", 4, 1024));
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
        QVERIFY(completed.last()[0].toBool());
        QTRY_VERIFY(scope->source() && scope->source()->status().finished);
        while (scope->refreshSource()) {}
        auto* start = scope->findChild<QDoubleSpinBox*>("scope.analysisStart"); QVERIFY(start);
        auto* end = scope->findChild<QDoubleSpinBox*>("scope.analysisEnd"); QVERIFY(end);
        QCOMPARE(start->value(), 0.);
        QTRY_COMPARE(end->value(), 1.);
        QCOMPARE(end->findChild<QLabel*>("control.numberReadout")->text(),
            QString::number(end->value(), 'g', 5) + QStringLiteral(" s"));
        auto* analyze = scope->findChild<QPushButton*>("scope.analyze"); QVERIFY(analyze);
        analyze->click();
        auto* tabs = scope->findChild<QTabWidget*>("scope.analysisTabs"); QVERIFY(tabs);
        tabs->setCurrentIndex(1);
        auto* spectrum = scope->findChild<s7studio::viewers::SpectrumView*>(); QVERIFY(spectrum);
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == s7studio::analysis::SpectrumResult::State::Ready);
        const auto& result = *spectrum->result();
        QCOMPARE(result.size, std::size_t(1024));
        QVERIFY(std::abs(result.binSpacing-1.) < 1e-12);
        const auto near = [&](std::size_t channel, std::size_t bin, double expected, double tolerance) {
            QVERIFY(channel < result.traces.size());
            QVERIFY(std::abs(result.traces[channel].values[bin]-expected) < tolerance);
        };
        if (example == "three_bin_tones") {
            QCOMPARE(result.traces.size(), std::size_t(3));
            near(0, 32, 1., 1e-9); near(1, 96, .5, 1e-9); near(2, 160, .25, 1e-9);
            QVERIFY(spectrum->plotView()->xMax() > 160 && spectrum->plotView()->xMax() < 220);
        } else if (example == "two_tone_sum") {
            QCOMPARE(result.traces.size(), std::size_t(1));
            near(0, 0, .1, 1e-9); near(0, 32, 1., 1e-9); near(0, 96, .25, 1e-9);
            QVERIFY(spectrum->plotView()->xMax() > 96 && spectrum->plotView()->xMax() < 140);
        } else {
            QCOMPARE(result.traces.size(), std::size_t(1));
            near(0, 32, .8488, .001); near(0, 33, .8488, .001);
            QVERIFY(spectrum->plotView()->xMax() > 33 && spectrum->plotView()->xMax() < 70);
        }
        auto* range = spectrum->findChild<QComboBox*>("spectrum.frequencyRange"); QVERIFY(range);
        range->setCurrentIndex(1);
        QCOMPARE(spectrum->plotView()->xMax(), 512.);
        range->setCurrentIndex(0);
        scope->raise(); scope->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(scope));
        capture(scope, "spectrum-example-" + example, true);
        QTRY_VERIFY(!live->displayedFrame().steps.empty());
        const auto& frame = live->displayedFrame();
        QCOMPARE(frame.channels.size(), result.traces.size());
        QCOMPARE(frame.rateNumerator, std::uint64_t(1024));
        for (qsizetype channel = 0; channel < frame.channels.size(); ++channel) {
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -minimum;
            double sum = 0;
            for (std::size_t step = 0; step < frame.steps.size(); ++step) {
                const auto value = frame.values[step*static_cast<std::size_t>(frame.channels.size())+static_cast<std::size_t>(channel)];
                minimum = std::min(minimum, value); maximum = std::max(maximum, value); sum += value;
            }
            if (example == "three_bin_tones") {
                const double amplitude = channel == 0 ? 1. : channel == 1 ? .5 : .25;
                QVERIFY(std::abs(maximum-amplitude) < 1e-9 && std::abs(minimum+amplitude) < 1e-9);
            } else if (example == "two_tone_sum") {
                QVERIFY(std::abs(sum/frame.steps.size()-.1) < 1e-9);
            } else {
                QVERIFY(maximum > .999 && minimum < -.999);
            }
        }
        auto* timeScale = live->findChild<QDoubleSpinBox*>("live.timeScale"); QVERIFY(timeScale);
        timeScale->setValue(.02);
        QTRY_VERIFY(std::abs((live->plotView()->xMax()-live->plotView()->xMin())-.2) < .002);
        if (example == "three_bin_tones") QVERIFY(live->plotView()->yMin() < -1. && live->plotView()->yMax() > 1.);
        live->raise(); live->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(live));
        auto* liveControls = live->findChild<QWidget*>("live.controls"); QVERIFY(liveControls);
        auto* freeze = live->findChild<QPushButton*>("live.freeze"); QVERIFY(freeze);
        auto* follow = live->findChild<QPushButton*>("live.follow"); QVERIFY(follow);
        auto* toggle = live->findChild<QToolButton*>("live.controlsToggle"); QVERIFY(toggle);
        QVERIFY2(liveControls->width() <= 285 && live->plotView()->width() >= 900,
            qPrintable(QStringLiteral("controls=%1 plot=%2 window=%3")
                .arg(liveControls->width()).arg(live->plotView()->width()).arg(live->width())));
        QVERIFY(freeze->width() <= 30 && follow->width() <= 30 && toggle->width() <= 30);
        QVERIFY(!freeze->toolTip().isEmpty() && !follow->toolTip().isEmpty() && !toggle->toolTip().isEmpty());
        QTest::mouseMove(freeze, freeze->rect().center()); QCoreApplication::processEvents();
        capture(live, "live-spectrum-example-" + example, true);
        if (example == "three_bin_tones") {
            live->resize(900, 520);
            QTRY_VERIFY(live->plotView()->width() >= 580 && liveControls->width() <= 285);
            capture(live, "live-spectrum-example-compact-900", true);
        }
        auto* liveTabs = live->findChild<QTabWidget*>("live.views"); QVERIFY(liveTabs);
        liveTabs->setCurrentIndex(1);
        auto* liveSpectrum = live->findChild<s7studio::viewers::SpectrumView*>(); QVERIFY(liveSpectrum);
        QTRY_VERIFY(liveSpectrum->result() && liveSpectrum->result()->state == s7studio::analysis::SpectrumResult::State::Ready);
        QCOMPARE(liveSpectrum->result()->size, std::size_t(4096));
        QVERIFY(std::abs(liveSpectrum->result()->binSpacing-.25) < 1e-12);
        auto* liveRange = liveSpectrum->controlPanel()->findChild<QComboBox*>("spectrum.frequencyRange"); QVERIFY(liveRange);
        QCOMPARE(liveRange->currentIndex(), 0);
        QVERIFY(liveSpectrum->plotView()->xMax() < 512.);
        liveRange->setCurrentIndex(1);
        QCOMPARE(liveSpectrum->plotView()->xMax(), 512.);
        liveRange->setCurrentIndex(0);
        if (example == "off_bin_tone") {
            QVERIFY(std::abs(liveSpectrum->result()->values[130]-1.) < 1e-9);
            QVERIFY(liveSpectrum->plotView()->xMax() > 32.5 && liveSpectrum->plotView()->xMax() < 70);
        } else {
            QVERIFY(std::abs(liveSpectrum->result()->values[128]-1.) < 1e-9);
            if (example == "three_bin_tones") {
                auto* channel = liveSpectrum->controlPanel()->findChild<QComboBox*>("spectrum.channel"); QVERIFY(channel);
                channel->setCurrentIndex(1);
                QTRY_VERIFY(liveSpectrum->result() && liveSpectrum->result()->state == s7studio::analysis::SpectrumResult::State::Ready);
                QVERIFY(std::abs(liveSpectrum->result()->values[384]-.5) < 1e-9);
                channel->setCurrentIndex(2);
                QTRY_VERIFY(liveSpectrum->result() && liveSpectrum->result()->state == s7studio::analysis::SpectrumResult::State::Ready);
                QVERIFY(std::abs(liveSpectrum->result()->values[640]-.25) < 1e-9);
                channel->setCurrentIndex(0);
                QTRY_VERIFY(liveSpectrum->result() && liveSpectrum->result()->state == s7studio::analysis::SpectrumResult::State::Ready);
            } else {
                QVERIFY(std::abs(liveSpectrum->result()->values[384]-.25) < 1e-9);
                QVERIFY(std::abs(liveSpectrum->result()->values[0]-.1) < 1e-9);
            }
        }
        QTest::mouseMove(freeze, freeze->rect().center()); QCoreApplication::processEvents();
        capture(live, "live-fft-example-" + example, true);
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
        // Without a window manager another top-level window can end up stacked
        // over the viewer, which then never sees the pointer; raise it first.
        viewer->raise(); viewer->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(viewer));
        QTRY_COMPARE(QApplication::topLevelAt(quick->mapToGlobal(center)), viewer);
        QVERIFY(hoverUntil(quick, [center] { return center; }, [&] { return viewer->plotView()->bridge()->cursorVisible; }));
        auto* readout = viewer->findChild<QLabel*>("live.readout"); QVERIFY(readout);
        QTRY_VERIFY(readout->text().contains("Stored sample")); QVERIFY(readout->text().contains("step"));
        const auto index = viewer->displayedFrame().nearest(viewer->plotView()->bridge()->cursorX); QVERIFY(index);
        QVERIFY(readout->text().contains(QString::number(viewer->displayedFrame().values[*index], 'g', 5)));
        capture(viewer, "live-oscilloscope-hover");
        // Pointer on the trace: a ring marks the nearest stored sample. The frame
        // and y range can still change after the time-scale edit, so each attempt
        // re-reads them instead of holding a reference to one frame.
        auto* view = viewer->plotView();
        double hoveredTime = 0;
        const auto onTrace = [&] {
            const auto& shown = viewer->displayedFrame();
            const auto sample = *shown.nearest((view->xMin()+view->xMax())/2);
            hoveredTime = shown.plottedTime(sample);
            return graph->mapToScene(QPointF(area.left()+(hoveredTime-view->xMin())/(view->xMax()-view->xMin())*area.width(),
                area.top()+(view->yMax()-shown.values[sample*static_cast<std::size_t>(shown.channels.size())])/(view->yMax()-view->yMin())*area.height())).toPoint();
        };
        auto* tip = quick->rootObject()->findChild<QQuickItem*>("viewer.hoverTip"); QVERIFY(tip);
        QVERIFY(hoverUntil(quick, onTrace, [&] { return view->bridge()->hoverMarkers.size() == 1 && tip->isVisible(); }));
        QVERIFY(view->bridge()->hoverTip.last().toMap()["text"].toString().startsWith(viewer->displayedFrame().channels.front().name + " = "));
        // The one-pixel nudge may pick a neighbouring sample.
        QVERIFY(std::abs(view->bridge()->hoverMarkers.front().toMap()["x"].toDouble() - hoveredTime)
                <= 2*(view->xMax()-view->xMin())/area.width());
        capture(viewer, "live-oscilloscope-hover-tip", true);
        QTest::mouseMove(quick, center);
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
        // The spectrum may finish computing after the first hover.
        QVERIFY(hoverUntil(quick, [position] { return position; }, [&] {
            return spectrum->controlPanel()->findChild<QLabel*>("spectrum.readout")->text().contains("Computed spectrum"); }));
        QVERIFY(spectrum->controlPanel()->findChild<QLabel*>("spectrum.readout")->text().contains("100 Hz baseband"));
        const auto spectrumGeometry = spectrum->plotView()->geometry();
        spectrum->inspect(-1); QCoreApplication::processEvents();
        spectrum->inspect(100); QCoreApplication::processEvents();
        QCOMPARE(spectrum->plotView()->geometry(), spectrumGeometry);
        QVERIFY(spectrum->controlPanel()->findChild<QScrollArea*>("spectrum.readoutScroll")->height() >
                spectrum->controlPanel()->findChild<QScrollArea*>("spectrum.statusScroll")->height()*2);
        capture(viewer, "live-spectrum-am-carrier", true);
        // Follow leaves the zoom and refits the significant peaks around the carrier.
        viewer->findChild<QPushButton*>("live.follow")->click();
        QTRY_VERIFY(spectrum->plotView()->xMin() < 50 && spectrum->plotView()->xMax() > 100 && spectrum->plotView()->xMax() != 150);
        QTest::mouseMove(tabs->tabBar(), tabs->tabBar()->tabRect(1).center());
        auto* size = spectrum->controlPanel()->findChild<QComboBox*>("spectrum.size"); QVERIFY(size); size->setCurrentIndex(size->findData(65536));
        QTRY_VERIFY(spectrum->result() && spectrum->result()->state == analysis::SpectrumResult::State::Unavailable);
        QVERIFY(spectrum->result()->message.contains("Run finished"));
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
        for (int i = 0; i < 8; ++i) {
            auto* selector = viewer.findChild<QToolButton*>(QStringLiteral("live.channel.%1.select").arg(i)); QVERIFY(selector);
            const auto right = selector->mapTo(scroll->widget(), QPoint(selector->width()-1, 0)).x();
            QVERIFY2(right < scroll->widget()->width(), qPrintable(selector->objectName()));
        }
        auto* timeFine = viewer.findChild<QToolButton*>("live.timeScale.fine"); QVERIFY(timeFine);
        QVERIFY(timeFine->mapTo(scroll->widget(), QPoint(timeFine->width()-1, 0)).x() < scroll->widget()->width());
        QTest::mouseClick(selected, Qt::LeftButton); QVERIFY(selected->isChecked());
        auto* gain = viewer.findChild<QDoubleSpinBox*>("live.channel.7.gain"); auto* dial = viewer.findChild<QDial*>("live.channel.7.scale.dial"); QVERIFY(gain && dial && dial->isVisible());
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
        // Expanding a section lays it out on a later event-loop pass.
        auto* falling = viewer.findChild<QToolButton*>("trigger.edge.1"); QTRY_VERIFY(falling->height() > 0); scroll->ensureWidgetVisible(falling); QTest::mouseClick(falling, Qt::LeftButton); QVERIFY(!viewer.triggerSettings().rising);
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
        auto* spectrumSize=spectrum->controlPanel()->findChild<QComboBox*>("spectrum.size");QVERIFY(spectrumSize);
        spectrumSize->setCurrentIndex(spectrumSize->findData(64));
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
    void helpExternalBrowserLinks() {
        BrowserUrlCapture captureUrl;
        QDesktopServices::setUrlHandler("https", &captureUrl, "open");
        QDesktopServices::setUrlHandler("http", &captureUrl, "open");
        const auto restore = qScopeGuard([] {
            QDesktopServices::unsetUrlHandler("https"); QDesktopServices::unsetUrlHandler("http");
        });
        action("action.help")->trigger();
        auto* help = window_->findChild<QDialog*>("dialog.help"); QVERIFY(help);
        auto* browser = help->findChild<QTextBrowser*>("help.browser"); QVERIFY(browser);
        browser->setHtml("<p><a href='https://ocw.mit.edu/'>MIT reading</a></p>"
                         "<p><a href='http://example.org/reading?a=1&b=2#lesson'>HTTP reading</a></p>");
        const auto source = browser->source();
        const auto linkPoint = [&](const QString& label) {
            auto cursor = browser->document()->find(label);
            cursor.setPosition(cursor.selectionStart()+2);
            browser->setTextCursor(cursor); browser->ensureCursorVisible();
            return browser->cursorRect(cursor).center();
        };
        QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, linkPoint("MIT reading"));
        QCOMPARE(captureUrl.urls, QList<QUrl>{QUrl("https://ocw.mit.edu/")});
        QCOMPARE(browser->source(), source);
        QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, linkPoint("HTTP reading"));
        QCOMPARE(captureUrl.urls.last(), QUrl("http://example.org/reading?a=1&b=2#lesson"));
        QCOMPARE(browser->source(), source);
        const auto point = linkPoint("MIT reading");
        bool menuAction = false;
        QTimer::singleShot(0, browser, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto* item : menu->actions()) if (item->objectName() == "help.openExternalLink") {
                menuAction = true;
                QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(item).center());
                return;
            }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(browser, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
        QVERIFY(menuAction); QCOMPARE(captureUrl.urls.size(), 3);
        QCOMPARE(captureUrl.urls.last(), QUrl("https://ocw.mit.edu/"));
        QCOMPARE(browser->source(), source);
        // A bundled page need not be a top-level topic to be navigable.
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("qrc:/s7studio/help/education/index.md"))));
        QVERIFY(browser->toPlainText().contains("S7 teaching labs"));
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("labs/SP06-three-tap-fir/README.md#predict"))));
        QVERIFY(browser->toPlainText().contains("Build and test a three-tap FIR filter"));
        capture(help, "education-sp06-help", true);
        const auto internal = browser->source();
        QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Qt::DirectConnection,
            Q_ARG(QUrl, QUrl("file:///tmp/not-a-help-article"))));
        QCOMPARE(browser->source(), internal); QCOMPARE(captureUrl.urls.size(), 3);
        QVERIFY(help->findChild<QLabel*>("help.linkStatus")->isVisible());
    }
    void helpExampleLinks() {
        const auto input = QString(S7_GUI_EDUCATION_DIR)+"/SP14-coherent-am/sp14_coherent_am.s7diagram";
        QFile original(input); QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll(); original.close();
        const auto current = window_->currentDocument();
        auto* menu = window_->findChild<QMenu*>("menu.file"); QVERIFY(menu);
        QVERIFY(menu->actions().contains(action("action.openLabs")));
        activateMenu("menu.file", action("action.openLabs"));
        auto* help = window_->findChild<QDialog*>("dialog.help"); QVERIFY(help);
        QVERIFY(help->isVisible()); QVERIFY(!help->isModal());
        QCOMPARE(window_->currentDocument(), current);
        auto* browser = help->findChild<QTextBrowser*>("help.browser"); QVERIFY(browser);
        QCOMPARE(browser->source(), QUrl("qrc:/s7studio/help/education/index.md"));
        QVERIFY(browser->toPlainText().contains("S7 teaching labs"));
        const QUrl guide("qrc:/s7studio/help/education/labs/SP14-coherent-am/README.md");
        browser->setSource(guide, QTextDocument::MarkdownResource);
        const auto linkPoint = [&] {
            auto cursor = browser->document()->find("sp14_coherent_am.s7diagram");
            if (cursor.isNull()) return QPoint(-1, -1);
            cursor.setPosition(cursor.selectionStart() + 3);
            browser->setTextCursor(cursor); browser->ensureCursorVisible();
            return browser->cursorRect(cursor).center();
        };
        auto point = linkPoint(); QVERIFY(browser->viewport()->rect().contains(point));
        QVERIFY(browser->anchorAt(point).endsWith("sp14_coherent_am.s7diagram"));
        capture(help, "education-open-example", true);
        QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, point);
        QVERIFY(!help->isVisible()); QVERIFY(scene()->elementNode("recovered"));
        QVERIFY(scene()->elementNode("message"));
        // The same link must honor Cancel, including a running/paused capture.
        QVERIFY(scene()->addElement({"unsaved_help", "Constant", {}, {}, {}, "Unsaved"}, {0, 0}));
        const auto pending = window_->currentDocument();
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        const boost::json::object running{{"name", "help-cancel-run"},
            {"elements", boost::json::array{boost::json::object{{"id", "source"}, {"type", "Constant"}}}},
            {"connections", boost::json::array{}}};
        runner->run(running, .001, -1, {});
        QTRY_VERIFY(runner->isRunning()); runner->pause(); QTRY_VERIFY(runner->isPaused());
        action("action.help")->trigger(); point = linkPoint();
        QVERIFY(modalWorkflow([&] { QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, point); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box) return false;
            QTest::mouseClick(box->button(QMessageBox::Cancel), Qt::LeftButton); return true;
        }));
        QCOMPARE(window_->currentDocument(), pending); QVERIFY(runner->isPaused());
        QVERIFY(help->isVisible()); QVERIFY(help->findChild<QLabel*>("help.linkStatus")->isVisible());
        // Local files, traversal, remote qrc authorities and parameterized paths
        // must not trigger document replacement or the external browser.
        BrowserUrlCapture captureUrl;
        QDesktopServices::setUrlHandler("file", &captureUrl, "open");
        const auto restore = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        for (const auto& link : {"file:///tmp/other.s7diagram",
                 "qrc:/s7studio/help/examples/../../other.s7diagram",
                 "qrc://other/s7studio/help/education/labs/SP14-coherent-am/sp14_coherent_am.s7diagram",
                 "sp14_coherent_am.s7diagram?run=1",
                 "sp14_coherent_am.s7diagram#run",
                 "missing.s7diagram"}) {
            Q_EMIT browser->anchorClicked(QUrl(link));
            QCOMPARE(window_->currentDocument(), pending); QVERIFY(runner->isPaused());
            QVERIFY(help->isVisible()); QVERIFY(help->findChild<QLabel*>("help.linkStatus")->isVisible());
        }
        QVERIFY(captureUrl.urls.isEmpty());
        point = linkPoint();
        QVERIFY(modalWorkflow([&] { QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, point); }, [](QDialog* dialog) {
            auto* box = qobject_cast<QMessageBox*>(dialog); if (!box) return false;
            QTest::mouseClick(box->button(QMessageBox::Discard), Qt::LeftButton); return true;
        }));
        QVERIFY(!help->isVisible()); QVERIFY(!scene()->elementNode("unsaved_help"));
        QCOMPARE(runner->state(), s7studio::RunState::Idle);
        const auto saved = temporary_.filePath("help-copy.s7diagram");
        QVERIFY(fileAction("action.save", saved)); QVERIFY(QFileInfo::exists(saved));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
        original.close();
        // Every core row in the real navigator must open its working copy.
        QDirIterator labs(QString(S7_GUI_EDUCATION_DIR), {"*.s7diagram"}, QDir::Files, QDirIterator::Subdirectories);
        QStringList diagrams;
        while (labs.hasNext()) diagrams << labs.next();
        diagrams.sort();
        QCOMPARE(diagrams.size(), 24);
        for (const auto& path : diagrams) {
            const auto name = QFileInfo(path).fileName();
            action("action.help")->trigger();
            browser->setSource(QUrl("qrc:/s7studio/help/education/index.md"), QTextDocument::MarkdownResource);
            auto cursor = browser->document()->find(name); QVERIFY2(!cursor.isNull(), qPrintable(name));
            cursor.setPosition(cursor.selectionStart() + 3);
            browser->setTextCursor(cursor); browser->ensureCursorVisible();
            point = browser->cursorRect(cursor).center();
            QVERIFY(browser->viewport()->rect().contains(point));
            QVERIFY(browser->anchorAt(point).endsWith(name));
            QTest::mouseClick(browser->viewport(), Qt::LeftButton, {}, point);
            QVERIFY2(!help->isVisible(), qPrintable(name));
            QFile diagram(path); QVERIFY(diagram.open(QIODevice::ReadOnly));
            const auto expected = QJsonDocument::fromJson(diagram.readAll()).object().value("schema").toObject().value("elements").toArray();
            QVERIFY(!expected.isEmpty());
            for (const auto element : expected)
                QVERIFY2(scene()->elementNode(element.toObject().value("id").toString().toStdString()), qPrintable(name));
        }
    }
    void helpExamplePathContainment() {
        const auto root = temporary_.filePath("example-paths");
        QVERIFY(QDir{}.mkpath(root + "/education"));
        const auto outside = temporary_.filePath("outside.s7diagram");
        QFile file(outside); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{}"); file.close();
        const auto inside = root + "/education/local.s7diagram";
        QVERIFY(QFile::copy(outside, inside));
        QCOMPARE(s7studio::resolveHelpExample(root, "education/local.s7diagram"), QFileInfo(inside).canonicalFilePath());
        for (const auto& path : {QString("../outside.s7diagram"), outside, QString("education/missing.s7diagram"), QString("education")})
            QVERIFY(s7studio::resolveHelpExample(root, path).isEmpty());
#if defined(Q_OS_UNIX)
        QVERIFY(QFile::link(outside, root + "/education/escape.s7diagram"));
        QVERIFY(s7studio::resolveHelpExample(root, "education/escape.s7diagram").isEmpty());
        QVERIFY(QFile::link(inside, root + "/education/alias.s7diagram"));
        QCOMPARE(s7studio::resolveHelpExample(root, "education/alias.s7diagram"), QFileInfo(inside).canonicalFilePath());
#endif
    }
    void diagramFrameLinks() {
        BrowserUrlCapture captureUrl;
        QDesktopServices::setUrlHandler("https", &captureUrl, "open");
        const auto restore = qScopeGuard([] { QDesktopServices::unsetUrlHandler("https"); });
        auto frame = scene()->addTextFrame("lesson", {{"text_format", "markdown"},
            {"text", "[Student lab](qrc:/s7studio/help/education/labs/SP06-three-tap-fir/README.md)  \n"
                     "[MIT teaching material](https://ocw.mit.edu/)"},
            {"width", 500}, {"height", 140}}, {100, 100});
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        editor()->showItem(frame.get());
        auto linkPoint = [&](const QString& scheme) {
            for (int y = 8; y < 130; y += 2) for (int x = 10; x < 480; x += 2)
                if (frame->linkAt(QPointF(x, y)).scheme() == scheme)
                    return view->mapFromScene(frame->mapToScene(QPointF(x, y)));
            return QPoint(-1, -1);
        };
        const auto before = frame->params(); const auto position = frame->pos();
        const auto web = linkPoint("https"); QVERIFY(web.x() >= 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, web);
        QCOMPARE(captureUrl.urls, QList<QUrl>{QUrl("https://ocw.mit.edu/")});
        QCOMPARE(frame->pos(), position); QCOMPARE(frame->params(), before);
        const auto internal = linkPoint("qrc"); QVERIFY(internal.x() >= 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, internal);
        auto* help = window_->findChild<QDialog*>("dialog.help"); QVERIFY(help);
        QVERIFY(help->findChild<QTextBrowser*>("help.browser")->toPlainText().contains("Build and test a three-tap FIR filter"));
        help->close(); window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        // Press/drag/release over a link is not an activation.
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, web);
        QTest::mouseMove(view->viewport(), web+QPoint(40, 0));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, web);
        QCOMPARE(captureUrl.urls.size(), 1); QCOMPARE(frame->pos(), position);
        // The border retains normal frame selection and keyboard editing.
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->mapFromScene(frame->mapToScene(QPointF(3, 3))));
        QVERIFY(frame->isSelected()); view->setFocus();
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F4); }, [](QDialog* dialog) {
            auto* format = dialog->findChild<QComboBox*>("frame.textFormat");
            auto* text = dialog->findChild<QPlainTextEdit*>("frame.text");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!format || !text || !buttons || format->currentData().toString() != "markdown") return false;
            format->setCurrentIndex(0); text->setPlainText("Read https://ocw.mit.edu/ today.");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QCOMPARE(frame->params().at("text_format").as_string(), "plain");
        const auto plain = linkPoint("https"); QVERIFY(plain.x() >= 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, plain);
        QCOMPARE(captureUrl.urls.size(), 2);
        const auto saved = temporary_.filePath("notes-with-links.s7diagram");
        QVERIFY(fileAction("action.saveAs", saved)); QVERIFY(fileAction("action.open", saved));
        const auto reopened = scene()->textFrames(); QCOMPARE(reopened.size(), std::size_t(1));
        QCOMPARE(reopened.front()->params().at("text").as_string(), "Read https://ocw.mit.edu/ today.");
    }
    void educationFirEditAndRerun() {
        QVERIFY(fileAction("action.open", QString(S7_GUI_EDUCATION_DIR)+"/SP06-three-tap-fir/sp06_three_tap_fir.s7diagram"));
        const auto notes = scene()->textFrames(); QCOMPARE(notes.size(), std::size_t(1));
        QSet<QString> visibleLinks;
        for (int y = 8; y < notes.front()->height()-8; y += 3)
            for (int x = 10; x < notes.front()->width()-10; x += 3) {
                const auto url = notes.front()->linkAt(QPointF(x, y));
                if (!url.isEmpty()) visibleLinks.insert(url.toString());
            }
        // Links must actually be visible inside the frame, not clipped below it.
        QCOMPARE(visibleLinks.size(), qsizetype(3));
        QVERIFY(visibleLinks.contains("qrc:/s7studio/help/education/labs/SP06-three-tap-fir/README.md"));
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        QVERIFY(QMetaObject::invokeMethod(editor(), "elementNodeDoubleClicked", Qt::DirectConnection,
            Q_ARG(QString, "scope"), Q_ARG(QString, "Scope"), Q_ARG(int, 1)));
        QPointer<s7studio::ui::ScopeViewWindow> scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->find("Scope", "scope"));
        QVERIFY(scope); QTRY_VERIFY(scope->isVisible());
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        const auto referenceInput = [](qint64 n) {
            if (n < 0) return 0.;
            const double t = double(n)/300, pi = std::acos(-1.);
            return .25 + std::sin(2*pi*10*t+.2) + .5*std::sin(2*pi*100*t+.7);
        };
        auto checkCapture = [&](bool missingTap) {
            QVERIFY(scope);
            const auto source = runner->recordedSource();
            QVERIFY(source && source->status().finished && !source->status().incomplete());
            std::optional<s7studio::observation::ChannelId> input, filtered;
            for (const auto& channel : source->channels()) {
                if (channel.name == "scope/input") input = channel.id;
                if (channel.name == "scope/filtered") filtered = channel.id;
            }
            QVERIFY(input && filtered);
            const auto page = source->read(0, 6000); QVERIFY(page.error.isEmpty());
            std::size_t inputCount = 0, outputCount = 0;
            for (const auto& sample : page.samples) {
                if (sample.channel != *input && sample.channel != *filtered) continue;
                QVERIFY(sample.step >= 1 && sample.step <= 1200);
                QCOMPARE(sample.timeSeconds, double(sample.step)/300);
                const auto n = sample.step-1;
                const double expected = sample.channel == *input ? referenceInput(n) :
                    (referenceInput(n) + (missingTap ? 0 : referenceInput(n-1)) + referenceInput(n-2))/3;
                QVERIFY(std::abs(sample.value-expected) < 1e-10);
                (sample.channel == *input ? inputCount : outputCount)++;
            }
            QCOMPARE(inputCount, std::size_t(1200)); QCOMPARE(outputCount, std::size_t(1200));
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);
            while (scope->refreshSource()) {}
            std::size_t liveChannels = 0;
            for (const auto& series : scope->recording().series()) {
                if (series.isComparison()) continue;
                QCOMPARE(series.channelUnit, QString("V"));
                QCOMPARE(series.receivedSamples, std::uint64_t(1200));
                ++liveChannels;
            }
            QCOMPARE(liveChannels, std::size_t(2));
        };
        QVERIFY(configure("action.run", 4, 300));
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() > 0, 10000);
        QVERIFY2(completed.last()[0].toBool(), qPrintable(completed.last()[1].toString()));
        QCOMPARE(runner->completedTicks(), 1200L);
        checkCapture(false);
        const auto firstRun = runner->recordedSource()->runId();
        for (auto* check : scope->findChildren<QCheckBox*>())
            if (check->text() == "Fit full run") check->setChecked(false);
        scope->plotView()->setXRange(0, .2);
        capture(scope, "education-sp06-correct", true);
        capture(window_.get(), "education-sp06-diagram", true);
        scope->hide(); window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto tap = scene()->elementNode("tap1"); QVERIFY(tap);
        scene()->clearSelection(); tap->setSelected(true);
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view); view->setFocus();
        QVERIFY(modalWorkflow([view] { QTest::keyClick(view->viewport(), Qt::Key_F4); }, [](QDialog* dialog) {
            auto* factor = dialog->findChild<QLineEdit*>("control.factor");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!factor || !buttons) return false;
            factor->setText("0");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        QCOMPARE(tap->element().params.at("factor").to_number<double>(), 0.);
        const auto copy = temporary_.filePath("sp06-missing-tap.s7diagram");
        QVERIFY(fileAction("action.saveAs", copy));
        QVERIFY(fileAction("action.open", copy));
        QCOMPARE(scene()->elementNode("tap1")->element().params.at("factor").to_number<double>(), 0.);
        // Reopening a diagram replaces its observer windows; use the new binding.
        manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope", "scope"));
        QVERIFY(scope);
        completed.clear();
        QVERIFY(configure("action.run", 4, 300));
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() > 0, 10000);
        QVERIFY2(completed.last()[0].toBool(), qPrintable(completed.last()[1].toString()));
        QVERIFY(runner->recordedSource()->runId() != firstRun);
        checkCapture(true);
        for (auto* check : scope->findChildren<QCheckBox*>())
            if (check->text() == "Fit full run") check->setChecked(false);
        scope->plotView()->setXRange(0, .2);
        scope->show(); capture(scope, "education-sp06-missing-tap", true);
    }
    void educationFoundations_data() {
        QTest::addColumn<QString>("lab"); QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("slug"); QTest::addColumn<QString>("editedId");
        QTest::addColumn<QString>("parameter"); QTest::addColumn<QString>("newValue");
        QTest::newRow("FN01") << "FN01" << "fn01_unit_delay" << "FN01-unit-delay"
            << "output_gain" << "factor" << "1.25";
        QTest::newRow("FN02") << "FN02" << "fn02_natural_forced" << "FN02-natural-forced"
            << "step" << "final_value" << "2";
        QTest::newRow("FN03") << "FN03" << "fn03_initial_conditions" << "FN03-initial-conditions"
            << "total_memory" << "initial_value" << "0";
    }
    void educationFoundations() {
        QFETCH(QString, lab); QFETCH(QString, file); QFETCH(QString, slug);
        QFETCH(QString, editedId); QFETCH(QString, parameter); QFETCH(QString, newValue);
        QVERIFY(fileAction("action.open", QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto notes = scene()->textFrames(); QCOMPARE(notes.size(), std::size_t(1));
        const auto frame = notes.front();
        QSet<QString> links;
        QPoint guidePoint(-1, -1);
        for (int y = 8; y < frame->height()-8; y += 3) for (int x = 10; x < frame->width()-10; x += 3) {
            const auto url = frame->linkAt(QPointF(x, y));
            if (!url.isEmpty()) links.insert(url.toString());
            if (url.scheme() == "qrc" && guidePoint.x() < 0)
                guidePoint = view->mapFromScene(frame->mapToScene(QPointF(x+2, y+2)));
        }
        QCOMPARE(links.size(), qsizetype(3));
        QVERIFY(links.contains("qrc:/s7studio/help/education/labs/"+slug+"/README.md"));
        QVERIFY(view->viewport()->rect().contains(guidePoint));
        // Follow the actual visible instruction-frame link as a student would.
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, guidePoint);
        auto* help = window_->findChild<QDialog*>("dialog.help"); QVERIFY(help && help->isVisible());
        auto* browser = help->findChild<QTextBrowser*>("help.browser"); QVERIFY(browser);
        QVERIFY(browser->source().path().contains(slug));
        QVERIFY(browser->toPlainText().contains("Predict", Qt::CaseInsensitive));
        capture(help, lab+"-student-guide", true);
        help->close(); window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));

        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        QPointer<s7studio::ui::ScopeViewWindow> scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope", "scope"));
        QVERIFY(scope);
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        const auto expected = [&](QString channel, int n, bool edited) -> double {
            if (lab == "FN01") {
                const double a = edited ? 1.25 : .5;
                if (channel == "input") return 1;
                if (channel == "output") return (1-std::pow(a, n+1))/(1-a);
                if (channel == "previous") return n == 0 ? 0 : (1-std::pow(a, n))/(1-a);
            }
            const double natural = std::pow(.5, n);
            const double forced = n < 5 ? 0 : 2*(1-std::pow(.5, n-4));
            if (channel == "natural") return natural;
            if (lab == "FN02") {
                if (channel == "impulse response") return n < 5 ? 0 : std::pow(.5, n-5);
                if (channel == "step response") return forced*(edited ? 2 : 1);
            } else {
                if (channel == "total") return forced+(edited ? 0 : natural);
                if (channel == "forced") return forced;
                if (channel == "residual") return edited ? -natural : 0;
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto checkCapture = [&](bool edited) {
            QVERIFY(scope);
            const auto source = runner->recordedSource(); QVERIFY(source);
            QVERIFY(source->status().finished && !source->status().incomplete());
            const auto channels = source->channels();
            QCOMPARE(channels.size(), lab == "FN03" ? qsizetype(4) : qsizetype(3));
            const auto page = source->read(0, 200); QVERIFY(page.error.isEmpty());
            QMap<s7studio::observation::ChannelId, int> counts;
            for (const auto& sample : page.samples) {
                QCOMPARE(sample.step, qint64(++counts[sample.channel]));
                QCOMPARE(sample.timeSeconds, double(sample.step)/10);
                QString name;
                for (const auto& channel : channels) if (channel.id == sample.channel) name = channel.name;
                QVERIFY(name.startsWith("scope/"));
                const double reference = expected(name.mid(6), int(sample.step)-1, edited);
                QVERIFY(std::isfinite(reference));
                QVERIFY(std::abs(sample.value-reference) < (lab == "FN01" && edited ? 1e-10 : 1e-12));
            }
            QCOMPARE(counts.size(), channels.size());
            for (const auto count : counts) QCOMPARE(count, 20);
            QCOMPARE(runner->completedTicks(), 20L);
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);
            while (scope->refreshSource()) {}
            QCOMPARE(scope->recording().series().size(), channels.size());
            for (const auto& series : scope->recording().series()) {
                QCOMPARE(series.channelUnit, QString("V"));
                QCOMPARE(series.receivedSamples, std::uint64_t(20));
                QVERIFY(!series.isComparison());
            }
        };
        // Do not repair settings in the test: the saved file must be ready to run.
        const auto acceptSavedRun = [](QDialog* dialog) {
            auto* duration = dialog->findChild<QDoubleSpinBox*>("run.duration");
            auto* rate = dialog->findChild<QDoubleSpinBox*>("run.frequency");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!duration || !rate || !buttons || duration->value() != 2 || rate->value() != 10) return false;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        };
        QVERIFY(dialogAction("action.run", acceptSavedRun));
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 10000);
        QVERIFY2(completed.last()[0].toBool(), qPrintable(completed.last()[1].toString()));
        checkCapture(false);
        capture(scope, lab+"-baseline-scope", true);
        capture(window_.get(), lab+"-diagram", true);
        scope->hide(); window_->raise(); window_->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node = scene()->elementNode(editedId.toStdString()); QVERIFY(node);
        scene()->clearSelection(); node->setSelected(true); view->setFocus();
        QVERIFY(modalWorkflow([view] {QTest::keyClick(view->viewport(), Qt::Key_F4);}, [&](QDialog* dialog) {
            auto* field = dialog->findChild<QLineEdit*>("control."+parameter);
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!field || !buttons) return false;
            field->setText(newValue);
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
            return true;
        }));
        const auto copy = temporary_.filePath(file+"-edited.s7diagram");
        QVERIFY(fileAction("action.saveAs", copy)); QVERIFY(fileAction("action.open", copy));
        node = scene()->elementNode(editedId.toStdString()); QVERIFY(node);
        QCOMPARE(node->element().params.at(parameter.toStdString()).to_number<double>(), newValue.toDouble());
        manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope", "scope")); QVERIFY(scope);
        completed.clear();
        QVERIFY(dialogAction("action.run", acceptSavedRun));
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 10000);
        QVERIFY2(completed.last()[0].toBool(), qPrintable(completed.last()[1].toString()));
        checkCapture(true);
        capture(scope, lab+"-edited-scope", true);
    }
    void educationLinear_data() {
        QTest::addColumn<int>("index");
        int index = 0;
        for (const auto& lab : s7tests::education::linear_labs)
            QTest::newRow(lab.id.c_str()) << index++;
    }
    void educationLinear() {
        QFETCH(int, index);
        const auto& lab = s7tests::education::linear_labs.at(index);
        const auto id = QString::fromStdString(lab.id), file = QString::fromStdString(lab.file);
        QVERIFY(fileAction("action.open", QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto frames = scene()->textFrames(); QCOMPARE(frames.size(), std::size_t(1));
        const auto frame = frames.front();
        QSet<QString> links; QPoint guidePoint(-1, -1);
        for (int y=8; y<frame->height()-8; y+=3) for (int x=10; x<frame->width()-10; x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));
            if (!url.isEmpty()) links.insert(url.toString());
            if (url.path().contains("/labs/") && guidePoint.x()<0)
                guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(), qsizetype(3));
        QVERIFY(links.contains("qrc:/s7studio/help/education/labs/"+QString::fromStdString(lab.slug)+"/README.md"));
        QVERIFY(view->viewport()->rect().contains(guidePoint));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help"); QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser"); QVERIFY(browser);
        QVERIFY(browser->source().path().contains(QString::fromStdString(lab.slug)));
        QVERIFY(browser->toPlainText().contains("Predict", Qt::CaseInsensitive));
        capture(help,id+"-student-guide",true); help->close();
        window_->raise(); window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        capture(window_.get(),id+"-diagram",true);

        QList<QPointer<s7studio::ui::ScopeViewWindow>> scopes;
        QPointer<s7studio::viewers::ResponseWindow> response;
        auto openViewers = [&] {
            scopes.clear(); response.clear();
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
            for (const auto& name : QStringList{"scope", lab.id=="FN06" ? "stable_scope" : lab.id=="CT02" ? "velocity_scope" : ""}) {
                if (name.isEmpty()) continue;
                auto* scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope",name));
                QVERIFY(scope); scopes.push_back(scope);
            }
            if (lab.id=="FN04") {
                response=dynamic_cast<s7studio::viewers::ResponseWindow*>(manager->openObserver("FrequencyResponse","response"));
                QVERIFY(response);
            }
        };
        openViewers();
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto acceptRun = [&](bool edited, bool changeRate=false) {
            return dialogAction("action.run",[&](QDialog* dialog) {
                auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");
                auto* rate=dialog->findChild<QDoubleSpinBox*>("run.frequency");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();
                if (!duration || !rate || !buttons || duration->value()!=lab.seconds) return false;
                const double expected=edited && lab.id=="CT03" ? .4 : 1/lab.h;
                if (changeRate) rate->setValue(expected);
                if (std::abs(rate->value()-expected)>1e-6) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
                return true;
            });
        };
        auto checkCapture = [&](bool edited) {
            const double h=edited && lab.id=="CT03" ? 2.5 : lab.h;
            const auto count=int(std::floor(lab.seconds/h+1e-10));
            const auto config=runner->activeConfiguration(); QVERIFY(config);
            QVERIFY(std::abs(config->dt-h)<1e-14);
            const auto source=runner->recordedSource(); QVERIFY(source);
            QVERIFY(source->status().finished && !source->status().incomplete());
            const auto channels=source->channels();
            QCOMPARE(channels.size(), qsizetype(lab.outputs.size()+(lab.id=="FN04" ? 2 : 0)));
            const auto page=source->read(0,5000); QVERIFY(page.error.isEmpty());
            QMap<s7studio::observation::ChannelId,int> counts;
            QSet<QString> observed;
            for (const auto& sample : page.samples) {
                QCOMPARE(sample.step,qint64(++counts[sample.channel]));
                QVERIFY(std::abs(sample.timeSeconds-sample.step*h)<1e-12);
                QString name; for (const auto& c : channels) if (c.id==sample.channel) name=c.name;
                std::string element;
                for (const auto& probe : config->probes) if (QString::fromStdString(probe.name)==name) element=probe.element;
                QVERIFY(std::find(lab.outputs.begin(),lab.outputs.end(),element)!=lab.outputs.end());
                observed.insert(QString::fromStdString(element));
                const auto reference=s7tests::education::discrete(lab.id,element,int(sample.step)-1,h,edited);
                QVERIFY(std::abs(sample.value-reference)<1e-10*std::max(1.,std::abs(reference)));
            }
            QCOMPARE(observed.size(),qsizetype(lab.outputs.size()));
            QCOMPARE(counts.size(),channels.size()); for (const auto n : counts) QCOMPARE(n,count);
            QCOMPARE(runner->completedTicks(),long(count));
            for (auto scope : scopes) {
                QVERIFY(scope);
                QTRY_VERIFY(scope->source() && scope->source()->status().finished);
                while (scope->refreshSource()) {}
                QVERIFY(!scope->recording().series().isEmpty());
                for (const auto& series : scope->recording().series()) {
                    QCOMPARE(series.receivedSamples,std::uint64_t(count));
                    QCOMPARE(series.channelUnit,lab.id=="CT02" ? (scope==scopes.front() ? QString("m") : QString("m/s")) : QString("V"));
                    QVERIFY(!series.isComparison());
                }
                if (lab.id=="FN04") scope->plotView()->setXRange(0,1);
                if (lab.id=="FN06" && scope!=scopes.front()) scope->plotView()->setXRange(0,12);
                capture(scope,id+(edited ? "-edited-" : "-baseline-")+scope->probeName().replace('/','-'),true);
                scope->hide();
            }
            if (response) {
                while (response->refreshSource()) {}
                QTRY_VERIFY_WITH_TIMEOUT(response->hasResult() || !response->error().isEmpty(),10000);
                QVERIFY2(response->hasResult(),qPrintable(response->error()));
                QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
                // Check the viewer's measured complex result, not only its ability to draw.
                const auto resultPath=temporary_.filePath(edited ? "FN04-edited.s7analysis" : "FN04-baseline.s7analysis");
                response->saveResult(resultPath);
                const auto result=s7studio::analysis::ResultFile::open(resultPath);
                QCOMPARE(result.kind,QString("frequency_response"));
                QCOMPARE(result.metadata["pulse_index"].toInt(),4);
                QCOMPARE(result.metadata["pulse_height"].toDouble(),1.);
                QCOMPARE(result.metadata["input_unit"].toString(),QString("V"));
                QCOMPARE(result.metadata["output_unit"].toString(),QString("V"));
                QVERIFY(result.x.size()>4); QCOMPARE(result.values.size(),result.x.size());
                QCOMPARE(result.x.front(),0.); QCOMPARE(result.x.back(),10.);
                for (std::size_t i=0; i<result.x.size(); ++i) {
                    const auto exact=.5/(1.-(edited ? .8 : .5)*std::polar(1.,-2*std::acos(-1.)*result.x[i]/20));
                    QVERIFY(result.valid[i]);
                    QVERIFY(std::abs(result.values[i]-exact)<1e-10);
                }
                capture(response,id+(edited ? "-edited-response" : "-baseline-response"),true);
                response->hide();
            }
        };
        QVERIFY(acceptRun(false)); QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);
        QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        checkCapture(false);
        window_->raise(); window_->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        if (lab.id=="CT03") {
            completed.clear(); QVERIFY(acceptRun(true,true));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);
            QVERIFY(completed.last()[0].toBool());
        } else {
            auto node=scene()->elementNode(lab.edit_id); QVERIFY(node);
            scene()->clearSelection(); node->setSelected(true); view->setFocus();
            QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
                auto* field=dialog->findChild<QLineEdit*>("control."+QString::fromStdString(lab.parameter));
                auto* buttons=dialog->findChild<QDialogButtonBox*>(); if (!field || !buttons) return false;
                field->setText(QString::number(lab.edited_value,'g',16));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton); return true;
            }));
        }
        const auto copy=temporary_.filePath(file+"-edited.s7diagram");
        QVERIFY(fileAction("action.saveAs",copy)); QVERIFY(fileAction("action.open",copy));
        if (!lab.edit_id.empty()) {
            auto node=scene()->elementNode(lab.edit_id); QVERIFY(node);
            QCOMPARE(node->element().params.at(lab.parameter).to_number<double>(),lab.edited_value);
        }
        openViewers(); completed.clear(); QVERIFY(acceptRun(true));
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);
        QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        checkCapture(true);
    }
    void educationSignals_data() {
        QTest::addColumn<QString>("lab");
        QTest::newRow("SP01") << "SP01"; QTest::newRow("SP03") << "SP03";
    }
    void educationSignals() {
        QFETCH(QString, lab);
        const bool synthesis=lab=="SP01";
        const auto file=synthesis ? QString("sp01_fourier_synthesis") : QString("sp03_aliasing");
        const auto slug=synthesis ? QString("SP01-fourier-synthesis") : QString("SP03-aliasing");
        const double savedRate=synthesis ? 128 : 64;
        QVERIFY(fileAction("action.open",QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* view=editor()->findChild<QGraphicsView*>(); QVERIFY(view);
        const auto frames=scene()->textFrames(); QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front(); QPoint guidePoint(-1,-1); QSet<QString> links;
        for (int y=8;y<frame->height()-8;y+=3) for (int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y)); if (!url.isEmpty()) links.insert(url.toString());
            if (url.path().contains("/labs/") && guidePoint.x()<0)
                guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));
        QVERIFY(links.contains("qrc:/s7studio/help/education/labs/"+slug+"/README.md"));
        QVERIFY(QFile::exists(":/s7studio/help/docs/SIGNAL_SOURCES.md"));
        QVERIFY(view->viewport()->rect().contains(guidePoint));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help"); QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser"); QVERIFY(browser);
        QVERIFY(browser->source().path().contains(slug));
        QVERIFY(browser->toPlainText().contains("Predict",Qt::CaseInsensitive));
        capture(help,lab+"-student-guide",true);help->close();
        capture(window_.get(),lab+"-diagram",true);
        QPointer<s7studio::ui::ScopeViewWindow> scope;
        auto openScope=[&] {
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
            scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope","scope")); QVERIFY(scope);
        };
        openScope();
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&](double rate,bool changeRate=false) {
            completed.clear();
            QVERIFY(dialogAction("action.run",[&](QDialog* dialog) {
                auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");
                auto* frequency=dialog->findChild<QDoubleSpinBox*>("run.frequency");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();
                if (!duration || !frequency || !buttons || duration->value()!=2) return false;
                if (changeRate) frequency->setValue(rate);
                if (frequency->value()!=rate) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);
            QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        };
        auto checkViewers=[&](double rate,bool edited,const QString& label) {
            QVERIFY(scope);
            const auto source=runner->recordedSource(); QVERIFY(source);
            QVERIFY(source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration(); QVERIFY(config);
            const auto channels=source->channels(); QCOMPARE(channels.size(),qsizetype(4));
            const auto page=source->read(0,4096); QVERIFY(page.error.isEmpty());
            const int count=int(2*rate);
            QCOMPARE(page.samples.size(),qsizetype(4*count)); QCOMPARE(runner->completedTicks(),long(count));
            QMap<s7studio::observation::ChannelId,int> counts;
            for (const auto& sample:page.samples) {
                QCOMPARE(sample.step,qint64(++counts[sample.channel]));
                QVERIFY(std::abs(sample.timeSeconds-sample.step/rate)<1e-12);
                QString name;for (const auto& c:channels) if (c.id==sample.channel) name=c.name;
                std::string element;
                for (const auto& probe:config->probes) if (QString::fromStdString(probe.name)==name) element=probe.element;
                QVERIFY(!element.empty());
                const auto expected=s7tests::education::signal_reference(lab.toStdString(),element,int(sample.step)-1,rate,edited);
                QVERIFY(std::abs(sample.value-expected)<1e-10);
            }
            QCOMPARE(counts.size(),qsizetype(4));for (auto n:counts) QCOMPARE(n,count);
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);
            while (scope->refreshSource()) {}
            QTRY_COMPARE(scope->recording().series().size(),qsizetype(4));
            for (auto it=scope->recording().series().cbegin();it!=scope->recording().series().cend();++it) {
                const auto& series=it.value();
                QCOMPARE(series.receivedSamples,std::uint64_t(count));QCOMPARE(series.channelUnit,QString("V"));
                QVERIFY(!series.isComparison());
                auto* line=scope->plottedSeries(it.key()); QVERIFY(line);
                QTRY_COMPARE(line->points().size(),qsizetype(count));
                QCOMPARE(line->name(),series.displayName());
                const auto points=line->points();
                std::string element;
                for (const auto& probe:config->probes)
                    if (probe.name==("scope/"+series.channelName).toStdString()) element=probe.element;
                QVERIFY(!element.empty());
                for (int n=0;n<count;++n) {
                    QCOMPARE(points[n].x(),series.time[n]);QCOMPARE(points[n].y(),series.value[n]);
                    QVERIFY(std::abs(points[n].x()-(n+1)/rate)<1e-12);
                    const auto expected=s7tests::education::signal_reference(lab.toStdString(),element,n,rate,edited);
                    QVERIFY(std::abs(points[n].y()-expected)<1e-10);
                }
            }
            QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));
            QCOMPARE(scope->plotView()->yTitle(),QString("value (V)"));
            QCOMPARE(scope->plotView()->xMin(),1/rate);QCOMPARE(scope->plotView()->xMax(),2.);
            capture(scope,lab+'-'+label+"-time",true);

            auto* start=scope->findChild<QDoubleSpinBox*>("scope.analysisStart"); QVERIFY(start);
            auto* end=scope->findChild<QDoubleSpinBox*>("scope.analysisEnd"); QVERIFY(end);
            auto* analyze=scope->findChild<QPushButton*>("scope.analyze"); QVERIFY(analyze);
            auto* tabs=scope->findChild<QTabWidget*>("scope.analysisTabs"); QVERIFY(tabs);
            auto* spectrum=scope->findChild<s7studio::viewers::SpectrumView*>(); QVERIFY(spectrum);
            auto* window=spectrum->controlPanel()->findChild<QComboBox*>("spectrum.window"); QVERIFY(window);
            auto* quantity=spectrum->controlPanel()->findChild<QComboBox*>("spectrum.quantity"); QVERIFY(quantity);
            auto* scale=spectrum->controlPanel()->findChild<QComboBox*>("spectrum.scale"); QVERIFY(scale);
            auto* all=spectrum->controlPanel()->findChild<QCheckBox*>("spectrum.allInputs"); QVERIFY(all);
            auto* remove=spectrum->controlPanel()->findChild<QCheckBox*>("spectrum.removeMean"); QVERIFY(remove);
            auto* range=spectrum->controlPanel()->findChild<QComboBox*>("spectrum.frequencyRange"); QVERIFY(range);
            window->setCurrentIndex(0);quantity->setCurrentIndex(0);scale->setCurrentIndex(0);
            all->setChecked(true);remove->setChecked(false);range->setCurrentIndex(1);
            auto checkSpectrum=[&](double seconds,int q) {
                if (q==0) {
                    start->setValue(0);end->setValue(seconds);analyze->click();tabs->setCurrentIndex(1);
                }
                quantity->setCurrentIndex(q);
                const auto expectedQuantity=static_cast<s7studio::analysis::SpectrumQuantity>(q);
                const auto size=std::size_t(rate*seconds);
                QTRY_VERIFY_WITH_TIMEOUT(spectrum->result() && spectrum->result()->state==s7studio::analysis::SpectrumResult::State::Ready
                    && spectrum->result()->size==size && spectrum->result()->settings.quantity==expectedQuantity,10000);
                const auto& result=*spectrum->result();
                QCOMPARE(result.runId,source->runId());QCOMPARE(result.firstStep,std::int64_t(1));
                QCOMPARE(result.lastStep,std::int64_t(size));QCOMPARE(result.sampleRate,rate);
                QCOMPARE(result.binSpacing,rate/size);QCOMPARE(result.coherentGain,1.);
                QCOMPARE(result.settings.window,s7studio::analysis::SpectrumWindow::Rectangular);
                QCOMPARE(result.traces.size(),std::size_t(4)); QVERIFY(result.frame);
                QCOMPARE(result.frame->steps.size(),qsizetype(size));
                QCOMPARE(result.frequencies.front(),0.);QCOMPARE(result.frequencies.back(),rate/2);
                const auto names=result.frame->channels;
                QSet<QLineSeries*> checkedLines;
                for (const auto& trace:result.traces) {
                    QCOMPARE(trace.unit,QString("V"));
                    std::vector<double> values;
                    const auto name=names[qsizetype(trace.channel)].name;
                    std::string element;
                    for (const auto& probe:config->probes) if (probe.name==("scope/"+name).toStdString()) element=probe.element;
                    QVERIFY(!element.empty());
                    for (std::size_t n=0;n<size;++n) {
                        const auto actual=result.frame->values[qsizetype(n*4+trace.channel)];
                        const auto expected=s7tests::education::signal_reference(lab.toStdString(),element,int(n),rate,edited);
                        QVERIFY(std::abs(actual-expected)<1e-10);values.push_back(actual);
                    }
                    const auto direct=s7tests::education::signal_dft(values);
                    QLineSeries* line=nullptr;
                    const QString plottedUnit=q==0 ? "V" : q==1 ? "V²" : "V²/Hz";
                    for (auto* candidate:spectrum->plotView()->series()) if (candidate->name()==name+" ["+plottedUnit+"]") line=candidate;
                    QVERIFY(line && line->isVisible());checkedLines.insert(line);
                    const auto points=line->points();QCOMPARE(points.size(),qsizetype(size/2+1));
                    double integrated=0,meanSquare=0;for (double value:values) meanSquare+=value*value/size;
                    for (std::size_t k=0;k<trace.values.size();++k) {
                        const double fold=k==0 || k==size/2 ? 1 : 2;
                        const double expected=q==0 ? fold*std::abs(direct[k]) : fold*std::norm(direct[k])/(q==2 ? result.binSpacing : 1);
                        QVERIFY(std::abs(trace.values[k]-expected)<1e-9);
                        QCOMPARE(result.frequencies[k],k*result.binSpacing);
                        QCOMPARE(points[qsizetype(k)].x(),result.frequencies[k]);
                        QVERIFY(std::abs(points[qsizetype(k)].y()-expected)<1e-9);
                        if (q>0) integrated+=trace.values[k]*(q==2 ? result.binSpacing : 1);
                    }
                    if (q>0) QVERIFY(std::abs(integrated-meanSquare)<1e-10);
                }
                QCOMPARE(checkedLines.size(),qsizetype(4));
                QCOMPARE(spectrum->plotView()->xTitle(),QString("Frequency (Hz)"));
                QCOMPARE(spectrum->plotView()->yTitle(),q==0 ? QString("Peak amplitude (V)") : q==1 ? QString("Power / bin (V²)") : QString("PSD (V²/Hz)"));
                QCOMPARE(spectrum->plotView()->xMin(),0.);QCOMPARE(spectrum->plotView()->xMax(),rate/2);
            };
            checkSpectrum(2,0);capture(scope,lab+'-'+label+"-spectrum",true);
            checkSpectrum(2,1);checkSpectrum(2,2);checkSpectrum(1,0);
            capture(scope,lab+'-'+label+"-selected-interval",true);
            tabs->setCurrentIndex(0);scope->hide();
        };
        auto editParameter=[&](const std::string& id,const QString& parameter,double value) {
            window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
            auto node=scene()->elementNode(id);QVERIFY(node);
            scene()->clearSelection();node->setSelected(true);view->setFocus();
            QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
                auto* field=dialog->findChild<QLineEdit*>("control."+parameter);
                auto* buttons=dialog->findChild<QDialogButtonBox*>();if (!field || !buttons) return false;
                field->setText(QString::number(value,'g',16));QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
        };
        run(savedRate);checkViewers(savedRate,false,"baseline");
        editParameter(synthesis ? "weight9" : "shifted",synthesis ? "factor" : "frequency_hz",synthesis ? 0 : 70);
        const auto copy=temporary_.filePath(file+"-edited.s7diagram");
        QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openScope();run(savedRate);checkViewers(savedRate,true,"edited");
        if (!synthesis) {
            editParameter("shifted","frequency_hz",74);
            openScope();run(256,true);checkViewers(256,false,"256Hz");
        }
    }
    void educationLti_data() {
        QTest::addColumn<QString>("lab");
        QTest::newRow("SP05") << "SP05"; QTest::newRow("SP07") << "SP07";
    }
    void educationLti() {
        QFETCH(QString,lab);
        const bool convolution=lab=="SP05";
        const auto file=convolution ? QString("sp05_convolution") : QString("sp07_filter_transfer");
        const auto slug=convolution ? QString("SP05-convolution") : QString("SP07-filter-transfer");
        const double rate=convolution ? 20 : 64,seconds=convolution ? 2 : 64;
        const int count=int(rate*seconds);
        QVERIFY(fileAction("action.open",QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);
        const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front();QSet<QString> links;QPoint guidePoint(-1,-1);
        for (int y=8;y<frame->height()-8;y+=3) for (int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if (!url.isEmpty()) links.insert(url.toString());
            if (url.path().contains("/labs/") && guidePoint.x()<0)
                guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));
        QVERIFY(links.contains("qrc:/s7studio/help/education/labs/"+slug+"/README.md"));
        QVERIFY(view->viewport()->rect().contains(guidePoint));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");QVERIFY(browser);
        QVERIFY(browser->source().path().contains(slug));QVERIFY(browser->toPlainText().contains("Predict",Qt::CaseInsensitive));
        capture(help,lab+"-student-guide",true);help->close();capture(window_.get(),lab+"-diagram",true);
        QMap<QString,QPointer<s7studio::ui::ScopeViewWindow>> scopes;
        QMap<QString,QPointer<s7studio::viewers::ResponseWindow>> responses;
        auto openViewers=[&] {
            scopes.clear();responses.clear();
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
            for (const auto& name:QStringList{"scope",convolution ? "parts_scope" : "input_scope"}) {
                auto* scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope",name));QVERIFY(scope);scopes[name]=scope;
            }
            if (!convolution) for (const auto& name:QStringList{"low_response","resonant_response"}) {
                auto* response=dynamic_cast<s7studio::viewers::ResponseWindow*>(manager->openObserver("FrequencyResponse",name));
                QVERIFY(response);responses[name]=response;
            }
        };
        openViewers();
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&] {
            completed.clear();QVERIFY(dialogAction("action.run",[&](QDialog* dialog) {
                auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");
                auto* frequency=dialog->findChild<QDoubleSpinBox*>("run.frequency");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();
                if (!duration || !frequency || !buttons || duration->value()!=seconds || frequency->value()!=rate) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),15000);
            QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        };
        auto checkViewers=[&](bool edited) {
            const auto source=runner->recordedSource();QVERIFY(source);
            QVERIFY(source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration();QVERIFY(config);
            const auto channels=source->channels();QCOMPARE(channels.size(),qsizetype(convolution ? 6 : 7));
            auto elementFor=[&](const QString& name) {
                for (const auto& probe:config->probes) if (QString::fromStdString(probe.name)==name) return probe.element;
                return std::string{};
            };
            QMap<s7studio::observation::ChannelId,int> counts;std::uint64_t cursor=0;
            while (cursor<source->status().samples) {
                const auto page=source->read(cursor,4096);QVERIFY(page.error.isEmpty());QVERIFY(page.next>cursor);
                for (const auto& sample:page.samples) {
                    QCOMPARE(sample.step,qint64(++counts[sample.channel]));
                    QVERIFY(std::abs(sample.timeSeconds-sample.step/rate)<1e-12);
                    QString name;for (const auto& c:channels) if (c.id==sample.channel) name=c.name;
                    const auto element=elementFor(name);QVERIFY(!element.empty());
                    const auto expected=s7tests::education::lti_reference(lab.toStdString(),element,int(sample.step)-1,rate,edited);
                    QVERIFY(std::abs(sample.value-expected)<1e-10*std::max(1.,std::abs(expected)));
                }
                cursor=page.next;
            }
            QCOMPARE(counts.size(),channels.size());for (auto n:counts) QCOMPARE(n,count);
            QCOMPARE(runner->completedTicks(),long(count));
            for (auto it=scopes.cbegin();it!=scopes.cend();++it) {
                auto scope=it.value();QVERIFY(scope);QTRY_VERIFY(scope->source() && scope->source()->status().finished);
                while (scope->refreshSource()) {}
                const int expectedChannels=it.key()=="scope" ? (convolution ? 4 : 2) : (convolution ? 2 : 1);
                QCOMPARE(scope->recording().series().size(),qsizetype(expectedChannels));
                for (auto series=scope->recording().series().cbegin();series!=scope->recording().series().cend();++series) {
                    QCOMPARE(series->receivedSamples,std::uint64_t(count));QCOMPARE(series->channelUnit,QString("V"));QVERIFY(!series->isComparison());
                    auto* line=scope->plottedSeries(series.key());QVERIFY(line);
                    QTRY_VERIFY(!line->points().isEmpty());
                    QTRY_COMPARE(line->points().back().x(),series->lastTime);
                    QCOMPARE(line->name(),series->displayName());
                    const auto element=elementFor(it.key()+"/"+series->channelName);QVERIFY(!element.empty());
                    QCOMPARE(series->time.size(),qsizetype(count));QCOMPARE(series->value.size(),qsizetype(count));
                    for (int n=0;n<count;++n) {
                        QVERIFY(std::abs(series->time[n]-(n+1)/rate)<1e-12);
                        const auto expected=s7tests::education::lti_reference(lab.toStdString(),element,n,rate,edited);
                        QVERIFY(std::abs(series->value[n]-expected)<1e-10*std::max(1.,std::abs(expected)));
                    }
                    // Dense displays retain a pixel envelope, while the recording stays exact.
                    const auto points=line->points();QVERIFY(points.size()<=count);
                    if (convolution) QCOMPARE(points.size(),qsizetype(count));
                    QCOMPARE(points.front().x(),series->time.front());QCOMPARE(points.back().x(),series->time.back());
                    double plottedLow=points.front().y(),plottedHigh=plottedLow,previous=0;
                    for (const auto& point:points) {
                        const int n=int(std::llround(point.x()*rate))-1;QVERIFY(n>=0 && n<count);
                        QCOMPARE(point.x(),series->time[n]);QCOMPARE(point.y(),series->value[n]);
                        QVERIFY(point.x()>previous);previous=point.x();
                        plottedLow=std::min(plottedLow,point.y());plottedHigh=std::max(plottedHigh,point.y());
                    }
                    QCOMPARE(plottedLow,*std::min_element(series->value.cbegin(),series->value.cend()));
                    QCOMPARE(plottedHigh,*std::max_element(series->value.cbegin(),series->value.cend()));
                }
                QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),QString("value (V)"));
                QCOMPARE(scope->plotView()->xMin(),1/rate);QCOMPARE(scope->plotView()->xMax(),seconds);
                QCheckBox* fitFull=nullptr;
                for (auto* box:scope->findChildren<QCheckBox*>()) if (box->text()=="Fit full run") fitFull=box;
                QVERIFY(fitFull);fitFull->setChecked(false);
                scope->plotView()->setXRange(0,convolution ? .6 : it.key()=="input_scope" ? .25 : 10);
                capture(scope,lab+(edited ? "-edited-" : "-baseline-")+it.key(),true);scope->hide();
            }
            for (auto it=responses.cbegin();it!=responses.cend();++it) {
                auto response=it.value();QVERIFY(response);
                while (response->refreshSource()) {}
                QTRY_VERIFY_WITH_TIMEOUT(response->hasResult() || !response->error().isEmpty(),15000);
                QVERIFY2(response->hasResult(),qPrintable(response->error()));
                QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
                const auto channel=it.key()=="low_response" ? std::string("low") : std::string("resonant");
                const auto resultPath=temporary_.filePath(lab+it.key()+(edited ? "-edited" : "-baseline")+".s7analysis");
                response->saveResult(resultPath);const auto result=s7studio::analysis::ResultFile::open(resultPath);
                QCOMPARE(result.kind,QString("frequency_response"));
                QCOMPARE(result.metadata["run_id"].toString(),QString::number(source->runId()));
                QCOMPARE(result.metadata["pulse_index"].toInt(),8);
                QCOMPARE(result.metadata["pulse_height"].toDouble(),1.);QCOMPARE(result.metadata["response_samples"].toInt(),count-8);
                QCOMPARE(result.metadata["input_unit"].toString(),QString("V"));QCOMPARE(result.metadata["output_unit"].toString(),QString("V"));
                QCOMPARE(result.x.size(),std::size_t(2049));QCOMPARE(result.x.front(),0.);QCOMPARE(result.x.back(),rate/2);
                for (std::size_t k=0;k<result.x.size();++k) {
                    QCOMPARE(result.x[k],double(k)/64);QVERIFY(result.valid[k]);
                    const auto expected=s7tests::education::lti_transfer(channel,result.x[k],rate,edited);
                    QVERIFY(std::abs(result.values[k]-expected)<1e-9);
                }
                auto* magnitude=response->findChild<s7studio::viewers::LinePlotView*>("response.magnitude");QVERIFY(magnitude);
                auto* phase=response->findChild<s7studio::viewers::LinePlotView*>("response.phase");QVERIFY(phase);
                auto* unwrap=response->findChild<QCheckBox*>("response.unwrap");QVERIFY(unwrap && unwrap->isChecked());
                auto* reference=response->findChild<QDoubleSpinBox*>("response.reference");QVERIFY(reference);QCOMPARE(reference->value(),1.);
                auto* overlay=response->findChild<QCheckBox*>("response.referenceModel");QVERIFY(overlay && !overlay->isChecked());
                auto checkBode=[&](bool unwrapped,double gainReference) {
                    QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
                    QCOMPARE(magnitude->xTitle(),QString("log10(f / 1 Hz)"));QCOMPARE(phase->xTitle(),magnitude->xTitle());
                    QCOMPARE(magnitude->yTitle(),QString("Gain (dB, explicit reference)"));
                    QCOMPARE(phase->yTitle(),unwrapped ? QString("Phase (degrees; unwrapped)") : QString("Phase (degrees; wrapped)"));
                    int magnitudePoints=0,phasePoints=0;
                    for (auto* line:magnitude->series()) {
                        if (line->name()=="Analytical reference") continue;
                        QCOMPARE(line->name(),QString("Response"));
                        for (const auto& point:line->points()) {
                            const double f=std::pow(10.,point.x());
                            const auto expected=s7tests::education::lti_transfer(channel,f,rate,edited);
                            QVERIFY(std::abs(point.y()-20*std::log10(std::abs(expected)/gainReference))<1e-7);++magnitudePoints;
                        }
                    }
                    for (auto* line:phase->series()) {
                        if (line->name()=="Analytical reference") continue;
                        QCOMPARE(line->name(),QString("Response"));
                        for (const auto& point:line->points()) {
                        const double f=std::pow(10.,point.x());
                        const double expected=unwrapped ? s7tests::education::lti_phase(channel,f,rate,edited)
                            : std::arg(s7tests::education::lti_transfer(channel,f,rate,edited))*180/std::acos(-1.);
                        if (unwrapped) QVERIFY2(std::abs(point.y()-expected)<1e-6,
                            qPrintable(QString("%1 phase at %2 Hz: actual %3, expected %4").arg(QString::fromStdString(channel)).arg(f,0,'g',17).arg(point.y(),0,'g',17).arg(expected,0,'g',17)));
                        else {QVERIFY(std::abs(std::remainder(point.y()-expected,360.))<1e-6);QVERIFY(std::abs(point.y())<=180+1e-8);}
                        ++phasePoints;
                        }
                    }
                    QCOMPARE(magnitudePoints,2048);QCOMPARE(phasePoints,2048);
                };
                checkBode(true,1);QVERIFY(!QTest::currentTestFailed());capture(response,lab+(edited ? "-edited-" : "-baseline-")+it.key(),true);
                reference->setValue(2);checkBode(true,2);QVERIFY(!QTest::currentTestFailed());
                reference->setValue(1);unwrap->setChecked(false);checkBode(false,1);QVERIFY(!QTest::currentTestFailed());
                capture(response,lab+(edited ? "-edited-" : "-baseline-")+it.key()+"-wrapped",true);
                unwrap->setChecked(true);
                if (channel=="resonant") {
                    auto* omega=response->findChild<QDoubleSpinBox*>("response.omega");QVERIFY(omega);
                    auto* damping=response->findChild<QDoubleSpinBox*>("response.damping");QVERIFY(damping);
                    auto* gain=response->findChild<QDoubleSpinBox*>("response.modelGain");QVERIFY(gain);
                    overlay->setChecked(true);
                    QVERIFY(omega->isEnabled() && damping->isEnabled() && gain->isEnabled());
                    omega->setValue(4);damping->setValue(edited ? .4 : .2);gain->setValue(1);
                    checkBode(true,1);QVERIFY(!QTest::currentTestFailed());
                    for (auto* plot:{magnitude,phase}) {
                        int checked=0;
                        for (auto* line:plot->series()) if (line->name()=="Analytical reference") {
                            for (const auto& point:line->points()) {
                                const double f=std::pow(10.,point.x());const std::complex<double> s(0,2*std::acos(-1.)*f);
                                const auto continuous=16./(s*s+(edited ? 3.2 : 1.6)*s+16.);
                                const double expected=plot==magnitude ? 20*std::log10(std::abs(continuous)) : std::arg(continuous)*180/std::acos(-1.);
                                QVERIFY(std::abs(point.y()-expected)<1e-7);++checked;
                            }
                        }
                        QCOMPARE(checked,2048);
                    }
                    capture(response,lab+(edited ? "-edited-" : "-baseline-")+it.key()+"-continuous-reference",true);
                }
                response->hide();
            }
        };
        run();QVERIFY(!QTest::currentTestFailed());checkViewers(false);QVERIFY(!QTest::currentTestFailed());
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node=scene()->elementNode(convolution ? "weight" : "damping");QVERIFY(node);
        scene()->clearSelection();node->setSelected(true);view->setFocus();
        QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>("control.factor");auto* buttons=dialog->findChild<QDialogButtonBox*>();
            if (!field || !buttons) return false;field->setText(convolution ? "0.75" : "3.2");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
        const auto copy=temporary_.filePath(file+"-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openViewers();run();QVERIFY(!QTest::currentTestFailed());checkViewers(true);QVERIFY(!QTest::currentTestFailed());
    }
    void educationAm() {
        using namespace s7tests::education;
        QVERIFY(fileAction("action.open",QString(S7_GUI_EDUCATION_DIR)+"/SP14-coherent-am/sp14_coherent_am.s7diagram"));openTeachingGuide("SP14","SP14-coherent-am");QVERIFY(!QTest::currentTestFailed());
        const TeachingBindings bindings{{"rf_scope/rf",{"transmit",0,"V"}},{"baseband_scope/target",{"envelope",0,"V"}},
            {"baseband_scope/recovered",{"recovered",0,"V"}},{"mixer_scope/mixed",{"mixed",0,"V"}},
            {"carrier_scope/carrier",{"carrier",0,"1"}},{"carrier_scope/local",{"local",0,"1"}}};
        for(bool edited:{false,true}) {
            const double phase=edited ? std::numbers::pi/3:0;
            if(edited) {editTeachingNumber("local","phase",phase);QVERIFY(!QTest::currentTestFailed());const auto copy=temporary_.filePath("SP14-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));}
            TeachingScopes scopes;openTeachingScopes({"rf_scope","baseband_scope","mixer_scope","carrier_scope"},scopes);
            runTeachingProfile(4,256);QVERIFY(!QTest::currentTestFailed());const auto trace=am_reference(phase);const QString stem=edited ? "SP14-edited":"SP14-baseline";
            checkTeachingCapture(bindings,[&](const std::string& id,std::size_t port,int n) {return trace[std::size_t(n)].at(control_key(id,port));},1024,256,scopes,stem,.25);QVERIFY(!QTest::currentTestFailed());
            auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
            for(const auto& id:QStringList{"rf_scope","baseband_scope","mixer_scope"}) {
                auto* scope=scopes[id].data();QVERIFY(scope);scope->show();
                auto* start=scope->findChild<QDoubleSpinBox*>("scope.analysisStart");auto* end=scope->findChild<QDoubleSpinBox*>("scope.analysisEnd");
                auto* analyze=scope->findChild<QPushButton*>("scope.analyze");auto* tabs=scope->findChild<QTabWidget*>("scope.analysisTabs");
                auto* spectrum=scope->findChild<s7studio::viewers::SpectrumView*>();QVERIFY(start && end && analyze && tabs && spectrum);
                auto* panel=spectrum->controlPanel();auto* window=panel->findChild<QComboBox*>("spectrum.window");auto* quantity=panel->findChild<QComboBox*>("spectrum.quantity");
                auto* scale=panel->findChild<QComboBox*>("spectrum.scale");auto* range=panel->findChild<QComboBox*>("spectrum.frequencyRange");
                auto* all=panel->findChild<QCheckBox*>("spectrum.allInputs");auto* remove=panel->findChild<QCheckBox*>("spectrum.removeMean");
                auto* size=panel->findChild<QComboBox*>("spectrum.size");QVERIFY(window && quantity && scale && range && all && remove && size);
                window->setCurrentIndex(0);scale->setCurrentIndex(0);range->setCurrentIndex(1);all->setChecked(true);remove->setChecked(false);
                start->setValue(2.002);end->setValue(4);analyze->click();tabs->setCurrentIndex(1);size->setCurrentIndex(size->findData(512));
                for(int q=0;q<3;++q) {
                    quantity->setCurrentIndex(q);
                    QTRY_VERIFY_WITH_TIMEOUT(spectrum->result() && spectrum->result()->state==s7studio::analysis::SpectrumResult::State::Ready && spectrum->result()->size==512 && int(spectrum->result()->settings.quantity)==q,10000);
                    const auto& r=*spectrum->result();QCOMPARE(r.runId,runner->recordedSource()->runId());QCOMPARE(r.firstStep,std::int64_t(513));QCOMPARE(r.lastStep,std::int64_t(1024));
                    QCOMPARE(r.sampleRate,256.);QCOMPARE(r.binSpacing,.5);QCOMPARE(r.coherentGain,1.);QVERIFY(r.frame);QCOMPARE(r.frame->steps.size(),qsizetype(512));
                    QCOMPARE(r.traces.size(),std::size_t(id=="baseband_scope" ? 2:1));
                    for(const auto& t:r.traces) {
                        QCOMPARE(t.unit,QString("V"));const auto name=r.frame->channels[qsizetype(t.channel)].name;const auto key=id+"/"+name;QVERIFY(bindings.contains(key));
                        const auto& [element,port,unit]=bindings[key];(void)unit;std::vector<double> values;
                        for(int n=0;n<512;++n) {
                            const auto value=trace[std::size_t(n+512)].at(control_key(element,port));values.push_back(value);
                            QVERIFY(std::abs(r.frame->values[qsizetype(n*r.frame->channels.size()+qsizetype(t.channel))]-value)<1e-10);
                        }
                        const QString plottedUnit=q==0 ? "V":q==1 ? "V²":"V²/Hz";QLineSeries* line=nullptr;
                        for(auto* candidate:spectrum->plotView()->series()) if(candidate->name()==name+" ["+plottedUnit+"]") line=candidate;
                        QVERIFY(line && line->isVisible());QCOMPARE(line->points().size(),qsizetype(257));double integrated=0,meanSquare=0;for(double value:values) meanSquare+=value*value/512;
                        for(std::size_t k=0;k<257;++k) {
                            const auto direct=am_bin(values,k);const double fold=k==0 || k==256 ? 1:2;
                            const auto value=q==0 ? fold*std::abs(direct):fold*std::norm(direct)/(q==2 ? .5:1);
                            QCOMPARE(r.frequencies[k],k*.5);QVERIFY(std::abs(t.values[k]-value)<1e-9);QCOMPARE(line->points()[qsizetype(k)].x(),k*.5);
                            QVERIFY(std::abs(line->points()[qsizetype(k)].y()-value)<1e-9);if(q>0) integrated+=value*(q==2 ? .5:1);
                        }if(q>0) QVERIFY(std::abs(integrated-meanSquare)<1e-10);
                    }
                    QCOMPARE(spectrum->plotView()->xTitle(),QString("Frequency (Hz)"));QCOMPARE(spectrum->plotView()->yTitle(),q==0 ? QString("Peak amplitude (V)"):q==1 ? QString("Power / bin (V²)"):QString("PSD (V²/Hz)"));
                    QCOMPARE(spectrum->plotView()->xMin(),0.);QCOMPARE(spectrum->plotView()->xMax(),128.);
                    if(q!=1) capture(scope,stem+"-"+id+QString("-spectrum-q%1").arg(q),true);
                }
                tabs->setCurrentIndex(0);scope->hide();
            }
        }
    }
    void educationCompensator() {
        using namespace s7tests::education;
        QVERIFY(fileAction("action.open",QString(S7_GUI_EDUCATION_DIR)+"/CT08-lead-lag/ct08_lead_lag.s7diagram"));openTeachingGuide("CT08","CT08-lead-lag");QVERIFY(!QTest::currentTestFailed());
        const TeachingBindings bindings{{"scope/reference",{"reference",0,"1"}},{"scope/P_output",{"base_y",0,"1"}},{"scope/lead_output",{"lead_y",0,"1"}},
            {"command_scope/P_command",{"base",0,"1"}},{"command_scope/lead_command",{"lead",0,"1"}},
            {"pulse_scope/pulse",{"pulse",0,"1"}},{"pulse_scope/open_output",{"open_y",0,"1"}},
            {"response/input",{"pulse",0,"1"}},{"response/output",{"open_y",0,"1"}}};
        for(int variant:{0,1}) {
            if(variant) {editTeachingNumber("zero","value",.2);QVERIFY(!QTest::currentTestFailed());const auto copy=temporary_.filePath("CT08-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));}
            TeachingScopes scopes;openTeachingScopes({"scope","command_scope","pulse_scope"},scopes);
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
            auto* response=dynamic_cast<s7studio::viewers::ResponseWindow*>(manager->openObserver("FrequencyResponse","response"));QVERIFY(response);
            runTeachingProfile(64,10);QVERIFY(!QTest::currentTestFailed());const auto trace=compensator_reference(variant);const QString stem=variant ? "CT08-edited":"CT08-baseline";
            checkTeachingCapture(bindings,[&](const std::string& id,std::size_t port,int n) {return trace[std::size_t(n)].at(control_key(id,port));},640,10,scopes,stem,10);QVERIFY(!QTest::currentTestFailed());
            response->show();while(response->refreshSource()) {}
            QTRY_VERIFY_WITH_TIMEOUT(response->hasResult() || !response->error().isEmpty(),10000);QVERIFY2(response->hasResult(),qPrintable(response->error()));QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
            auto* reference=response->findChild<QDoubleSpinBox*>("response.reference");QVERIFY(reference);QCOMPARE(reference->value(),1.);
            auto* unwrap=response->findChild<QCheckBox*>("response.unwrap");QVERIFY(unwrap && unwrap->isChecked());
            const auto path=temporary_.filePath(stem+".s7analysis");response->saveResult(path);const auto result=s7studio::analysis::ResultFile::open(path);
            QCOMPARE(result.x.size(),std::size_t(513));QCOMPARE(result.metadata["pulse_index"].toInt(),8);QCOMPARE(result.metadata["response_samples"].toInt(),632);
            QCOMPARE(result.metadata["input_unit"].toString(),QString("1"));QCOMPARE(result.metadata["output_unit"].toString(),QString("1"));
            for(std::size_t k=0;k<513;++k) {QCOMPARE(result.x[k],k*10./1024);QVERIFY(result.valid[k]);QVERIFY(std::abs(result.values[k]-compensator_transfer(result.x[k],variant))<1e-10);}
            auto* magnitude=response->findChild<s7studio::viewers::LinePlotView*>("response.magnitude");auto* phase=response->findChild<s7studio::viewers::LinePlotView*>("response.phase");
            s7studio::viewers::LinePlotView* nyquist=nullptr;for(auto* p:response->findChildren<s7studio::viewers::LinePlotView*>()) if(p->xTitle()=="Re(H)") nyquist=p;
            QVERIFY(magnitude && phase && nyquist);auto* tabs=response->findChild<QTabWidget*>("response.views");QVERIFY(tabs);
            for(int p=0;p<3;++p) {
                auto* plot=p==0 ? magnitude:p==1 ? phase:nyquist;tabs->setCurrentIndex(p==2 ? 1:0);
                QTRY_VERIFY(plot->quickWidget());QTRY_COMPARE(plot->quickWidget()->status(),QQuickWidget::Ready);
                std::size_t count=0;
                for(auto* series:plot->series()) if(series->isVisible()) for(const auto& point:series->points()) {
                    const auto k=p==2 ? count:count+1;QVERIFY(k<513);const auto f=result.x[k],theta=2*std::numbers::pi*f/10;
                    const auto h=compensator_transfer(f,variant);const double a=variant ? .2:.8;
                    const auto degrees=(std::atan2(std::sin(theta),std::cos(theta)-a)-std::atan2(std::sin(theta),std::cos(theta)-.2)-2*std::atan2(std::sin(theta),std::cos(theta)-.9))*180/std::numbers::pi;
                    QVERIFY(std::abs(point.x()-(p==2 ? h.real():std::log10(f)))<1e-9);
                    QVERIFY(std::abs(point.y()-(p==0 ? 20*std::log10(std::abs(h)):p==1 ? degrees:h.imag()))<1e-8);++count;
                }QCOMPARE(count,std::size_t(p==2 ? 513:512));capture(response,stem+(p==2 ? "-nyquist":"-bode"),true);
            }
            response->openResult(path);QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);const auto copy=temporary_.filePath(stem+"-reopened.s7analysis");response->saveResult(copy);QCOMPARE(s7studio::analysis::ResultFile::open(copy).values,result.values);response->hide();
        }
    }
    void educationLoop_data() {
        QTest::addColumn<bool>("margins");QTest::newRow("CT06")<<false;QTest::newRow("CT07")<<true;
    }
    void educationLoop() {
        QFETCH(bool,margins);using namespace s7tests::education;
        const QString lab=margins ? "CT07":"CT06",file=margins ? "ct07_stability_margins":"ct06_closed_loop_poles",slug=margins ? "CT07-stability-margins":"CT06-closed-loop-poles";
        QVERIFY(fileAction("action.open",QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));openTeachingGuide(lab,slug);QVERIFY(!QTest::currentTestFailed());
        TeachingBindings bindings;
        if(margins) bindings={{"scope/reference",{"reference",0,"1"}},{"scope/closed_output",{"closed_y",0,"1"}},
            {"pulse_scope/pulse",{"pulse",0,"1"}},{"pulse_scope/open_output",{"open_y",0,"1"}},
            {"response/input",{"pulse",0,"1"}},{"response/output",{"open_y",0,"1"}}};
        else bindings={{"scope/reference",{"reference",0,"1"}},{"scope/first_state",{"x",0,"1"}},{"scope/output",{"y",0,"1"}},
            {"command_scope/command",{"command",0,"1"}},{"command_scope/error",{"error",0,"1"}}};
        TeachingScopes scopes;QPointer<s7studio::viewers::ResponseWindow> response;
        auto open=[&] {
            openTeachingScopes({"scope",margins ? "pulse_scope":"command_scope"},scopes);
            if(margins) {
                auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
                response=dynamic_cast<s7studio::viewers::ResponseWindow*>(manager->openObserver("FrequencyResponse","response"));QVERIFY(response);
            }
        };
        for(bool edited:{false,true}) {
            if(edited) {
                editTeachingNumber("gain","value",16);QVERIFY(!QTest::currentTestFailed());const auto copy=temporary_.filePath(file+"-edited.s7diagram");
                QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
            }
            open();runTeachingProfile(margins ? 64:32,10);QVERIFY(!QTest::currentTestFailed());
            const double gain=edited ? 16:4;const auto trace=loop_reference(margins,gain);const auto stem=lab+(edited ? "-edited":"-baseline");
            checkTeachingCapture(bindings,[&](const std::string& id,std::size_t port,int n) {return trace[std::size_t(n)].at(control_key(id,port));},int(trace.size()),10,scopes,stem);QVERIFY(!QTest::currentTestFailed());
            if(!margins) continue;
            response->show();while(response->refreshSource()) {}
            QTRY_VERIFY_WITH_TIMEOUT(response->hasResult() || !response->error().isEmpty(),10000);QVERIFY2(response->hasResult(),qPrintable(response->error()));
            QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
            auto* reference=response->findChild<QDoubleSpinBox*>("response.reference");QVERIFY(reference);QCOMPARE(reference->value(),1.);
            auto* unwrap=response->findChild<QCheckBox*>("response.unwrap");QVERIFY(unwrap && unwrap->isChecked());
            const auto path=temporary_.filePath(stem+".s7analysis");response->saveResult(path);const auto result=s7studio::analysis::ResultFile::open(path);
            QCOMPARE(result.kind,QString("frequency_response"));QCOMPARE(result.x.size(),std::size_t(513));QCOMPARE(result.x.front(),0.);QCOMPARE(result.x.back(),5.);
            QCOMPARE(result.metadata["pulse_index"].toInt(),8);QCOMPARE(result.metadata["response_samples"].toInt(),632);
            QCOMPARE(result.metadata["input_unit"].toString(),QString("1"));QCOMPARE(result.metadata["output_unit"].toString(),QString("1"));
            for(std::size_t k=0;k<result.x.size();++k) {QCOMPARE(result.x[k],k*10./1024);QVERIFY(result.valid[k]);QVERIFY(std::abs(result.values[k]-loop_transfer(result.x[k],gain))<1e-10);}
            auto* magnitude=response->findChild<s7studio::viewers::LinePlotView*>("response.magnitude");auto* phase=response->findChild<s7studio::viewers::LinePlotView*>("response.phase");
            s7studio::viewers::LinePlotView* nyquist=nullptr;for(auto* plot:response->findChildren<s7studio::viewers::LinePlotView*>()) if(plot->xTitle()=="Re(H)") nyquist=plot;
            QVERIFY(magnitude && phase && nyquist);QCOMPARE(nyquist->yTitle(),QString("Im(H)"));
            auto* tabs=response->findChild<QTabWidget*>("response.views");QVERIFY(tabs);
            for(int which=0;which<3;++which) {
                auto* plot=which==0 ? magnitude:which==1 ? phase:nyquist;tabs->setCurrentIndex(which==2 ? 1:0);
                QTRY_VERIFY(plot->quickWidget());QTRY_COMPARE(plot->quickWidget()->status(),QQuickWidget::Ready);
                std::size_t points=0;
                for(auto* line:plot->series()) if(line->isVisible()) for(const auto& point:line->points()) {
                    const std::size_t k=which==2 ? points:points+1;QVERIFY(k<result.x.size());const auto h=loop_transfer(result.x[k],gain);
                    const auto theta=2*std::numbers::pi*result.x[k]/10;
                    const auto degrees=-2*std::atan2(std::sin(theta),std::cos(theta)-.9)*180/std::numbers::pi;
                    const auto x=which==2 ? h.real():std::log10(result.x[k]);const auto y=which==0 ? 20*std::log10(std::abs(h)):which==1 ? degrees:h.imag();
                    QVERIFY(std::abs(point.x()-x)<1e-9);QVERIFY(std::abs(point.y()-y)<1e-8);++points;
                }
                QCOMPARE(points,std::size_t(which==2 ? 513:512));capture(response,stem+(which==2 ? "-nyquist":"-bode"),true);
            }
            // Inspector must label the actual nearest bin, not the requested continuous crossover.
            auto* frequency=response->findChild<QDoubleSpinBox*>("response.frequency");QVERIFY(frequency);
            frequency->setValue(loop_margins(gain).phaseCrossover);
            QLabel* inspection=nullptr;for(auto* label:response->findChildren<QLabel*>()) if(label->text().contains("| H=(")) inspection=label;
            QVERIFY(inspection);const auto bin=std::size_t(std::llround(frequency->value()*1024/10));
            QTRY_VERIFY(inspection->text().startsWith(QString::number(result.x[bin],'g',9)+" Hz"));
            response->openResult(path);response->show();QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(),10000);
            const auto reopened=temporary_.filePath(stem+"-reopened.s7analysis");response->saveResult(reopened);
            QCOMPARE(s7studio::analysis::ResultFile::open(reopened).values,result.values);response->hide();
        }
    }
    void educationControl_data() {
        QTest::addColumn<bool>("windup");QTest::newRow("CT04")<<false;QTest::newRow("CT05")<<true;
    }
    void educationControl() {
        QFETCH(bool,windup);
        using namespace s7tests::education;
        const QString lab=windup ? "CT05":"CT04",file=windup ? "ct05_actuator_windup":"ct04_controller_comparison";
        const QString slug=windup ? "CT05-actuator-windup":"CT04-controller-comparison";
        const int count=windup ? 1200:1600;const double seconds=windup ? 12:16;
        QVERIFY(fileAction("action.open",QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* root=editor();auto* view=root->findChild<QGraphicsView*>();QVERIFY(view);
        const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        auto frame=frames.front();QSet<QString> links;QPoint guide(-1,-1);
        for(int y=8;y<frame->height()-8;y+=3) for(int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if(!url.isEmpty()) links.insert(url.toString());
            if(url.path().contains("/labs/") && guide.x()<0) guide=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));QVERIFY(view->viewport()->rect().contains(guide));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guide);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");QVERIFY(browser && browser->source().path().contains(slug));
        QVERIFY(browser->toPlainText().contains("Predict"));capture(help,lab+"-student-guide",true);help->close();
        capture(window_.get(),lab+"-diagram",true);
        using Binding=std::pair<std::string,std::size_t>;QMap<QString,Binding> bindings;
        if(windup) bindings={{"scope/reference",{"reference",0}}, {"scope/aw_output",{"aw_y",0}}, {"scope/free_output",{"free_y",0}},
            {"actuator_scope/aw_control",{"aw",0}}, {"actuator_scope/free_control",{"free",0}},
            {"raw_scope/aw_unlimited",{"aw",1}}, {"raw_scope/free_unlimited",{"free",1}},
            {"state_scope/aw_integral",{"aw",2}}, {"state_scope/free_integral",{"free",2}}};
        else {
            bindings.insert("scope/reference",{"reference_step",0});
            for(const std::string mode:{"p","pi","pid"}) {
                const auto m=QString::fromStdString(mode);bindings.insert("scope/"+m+"_output",{mode+"_y",0});
                bindings.insert("error_scope/"+m+"_error",{mode+"_error",0});
                bindings.insert("actuator_scope/"+m+"_control",{mode=="p" ? "p_limit":mode,0});
            }
        }
        QMap<QString,QPointer<s7studio::ui::ScopeViewWindow>> scopes;
        auto openScopes=[&] {
            scopes.clear();auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
            for(const auto& fullName:bindings.keys()) {
                const auto id=fullName.section('/',0,0);if(scopes.contains(id)) continue;
                auto* scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope",id));QVERIFY(scope);scopes[id]=scope;
            }
        };
        auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);QSignalSpy complete(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&] {
            complete.clear();QVERIFY(dialogAction("action.run",[&](QDialog* dialog) {
                auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");auto* frequency=dialog->findChild<QDoubleSpinBox*>("run.frequency");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!duration || !frequency || !buttons || duration->value()!=seconds || frequency->value()!=100) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!complete.empty(),10000);QVERIFY2(complete.last()[0].toBool(),qPrintable(complete.last()[1].toString()));
            QCOMPARE(runner->completedTicks(),long(count));
        };
        auto check=[&](bool edited) {
            const auto expected=control_reference(windup,edited);
            const auto source=runner->recordedSource();QVERIFY(source && source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration();QVERIFY(config);QCOMPARE(config->probes.size(),std::size_t(bindings.size()));
            for(const auto& probe:config->probes) {
                const auto name=QString::fromStdString(probe.name);QVERIFY(bindings.contains(name));
                QCOMPARE(probe.element,bindings[name].first);QCOMPARE(probe.port,bindings[name].second);QCOMPARE(probe.unit,std::string("1"));
            }
            const auto channels=source->channels();QCOMPARE(channels.size(),bindings.size());
            QMap<s7studio::observation::ChannelId,int> counts;std::uint64_t cursor=0;
            while(true) {
                const auto page=source->read(cursor,4096);QVERIFY(page.error.isEmpty());if(page.samples.isEmpty()) break;
                for(const auto& sample:page.samples) {
                    QString name;for(const auto& c:channels) if(c.id==sample.channel) {name=c.name;}
                    QVERIFY(bindings.contains(name));const auto binding=bindings[name];
                    QCOMPARE(sample.step,qint64(++counts[sample.channel]));QCOMPARE(sample.timeSeconds,sample.step/100.);
                    const auto value=expected[std::size_t(sample.step-1)].at(control_key(binding.first,binding.second));
                    QVERIFY(std::abs(sample.value-value)<1e-10*std::max(1.,std::abs(value)));
                }
                QVERIFY(page.next>cursor);cursor=page.next;
            }
            QCOMPARE(counts.size(),channels.size());for(auto n:counts) QCOMPARE(n,count);
            for(auto it=scopes.cbegin();it!=scopes.cend();++it) {
                auto* scope=it.value().data();QVERIFY(scope);scope->show();scope->resize(1100,750);
                QTRY_VERIFY(scope->source() && scope->source()->status().finished);while(scope->refreshSource()) {}
                int expectedChannels=0;for(const auto& key:bindings.keys()) expectedChannels+=key.startsWith(it.key()+"/");
                QTRY_COMPARE(scope->recording().series().size(),qsizetype(expectedChannels));
                for(auto s=scope->recording().series().cbegin();s!=scope->recording().series().cend();++s) {
                    QVERIFY(!s->isComparison());QCOMPARE(s->receivedSamples,std::uint64_t(count));QCOMPARE(s->channelUnit,QString("1"));
                    const auto binding=bindings[it.key()+"/"+s->channelName];
                    QCOMPARE(s->time.size(),qsizetype(count));QCOMPARE(s->value.size(),qsizetype(count));
                    for(int n=0;n<count;++n) {
                        QCOMPARE(s->time[n],(n+1)/100.);const auto value=expected[std::size_t(n)].at(control_key(binding.first,binding.second));
                        QVERIFY(std::abs(s->value[n]-value)<1e-10*std::max(1.,std::abs(value)));
                    }
                    auto* line=scope->plottedSeries(s.key());QVERIFY(line);QTRY_VERIFY(!line->points().empty());QTRY_COMPARE(line->points().back().x(),seconds);
                    QCOMPARE(line->name(),s->displayName());QVERIFY(line->isVisible());const auto points=line->points();
                    QCOMPARE(points.front().x(),.01);QVERIFY(points.size()<=count);double previous=0,low=points.front().y(),high=low;
                    for(const auto& point:points) {
                        const auto n=int(std::llround(point.x()*100))-1;QVERIFY(n>=0 && n<count);QVERIFY(point.x()>previous);previous=point.x();
                        QCOMPARE(point.x(),s->time[n]);QCOMPARE(point.y(),s->value[n]);low=std::min(low,point.y());high=std::max(high,point.y());
                    }
                    QCOMPARE(low,*std::min_element(s->value.cbegin(),s->value.cend()));QCOMPARE(high,*std::max_element(s->value.cbegin(),s->value.cend()));
                }
                QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),QString("value (1)"));
                QCOMPARE(scope->plotView()->xMin(),.01);QCOMPARE(scope->plotView()->xMax(),seconds);
                capture(scope,lab+(edited ? "-edited-":"-baseline-")+it.key(),true);scope->hide();
            }
        };
        openScopes();run();check(false);QVERIFY(!QTest::currentTestFailed());
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        s7studio::diagram::DiagramEditor* edit=root;
        if(windup) {
            // Exercise the same double-click instance navigation as the guide.
            QVERIFY(QMetaObject::invokeMethod(root,"elementNodeDoubleClicked",Qt::DirectConnection,Q_ARG(QString,"aw"),Q_ARG(QString,"PIController"),Q_ARG(int,1)));
            auto* tabs=window_->findChild<s7studio::SchemaTabs*>();QVERIFY(tabs);edit=tabs->activeEditor();QVERIFY(edit && edit!=root);
        }
        auto node=edit->scene()->elementNode(windup ? "i_rate":"kd");QVERIFY(node);edit->showItem(node.get());
        auto* canvas=edit->findChild<QGraphicsView*>();QVERIFY(canvas);canvas->setFocus();
        QVERIFY(modalWorkflow([&] {QTest::keyClick(canvas->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>(windup ? "control.weights":"control.value");auto* buttons=dialog->findChild<QDialogButtonBox*>();
            if(!field || !buttons) return false;field->setText(windup ? "[1,0]":"0");QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
        if(windup) {
            capture(window_.get(),lab+"-nested-edit",true);bool rootFound=false;
            for(auto* tabs:window_->findChildren<QTabWidget*>()) if(tabs->indexOf(root)>=0) {tabs->setCurrentWidget(root);rootFound=true;break;}
            QVERIFY(rootFound);
        }
        const auto copy=temporary_.filePath(file+"-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openScopes();run();check(true);
    }
    void hiddenPlotReplacementClearsOldCurves() {
        s7studio::viewers::LinePlotView plot;plot.resize(640,400);
        plot.setXRange(0,1);plot.setYRange(0,1);
        auto* old=plot.addSeries("old",{QColor("#d62728"),3,false});
        s7studio::viewers::LinePlotView::setPoints(old,{{0,.8},{1,.8}});plot.show();
        QTRY_VERIFY(plot.quickWidget());QTRY_COMPARE(plot.quickWidget()->status(),QQuickWidget::Ready);
        auto* graph=plot.quickWidget()->rootObject()->findChild<QQuickItem*>("viewer.graphs");QVERIFY(graph);
        auto colorCounts=[&] {
            const auto image=plot.quickWidget()->grabFramebuffer();std::array<int,2> count{};
            const auto area=graph->mapRectToScene(graph->property("plotArea").toRectF());
            const double dpr=image.devicePixelRatio();
            const auto clipped=QRectF(area.topLeft()*dpr,area.size()*dpr).toAlignedRect().intersected(image.rect());
            // Font subpixel antialiasing can create red/blue label pixels.
            // Only data inside the graph's current plot area counts.
            for(int y=clipped.top();y<=clipped.bottom();++y) for(int x=clipped.left();x<=clipped.right();++x) {
                const auto c=image.pixelColor(x,y);
                count[0]+=c.red()>150 && c.green()<100 && c.blue()<100;
                count[1]+=c.blue()>150 && c.red()<100 && c.green()<140;
            }return count;
        };
        QTRY_VERIFY(colorCounts()[0]>50);
        plot.hide();plot.clearSeries();
        auto* current=plot.addSeries("current",{QColor("#1565c0"),3,false});
        s7studio::viewers::LinePlotView::setPoints(current,{{0,.2},{1,.2}});plot.show();
        QTRY_VERIFY(colorCounts()[1]>50);capture(&plot,"hidden-plot-replacement",true);QTRY_COMPARE(colorCounts()[0],0);
        QCOMPARE(plot.series().size(),qsizetype(1));
    }
    void educationStft() {
        using namespace s7tests::education;
        QVERIFY(fileAction("action.open",QString(S7_GUI_EDUCATION_DIR)+"/SP11-short-time-fourier/sp11_short_time_fourier.s7diagram"));
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);
        const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front();QPoint guidePoint(-1,-1);QSet<QString> links;
        for(int y=8;y<frame->height()-8;y+=3) for(int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if(!url.isEmpty()) links.insert(url.toString());
            if(url.path().contains("/labs/") && guidePoint.x()<0) guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));QVERIFY(view->viewport()->rect().contains(guidePoint));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");QVERIFY(browser && browser->source().path().contains("SP11-short-time-fourier"));
        QVERIFY(browser->toPlainText().contains("Predict"));capture(help,"SP11-student-guide",true);help->close();
        capture(window_.get(),"SP11-diagram",true);
        QPointer<s7studio::ui::ScopeViewWindow> scope;
        QMap<QString,QPointer<s7studio::viewers::TimeFrequencyWindow>> maps;
        auto openViewers=[&] {
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);maps.clear();
            scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope","scope"));QVERIFY(scope);
            for(const auto& id:{QString("burst_map"),QString("map")}) {
                auto* map=dynamic_cast<s7studio::viewers::TimeFrequencyWindow*>(manager->openObserver("TimeFrequency",id));QVERIFY(map);maps[id]=map;
            }
        };
        openViewers();auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&] {
            completed.clear();QVERIFY(dialogAction("action.run",[](QDialog* dialog) {
                auto* rate=dialog->findChild<QDoubleSpinBox*>("run.frequency");auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!rate || !duration || !buttons || rate->value()!=128 || duration->value()!=4) return false;
                buttons->button(QDialogButtonBox::Ok)->click();return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        };
        auto check=[&](bool edited) {
            const auto source=runner->recordedSource();QVERIFY(source && source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration();QVERIFY(config);QCOMPARE(config->dt,1./128);QCOMPARE(runner->completedTicks(),512L);
            const QMap<QString,QString> binding{{"scope/burst","burst"},{"scope/chirp","chirp"},{"scope/mixture","mixture"},
                                               {"burst_map/in","burst"},{"map/in","mixture"}};
            for(auto it=binding.cbegin();it!=binding.cend();++it) {
                bool found=false;for(const auto& p:config->probes) if(QString::fromStdString(p.name)==it.key()) {
                    QCOMPARE(QString::fromStdString(p.element),it.value());found=true;
                }QVERIFY(found);
            }
            const auto channels=source->channels();QCOMPARE(channels.size(),qsizetype(5));
            const auto page=source->read(0,4096);QVERIFY(page.error.isEmpty());QCOMPARE(page.samples.size(),qsizetype(2560));
            QMap<s7studio::observation::ChannelId,int> count;
            for(const auto& sample:page.samples) {
                QCOMPARE(sample.step,qint64(++count[sample.channel]));QCOMPARE(sample.timeSeconds,sample.step/128.);
                QString name;for(const auto& c:channels) if(c.id==sample.channel) name=c.name;
                QVERIFY(binding.contains(name));QVERIFY(std::abs(sample.value-stft_signal(binding[name].toStdString(),int(sample.step)-1,edited))<1e-10);
            }
            QCOMPARE(count.size(),qsizetype(5));for(auto n:count) QCOMPARE(n,512);
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);while(scope->refreshSource()) {}
            QCOMPARE(scope->recording().series().size(),qsizetype(3));
            for(auto it=scope->recording().series().cbegin();it!=scope->recording().series().cend();++it) {
                QCOMPARE(it->receivedSamples,std::uint64_t(512));QCOMPARE(it->channelUnit,QString("V"));
                auto* line=scope->plottedSeries(it.key());QVERIFY(line);QTRY_COMPARE(line->points().size(),qsizetype(512));
                QCOMPARE(line->name(),it->displayName());
                for(int n=0;n<512;++n) {
                    QCOMPARE(line->points()[n].x(),(n+1)/128.);QCOMPARE(line->points()[n].y(),it->value[n]);
                    QVERIFY(std::abs(it->value[n]-stft_signal(it->channelName.toStdString(),n,edited))<1e-10);
                }
            }
            QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),QString("value (V)"));
            capture(scope,QString("SP11-")+(edited ? "edited" : "baseline")+"-time",true);scope->hide();
            const boost::json::value schema=window_->currentDocument().at("schema");
            for(auto it=maps.cbegin();it!=maps.cend();++it) {
                auto map=it.value();QVERIFY(map);map->show();map->raise();while(map->refreshSource()) {}
                QTRY_VERIFY_WITH_TIMEOUT(map->hasResult() || !map->error().isEmpty(),15000);QVERIFY2(map->hasResult(),qPrintable(map->error()));
                auto* fft=map->findChild<QComboBox*>("timeFrequency.fftSize");auto* hop=map->findChild<QSpinBox*>("timeFrequency.hopSize");
                auto* quantity=map->findChild<QComboBox*>("timeFrequency.quantity");auto* tabs=map->findChild<QTabWidget*>("timeFrequency.views");
                auto* row=map->findChild<QSpinBox*>("timeFrequency.row");auto* column=map->findChild<QSpinBox*>("timeFrequency.column");
                auto* band=map->findChild<QDoubleSpinBox*>("timeFrequency.displayMaximum");
                auto* plot=map->findChild<s7studio::viewers::SurfacePlotView*>();
                auto* slice=map->findChild<s7studio::viewers::LinePlotView*>("timeFrequency.frequencySlice");
                auto* trace=map->findChild<s7studio::viewers::LinePlotView*>("timeFrequency.inputTrace");
                QVERIFY(fft && hop && quantity && tabs && row && column && band && plot && slice && trace);band->setValue(64);
                std::vector<double> input;for(int n=0;n<512;++n) input.push_back(stft_signal(it.key()=="map" ? "mixture" : "burst",n,edited));
                for(const auto [n,h]:std::array<std::pair<int,int>,3>{{{64,32},{128,64},{64,16}}}) {
                    fft->setCurrentIndex(fft->findData(n));hop->setValue(h);
                    QTRY_VERIFY_WITH_TIMEOUT(map->hasResult() && map->result()->metadata["window_size"].toInt()==n &&
                        map->result()->metadata["hop_size"].toInt()==h && !map->isPreparingDisplay(),15000);
                    QVERIFY2(map->error().isEmpty(),qPrintable(map->error()));const auto& result=*map->result();
                    QCOMPARE(result.metadata["run_id"].toString(),QString::number(source->runId()));QCOMPARE(result.metadata["unit"].toString(),QString("V"));
                    QCOMPARE(result.metadata["first_step"].toString(),QString("1"));QCOMPARE(result.metadata["last_step"].toString(),QString("512"));
                    QVERIFY(result.metadata["boundary"].toString().contains("full windows only"));
                    QCOMPARE(result.metadata["rate_hz"].toDouble(),128.);QCOMPARE(result.metadata["origin_time"].toDouble(),1./128);
                    QCOMPARE(result.input.size(),std::size_t(512));for(int i=0;i<512;++i) QVERIFY(std::abs(result.input[std::size_t(i)]-input[std::size_t(i)])<1e-10);
                    const auto expected=stft_reference(input,std::size_t(n),std::size_t(h));QVERIFY(result.x==expected.times && result.y==expected.frequencies);
                    QCOMPARE(result.values.size(),expected.coefficients.size());
                    for(std::size_t i=0;i<result.values.size();++i) {QVERIFY(result.valid[i]);QVERIFY(std::abs(result.values[i]-expected.coefficients[i])<1e-9);}
                    column->setValue(128/h);row->setValue(32*n/128);
                    for(int q=0;q<5;++q) {
                        tabs->setCurrentIndex(0);quantity->setCurrentIndex(q);
                        QTRY_VERIFY_WITH_TIMEOUT(!map->isPreparingDisplay() && !plot->isPreparing(),10000);
                        const auto grid=plot->displayedGrid();QVERIFY(grid);QVERIFY(grid->x==expected.times && grid->y==expected.frequencies);
                        QCOMPARE(grid->values.size(),expected.coefficients.size());
                        const QString unit=q==2 ? "rad" : q==4 ? "V²/Hz" : q==0 ? "V" : "V²";
                        const auto label=quantity->currentText()+" ("+unit+")";
                        bool unitLabel=false;for(const auto& l:plot->bridge()->labels()) if(l.toMap()["text"].toString()==label) unitLabel=true;QVERIFY(unitLabel);
                        for(std::size_t k=0;k<expected.frequencies.size();++k) for(std::size_t t=0;t<expected.times.size();++t) {
                            const auto i=k*expected.times.size()+t;const auto c=expected.coefficients[i];
                            const bool valid=q!=2 || std::abs(c)>1e-12;QCOMPARE(bool(grid->present[i]),valid);
                            if(!valid) continue;
                            const auto value=stft_quantity(c,k,std::size_t(n),q);
                            const auto error=q==2 ? std::remainder(grid->values[i].real()-value,2*std::numbers::pi) : grid->values[i].real()-value;
                            QVERIFY(std::abs(error)<(q==2 ? 1e-6 : 1e-9));
                        }
                        if(!edited && it.key()=="burst_map" && n==64 && h==32 && q==2) {
                            // At 19% of the numeric time extent, nearest center
                            // is column 3. Check the coordinate-rasterized color
                            // and cursor agree, rather than trusting matrix indices.
                            auto* heat=map->findChild<QWidget*>("surface.heatmap");QVERIFY(heat);
                            map->raise();map->activateWindow();QVERIFY(QTest::qWaitForWindowActive(map));
                            QPoint position;
                            QVERIFY(hoverUntil(heat,[&] {
                                const auto area=QRectF(heat->rect()).adjusted(62,16,-18,-52);
                                position=QPointF(area.left()+.19*area.width(),area.bottom()-area.height()*4/64).toPoint();
                                return position;
                            },[&] {return column->value()==3 && row->value()==2;}));
                            const auto image=heat->grab().toImage();const auto color=image.pixelColor((position.x())*image.devicePixelRatio(),(position.y()+3)*image.devicePixelRatio());
                            QVERIFY(std::max({color.red(),color.green(),color.blue()})-std::min({color.red(),color.green(),color.blue()})>80);
                            column->setValue(128/h);row->setValue(32*n/128);
                        }
                        tabs->setCurrentIndex(2);QTRY_VERIFY_WITH_TIMEOUT(!map->isPreparingDisplay(),10000);
                        QCOMPARE(slice->xTitle(),QString("Frequency (Hz)"));QCOMPARE(slice->yTitle(),label);
                        int points=0;for(auto* line:slice->series()) for(const auto& p:line->points()) {
                            const auto k=std::size_t(std::llround(p.x()*n/128));QCOMPARE(p.x(),expected.frequencies[k]);
                            const auto c=expected.coefficients[k*expected.times.size()+std::size_t(column->value())];
                            QVERIFY(q!=2 || std::abs(c)>1e-12);const auto value=stft_quantity(c,k,std::size_t(n),q);
                            const auto error=q==2 ? std::remainder(p.y()-value,2*std::numbers::pi) : p.y()-value;
                            QVERIFY(std::abs(error)<(q==2 ? 1e-6 : 1e-9));++points;
                        }
                        int expectedPoints=0;for(std::size_t k=0;k<expected.frequencies.size();++k)
                            if(q!=2 || std::abs(expected.coefficients[k*expected.times.size()+std::size_t(column->value())])>1e-12) ++expectedPoints;
                        QCOMPARE(points,expectedPoints);
                        if(!edited && it.key()=="map" && n==64 && h==32 && q==4) {
                            // Compare actual rendered strokes with the independent
                            // PSD polyline. Model points alone cannot catch retired
                            // phase/magnitude geometry remaining in Qt Graphs.
                            auto* quick=slice->quickWidget();QVERIFY(quick);QTRY_COMPARE(quick->status(),QQuickWidget::Ready);
                            auto* graph=quick->rootObject()->findChild<QQuickItem*>("viewer.graphs");QVERIFY(graph);
                            double worstPixel=0;
                            auto pixelsMatch=[&] {
                                const auto area=graph->property("plotArea").toRectF();if(area.width()<100 || area.height()<100) return false;
                                const auto image=quick->grabFramebuffer();const double dpr=image.devicePixelRatio();
                                QList<QPointF> curve;
                                for(std::size_t k=0;k<expected.frequencies.size();++k) {
                                    const auto value=stft_quantity(expected.coefficients[k*expected.times.size()+std::size_t(column->value())],k,std::size_t(n),4);
                                    curve.append(graph->mapToScene(QPointF(area.left()+(expected.frequencies[k]-slice->xMin())/(slice->xMax()-slice->xMin())*area.width(),
                                        area.top()+(slice->yMax()-value)/(slice->yMax()-slice->yMin())*area.height()))*dpr);
                                }
                                const auto visible=graph->mapRectToScene(area).toAlignedRect();int blue=0;worstPixel=0;
                                const auto clipped=QRect(int(visible.x()*dpr),int(visible.y()*dpr),int(visible.width()*dpr),int(visible.height()*dpr)).intersected(image.rect());
                                for(int y=clipped.top();y<=clipped.bottom();++y) for(int x=clipped.left();x<=clipped.right();++x) {
                                    const auto color=image.pixelColor(x,y);if(!(color.blue()>150 && color.red()<100 && color.green()<160)) continue;
                                    ++blue;double nearest=1e9;
                                    for(int j=1;j<curve.size();++j) {
                                        const auto a=curve[j-1],delta=curve[j]-a;const double length=delta.x()*delta.x()+delta.y()*delta.y();
                                        const double u=length ? std::clamp(((x-a.x())*delta.x()+(y-a.y())*delta.y())/length,0.,1.) : 0;
                                        nearest=std::min(nearest,std::hypot(x-a.x()-u*delta.x(),y-a.y()-u*delta.y()));
                                    }worstPixel=std::max(worstPixel,nearest);
                                }
                                return blue>50 && worstPixel<3;
                            };
                            QTRY_VERIFY_WITH_TIMEOUT(pixelsMatch(),5000);QVERIFY2(worstPixel<3,qPrintable(QString("Unexpected stale stroke, %1 px from independent PSD").arg(worstPixel)));
                            capture(map,"SP11-baseline-map-slice-PSD",true);
                        }
                        tabs->setCurrentIndex(0);QTRY_VERIFY_WITH_TIMEOUT(!plot->isPreparing(),10000);
                        if(q==0 || (n==64 && h==32 && (q==2 || q==4)))
                            capture(map,QString("SP11-")+(edited ? "edited-" : "baseline-")+it.key()+QString("-N%1-H%2-q%3").arg(n).arg(h).arg(q),true);
                    }
                    QCOMPARE(trace->xTitle(),QString("Model time (s)"));QCOMPARE(trace->yTitle(),QString("Input (V)"));
                    int tracePoints=0;for(auto* line:trace->series()) for(const auto& p:line->points()) {
                        const auto index=std::size_t(std::llround(p.x()*128))-1;QVERIFY(index<512);QCOMPARE(p.x(),(index+1)/128.);
                        QVERIFY(std::abs(p.y()-input[index])<1e-10);++tracePoints;
                    }QCOMPARE(tracePoints,512);
                }
                const auto path=temporary_.filePath(QString("SP11-")+it.key()+ (edited ? "-edited" : "-baseline")+".s7analysis");
                map->saveResult(path);const auto saved=*map->result();hop->setValue(32);map->openResult(path);
                QTRY_VERIFY_WITH_TIMEOUT(!map->isPreparingDisplay(),10000);QCOMPARE(hop->value(),16);QVERIFY(map->result()->values==saved.values);
                for(int fault=0;fault<5;++fault) {
                    auto bad=saved;if(fault==0) bad.metadata["hop_size"]=0;
                    if(fault==1) bad.metadata["hop_size"]=65;
                    if(fault==2) bad.x[0]+=.001;
                    if(fault==3) bad.y[1]+=.125;
                    if(fault==4) bad.input.pop_back();
                    const auto badPath=temporary_.filePath(QString("SP11-invalid-%1.s7analysis").arg(fault));bad.save(badPath);
                    QVERIFY_EXCEPTION_THROWN(map->openResult(badPath),std::invalid_argument);QVERIFY(map->result()->values==saved.values);QCOMPARE(hop->value(),16);
                }
                map->hide();
            }
            QVERIFY(window_->currentDocument().at("schema")==schema);
        };
        run();QVERIFY(!QTest::currentTestFailed());check(false);QVERIFY(!QTest::currentTestFailed());
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node=scene()->elementNode("stop");QVERIFY(node);scene()->clearSelection();node->setSelected(true);view->setFocus();
        QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>("control.start_time");auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!field || !buttons) return false;
            field->setText("1.5");buttons->button(QDialogButtonBox::Ok)->click();return true;
        }));
        const auto copy=temporary_.filePath("sp11-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openViewers();run();QVERIFY(!QTest::currentTestFailed());check(true);QVERIFY(!QTest::currentTestFailed());
    }
    void finishedAnalysisFramesStopWaiting() {
        s7studio::viewers::SpectrumView spectrum;
        spectrum.resize(900,500);spectrum.show();
        auto frame=std::make_shared<s7studio::observation::LiveFrame>();
        frame->runId=42;frame->generation=1;frame->rateNumerator=8;frame->rateDenominator=1;frame->dt=.125;
        frame->channels={{"finite input","V","f64"}};frame->received=8;frame->capacity=8;
        for(int i=1;i<=8;++i) {frame->steps.push_back(i);frame->times.push_back(i/8.);frame->values.push_back(1);}
        spectrum.setAnalysisSize(16);spectrum.setFrame(frame);
        QTRY_VERIFY(spectrum.result() && spectrum.result()->state==s7studio::analysis::SpectrumResult::State::Filling);
        auto finished=std::make_shared<s7studio::observation::LiveFrame>(*frame);finished->finished=true;
        spectrum.setFrame(finished);
        QTRY_VERIFY(spectrum.result() && spectrum.result()->state==s7studio::analysis::SpectrumResult::State::Unavailable);
        QVERIFY(spectrum.result()->message.contains("Run finished"));QVERIFY(spectrum.plotView()->series().front()->points().isEmpty());
        spectrum.setAnalysisSize(8);
        QTRY_VERIFY(spectrum.result() && spectrum.result()->state==s7studio::analysis::SpectrumResult::State::Ready && spectrum.result()->size==8);
        QVERIFY(spectrum.result()->frame->finished);QCOMPARE(spectrum.result()->runId,std::uint64_t(42));
        QCOMPARE(spectrum.result()->firstStep,std::int64_t(1));QCOMPARE(spectrum.result()->lastStep,std::int64_t(8));
        QVERIFY(std::abs(spectrum.result()->values[0]-1)<1e-12);
        s7studio::viewers::PairAnalysisView correlation(s7studio::analysis::PairKind::Correlation);
        correlation.resize(900,500);correlation.show();
        auto pair=std::make_shared<s7studio::observation::LiveFrame>();
        pair->runId=43;pair->generation=1;pair->rateNumerator=128;pair->rateDenominator=1;pair->dt=1./128;
        pair->channels={{"A","V","f64"},{"B","V","f64"}};pair->received=64;pair->capacity=64;
        for(int i=1;i<=64;++i) {pair->steps.push_back(i);pair->times.push_back(i/128.);pair->values.push_back(i);pair->values.push_back(2*i+1);}
        correlation.setAnalysisSize(128);correlation.setFrame(pair);
        QTRY_VERIFY(correlation.result() && correlation.result()->state==s7studio::analysis::PairResult::State::Filling);
        auto pairFinished=std::make_shared<s7studio::observation::LiveFrame>(*pair);pairFinished->finished=true;
        correlation.setFrame(pairFinished);
        QTRY_VERIFY(correlation.result() && correlation.result()->state==s7studio::analysis::PairResult::State::Unavailable);
        QVERIFY(correlation.result()->message.contains("Run finished"));QVERIFY(correlation.plotView()->series().front()->points().isEmpty());
        correlation.setAnalysisSize(64);
        QTRY_VERIFY(correlation.result() && correlation.result()->state==s7studio::analysis::PairResult::State::Ready);
        QCOMPARE(correlation.result()->runId,std::uint64_t(43));QVERIFY(correlation.result()->frame->finished);
    }
    void educationConvolution() {
        QVERIFY(fileAction("action.open",QString(S7_GUI_EDUCATION_DIR)+"/SP10-circular-convolution/sp10_circular_convolution.s7diagram"));
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);
        const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front();QPoint guidePoint(-1,-1),implementationPoint(-1,-1);QSet<QString> links;
        for(int y=8;y<frame->height()-8;y+=3) for(int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if(!url.isEmpty()) links.insert(url.toString());
            if(url.path().contains("/labs/") && guidePoint.x()<0) guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
            if(url.path().endsWith("/docs/CONVOLUTION.md") && implementationPoint.x()<0) implementationPoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));QVERIFY(view->viewport()->rect().contains(guidePoint));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");
        QVERIFY(browser && browser->source().path().contains("SP10-circular-convolution"));
        QVERIFY(browser->toPlainText().contains("Predict"));capture(help,"SP10-student-guide",true);help->close();
        QVERIFY(view->viewport()->rect().contains(implementationPoint));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},implementationPoint);
        QVERIFY(help->isVisible());QVERIFY(browser->source().path().endsWith("/docs/CONVOLUTION.md"));
        QVERIFY(browser->toPlainText().contains("FirConvolution"));help->close();
        capture(window_.get(),"SP10-diagram",true);
        QMap<QString,QPointer<s7studio::ui::ScopeViewWindow>> scopes;
        auto openScopes=[&] {
            scopes.clear();auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
            for(const auto& id:{QString("inputs"),QString("scope")}) {
                auto* scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope",id));QVERIFY(scope);scopes[id]=scope;
            }
        };
        openScopes();auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&] {
            completed.clear();QVERIFY(dialogAction("action.run",[](QDialog* dialog) {
                auto* rate=dialog->findChild<QDoubleSpinBox*>("run.frequency");auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!rate || !duration || !buttons || rate->value()!=8 || duration->value()!=2) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        };
        auto check=[&](bool edited) {
            const auto source=runner->recordedSource();QVERIFY(source && source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration();QVERIFY(config);QCOMPARE(config->dt,.125);QCOMPARE(runner->completedTicks(),16L);
            const QMap<QString,QString> binding{{"inputs/isolated input","linear_input"},{"inputs/period 4 input","cyclic4_input"},
                {"inputs/period 8 input","padded8_input"},{"scope/linear","linear"},{"scope/circular 4","cyclic4"},
                {"scope/padded 8","padded8"},{"scope/padded minus linear","difference"}};
            for(auto it=binding.cbegin();it!=binding.cend();++it) {
                bool found=false;for(const auto& p:config->probes) if(QString::fromStdString(p.name)==it.key()) {
                    QCOMPARE(QString::fromStdString(p.element),it.value());found=true;
                }QVERIFY(found);
            }
            const auto channels=source->channels();QCOMPARE(channels.size(),qsizetype(7));
            const auto page=source->read(0,4096);QVERIFY(page.error.isEmpty());QCOMPARE(page.samples.size(),qsizetype(112));
            QMap<s7studio::observation::ChannelId,int> count;
            for(const auto& sample:page.samples) {
                QCOMPARE(sample.step,qint64(++count[sample.channel]));QCOMPARE(sample.timeSeconds,sample.step/8.);
                QString name;for(const auto& c:channels) if(c.id==sample.channel) name=c.name;
                QVERIFY(binding.contains(name));
                QCOMPARE(sample.value,s7tests::education::convolution_signal(binding[name].toStdString(),int(sample.step)-1,edited));
            }
            QCOMPARE(count.size(),qsizetype(7));for(auto n:count) QCOMPARE(n,16);
            const boost::json::value originalSchema=window_->currentDocument().at("schema");
            for(auto it=scopes.cbegin();it!=scopes.cend();++it) {
                auto scope=it.value();QVERIFY(scope);QTRY_VERIFY(scope->source() && scope->source()->status().finished);
                while(scope->refreshSource()) {}
                QCOMPARE(scope->recording().series().size(),qsizetype(it.key()=="inputs" ? 3 : 4));
                for(auto series=scope->recording().series().cbegin();series!=scope->recording().series().cend();++series) {
                    QCOMPARE(series->receivedSamples,std::uint64_t(16));QCOMPARE(series->channelUnit,QString("V"));
                    auto* line=scope->plottedSeries(series.key());QVERIFY(line);QTRY_COMPARE(line->points().size(),qsizetype(16));
                    QCOMPARE(line->name(),series->displayName());const auto element=binding[it.key()+"/"+series->channelName];QVERIFY(!element.isEmpty());
                    for(int n=0;n<16;++n) {
                        QCOMPARE(line->points()[n].x(),(n+1)/8.);
                        QCOMPARE(line->points()[n].y(),series->value[n]);
                        QCOMPARE(series->value[n],s7tests::education::convolution_signal(element.toStdString(),n,edited));
                    }
                }
                QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),QString("value (V)"));
                QCheckBox* fit=nullptr;for(auto* box:scope->findChildren<QCheckBox*>()) if(box->text()=="Fit full run") fit=box;
                QVERIFY(fit);fit->setChecked(false);scope->plotView()->setXRange(1,2);
                capture(scope,QString("SP10-")+(edited ? "edited-" : "baseline-")+it.key(),true);
                if(it.key()=="scope") {
                    auto* start=scope->findChild<QDoubleSpinBox*>("scope.analysisStart");auto* end=scope->findChild<QDoubleSpinBox*>("scope.analysisEnd");
                    auto* analyze=scope->findChild<QPushButton*>("scope.analyze");auto* tabs=scope->findChild<QTabWidget*>("scope.analysisTabs");
                    auto* spectrum=scope->findChild<s7studio::viewers::SpectrumView*>();QVERIFY(start && end && analyze && tabs && spectrum);
                    auto* controls=spectrum->controlPanel();
                    auto* window=controls->findChild<QComboBox*>("spectrum.window");auto* quantity=controls->findChild<QComboBox*>("spectrum.quantity");
                    auto* scale=controls->findChild<QComboBox*>("spectrum.scale");auto* all=controls->findChild<QCheckBox*>("spectrum.allInputs");
                    auto* remove=controls->findChild<QCheckBox*>("spectrum.removeMean");QVERIFY(window && quantity && scale && all && remove);
                    window->setCurrentIndex(0);quantity->setCurrentIndex(0);scale->setCurrentIndex(0);all->setChecked(true);remove->setChecked(false);
                    start->setValue(1.125);end->setValue(2);analyze->click();tabs->setCurrentIndex(1);
                    QTRY_VERIFY_WITH_TIMEOUT(spectrum->result() && spectrum->result()->state==s7studio::analysis::SpectrumResult::State::Ready &&
                        spectrum->result()->size==8 && spectrum->result()->firstStep==9 && spectrum->result()->lastStep==16 &&
                        spectrum->result()->traces.size()==4,10000);
                    const auto& result=*spectrum->result();QCOMPARE(result.runId,source->runId());QCOMPARE(result.binSpacing,1.);
                    QCOMPARE(result.frame->channels.size(),qsizetype(4));
                    for(const auto& trace:result.traces) {
                        const auto name=result.frame->channels[qsizetype(trace.channel)].name;const auto element=binding["scope/"+name];QVERIFY(!element.isEmpty());
                        std::vector<double> values;for(int n=8;n<16;++n) values.push_back(s7tests::education::convolution_signal(element.toStdString(),n,edited));
                        for(int n=0;n<8;++n) QCOMPARE(result.frame->values[n*4+trace.channel],values[std::size_t(n)]);
                        const auto dft=s7tests::education::signal_dft(values);QLineSeries* line=nullptr;
                        for(auto* candidate:spectrum->plotView()->series()) if(candidate->name()==name+" [V]") line=candidate;
                        QVERIFY(line);QCOMPARE(line->points().size(),qsizetype(5));QCOMPARE(trace.values.size(),std::size_t(5));
                        for(int k=0;k<5;++k) {
                            const double expected=(k==0 || k==4 ? 1 : 2)*std::abs(dft[std::size_t(k)]);
                            QVERIFY(std::abs(trace.values[std::size_t(k)]-expected)<1e-10);
                            QCOMPARE(line->points()[k].x(),double(k));QVERIFY(std::abs(line->points()[k].y()-expected)<1e-10);
                        }
                    }
                    capture(scope,QString("SP10-")+(edited ? "edited" : "baseline")+"-selected-spectrum",true);
                }
                scope->hide();
            }
            QVERIFY(window_->currentDocument().at("schema")==originalSchema);
        };
        run();QVERIFY(!QTest::currentTestFailed());check(false);QVERIFY(!QTest::currentTestFailed());
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node=scene()->elementNode("last_tap");QVERIFY(node);scene()->clearSelection();node->setSelected(true);view->setFocus();
        QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>("control.value");auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!field || !buttons) return false;
            field->setText("0.5");QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
        const auto copy=temporary_.filePath("sp10-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openScopes();run();QVERIFY(!QTest::currentTestFailed());check(true);QVERIFY(!QTest::currentTestFailed());
    }
    void educationSpectral_data() {
        QTest::addColumn<QString>("lab");QTest::newRow("SP08") << "SP08";QTest::newRow("SP09") << "SP09";
    }
    void educationSpectral() {
        QFETCH(QString,lab);
        const bool leakage=lab=="SP08";
        const auto file=leakage ? QString("sp08_dft_leakage") : QString("sp09_window_normalization");
        const auto slug=leakage ? QString("SP08-dft-leakage") : QString("SP09-window-normalization");
        QVERIFY(fileAction("action.open",QString::fromStdString(s7tests::education_diagram_path(S7_GUI_EDUCATION_DIR, (file+".s7diagram").toStdString()).string())));
        auto* view=editor()->findChild<QGraphicsView*>();QVERIFY(view);
        const auto frames=scene()->textFrames();QCOMPARE(frames.size(),std::size_t(1));
        const auto frame=frames.front();QPoint guidePoint(-1,-1);QSet<QString> links;
        for (int y=8;y<frame->height()-8;y+=3) for (int x=10;x<frame->width()-10;x+=3) {
            const auto url=frame->linkAt(QPointF(x,y));if (!url.isEmpty()) links.insert(url.toString());
            if (url.path().contains("/labs/") && guidePoint.x()<0) guidePoint=view->mapFromScene(frame->mapToScene(QPointF(x+2,y+2)));
        }
        QCOMPARE(links.size(),qsizetype(3));QVERIFY(links.contains("qrc:/s7studio/help/education/labs/"+slug+"/README.md"));
        QVERIFY(view->viewport()->rect().contains(guidePoint));QTest::mouseClick(view->viewport(),Qt::LeftButton,{},guidePoint);
        auto* help=window_->findChild<QDialog*>("dialog.help");QVERIFY(help && help->isVisible());
        auto* browser=help->findChild<QTextBrowser*>("help.browser");QVERIFY(browser && browser->source().path().contains(slug));
        QVERIFY(browser->toPlainText().contains("Predict",Qt::CaseInsensitive));capture(help,lab+"-student-guide",true);help->close();
        capture(window_.get(),lab+"-diagram",true);
        QPointer<s7studio::ui::ScopeViewWindow> scope;
        auto openScope=[&] {
            auto* manager=window_->findChild<s7studio::viewers::ViewerManager*>();QVERIFY(manager);
            scope=dynamic_cast<s7studio::ui::ScopeViewWindow*>(manager->openObserver("Scope","scope"));QVERIFY(scope);
        };
        openScope();auto* runner=window_->findChild<s7studio::LocalSchemaRunner*>();QVERIFY(runner);
        QSignalSpy completed(runner,&s7studio::LocalSchemaRunner::runCompleted);
        auto run=[&] {
            completed.clear();QVERIFY(dialogAction("action.run",[](QDialog* dialog) {
                auto* duration=dialog->findChild<QDoubleSpinBox*>("run.duration");auto* rate=dialog->findChild<QDoubleSpinBox*>("run.frequency");
                auto* buttons=dialog->findChild<QDialogButtonBox*>();if (!duration || !rate || !buttons || duration->value()!=2 || rate->value()!=128) return false;
                QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),10000);QVERIFY2(completed.last()[0].toBool(),qPrintable(completed.last()[1].toString()));
        };
        auto check=[&](bool edited) {
            const auto source=runner->recordedSource();QVERIFY(source);QVERIFY(source->status().finished && !source->status().incomplete());
            const auto config=runner->activeConfiguration();QVERIFY(config);QCOMPARE(config->dt,1./128);
            const auto channels=source->channels();QCOMPARE(channels.size(),qsizetype(4));
            auto elementFor=[&](const QString& name) {for (const auto& p:config->probes) if (QString::fromStdString(p.name)==name) return p.element;return std::string{};};
            const QMap<QString,QString> bindings=leakage
                ? QMap<QString,QString>{{"coherent reference","coherent"},{"test tone","test"},{"offset mixture","offset"},{"DC reference","dc"}}
                : QMap<QString,QString>{{"calibration","calibration"},{"strong tone","strong"},{"weak reference","weak"},{"mixture","mixture"}};
            for (auto it=bindings.cbegin();it!=bindings.cend();++it)
                QCOMPARE(QString::fromStdString(elementFor("scope/"+it.key())),it.value());
            const auto page=source->read(0,4096);QVERIFY(page.error.isEmpty());QCOMPARE(page.samples.size(),qsizetype(1024));
            QMap<s7studio::observation::ChannelId,int> counts;
            for (const auto& sample:page.samples) {
                QCOMPARE(sample.step,qint64(++counts[sample.channel]));QCOMPARE(sample.timeSeconds,sample.step/128.);
                QString name;for (const auto& c:channels) if (c.id==sample.channel) name=c.name;
                const auto element=elementFor(name);QVERIFY(!element.empty());
                QVERIFY(std::abs(sample.value-s7tests::education::spectral_signal(lab.toStdString(),element,int(sample.step)-1,edited))<1e-10);
            }
            QCOMPARE(counts.size(),qsizetype(4));for (auto n:counts) QCOMPARE(n,256);QCOMPARE(runner->completedTicks(),256L);
            QTRY_VERIFY(scope->source() && scope->source()->status().finished);while(scope->refreshSource()) {}
            QCOMPARE(scope->recording().series().size(),qsizetype(4));
            for (auto it=scope->recording().series().cbegin();it!=scope->recording().series().cend();++it) {
                QCOMPARE(it->receivedSamples,std::uint64_t(256));QCOMPARE(it->channelUnit,QString("V"));QVERIFY(!it->isComparison());
                auto* line=scope->plottedSeries(it.key());QVERIFY(line);QTRY_COMPARE(line->points().size(),qsizetype(256));
                const auto element=elementFor("scope/"+it->channelName);QVERIFY(!element.empty());QCOMPARE(line->name(),it->displayName());
                const auto points=line->points();
                for (int n=0;n<256;++n) {
                    QCOMPARE(points[n].x(),(n+1)/128.);QCOMPARE(points[n].y(),it->value[n]);
                    QVERIFY(std::abs(points[n].y()-s7tests::education::spectral_signal(lab.toStdString(),element,n,edited))<1e-10);
                }
            }
            QCOMPARE(scope->plotView()->xTitle(),QString("model time (s)"));QCOMPARE(scope->plotView()->yTitle(),QString("value (V)"));
            capture(scope,lab+(edited ? "-edited-time" : "-baseline-time"),true);
            const boost::json::value originalSchema=window_->currentDocument().at("schema");
            auto* start=scope->findChild<QDoubleSpinBox*>("scope.analysisStart");auto* end=scope->findChild<QDoubleSpinBox*>("scope.analysisEnd");
            auto* analyze=scope->findChild<QPushButton*>("scope.analyze");auto* tabs=scope->findChild<QTabWidget*>("scope.analysisTabs");
            auto* spectrum=scope->findChild<s7studio::viewers::SpectrumView*>();QVERIFY(start && end && analyze && tabs && spectrum);
            auto* controls=spectrum->controlPanel();
            auto* window=controls->findChild<QComboBox*>("spectrum.window");auto* quantity=controls->findChild<QComboBox*>("spectrum.quantity");
            auto* scale=controls->findChild<QComboBox*>("spectrum.scale");auto* range=controls->findChild<QComboBox*>("spectrum.frequencyRange");
            auto* all=controls->findChild<QCheckBox*>("spectrum.allInputs");auto* remove=controls->findChild<QCheckBox*>("spectrum.removeMean");
            auto* reference=controls->findChild<QDoubleSpinBox*>("spectrum.reference");auto* floor=controls->findChild<QDoubleSpinBox*>("spectrum.floor");
            auto* dbOffset=controls->findChild<QDoubleSpinBox*>("spectrum.dbOffset");auto* offset=controls->findChild<QDoubleSpinBox*>("spectrum.frequencyOffset");
            QVERIFY(window && quantity && scale && range && all && remove && reference && floor && dbOffset && offset);
            all->setChecked(true);remove->setChecked(false);scale->setCurrentIndex(0);window->setCurrentIndex(0);quantity->setCurrentIndex(0);
            range->setCurrentIndex(1);floor->setValue(-120);dbOffset->setValue(0);offset->setValue(0);reference->setValue(1);
            auto interval=[&](int first,int n) {
                start->setValue(first==0 ? 0 : (first+1)/128.);end->setValue((first+n)/128.);analyze->click();tabs->setCurrentIndex(1);
            };
            auto waitResult=[&](int first,int n,int w,int q,bool subtract) {
                QTRY_VERIFY_WITH_TIMEOUT(spectrum->result() && spectrum->result()->state==s7studio::analysis::SpectrumResult::State::Ready &&
                    spectrum->result()->size==std::size_t(n) && spectrum->result()->firstStep==first+1 &&
                    spectrum->result()->lastStep==first+n && spectrum->result()->settings.window==static_cast<s7studio::analysis::SpectrumWindow>(w) &&
                    spectrum->result()->settings.quantity==static_cast<s7studio::analysis::SpectrumQuantity>(q) && spectrum->result()->settings.removeMean==subtract,10000);
            };
            auto checkInterval=[&](int first,int n,int w,bool subtract,bool takeCapture) {
                window->setCurrentIndex(w);remove->setChecked(subtract);quantity->setCurrentIndex(0);scale->setCurrentIndex(0);interval(first,n);
                waitResult(first,n,w,0,subtract);QVERIFY(!QTest::currentTestFailed());
                const auto frame=spectrum->result()->frame;QVERIFY(frame);QCOMPARE(frame->channels.size(),qsizetype(4));
                std::vector<s7tests::education::LessonSpectrum> expected;
                for (int c=0;c<4;++c) {
                    const auto element=elementFor("scope/"+frame->channels[c].name);QVERIFY(!element.empty());std::vector<double> values;
                    QCOMPARE(frame->channels[c].unit,QString("V"));
                    for (int i=0;i<n;++i) {
                        const double actual=frame->values[i*4+c];QVERIFY(std::abs(actual-s7tests::education::spectral_signal(lab.toStdString(),element,first+i,edited))<1e-10);
                        values.push_back(actual);
                    }
                    expected.push_back(s7tests::education::lesson_spectrum(values,w,128,subtract));
                }
                for (int q=0;q<3;++q) {
                    quantity->setCurrentIndex(q);waitResult(first,n,w,q,subtract);QVERIFY(!QTest::currentTestFailed());
                    const auto& result=*spectrum->result();QCOMPARE(result.runId,source->runId());QCOMPARE(result.traces.size(),std::size_t(4));
                    QCOMPARE(result.sampleRate,128.);QCOMPARE(result.binSpacing,128./n);QCOMPARE(result.frequencies.back(),(n/2)*128./n);
                    QVERIFY(std::abs(result.coherentGain-expected[0].cg)<1e-12);QVERIFY(std::abs(result.enbw-expected[0].enbw)<1e-12);
                    for (const auto& trace:result.traces) {
                        const auto& ref=expected.at(trace.channel);QCOMPARE(trace.values.size(),ref.quantity[q].size());QCOMPARE(trace.unit,QString("V"));
                        double integrated=0;
                        for (std::size_t k=0;k<trace.values.size();++k) {
                            QCOMPARE(result.frequencies[k],k*128./n);QVERIFY(std::abs(trace.values[k]-ref.quantity[q][k])<1e-9);
                            if (q>0) integrated+=trace.values[k]*(q==2 ? 128./n : 1);
                        }
                        if (q>0) QVERIFY(std::abs(integrated-ref.weightedPower)<1e-9);
                    }
                    const QString unit=q==0 ? "V" : q==1 ? "V²" : "V²/Hz";
                    auto checkPlot=[&](bool db,double r) {
                        scale->setCurrentIndex(db ? 1 : 0);reference->setValue(r);
                        QCOMPARE(spectrum->plotView()->xTitle(),QString("Frequency (Hz)"));
                        QCOMPARE(spectrum->plotView()->yTitle(),db ? QString("dB re %1 %2").arg(r).arg(unit) : quantity->currentText()+" ("+unit+")");
                        QCOMPARE(spectrum->plotView()->xMin(),0.);QCOMPARE(spectrum->plotView()->xMax(),(n/2)*128./n);
                        for (const auto& trace:result.traces) {
                            const auto name=frame->channels[qsizetype(trace.channel)].name+" ["+unit+"]";
                            QLineSeries* line=nullptr;for (auto* candidate:spectrum->plotView()->series()) if(candidate->name()==name) line=candidate;
                            QVERIFY2(line && line->isVisible(),qPrintable("Missing quantity-labelled trace: "+name));
                            const auto points=line->points();QCOMPARE(points.size(),qsizetype(n/2+1));
                            for (int k=0;k<points.size();++k) {
                                const double value=expected.at(trace.channel).quantity[q][std::size_t(k)];
                                const double y=db ? std::max(-120.,value>0 ? (q==0 ? 20 : 10)*std::log10(value/r) : -std::numeric_limits<double>::infinity()) : value;
                                QCOMPARE(points[k].x(),k*128./n);QVERIFY(std::abs(points[k].y()-y)<(db ? 1e-4 : 1e-9));
                            }
                        }
                    };
                    checkPlot(false,1);QVERIFY(!QTest::currentTestFailed());
                    checkPlot(true,1);QVERIFY(!QTest::currentTestFailed());
                    if(takeCapture && (q==0 || (!leakage && w==1 && n==128)))
                        capture(scope,lab+(edited ? "-edited-" : "-baseline-")+QString("w%1-n%2-first%3-q%4-dB").arg(w).arg(n).arg(first).arg(q),true);
                    checkPlot(true,2);QVERIFY(!QTest::currentTestFailed());
                }
            };
            if (leakage) {
                checkInterval(0,128,0,false,true);QVERIFY(!QTest::currentTestFailed());
                checkInterval(0,256,0,false,true);QVERIFY(!QTest::currentTestFailed());
                checkInterval(128,128,0,false,true);QVERIFY(!QTest::currentTestFailed());
                checkInterval(127,129,0,false,true);QVERIFY(!QTest::currentTestFailed());
                checkInterval(0,128,0,true,false);QVERIFY(!QTest::currentTestFailed());
            } else {
                for (int w=0;w<3;++w) {checkInterval(0,128,w,false,true);QVERIFY(!QTest::currentTestFailed());}
                checkInterval(0,256,1,false,true);QVERIFY(!QTest::currentTestFailed());
                checkInterval(0,128,1,true,false);QVERIFY(!QTest::currentTestFailed());
                remove->setChecked(false);quantity->setCurrentIndex(2);all->setChecked(false);scale->setCurrentIndex(0);
                auto* channel=controls->findChild<QComboBox*>("spectrum.channel");QVERIFY(channel);
                const auto index=channel->findText("calibration [V]");QVERIFY(index>=0);channel->setCurrentIndex(index);
                QTRY_VERIFY(spectrum->result() && spectrum->result()->state==s7studio::analysis::SpectrumResult::State::Ready &&
                    spectrum->result()->traces.empty() && spectrum->result()->settings.channel==std::size_t(index) &&
                    spectrum->result()->settings.quantity==s7studio::analysis::SpectrumQuantity::Psd && !spectrum->result()->settings.removeMean &&
                    spectrum->result()->settings.window==s7studio::analysis::SpectrumWindow::Hann && spectrum->result()->size==128);
                QCOMPARE(spectrum->result()->runId,source->runId());
                auto* single=spectrum->plotView()->series().front();QVERIFY(single->name()=="calibration [V²/Hz]");
                std::vector<double> calibration;for (int n=0;n<128;++n) calibration.push_back(s7tests::education::spectral_signal("SP09","calibration",n,edited));
                const auto singleExpected=s7tests::education::lesson_spectrum(calibration,1,128);
                QCOMPARE(single->points().size(),qsizetype(65));
                for (std::size_t k=0;k<65;++k) {
                    QVERIFY(std::abs(spectrum->result()->values[k]-singleExpected.quantity[2][k])<1e-9);
                    QVERIFY(std::abs(single->points()[qsizetype(k)].y()-singleExpected.quantity[2][k])<1e-9);
                }
            }
            QVERIFY(window_->currentDocument().at("schema")==originalSchema);scope->hide();
        };
        run();QVERIFY(!QTest::currentTestFailed());check(false);QVERIFY(!QTest::currentTestFailed());
        window_->raise();window_->activateWindow();QVERIFY(QTest::qWaitForWindowActive(window_.get()));
        auto node=scene()->elementNode(leakage ? "test" : "weak");QVERIFY(node);scene()->clearSelection();node->setSelected(true);view->setFocus();
        QVERIFY(modalWorkflow([view]{QTest::keyClick(view->viewport(),Qt::Key_F4);},[&](QDialog* dialog) {
            auto* field=dialog->findChild<QLineEdit*>(leakage ? "control.frequency_hz" : "control.amplitude");
            auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!field || !buttons) return false;field->setText(leakage ? "18" : "0.1");
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);return true;
        }));
        const auto copy=temporary_.filePath(file+"-edited.s7diagram");QVERIFY(fileAction("action.saveAs",copy));QVERIFY(fileAction("action.open",copy));
        openScope();run();QVERIFY(!QTest::currentTestFailed());check(true);QVERIFY(!QTest::currentTestFailed());
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
        QSignalSpy samples(runner, &s7studio::LocalSchemaRunner::recordingAdvanced);
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
        if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) {
            QTRY_VERIFY(scope->source() && scope->source()->status().finished && !scope->source()->status().stopped);
        } else {
            auto* viewerStatus = viewer->widget()->findChild<QLabel*>("viewer.status");
            QVERIFY(viewerStatus);
            QTRY_VERIFY(viewerStatus->text().contains("completed"));
            QTRY_VERIFY(!viewerStatus->text().contains("loading history"));
        }
        auto* plot = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)
            ? static_cast<s7studio::ui::ScopeViewWindow*>(viewer)->plotView()
            : viewer->widget()->findChild<s7studio::viewers::LinePlotView*>();
        if (plot) {
            viewer->widget()->raise(); viewer->widget()->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(viewer->widget()));
            plot->setFocus(); QTRY_VERIFY(plot->hasFocus());
            // Read the range only now: a stopped run can still refit it while
            // the window activates.
            const auto width = plot->xMax() - plot->xMin();
            // A bare '=' does nothing; Ctrl+= zooms in.
            QTest::keyClick(plot, Qt::Key_Equal);
            QCOMPARE(plot->xMax() - plot->xMin(), width);
            QTest::keyClick(plot, Qt::Key_Equal, Qt::ControlModifier);
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
    void analysisOpensWithPlotsAndCachesPreview_data() {
        QTest::addColumn<int>("analysisRate");
        QTest::newRow("standard") << 1024;
        QTest::newRow("paged") << 4096;
    }
    void analysisOpensWithPlotsAndCachesPreview() {
        QFETCH(int, analysisRate);
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + "/time_frequency/impulse.s7diagram"));
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        auto* panel = window_->findChild<s7studio::ui::ObserversPanel*>(); QVERIFY(panel);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        QVERIFY(configure("action.run", 8, analysisRate));
        QTRY_VERIFY_WITH_TIMEOUT(!completed.empty(), 15000);
        QVERIFY(completed.last().at(0).toBool());
        auto* viewer = dynamic_cast<s7studio::viewers::TimeFrequencyWindow*>(manager->openObserver("TimeFrequency", "map"));
        QVERIFY(viewer);
        if (analysisRate > 1024) {
            while (viewer->refreshSource()) {}
            viewer->analyze();
        }
        QTRY_VERIFY_WITH_TIMEOUT(viewer->hasResult(), 10000); // Standard capture opens automatically.
        QTRY_VERIFY_WITH_TIMEOUT(!viewer->isPreparingDisplay(), 10000);
        QTRY_COMPARE(panel->activePresentations(), 1);
        auto* presentation = panel->findChild<s7studio::viewers::ObserverPresentation*>(); QVERIFY(presentation);
        auto* preview = presentation->findChild<s7studio::viewers::SurfacePlotView*>(); QVERIFY(preview);
        auto* full = viewer->findChild<s7studio::viewers::SurfacePlotView*>(); QVERIFY(full);
        QTRY_VERIFY(preview->bridge()->geometry.bounds().max[2] > 1);
        QTRY_VERIFY(preview->bridge()->geometry.bounds().min[0] < 1 && preview->bridge()->geometry.bounds().max[0] > 7);
        QTRY_VERIFY(std::abs(preview->bridge()->geometry.bounds().max[2] - full->bridge()->geometry.bounds().max[2]) < 1e-10);
        auto visibleImpulse = [&] {
            auto *heatmap = preview->findChild<QWidget *>("surface.heatmap");
            if (!heatmap) return false;
            const auto image = heatmap->grab().toImage();
            const auto area = QRectF(heatmap->rect()).adjusted(62, 16, -18, -52);
            const auto bounds = preview->bridge()->geometry.bounds();
            const auto x = area.left() + area.width() * (1.5 - bounds.min[0]) / (bounds.max[0] - bounds.min[0]);
            const auto dpr = image.devicePixelRatio();
            for (int py = int((area.top() + area.height() / 4) * dpr);
                 py < int((area.bottom() - area.height() / 4) * dpr); ++py)
                for (int px = int((x - 3) * dpr); px <= int((x + 3) * dpr); ++px) {
                    const auto color = image.pixelColor(px, py);
                    if (color.red() > color.green() + 20) return true;
                }
            return false;
        };
        QTRY_VERIFY2(visibleImpulse(), "The narrow impulse must survive heatmap downscaling in the dock");
        const auto requests = panel->submittedRequests();
        QTest::qWait(350);
        QCOMPARE(panel->submittedRequests(), requests);
        const auto peak = full->bridge()->geometry.bounds().max[2];
        panel->hide(); QTRY_COMPARE(panel->activePresentations(), 0);
        panel->show(); QTRY_COMPARE(panel->activePresentations(), 1);
        presentation = panel->findChild<s7studio::viewers::ObserverPresentation*>(); QVERIFY(presentation);
        preview = presentation->findChild<s7studio::viewers::SurfacePlotView*>(); QVERIFY(preview);
        QTRY_VERIFY(std::abs(preview->bridge()->geometry.bounds().max[2] - peak) < 1e-10);
        QTRY_VERIFY(visibleImpulse());
        QCOMPARE(panel->submittedRequests(), requests);
        capture(viewer, "analysis-automatic-impulse-map");
        capture(window_.get(), "analysis-measured-map-preview");
        const auto previous = QApplication::palette();
        const auto restore = qScopeGuard([previous] { QApplication::setPalette(previous); });
        auto palette = previous;
        palette.setColor(QPalette::Window, QColor("#eeeeee"));
        palette.setColor(QPalette::WindowText, Qt::black);
        palette.setColor(QPalette::Button, QColor("#eeeeee"));
        palette.setColor(QPalette::ButtonText, Qt::black);
        QApplication::setPalette(palette);
        capture(viewer, "analysis-primary-action-light");
    }
    // The run each bundled example's article documents; the rate is the
    // diagram's own 1/dt. Live-only demonstrations use a short run.
    // The documented run of a bundled example or lab: its saved studio_run.
    static double documentedSeconds(const QString& path) {
        QFile file(QDir(QString(S7_GUI_EXAMPLES_DIR)).filePath(path));
        if (!file.open(QIODevice::ReadOnly)) return 0;
        return QJsonDocument::fromJson(file.readAll()).object()["studio_run"].toObject()["runtime_seconds"].toDouble();
    }
    // Empty when the plot's ranges show its data sensibly: finite, every
    // drawn point inside the axes, and the data not squeezed into a sliver.
    static QString plotRangeProblem(s7studio::viewers::LinePlotView* plot) {
        if (!std::isfinite(plot->xMin()) || !std::isfinite(plot->xMax()) || !std::isfinite(plot->yMin()) || !std::isfinite(plot->yMax())
            || !(plot->xMax() > plot->xMin()) || !(plot->yMax() > plot->yMin()))
            return QString("degenerate axes x[%1,%2] y[%3,%4]").arg(plot->xMin()).arg(plot->xMax()).arg(plot->yMin()).arg(plot->yMax());
        double low = std::numeric_limits<double>::infinity(), high = -low; qsizetype count = 0;
        for (auto* series : plot->series()) {
            if (!series->isVisible()) continue;
            for (const auto& point : series->points()) {
                if (point.x() < plot->xMin() || point.x() > plot->xMax() || !std::isfinite(point.y())) continue;
                low = std::min(low, point.y()); high = std::max(high, point.y()); ++count;
            }
        }
        if (!count) return {};
        const double span = plot->yMax() - plot->yMin();
        if (low < plot->yMin() - span * 1e-9 || high > plot->yMax() + span * 1e-9)
            return QString("data y[%1,%2] outside axis y[%3,%4]").arg(low).arg(high).arg(plot->yMin()).arg(plot->yMax());
        // Flat dB responses intentionally retain a readable context range;
        // tiny FFT roundoff must not be enlarged into an apparent ripple.
        const bool flatGain = plot->yTitle().startsWith("Gain (dB") && high - low < 1e-10;
        if (!flatGain && high > low && (high - low) < span * 0.25)
            return QString("data y[%1,%2] fills %3% of axis y[%4,%5]").arg(low).arg(high)
                .arg((high - low) / span * 100, 0, 'f', 1).arg(plot->yMin()).arg(plot->yMax());
        return {};
    }
    // Release check, run by the s7studio_gui_examples_<shard> tests: each
    // takes several minutes, so the ordinary workflow suite skips it.
    // S7_GUI_EXAMPLES="<shard>/<count>" selects every count-th example.
    void bundledExamplesRunAndRender_data() {
        QTest::addColumn<QString>("diagram");
        const auto shard = qEnvironmentVariable("S7_GUI_EXAMPLES").split('/');
        const int index = shard.value(0).toInt(), count = std::max(1, shard.value(1).toInt());
        QDirIterator files(QString(S7_GUI_EXAMPLES_DIR), {"*.s7diagram"}, QDir::Files, QDirIterator::Subdirectories);
        QStringList all;
        while (files.hasNext()) all << QDir(QString(S7_GUI_EXAMPLES_DIR)).relativeFilePath(files.next());
        QDirIterator labs(QString(S7_GUI_EDUCATION_DIR), {"*.s7diagram"}, QDir::Files, QDirIterator::Subdirectories);
        while (labs.hasNext()) all << QDir(QString(S7_GUI_EXAMPLES_DIR)).relativeFilePath(labs.next());
        all.sort();
        QVERIFY(all.size() > 50);
        for (qsizetype i = 0; i < all.size(); ++i)
            if (i % count == index) QTest::newRow(qPrintable(all[i])) << all[i];
    }
    void bundledExamplesRunAndRender() {
        if (qEnvironmentVariableIsEmpty("S7_GUI_EXAMPLES")) QSKIP("Bundled examples run in the s7studio_gui_examples tests");
        QFETCH(QString, diagram);
        if (diagram == "sdr/fm_receiver.s7diagram") QSKIP("FM playback requires a live SDR and audio output");
        const bool publication = qEnvironmentVariableIsSet("S7_README_SCREENSHOTS");
        const bool lorenzOverview = publication && diagram == "lorenz/lorenz_attractor.s7diagram";
        const double seconds = documentedSeconds(diagram);
        QVERIFY2(seconds > 0, "every bundled example needs studio_run.runtime_seconds (tools/example_runs.py for generated ones)");
        const auto path = QString(S7_GUI_EXAMPLES_DIR) + '/' + diagram;
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        for(const auto& element:json["schema"].toObject()["elements"].toArray()){const auto type=element.toObject()["type"].toString();if((type=="FourierCoefficient"||type=="WaveletCoefficient")&&!s7::ElementRegistry::instance().find(type.toStdString()))QSKIP("Optional analysis plugin is not loaded; build/load it to run this graph example");}
        const double dt = json.value("dt").toDouble(); QVERIFY(dt > 0);
        QVERIFY(fileAction("action.open", path));
        const bool analysisGallery = diagram.startsWith("surfaces/") || diagram.startsWith("complex_functions/") ||
            diagram.startsWith("frequency_response/") || diagram.startsWith("time_frequency/") || diagram.startsWith("analysis_elements/");
        if (analysisGallery) {
            const auto page = editor()->pageLayout();
            const QRectF bounds(0, 0, page.widthMm * 96 / 25.4, page.heightMm * 96 / 25.4);
            const auto nodes = scene()->elementNodes();
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const QRectF body(nodes[i]->pos(), QSizeF(nodes[i]->width(), nodes[i]->height()));
                QVERIFY2(bounds.contains(body), qPrintable(diagram + ": element outside sheet: " + QString::fromStdString(nodes[i]->id())));
                for (std::size_t j = 0; j < i; ++j) {
                    const QRectF other(nodes[j]->pos(), QSizeF(nodes[j]->width(), nodes[j]->height()));
                    QVERIFY2(!body.intersects(other), qPrintable(diagram + ": overlapping elements: " +
                        QString::fromStdString(nodes[i]->id()) + " / " + QString::fromStdString(nodes[j]->id())));
                }
                for (const auto& frame : scene()->textFrames())
                    QVERIFY2(!body.intersects(frame->sceneBoundingRect()), qPrintable(diagram + ": element overlaps notes"));
            }
            for (const auto& frame : scene()->textFrames())
                QVERIFY2(bounds.contains(frame->sceneBoundingRect()), qPrintable(diagram + ": notes outside sheet"));
        }
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        QList<std::pair<QString, s7studio::viewers::ObservationViewer*>> viewers;
        for (const auto& node : json.value("diagram").toObject().value("nodes").toArray()) {
            const auto object = node.toObject();
            if (!object.value("observer").toBool()) continue;
            const auto type = object.value("type").toString(), id = object.value("id").toString();
            if (type == "PhaseTrajectory3D" && !graphicsAvailable_) continue;
            auto* viewer = manager->openObserver(type, id);
            QVERIFY2(viewer, qPrintable(type + ' ' + id));
            viewer->widget()->resize(publication ? QSize(1320, 820) : QSize(1100, 650));
            if (publication) {
                if (auto* retention = viewer->widget()->findChild<QSpinBox*>("live.samples")) retention->setValue(32768);
            }
            viewers.push_back({type + '-' + id, viewer});
        }
        QVERIFY(!viewers.isEmpty());
        auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
        QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
        // A message box during the run is a defect of the example; report its
        // text rather than hang in its modal loop.
        QStringList dialogs;
        QTimer dismiss; dismiss.setInterval(50);
        connect(&dismiss, &QTimer::timeout, this, [&dialogs] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                const auto* box = qobject_cast<QMessageBox*>(dialog);
                dialogs << (box ? box->windowTitle() + ": " + box->text() + ' ' + box->informativeText() : dialog->windowTitle());
                dialog->reject();
            }
        });
        QVERIFY(configure("action.run", seconds, std::round(1 / dt * 1e6) / 1e6));
        dismiss.start();
        // A refused run (see the Problems panel) never starts a worker.
        QTRY_VERIFY2(runner->isRunning() || completed.count() > 0 || !dialogs.isEmpty(), "the run did not start");
        QTRY_VERIFY_WITH_TIMEOUT(completed.count() > 0 || !dialogs.isEmpty(), 180000);
        QVERIFY2(dialogs.isEmpty(), qPrintable(dialogs.join('\n')));
        QVERIFY2(completed.last().at(0).toBool(), qPrintable(completed.last().value(1).toString()));
        const auto base = QString(diagram).replace('/', '_').remove(".s7diagram");
        QStringList problems;
        for (const auto& [name, viewer] : viewers) {
            if (publication && viewer->source()) {
                const auto status = viewer->source()->status();
                QVERIFY2(!status.incomplete(), qPrintable(status.storageError + ' ' + status.runError));
                QCOMPARE(status.captureDropped, std::uint64_t(0));
            }
            if (dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer) ||
                dynamic_cast<s7studio::ui::PhasePortraitViewWindow*>(viewer)) {
                QTRY_VERIFY(viewer->source() && viewer->source()->status().finished);
                while (viewer->refreshSource()) {}
            }
            if (auto* surface = dynamic_cast<s7studio::viewers::SurfaceWindow*>(viewer)) {
                while (surface->refreshSource()) {}
                QVERIFY2(surface->error().isEmpty(), qPrintable(surface->error()));
                QVERIFY(surface->grid().complete());
            }
            if (auto* scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)) {
                QTRY_VERIFY(scope->source() && scope->source()->status().finished);
                while (scope->refreshSource()) {}
                QVERIFY(!scope->recording().series().isEmpty());
            }
            if (auto* map = dynamic_cast<s7studio::viewers::TimeFrequencyWindow*>(viewer)) {
                while (map->refreshSource()) {}
                QTRY_VERIFY_WITH_TIMEOUT(map->hasResult() || !map->error().isEmpty(), 30000);
                QVERIFY2(map->hasResult(), qPrintable(map->error()));
                QTRY_VERIFY_WITH_TIMEOUT(!map->isPreparingDisplay(), 10000);
                capture(map, base + '-' + name + "-STFT");
                map->setWavelet(true);
                QTRY_VERIFY_WITH_TIMEOUT(map->result()->metadata["method"].toString().contains("Morlet") || !map->error().isEmpty(), 30000);
                QVERIFY2(map->error().isEmpty(), qPrintable(map->error()));
            }
            if (auto* response = dynamic_cast<s7studio::viewers::ResponseWindow*>(viewer)) {
                while (response->refreshSource()) {}
                QTRY_VERIFY_WITH_TIMEOUT(response->hasResult() || !response->error().isEmpty(), 10000);
                QVERIFY2(response->hasResult(), qPrintable(response->error()));
                QTRY_VERIFY_WITH_TIMEOUT(!response->isPreparingDisplay(), 10000);
            }
            QTest::qWait(400);
            auto* widget = viewer->widget();
            if (publication) {
                if (auto* trajectory = dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer)) {
                    trajectory->setViewPreset(s7studio::ui::PhaseTrajectory3DViewWindow::ViewPreset::Perspective);
                    auto* quick = widget->findChild<QQuickWidget*>(); QVERIFY(quick && quick->rootObject());
                    auto* camera = quick->rootObject()->findChild<QObject*>("viewer.camera"); QVERIFY(camera);
                    QVERIFY(camera->setProperty("z", 370.));
                }
                if (auto* fit = widget->findChild<QPushButton*>("live.timeScale.fitRun")) fit->click();
            }
            if(diagram=="frequency_response/rlc.s7diagram" && dynamic_cast<s7studio::viewers::ResponseWindow*>(viewer)) {auto* reference=widget->findChild<QCheckBox*>("response.referenceModel");QVERIFY(reference);reference->setChecked(true);auto* cursor=widget->findChild<QDoubleSpinBox*>("response.frequency");QVERIFY(cursor);cursor->setValue(5);}
            widget->raise();
            const auto checkPlots = [&](const QString& view) {
                for (auto* plot : widget->findChildren<s7studio::viewers::LinePlotView*>())
                    if (plot->isVisible())
                        if (const auto problem = plotRangeProblem(plot); !problem.isEmpty())
                            problems << QString("%1 %2 %3: %4").arg(diagram, name, view, problem);
            };
            auto* tabs = widget->findChild<QTabWidget*>(dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer)
                ? QStringLiteral("scope.analysisTabs") : QStringLiteral("live.views"));
            if(!tabs)tabs=widget->findChild<QTabWidget*>("response.views");
            if(!tabs)tabs=widget->findChild<QTabWidget*>("timeFrequency.views");
            if (auto* analyze = widget->findChild<QPushButton*>("scope.analyze"); analyze && analyze->isEnabled() && !lorenzOverview) analyze->click();
            const int pages = lorenzOverview ? 1 : (tabs ? tabs->count() : 1);
            for (int page = 0; page < pages; ++page) {
                if (tabs) tabs->setCurrentIndex(page);
                if (publication && diagram == "signal_measurement/am_coherent_detection_live.s7diagram" &&
                    name.endsWith("carrier_scope") && tabs && tabs->tabText(page) == "Spectrum") {
                    auto* channel = widget->findChild<QComboBox*>("spectrum.channel"); QVERIFY(channel);
                    const auto index = channel->findText("carrier_scope/carrier", Qt::MatchStartsWith);
                    QVERIFY(index >= 0); channel->setCurrentIndex(index);
                }
                QTest::qWait(page ? 1200 : 400);
                const auto view = tabs ? tabs->tabText(page).replace(' ', "").replace('/', "") : QStringLiteral("view");
                if (!dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer)) checkPlots(view);
                capture(widget, base + '-' + name + '-' + view);
            }
            if (tabs) tabs->setCurrentIndex(0);
            if (dynamic_cast<s7studio::viewers::SurfaceWindow*>(viewer)) {
                auto* presentation = widget->findChild<QComboBox*>("surface.presentation"); QVERIFY(presentation);
                for (int mode : {1, 2}) {
                    presentation->setCurrentIndex(mode); QTest::qWait(150);
                    capture(widget, base + '-' + name + (mode == 1 ? "-Wireframe" : "-Heatmap"));
                }
                presentation->setCurrentIndex(0);
            }
        }
        // Normal captures include observer previews; publication captures fit
        // the complete graph in the larger window.
        if (publication) {
            for (auto* dock : window_->findChildren<QDockWidget*>()) dock->hide();
            action("action.fitDiagram")->trigger();
        }
        capture(window_.get(), base + "-studio");
        if (lorenzOverview) {
            // Capture one actual desktop with the model and both observers;
            // do not assemble separate window images or alter recorded data.
            s7studio::ui::ScopeViewWindow* scope = nullptr;
            s7studio::ui::PhaseTrajectory3DViewWindow* trajectory = nullptr;
            for (const auto& [name, viewer] : viewers) {
                viewer->widget()->hide();
                if (name == "Scope-base_scope") scope = dynamic_cast<s7studio::ui::ScopeViewWindow*>(viewer);
                if (name == "PhaseTrajectory3D-base_trajectory") trajectory = dynamic_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(viewer);
            }
            QVERIFY(scope && trajectory);
            QCOMPARE(trajectory->points().size(), qsizetype(160000));
            QCOMPARE(scope->recording().series().size(), std::size_t(3));
            auto* screen = QGuiApplication::primaryScreen(); QVERIFY(screen);
            QVERIFY2(screen->size().width() >= 2400 && screen->size().height() >= 1500,
                     "Lorenz overview needs a 2400x1500 or larger private display");
            window_->setGeometry(0, 0, 2400, 620);
            trajectory->setGeometry(0, 620, 1200, 880);
            scope->setGeometry(1200, 620, 1200, 880);
            scope->show(); trajectory->show();
            QTest::qWait(500);
            auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
            QRectF modelBounds;
            for (const auto& node : scene()->elementNodes()) {
                if (QString::fromStdString(node->id()).startsWith("base_"))
                    modelBounds = modelBounds.united(QRectF(node->pos(), QSizeF(node->width(), node->height())));
            }
            QVERIFY(!modelBounds.isEmpty());
            view->resetTransform();
            view->fitInView(modelBounds.adjusted(-100, -40, 100, 40), Qt::KeepAspectRatio);
            view->centerOn(modelBounds.center());
            trajectory->setViewPreset(s7studio::ui::PhaseTrajectory3DViewWindow::ViewPreset::Perspective);
            auto* quick = trajectory->findChild<QQuickWidget*>(); QVERIFY(quick && quick->rootObject());
            auto* camera = quick->rootObject()->findChild<QObject*>("viewer.camera"); QVERIFY(camera);
            QVERIFY(camera->setProperty("z", 370.));
            for (auto* check : scope->findChildren<QCheckBox*>())
                if (check->text() == "Fit full run") check->setChecked(false);
            scope->plotView()->setXRange(20, 40);
            auto* visibleRange = scope->findChild<QPushButton*>("scope.visibleAnalysisRange"); QVERIFY(visibleRange);
            visibleRange->click();
            window_->raise(); scope->raise(); trajectory->raise();
            QTest::qWait(1500);
            const auto directory = qEnvironmentVariable("S7_USABILITY_CAPTURE_DIR");
            QVERIFY(!directory.isEmpty());
            QVERIFY(screen->grabWindow(0, 0, 0, 2400, 1500).save(directory + "/lorenz-overview.png"));
        }
        for (const auto& problem : problems) qWarning().noquote() << "plot range:" << problem;
        if (qEnvironmentVariableIsEmpty("S7_EXAMPLES_REPORT_ONLY")) QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
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
        QStringList diagrams;
        for (const auto& directory : {QString(S7_GUI_EXAMPLES_DIR), QString(S7_GUI_EDUCATION_DIR)}) {
            QDirIterator files(directory, {"*.s7diagram"}, QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) diagrams << files.next();
        }
        for (const auto& path : diagrams) {
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
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
    void analysisViewerDoesNotModifyDiagram_data() {
        QTest::addColumn<QString>("diagram");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("doubleClick");
        QTest::addColumn<bool>("runFirst");
        for (bool run : {false, true}) for (bool click : {false, true}) {
            const auto suffix = QString(click ? "-double-click" : "-open") + (run ? "-after-run" : "");
            QTest::newRow(qPrintable(QString("time-frequency") + suffix)) << "time_frequency/impulse.s7diagram" << "TimeFrequency" << "map" << click << run;
            QTest::newRow(qPrintable(QString("response") + suffix)) << "frequency_response/rlc.s7diagram" << "FrequencyResponse" << "response" << click << run;
            QTest::newRow(qPrintable(QString("surface") + suffix)) << "surfaces/saddle.s7diagram" << "Surface3D" << "surface" << click << run;
            QTest::newRow(qPrintable(QString("complex") + suffix)) << "complex_functions/inverse.s7diagram" << "ComplexSurface3D" << "surface" << click << run;
            QTest::newRow(qPrintable(QString("scope") + suffix)) << "2int_osc.s7diagram" << "Scope" << "scope_1" << click << run;
            QTest::newRow(qPrintable(QString("live") + suffix)) << "2int_osc_live.s7diagram" << "LiveOscilloscope" << "live_1" << click << run;
        }
    }
    void analysisViewerDoesNotModifyDiagram() {
        QFETCH(QString, diagram); QFETCH(QString, type); QFETCH(QString, id); QFETCH(bool, doubleClick); QFETCH(bool, runFirst);
        QVERIFY(fileAction("action.open", QString(S7_GUI_EXAMPLES_DIR) + '/' + diagram));
        QVERIFY(!window_->documentModified());
        const auto original = window_->currentDocument();
        if (runFirst) {
            auto* runner = window_->findChild<s7studio::LocalSchemaRunner*>(); QVERIFY(runner);
            QSignalSpy completed(runner, &s7studio::LocalSchemaRunner::runCompleted);
            QVERIFY(dialogAction("action.run", [](QDialog* dialog) {
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                if (!buttons) return false;
                buttons->button(QDialogButtonBox::Ok)->click(); return true;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(!completed.empty(), 15000);
            QVERIFY(completed.last().at(0).toBool());
        }
        auto* manager = window_->findChild<s7studio::viewers::ViewerManager*>(); QVERIFY(manager);
        if (doubleClick) {
            action("action.fitDiagram")->trigger();
            auto* view = editor()->findChild<QGraphicsView*>(); QVERIFY(view);
            const auto node = scene()->elementNode(id.toStdString()); QVERIFY(node);
            QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, view->mapFromScene(node->sceneBoundingRect().center()));
        } else manager->openObserver(type, id);
        auto* viewer = manager->find(type, id); QVERIFY(viewer);
        QTRY_VERIFY(viewer->widget()->isVisible());
        viewer->widget()->close();
        QTRY_VERIFY(!viewer->widget()->isVisible());
        QTest::qWait(200);
        if (window_->currentDocument() != original) {
            QFile baseline(artifacts_ + '/' + QString::fromLatin1(QTest::currentDataTag()) + "-before.json");
            QVERIFY(baseline.open(QIODevice::WriteOnly));
            baseline.write(QByteArray::fromStdString(boost::json::serialize(original)));
        }
        QCOMPARE(window_->currentDocument(), original);
        QVERIFY(!window_->documentModified());
        bool prompted = false;
        QTimer dismiss;
        dismiss.setInterval(10);
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                prompted = true; box->done(QMessageBox::Cancel);
            }
        });
        dismiss.start();
        const bool closed = window_->close();
        dismiss.stop();
        QVERIFY(!prompted); QVERIFY(closed); QVERIFY(!window_->isVisible());
    }
    void unchangedRunConfigurationPreservesPrecision() {
        QFile source(QString(S7_GUI_EXAMPLES_DIR) + "/time_frequency/impulse.s7diagram");
        QVERIFY(source.open(QIODevice::ReadOnly));
        auto document = boost::json::parse(source.readAll().toStdString()).as_object();
        document["dt"] = .00003;
        document["studio_run"] = boost::json::object{{"runtime_seconds", .123456789123}, {"clear_scope_traces", true}};
        const auto path = temporary_.filePath("precise-run.s7diagram");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromStdString(boost::json::serialize(document))); file.close();
        QVERIFY(fileAction("action.open", path));
        const auto original = window_->currentDocument();
        QVERIFY(!window_->documentModified());
        const auto accept = [](QDialog* dialog) {
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!buttons) return false;
            buttons->button(QDialogButtonBox::Ok)->click(); return true;
        };
        QVERIFY(dialogAction("action.runConfiguration", accept));
        QCOMPARE(window_->currentDocument(), original);
        QVERIFY(!window_->documentModified());
        QVERIFY(dialogAction("action.runConfiguration", [](QDialog* dialog) {
            dialog->findChild<QDoubleSpinBox*>("run.frequency")->setValue(2000);
            dialog->findChild<QDoubleSpinBox*>("run.duration")->setValue(.25);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); return true;
        }));
        QVERIFY(window_->documentModified());
        const auto edited = window_->currentDocument().as_object();
        QCOMPARE(edited.at("dt").to_number<double>(), .0005);
        QCOMPARE(edited.at("studio_run").at("runtime_seconds").to_number<double>(), .25);
    }
    void newDiagramFromFileMenu() {
        const auto originalPath = temporary_.filePath("new-diagram-original.s7diagram");
        QVERIFY(QFile::copy(QString(S7_GUI_EXAMPLES_DIR) + "/2int_osc.s7diagram", originalPath));
        QVERIFY(fileAction("action.open", originalPath));
        QVERIFY(scene()->addElement({"unsaved", "Constant", {}, {}, {}, "Unsaved"}, {0, 0}));
        const auto edited = window_->currentDocument();

        auto* command = action("action.newDiagram");
        auto* menu = window_->findChild<QMenu*>("menu.file");
        QVERIFY(menu && command && menu->actions().contains(command));
        const auto selectNew = [&] { activateMenu("menu.file", command); };
        for (const auto choice : {QMessageBox::Cancel, QMessageBox::Discard}) {
            QVERIFY(modalWorkflow(selectNew, [choice](QDialog* dialog) {
                auto* box = qobject_cast<QMessageBox*>(dialog);
                if (!box || !box->button(choice)) return false;
                QTest::mouseClick(box->button(choice), Qt::LeftButton);
                return true;
            }));
            if (choice == QMessageBox::Cancel) {
                QCOMPARE(window_->currentDocument(), edited);
                QVERIFY(window_->documentModified());
            }
        }
        QVERIFY(scene()->elementNodes().empty());
        QVERIFY(!window_->documentModified());
        QVERIFY(!action("action.undo")->isEnabled());
        QVERIFY(window_->windowTitle().contains("Untitled"));
        const auto newPath = temporary_.filePath("new-diagram.s7diagram");
        QVERIFY(fileAction("action.save", newPath));
        QVERIFY(QFile::exists(newPath));
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

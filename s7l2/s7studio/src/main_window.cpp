// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window.hpp"
#include "observers_panel.hpp"
#include <QApplication>
#include "parameter_editor.hpp"
#include "element_documentation.hpp"
#include "help_dialogs.hpp"
#include "atomic_file.hpp"
#include "schema_tabs.hpp"
#include <QUndoGroup>

#include "deployment_support.hpp"
#include "diagram_document.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "diagram/element_ports.hpp"
#include "element_picker_panel.hpp"
#include "element_requirements.hpp"
#include "headless_nodes_panel.hpp"
#include "json_utils.hpp"
#include "main_window_builder.hpp"
#include "editor_actions.hpp"
#include "tool_windows.hpp"
#include "ui_icons.hpp"
#include "scene_serializer.hpp"
#include "scope_sample_mailbox.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include "live_oscilloscope_window.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/io.hpp>
#include <s7/element_catalog.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/schema_library.hpp>

#include "s7/plugin_loader.hpp"
#include "wire_type_labels.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/scene.hpp>

#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>
#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QCloseEvent>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QUndoStack>
#include <QMessageBox>
#include <QFontComboBox>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QInputDialog>
#include <QIcon>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPen>
#include <QPalette>
#include <QPushButton>
#include <QProgressDialog>
#include <QStringList>
#include <QSpinBox>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidget>

#include <s7/deployment_client.hpp>

#include <algorithm>
#include <array>
#include <boost/system/error_code.hpp>
#include <cmath>
#include <string>
#include <vector>

namespace s7studio {

namespace {

constexpr double kPaperA5WidthMm = 210.0;
constexpr double kPaperA5HeightMm = 148.0;
constexpr double kPaperA4WidthMm = 297.0;
constexpr double kPaperA4HeightMm = 210.0;
constexpr double kPaperA3WidthMm = 420.0;
constexpr double kPaperA3HeightMm = 297.0;
constexpr double kPaperLetterWidthMm = 279.4;
constexpr double kPaperLetterHeightMm = 215.9;
constexpr const char* kBundledDemoDiagramPath = ":/s7/demo_startup.s7diagram";


auto selectedTextFrame(diagram::DiagramScene* scene) -> std::shared_ptr<diagram::TextFrameNode>
{
    if (!scene) {
        return {};
    }
    for (const auto& frame : scene->textFrames()) {
        if (frame->isSelected()) {
            return frame;
        }
    }
    return {};
}

auto nextTextFrameId(const diagram::DiagramScene& scene) -> std::string
{
    int maxSuffix = 0;
    for (const auto& frame : scene.textFrames()) {
        const auto id = QString::fromStdString(frame->id());
        if (!id.startsWith(QStringLiteral("text_frame_"))) {
            continue;
        }
        bool ok = false;
        const int suffix = id.mid(QStringLiteral("text_frame_").size()).toInt(&ok);
        if (ok) {
            maxSuffix = std::max(maxSuffix, suffix);
        }
    }
    return "text_frame_" + std::to_string(maxSuffix + 1);
}

auto developmentPluginCandidates() -> std::vector<std::string>
{
    const QString appDir = QCoreApplication::applicationDirPath();
#if defined(_WIN32)
    const QString suffix = QStringLiteral(".dll");
    const QString noiseFile = QStringLiteral("s7noise") + suffix;
#elif defined(__APPLE__)
    const QString suffix = QStringLiteral(".dylib");
    const QString noiseFile = QStringLiteral("libs7noise") + suffix;
#else
    const QString suffix = QStringLiteral(".so");
    const QString noiseFile = QStringLiteral("libs7noise") + suffix;
#endif

    const QString candidateA = QFileInfo(appDir + QStringLiteral("/../s7noise/") + noiseFile).absoluteFilePath();
    const QString candidateB = QFileInfo(appDir + QStringLiteral("/../../s7noise/") + noiseFile).absoluteFilePath();

    return {
        candidateA.toStdString(),
        candidateB.toStdString(),
    };
}

auto isDiagramDocument(const boost::json::value& json) -> bool
{
    if (!json.is_object()) {
        return false;
    }

    const auto& object = json.as_object();
    if (const auto it = object.find("document_kind"); it != object.end() && it->value().is_string()) {
        return it->value().as_string() == "s7.diagram";
    }

    return object.contains("diagram") || object.contains("schema");
}

auto isElementLibraryDocument(const boost::json::value& json) -> bool
{
    if (!json.is_object()) {
        return false;
    }

    const auto& object = json.as_object();
    if (const auto it = object.find("document_kind"); it != object.end() && it->value().is_string()) {
        return it->value().as_string() == "s7.element_library";
    }

    return false;
}

auto schemaDisplayName(const boost::json::value& schemaJson, const QString& fallback) -> QString
{
    if (schemaJson.is_object()) {
        const auto& object = schemaJson.as_object();
        if (const auto name = json_utils::stringValue(object, "name"); !name.empty()) {
            return QString::fromUtf8(name.c_str());
        }
    }

    return fallback;
}

auto elementLibraryForType(const QString& type) -> QString
{
    if (const auto* descriptor = s7::ElementRegistry::instance().find(type.toStdString())) {
        const auto& metadata = descriptor->metadata();
        if (metadata.schema_definition.has_value()) {
            return QStringLiteral("schema library: %1").arg(QString::fromStdString(metadata.category));
        }
        if (!metadata.category.empty()) {
            return QStringLiteral("registry category: %1").arg(QString::fromStdString(metadata.category));
        }
        return QStringLiteral("registry");
    }
    return QStringLiteral("s7core (.so at runtime link: libs7core)");
}

auto portListSummary(const std::vector<s7::PortSpec>& ports) -> QString
{
    if (ports.empty()) {
        return QStringLiteral("none");
    }

    QStringList lines;
    lines.reserve(static_cast<int>(ports.size()));
    for (const auto& port : ports) {
        const auto signalName = s7::signal_type_name(port.type);
        lines.push_back(QStringLiteral("- %1 (%2): %3")
                            .arg(QString::fromStdString(port.name),
                                 QString::fromLatin1(signalName.data(), static_cast<int>(signalName.size())),
                                 QString::fromStdString(port.description.empty() ? std::string("No description") : port.description)));
    }
    return lines.join('\n');
}


} // namespace

MainWindow::MainWindow(QWidget* parent, bool restoreStartupDiagram)
    : QMainWindow(parent)
    , runtimeSchema_(boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}})
{
    viewers_ = new viewers::ViewerManager(viewers::ViewerRegistry::instance(), this);
    connect(viewers_, &viewers::ViewerManager::viewerCreated, this,
        [this](const QString& typeId, const QString& instanceId, viewers::ObservationViewer* viewer) {
            auto* live = typeId == QStringLiteral("LiveOscilloscope") ? dynamic_cast<ui::LiveOscilloscopeWindow*>(viewer->widget()) : nullptr;
            if (!live) return;
            applyTriggerPreset(instanceId, true);
            connect(live, &ui::LiveOscilloscopeWindow::triggerSettingsEdited, this, [this, instanceId] { storeTriggerPreset(instanceId); });
        });
    runner_ = new LocalSchemaRunner(this);
    controller_ = std::make_unique<ui::ModelRunController>(runner_);

    buildActions();
    buildUi();
    setWindowTitle(QStringLiteral("s7studio - untitled[*]"));

    connectRunnerSignals();
    if (diagramEditor_) {
        connect(diagramEditor_, &diagram::DiagramEditor::elementNodeDoubleClicked,
                this,
                &MainWindow::onElementNodeDoubleClicked);
    }

    appendLog("[studio] Local embedded runtime ready");
    restoreWindowLayout();
    if (restoreStartupDiagram) this->restoreStartupDiagram();
    markDocumentSaved();
    refreshActions();
    onStatusChanged("idle");
}

MainWindow::~MainWindow()
{
    shuttingDown_ = true;
    retireRunForShutdown();
    delete std::exchange(observersPanel_, nullptr);
    // Nested pages can remain queued for deletion after SchemaTabs::reset().
    // Their scene teardown must not reach actions already destroyed by QWidget.
    for (auto* editor : findChildren<diagram::DiagramEditor*>()) {
        disconnect(editor, nullptr, this, nullptr);
        if (editor->scene()) disconnect(editor->scene(), nullptr, this, nullptr);
    }
    // Tear down tab callbacks while all editors and tool widgets still exist.
    // QTabWidget otherwise emits currentChanged while its pages are destroyed.
    if (schemaTabs_) {
        schemaTabs_->reset();
        delete schemaTabs_;
        schemaTabs_=nullptr;
    }
    if (networkTask_.joinable()) {
        networkTask_.join(); // bounded by the control request timeouts
    }
    if (runner_) {
        disconnect(runner_, nullptr, this, nullptr);
    }
    closeViewers();
}

void MainWindow::retireRunForShutdown()
{
    ++runGeneration_;
    if (runner_) {
        disconnect(runner_, nullptr, this, nullptr);
        runner_->reset(); // join before schemas, viewers or plugin loader are destroyed
    }
    runObserverTypes_.clear();
    observersFrozen_ = false;
}

void MainWindow::resetLocalRun()
{
    if (observersPanel_) observersPanel_->clearRun();
    // Disconnect before joining: the retiring worker may emit its final
    // notifications during reset. Already queued lambdas retain their old ID.
    ++runGeneration_;
    disconnect(runner_, nullptr, this, nullptr);
    runner_->reset();
    viewers_->presentLive({});
    runObserverTypes_.clear();
    observersFrozen_ = false;
    connectRunnerSignals();
    onStatusChanged(QStringLiteral("idle"));
}

void MainWindow::connectRunnerSignals()
{
    const auto generation = runGeneration_;
    connect(runner_, &LocalSchemaRunner::logMessage, this, [this, generation](const QString& message) {
        if (shuttingDown_ || generation != runGeneration_) return;
        appendLog(message);
        if (runPanel_) runPanel_->append(message);
    });
    connect(runner_, &LocalSchemaRunner::statusChanged, this, [this, generation](const QString& status) {
        if (!shuttingDown_ && generation == runGeneration_) onStatusChanged(status);
    });
    connect(runner_, &LocalSchemaRunner::runCompleted, this, [this, generation](bool success, const QString& error) {
        if (!shuttingDown_ && generation == runGeneration_) onRunCompleted(success, error);
    });
    connect(runner_, &LocalSchemaRunner::ticksProgress, this, [this, generation](long completed, long total) {
        if (!shuttingDown_ && generation == runGeneration_) onTickProgress(completed, total);
    });
    connect(runner_, &LocalSchemaRunner::stateChanged, this, [this, generation](RunState state) {
        // Prepared is published synchronously before the producer starts, so
        // open live windows can apply retention/trigger settings to short runs.
        if (!shuttingDown_ && generation == runGeneration_ && state == RunState::Prepared) {
            for (const auto& descriptor : preparedObservers_) if (descriptor.type == "LiveOscilloscope") applyTriggerPreset(descriptor.id, true);
            viewers_->presentLive(runner_->liveSources());
            viewers_->present(runner_->recordedSource(), 0, [this](const QString& id) { return observerViewerType(id); });
            if (const auto configuration = runner_->activeConfiguration()) observersPanel_->bindRun(configuration->id, preparedObservers_);
        }
    });
    connect(runner_, &LocalSchemaRunner::scopeSamplesAvailable, this, [this, generation] {
        if (!shuttingDown_ && generation == runGeneration_) onScopeSamplesAvailable();
    });
}

void MainWindow::buildActions()
{
    const auto actions = ui::MainWindowBuilder::createActions(this);
    openAction_ = actions.openAction;
    closeDiagramAction_ = actions.closeDiagramAction;
    saveAction_ = actions.saveAction;
    saveAsAction_ = actions.saveAsAction;
    loadLibraryAction_ = actions.loadLibraryAction;
    saveLibraryAction_ = actions.saveLibraryAction;
    examplesAction_ = actions.examplesAction;
    fitDiagramAction_ = actions.fitDiagramAction;
    exitAction_ = actions.exitAction;
    runAction_ = actions.runAction;
    runParametersAction_ = actions.runParametersAction;
    pauseAction_ = actions.pauseAction;
    resumeAction_ = actions.resumeAction;
    stopAction_ = actions.stopAction;
    resetAction_ = actions.resetAction;
    deployAction_ = actions.deployAction;
    openDeployedAction_ = actions.openDeployedAction;

    zoomInAction_ = new QAction("Zoom In", this);
    zoomInAction_->setObjectName(QStringLiteral("action.zoomIn"));
    zoomOutAction_ = new QAction("Zoom Out", this);
    zoomOutAction_->setObjectName(QStringLiteral("action.zoomOut"));
    resetZoomAction_ = new QAction("Actual Size", this);
    fitPageAction_ = new QAction("Fit Page", this);
    overlayScopeTracesAction_ = new QAction("Keep Previous Scope Traces", this);
    overlayScopeTracesAction_->setObjectName(QStringLiteral("action.keepScopeTraces"));
    overlayScopeTracesAction_->setCheckable(true);
    overlayScopeTracesAction_->setChecked(false);
    overlayScopeTracesAction_->setToolTip("On the next run, keep the preceding Scope recording as a labelled comparison. Phase views always restart.");
    connect(overlayScopeTracesAction_, &QAction::triggered, this, [this](bool keep) {
        clearScopesBeforeRun_ = !keep;
        setWindowModified(documentModified());
    });

    modeNormalAction_ = new QAction("Select/Move", this);
    modeNormalAction_->setObjectName(QStringLiteral("action.selectMove"));
    modeNormalAction_->setCheckable(true);
    connect(modeNormalAction_, &QAction::triggered, this, [this] { setDiagramMode(QSchematic::Scene::NormalMode); });
    modeNormalAction_->setChecked(true);
    modeNormalAction_->setToolTip("Switch to Normal mode (move/select elements)");
    modeNormalAction_->setIcon(ui::icon(QStringLiteral("pointer")));

    modeWireAction_ = new QAction("Wire Mode", this);
    modeWireAction_->setObjectName(QStringLiteral("action.drawWire"));
    modeWireAction_->setCheckable(true);
    modeWireAction_->setText(QStringLiteral("Wire Draw"));
    modeWireAction_->setToolTip("Draw wires click by click: a port or wire, bends, then a port or wire (Esc cancels). "
                                "In normal mode, drag from a port to connect, or Alt+drag from a wire.");
    modeWireAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    modeWireAction_->setIcon(ui::icon(QStringLiteral("vector-bezier-2")));

    addTextFrameAction_ = new QAction("Insert Text Frame", this);
    addTextFrameAction_->setObjectName(QStringLiteral("action.insertTextFrame"));
    addTextFrameAction_->setToolTip("Insert a diagram-only text frame");
    addTextFrameAction_->setIcon(ui::icon(QStringLiteral("text-caption")));

    textColorAction_ = new QAction("Text Color…", this);
    textColorAction_->setObjectName(QStringLiteral("action.textColor"));
    textColorAction_->setToolTip("Set selected text frame text color");
    textColorAction_->setIcon(ui::icon(QStringLiteral("text-color")));
    lineColorAction_ = new QAction("Line Color…", this);
    lineColorAction_->setObjectName(QStringLiteral("action.lineColor"));
    lineColorAction_->setToolTip("Set selected text frame border color");
    lineColorAction_->setIcon(ui::icon(QStringLiteral("border-style-2")));
    fillColorAction_ = new QAction("Fill Color…", this);
    fillColorAction_->setObjectName(QStringLiteral("action.fillColor"));
    fillColorAction_->setToolTip("Set selected text frame background color");
    fillColorAction_->setIcon(ui::icon(QStringLiteral("bucket")));

    paperA5Action_ = new QAction("A5", this);
    paperA4Action_ = new QAction("A4", this);
    paperA3Action_ = new QAction("A3", this);
    paperLetterAction_ = new QAction("Letter", this);
    portraitPageAction_ = new QAction("Portrait", this);
    landscapePageAction_ = new QAction("Landscape", this);

    paperA5Action_->setObjectName(QStringLiteral("action.paperA5"));
    paperA4Action_->setObjectName(QStringLiteral("action.paperA4"));
    paperA3Action_->setObjectName(QStringLiteral("action.paperA3"));
    paperLetterAction_->setObjectName(QStringLiteral("action.paperLetter"));
    portraitPageAction_->setObjectName(QStringLiteral("action.pagePortrait"));
    landscapePageAction_->setObjectName(QStringLiteral("action.pageLandscape"));
    paperA5Action_->setCheckable(true);
    paperA4Action_->setCheckable(true);
    paperA3Action_->setCheckable(true);
    paperLetterAction_->setCheckable(true);
    portraitPageAction_->setCheckable(true);
    landscapePageAction_->setCheckable(true);

     paperSizeGroup_ = new QActionGroup(this);
    paperSizeGroup_->setExclusive(true);
    paperSizeGroup_->addAction(paperA5Action_);
    paperSizeGroup_->addAction(paperA4Action_);
    paperSizeGroup_->addAction(paperA3Action_);
    paperSizeGroup_->addAction(paperLetterAction_);

    pageOrientationGroup_ = new QActionGroup(this);
    pageOrientationGroup_->setExclusive(true);
    pageOrientationGroup_->addAction(portraitPageAction_);
    pageOrientationGroup_->addAction(landscapePageAction_);

    zoomInAction_->setIcon(ui::icon(QStringLiteral("zoom-in")));
    zoomOutAction_->setIcon(ui::icon(QStringLiteral("zoom-out")));
    auto zoomInShortcuts = QKeySequence::keyBindings(QKeySequence::ZoomIn);
    // Main-keyboard '+' can carry Shift; Ctrl+= also works without Shift.
    // Handle both through the action, rather than the vendor view's different
    // zoom increment. Qt normalizes KeypadModifier for shortcut matching.
    for (const auto& shortcut : {QKeySequence(Qt::CTRL | Qt::Key_Equal),
                                 QKeySequence(Qt::CTRL | Qt::Key_Plus),
                                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus)}) {
        if (!zoomInShortcuts.contains(shortcut)) zoomInShortcuts.append(shortcut);
    }
    zoomInAction_->setShortcuts(zoomInShortcuts);
    zoomOutAction_->setShortcuts(QKeySequence::keyBindings(QKeySequence::ZoomOut));
    // Menus remain available while editing text, but canvas shortcuts should
    // only dispatch from a root or nested diagram editor.
    zoomInAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    zoomOutAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    resetZoomAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fitPageAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));
    fitDiagramAction_->setText(QStringLiteral("Fit Diagram to Window"));
    fitDiagramAction_->setToolTip(QStringLiteral("Zoom so that the whole diagram fills the window (Ctrl+8)"));
    fitDiagramAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_8));

    openAction_->setShortcut(QKeySequence::Open);
    closeDiagramAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);

    connect(openAction_, &QAction::triggered, this, &MainWindow::openDocumentFile);
    connect(closeDiagramAction_, &QAction::triggered, this, &MainWindow::closeCurrentDiagram);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveDocument);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveDocumentAs);
    connect(loadLibraryAction_, &QAction::triggered, this, &MainWindow::loadElementLibrary);
    connect(saveLibraryAction_, &QAction::triggered, this, &MainWindow::saveSchemaLibrary);
    connect(examplesAction_, &QAction::triggered, this, &MainWindow::chooseExampleDiagram);
    connect(fitDiagramAction_, &QAction::triggered, this, [this] {
        if (activeDiagramEditor()) {
            activeDiagramEditor()->fitDiagramInView();
        }
    });
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    connect(runAction_, &QAction::triggered, this, [this] {
        configureRun(true);
    });
    connect(runParametersAction_, &QAction::triggered, this, &MainWindow::openRunParametersDialog);
    connect(deployAction_, &QAction::triggered, this, &MainWindow::deployToNodes);
    connect(openDeployedAction_, &QAction::triggered, this, &MainWindow::openDeployedDiagram);
    connect(pauseAction_, &QAction::triggered, this, [this] { controller_->pause(); });
    connect(resumeAction_, &QAction::triggered, this, [this] { controller_->resume(); });
    connect(stopAction_, &QAction::triggered, this, [this] { controller_->stop(); });
    connect(resetAction_, &QAction::triggered, this, [this] {
        resetLocalRun();
        clearViewerSamples();
        refreshActions();
    });

    connect(zoomInAction_, &QAction::triggered, this, &MainWindow::zoomDiagramIn);
    connect(zoomOutAction_, &QAction::triggered, this, &MainWindow::zoomDiagramOut);
    connect(resetZoomAction_, &QAction::triggered, this, &MainWindow::resetDiagramZoom);
    connect(fitPageAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(modeWireAction_, &QAction::toggled, this, [this](const bool checked) {
        setDiagramMode(checked ? diagram::DiagramScene::DrawMode : QSchematic::Scene::NormalMode);
    });
    connect(addTextFrameAction_, &QAction::triggered, this, &MainWindow::addTextFrame);
    connect(textColorAction_, &QAction::triggered, this, &MainWindow::setSelectedTextFrameTextColor);
    connect(lineColorAction_, &QAction::triggered, this, &MainWindow::setSelectedTextFrameLineColor);
    connect(fillColorAction_, &QAction::triggered, this, &MainWindow::setSelectedTextFrameFillColor);

    connect(paperA5Action_, &QAction::triggered, this, &MainWindow::setPaperA5);
    connect(paperA4Action_, &QAction::triggered, this, &MainWindow::setPaperA4);
    connect(paperA3Action_, &QAction::triggered, this, &MainWindow::setPaperA3);
    connect(paperLetterAction_, &QAction::triggered, this, &MainWindow::setPaperLetter);
    connect(portraitPageAction_, &QAction::triggered, this, &MainWindow::setPagePortrait);
    connect(landscapePageAction_, &QAction::triggered, this, &MainWindow::setPageLandscape);
}

void MainWindow::buildUi()
{
    ui::MainWindowActions actions;
    actions.openAction = openAction_;
    actions.closeDiagramAction = closeDiagramAction_;
    actions.saveAction = saveAction_;
    actions.saveAsAction = saveAsAction_;
    actions.loadLibraryAction = loadLibraryAction_;
    actions.saveLibraryAction = saveLibraryAction_;
    actions.examplesAction = examplesAction_;
    actions.fitDiagramAction = fitDiagramAction_;
    actions.exitAction = exitAction_;
    actions.runAction = runAction_;
    actions.runParametersAction = runParametersAction_;
    actions.pauseAction = pauseAction_;
    actions.resumeAction = resumeAction_;
    actions.stopAction = stopAction_;
    actions.resetAction = resetAction_;
    actions.deployAction = deployAction_;
    actions.openDeployedAction = openDeployedAction_;

    const auto menus = ui::MainWindowBuilder::buildMenuAndToolbar(this, actions);
    recentDiagramsMenu_ = new QMenu(QStringLiteral("Recent Diagrams"), this);
    recentDiagramsMenu_->setObjectName(QStringLiteral("menu.recentDiagrams"));
    findChild<QMenu*>(QStringLiteral("menu.file"))->insertMenu(closeDiagramAction_, recentDiagramsMenu_);
    refreshRecentDiagrams();

    diagramDrawToolBar_ = addToolBar(QStringLiteral("Diagram Drawing"));
    diagramDrawToolBar_->setObjectName(QStringLiteral("toolbar.drawing"));
    diagramDrawToolBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    diagramDrawToolBar_->addAction(modeWireAction_);
    diagramDrawToolBar_->addSeparator();
    diagramDrawToolBar_->addAction(addTextFrameAction_);
    diagramDrawToolBar_->addSeparator();
    diagramDrawToolBar_->addAction(textColorAction_);
    diagramDrawToolBar_->addAction(lineColorAction_);
    diagramDrawToolBar_->addAction(fillColorAction_);
    diagramDrawToolBar_->addSeparator();
    textFrameFontCombo_ = new QFontComboBox(diagramDrawToolBar_);
    textFrameFontCombo_->setMinimumContentsLength(10);
    diagramDrawToolBar_->addWidget(textFrameFontCombo_);
    textFrameFontSizeSpin_ = new QSpinBox(diagramDrawToolBar_);
    textFrameFontSizeSpin_->setObjectName(QStringLiteral("frame.toolbarFontSize"));
    textFrameFontSizeSpin_->setRange(diagram::TextFrameNode::kMinimumFontPoints, 72);
    textFrameFontSizeSpin_->setValue(diagram::TextFrameNode::kDefaultFontPoints);
    textFrameFontSizeSpin_->setSuffix(QStringLiteral(" pt"));
    diagramDrawToolBar_->addWidget(textFrameFontSizeSpin_);

    connect(textFrameFontCombo_, &QFontComboBox::currentFontChanged,
            this, &MainWindow::setSelectedTextFrameFont);
    connect(textFrameFontSizeSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::setSelectedTextFrameFontSize);

    auto* viewMenu = menus.view;
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
    viewMenu->addAction(resetZoomAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fitDiagramAction_);
    viewMenu->addAction(fitPageAction_);
    viewMenu->addSeparator();
    menus.run->addSeparator();
    menus.run->addAction(overlayScopeTracesAction_);

    auto* diagramMenu = menus.diagram;
    diagramMenu->addAction(modeNormalAction_);
    diagramMenu->addAction(modeWireAction_);
    diagramMenu->addSeparator();
    diagramMenu->addAction(addTextFrameAction_);
    auto* formatting = diagramMenu->addMenu(QStringLiteral("Text Frame"));
    formatting->setObjectName(QStringLiteral("menu.textFrame"));
    textFrameFontAction_ = formatting->addAction(QStringLiteral("Font and Size…"));
    textFrameFontAction_->setObjectName(QStringLiteral("action.textFrameFont"));
    connect(textFrameFontAction_, &QAction::triggered, this, &MainWindow::chooseTextFrameFont);
    formatting->addAction(textColorAction_);
    formatting->addAction(lineColorAction_);
    formatting->addAction(fillColorAction_);
    diagramMenu->addSeparator();
    auto* pageMenu = diagramMenu->addMenu(QStringLiteral("Page"));
    pageMenu->setObjectName(QStringLiteral("menu.page"));
    pageMenu->addAction(paperA5Action_);
    pageMenu->addAction(paperA4Action_);
    pageMenu->addAction(paperA3Action_);
    pageMenu->addAction(paperLetterAction_);
    pageMenu->addSeparator();
    pageMenu->addAction(portraitPageAction_);
    pageMenu->addAction(landscapePageAction_);

    auto* settingsMenu = menus.settings;
    auto* scopeTempDirectoryAction = settingsMenu->addAction(QStringLiteral("Scope Temporary Data Directory…"));
    connect(scopeTempDirectoryAction, &QAction::triggered,
            this, &MainWindow::chooseScopeTemporaryDirectory);

    auto* helpMenu = menus.help;
    helpMenu->setObjectName(QStringLiteral("menu.help"));
    auto* contentsAction = helpMenu->addAction(QStringLiteral("S7 Studio &Help"));
    contentsAction->setObjectName(QStringLiteral("action.help"));
    contentsAction->setShortcut(QKeySequence::HelpContents);
    connect(contentsAction, &QAction::triggered, this, [this] { showHelp(this); });
    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction(QStringLiteral("&About S7 Studio…"));
    aboutAction->setObjectName(QStringLiteral("action.about"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, [this] { showAbout(this); });
    auto* aboutQtAction = helpMenu->addAction(QStringLiteral("About &Qt…"));
    aboutQtAction->setObjectName(QStringLiteral("action.aboutQt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    connect(aboutQtAction, &QAction::triggered, this, [this] { QMessageBox::aboutQt(this); });

    const auto widgets = ui::MainWindowBuilder::buildCentralWidgets(this);
    diagramEditor_ = widgets.diagramEditor;
    connect(diagramEditor_, &diagram::DiagramEditor::libraryElementAdded, this, &MainWindow::onLibraryElementAdded);
    schemaTabs_=new SchemaTabs(widgets.diagramTabs,diagramEditor_,this);
    connect(schemaTabs_,&SchemaTabs::message,this,&MainWindow::appendLog);
    connect(schemaTabs_,&SchemaTabs::editorOpened,this,[this](diagram::DiagramEditor* editor) {
        editor->addActions(editorShortcuts_);
        connect(editor, &diagram::DiagramEditor::libraryElementAdded, this, &MainWindow::onLibraryElementAdded);
        connect(editor,&diagram::DiagramEditor::elementNodeDoubleClicked,this,&MainWindow::onElementNodeDoubleClicked);
        connect(editor->scene(),&QGraphicsScene::selectionChanged,this,&MainWindow::syncTextFrameStyleControls);
        connect(editor->scene(),&QSchematic::Scene::modeChanged,this,[this,editor](int mode) {
            if (activeDiagramEditor()==editor && modeWireAction_) {
                const QSignalBlocker blocker(modeWireAction_);
                modeWireAction_->setChecked(mode==diagram::DiagramScene::DrawMode);
                modeNormalAction_->setChecked(mode==QSchematic::Scene::NormalMode);
            }
        });
        connect(editor->scene(),&diagram::DiagramScene::statusMessage,this,[this](const QString& message) {statusBar()->showMessage(message,6000);});
        connect(editor->scene(),&diagram::DiagramScene::diagramChanged,this,[editor] {
            QString error;
            static_cast<void>(diagram::updateWireTypeLabels(*editor->scene(),&error));
        });
    });
    connect(schemaTabs_,&SchemaTabs::activeEditorChanged,this,[this](diagram::DiagramEditor* editor) {
        syncPageActions();syncTextFrameStyleControls();
        if (modeWireAction_) {
            const QSignalBlocker blocker(modeWireAction_);
            modeWireAction_->setChecked(editor->scene()->mode()==diagram::DiagramScene::DrawMode);
            modeNormalAction_->setChecked(editor->scene()->mode()==QSchematic::Scene::NormalMode);
        }
    });
    const auto editing = ui::buildEditorActions(this, menus.edit, schemaTabs_->undoGroup(), [this] { return activeDiagramEditor(); });
    undoAction_ = editing.undo;
    redoAction_ = editing.redo;
    editorShortcuts_ = editing.shortcuts;
    auto* information = menus.edit->addAction(QStringLiteral("Element Information…"));
    information->setObjectName(QStringLiteral("action.elementInformation"));
    connect(information, &QAction::triggered, this, [this] {
        const auto* editor = activeDiagramEditor();
        if (!editor) return;
        for (const auto& node : editor->scene()->elementNodes()) if (node->isSelected()) {
            ui::showElementInformation(s7::ElementRegistry::instance(), QString::fromStdString(node->element().type),
                                       this, node->element().schema);
            return;
        }
    });
    connect(menus.edit, &QMenu::aboutToShow, this, [this, information] {
        const auto* editor = activeDiagramEditor();
        bool selected = false;
        if (editor) for (const auto& node : editor->scene()->elementNodes()) selected |= node->isSelected();
        information->setEnabled(selected);
    });
    editorShortcuts_.append({zoomInAction_, zoomOutAction_});
    diagramEditor_->addActions(editorShortcuts_);
    diagramDrawToolBar_->insertAction(modeWireAction_, undoAction_);
    diagramDrawToolBar_->insertAction(modeWireAction_, redoAction_);
    diagramDrawToolBar_->insertSeparator(modeWireAction_);
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_io_elements(registry);
    s7::register_net_elements(registry);
    pluginLoader_ = std::make_unique<s7::PluginLoader>();
    pluginLoader_->load_plugins(registry);
    if (pluginLoader_ && !registry.find("WhiteNoiseGenerator")) {
        for (const auto& candidate : developmentPluginCandidates()) {
            QFileInfo info(QString::fromStdString(candidate));
            if (!info.exists() || !info.isFile()) {
                continue;
            }
            pluginLoader_->load_plugin(candidate, registry);
            if (registry.find("WhiteNoiseGenerator")) {
                appendLog(QStringLiteral("[studio] loaded noise plugin from build tree: %1").arg(info.absoluteFilePath()));
                break;
            }
        }
    }

    elementPickerPanel_ = new ui::ElementPickerPanel(registry, this);
    connect(elementPickerPanel_, &ui::ElementPickerPanel::elementRequested,
            this, &MainWindow::onElementRequested);
    headlessNodesPanel_ = new ui::HeadlessNodesPanel(this);
    buildToolWindows();
    auto* headlessNodes = new QAction(QStringLiteral("Headless Nodes"), this);
    headlessNodes->setObjectName(QStringLiteral("action.headlessNodes"));
    connect(headlessNodes, &QAction::triggered, this, [this] { toolWindows_->show(headlessNodesDock_); });
    menus.nodes->insertAction(deployAction_, headlessNodes);
    menus.nodes->insertSeparator(deployAction_);
    viewMenu->addSeparator();
    viewMenu->addMenu(toolWindows_->menu());
    viewMenu->addAction(toolWindows_->hideAllAction());
    auto* appearance = viewMenu->addMenu(QStringLiteral("&Appearance"));
    appearance->addAction(toolWindows_->stripesAction());
    for (auto* toolBar : findChildren<QToolBar*>(Qt::FindDirectChildrenOnly)) {
        if (toolBar->objectName().startsWith(QStringLiteral("toolbar."))) {
            auto* toggle = toolBar->toggleViewAction();
            toggle->setText(toolBar->windowTitle() + QStringLiteral(" Toolbar"));
            appearance->addAction(toggle);
        }
    }

    connect(headlessNodesPanel_, &ui::HeadlessNodesPanel::nodeActivated,
            this, &MainWindow::onHeadlessNodeActivated);

    if (pluginLoader_) {
        appendLog(QStringLiteral("[studio] plugins loaded: %1").arg(pluginLoader_->loaded_count()));
    }

    connectSceneSignals();
    syncPageActions();
    syncTextFrameStyleControls();
}

diagram::DiagramEditor* MainWindow::activeDiagramEditor() const
{
    return schemaTabs_ ? schemaTabs_->activeEditor() : diagramEditor_;
}

void MainWindow::onElementRequested(const QString& elementType, const QString& /*displayName*/)
{
    if (auto* editor = activeDiagramEditor()) editor->addLibraryElement(elementType);
}

void MainWindow::onLibraryElementAdded(const QString& elementId)
{
    refreshPlacementLabels();
    appendLog(QStringLiteral("[studio] added element %1").arg(elementId));
}

void MainWindow::onHeadlessNodeActivated(const QString& endpoint,
                                         const QString& nodeUuid,
                                         const QString& nodeName)
{
    const auto title = nodeName.isEmpty() ? endpoint : nodeName;
    appendLog(QStringLiteral("[studio] headless node activated: %1 (%2)").arg(title, nodeUuid.isEmpty() ? QStringLiteral("no-uuid") : nodeUuid));
}

void MainWindow::buildToolWindows()
{
    toolWindows_ = new ui::ToolWindows(this);
    elementPickerDock_ = toolWindows_->add(QStringLiteral("elementLibrary"), QStringLiteral("Element Library"), QStringLiteral("books"),
                                           elementPickerPanel_, Qt::RightDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_1));
    headlessNodesDock_ = toolWindows_->add(QStringLiteral("headlessNodes"), QStringLiteral("Headless Nodes"), QStringLiteral("server-2"),
                                           headlessNodesPanel_, Qt::RightDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_2));

    problemsPanel_ = new ui::ProblemsPanel(this);
    connect(problemsPanel_, &ui::ProblemsPanel::activated, this, &MainWindow::showProblem);
    problemsDock_ = toolWindows_->add(QStringLiteral("problems"), QStringLiteral("Problems"), QStringLiteral("alert-triangle"), problemsPanel_,
                                      Qt::BottomDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_6));
    runPanel_ = new ui::RunPanel({runAction_, pauseAction_, resumeAction_, stopAction_, resetAction_, nullptr, runParametersAction_}, this);
    runDock_ = toolWindows_->add(QStringLiteral("run"), QStringLiteral("Run"), QStringLiteral("player-play"), runPanel_,
                                 Qt::BottomDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_4));
    deployPanel_ = new ui::DeployPanel({deployAction_, openDeployedAction_}, this);
    deployDock_ = toolWindows_->add(QStringLiteral("deploy"), QStringLiteral("Deploy"), QStringLiteral("cloud-upload"), deployPanel_,
                                    Qt::BottomDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_8));
    logPanel_ = new ui::LogPanel(this);
    logDock_ = toolWindows_->add(QStringLiteral("log"), QStringLiteral("Log"), QStringLiteral("file-text"), logPanel_,
                                 Qt::BottomDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_7));

    recordedSignalsDialog_ = new QDialog(this); recordedSignalsDialog_->setWindowTitle("Recorded Signals"); recordedSignalsDialog_->setObjectName("observers.recordedSignalsDialog"); recordedSignalsDialog_->resize(650, 400);
    probesPanel_ = new ui::ProbesPanel(recordedSignalsDialog_); (new QVBoxLayout(recordedSignalsDialog_))->addWidget(probesPanel_);
    connect(probesPanel_, &ui::ProbesPanel::openScopeRequested, this, &MainWindow::onProbeOpenScopeRequested);
    connect(probesPanel_, &ui::ProbesPanel::openPhasePortraitRequested, this, &MainWindow::onProbeOpenPhasePortraitRequested);
    connect(probesPanel_, &ui::ProbesPanel::renameRequested, this, &MainWindow::onProbeRenameRequested);
    connect(probesPanel_, &ui::ProbesPanel::deleteRequested, this, &MainWindow::onProbeDeleteRequested);
    observersPanel_ = new ui::ObserversPanel(viewers_, this);
    connect(observersPanel_, &ui::ObserversPanel::selectionChanged, this, [this](const boost::json::array& selection) {
        if (diagramEditor_ && diagramEditor_->scene()) diagramEditor_->scene()->setObserversPanel(selection, true);
    });
    connect(observersPanel_, &ui::ObserversPanel::recordedSignalsRequested, recordedSignalsDialog_, [this] { refreshProbesPanel(); recordedSignalsDialog_->show(); recordedSignalsDialog_->raise(); });
    observersDock_ = toolWindows_->add(QStringLiteral("observers"), QStringLiteral("Observers"), QStringLiteral("pointer"), observersPanel_,
                                    Qt::RightDockWidgetArea, QKeySequence(Qt::ALT | Qt::Key_3));

    // Problem count in the status bar; a click opens Problems.
    problemsStatus_ = new QToolButton(this);
    problemsStatus_->setAutoRaise(true);
    problemsStatus_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    problemsStatus_->setToolTip(QStringLiteral("Problems (Alt+6)"));
    connect(problemsStatus_, &QToolButton::clicked, this, [this] { toolWindows_->show(problemsDock_); });
    statusBar()->addPermanentWidget(problemsStatus_);

    // The Element Library is open at first start; restoreWindowLayout() brings back the last layout.
    toolWindows_->show(elementPickerDock_);
    toolWindows_->normalize();
    refreshProblems();
}

void MainWindow::refreshProblems()
{
    if (!problemsPanel_) {
        return;
    }
    std::vector<ui::Problem> problems;
    if (diagramEditor_ && diagramEditor_->scene()) {
        for (const auto& node : diagramEditor_->scene()->elementNodes()) {
            if (!node->problem().isEmpty()) {
                problems.push_back({ui::Problem::Severity::Error, node->problem(), QString::fromStdString(node->id()), {}});
            }
        }
        for (const auto& wire : diagramEditor_->scene()->signalWires()) {
            if (!wire->problem().isEmpty()) {
                const auto target = wire->downstream.node.lock();
                problems.push_back({ui::Problem::Severity::Error, wire->problem(), {},
                                    target ? QStringLiteral("%1:%2").arg(QString::fromStdString(target->id())).arg(wire->downstream.index)
                                           : QString()});
            }
        }
    }
    // Type errors only count when every element type is available (otherwise they follow from that).
    const bool typesKnown = std::ranges::none_of(problems, [](const ui::Problem& problem) { return !problem.element.isEmpty(); });
    if (!lastWireTypeError_.isEmpty() && typesKnown) {
        problems.push_back({ui::Problem::Severity::Error, QStringLiteral("Types: %1").arg(lastWireTypeError_), {}, {}});
    }
    for (const auto& warning : loadWarnings_) {
        problems.push_back({ui::Problem::Severity::Warning, warning, {}, {}});
    }
    problemsPanel_->setProblems(std::move(problems));

    const int errors = problemsPanel_->errorCount();
    const int warnings = problemsPanel_->warningCount();
    if (errors > 0) {
        problemsStatus_->setIcon(ui::icon(QStringLiteral("circle-x"), QColor(0xc6, 0x28, 0x28)));
        problemsStatus_->setText(QStringLiteral("%1 error(s)").arg(errors));
    } else if (warnings > 0) {
        problemsStatus_->setIcon(ui::icon(QStringLiteral("alert-triangle"), QColor(0xc7, 0x7c, 0x02)));
        problemsStatus_->setText(QStringLiteral("%1 warning(s)").arg(warnings));
    } else {
        problemsStatus_->setIcon(ui::icon(QStringLiteral("circle-check"), QColor(0x2e, 0x7d, 0x32)));
        problemsStatus_->setText(QStringLiteral("No problems"));
    }
}

void MainWindow::showProblem(const ui::Problem& problem)
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (!scene) {
        return;
    }
    QGraphicsItem* item = nullptr;
    if (!problem.element.isEmpty()) {
        if (const auto node = scene->elementNode(problem.element.toStdString())) {
            item = node.get();
        }
    } else if (!problem.wireInput.isEmpty()) {
        for (const auto& wire : scene->signalWires()) {
            const auto target = wire->downstream.node.lock();
            if (target && QStringLiteral("%1:%2").arg(QString::fromStdString(target->id())).arg(wire->downstream.index) == problem.wireInput) {
                item = wire.get();
            }
        }
    }
    if (item) {
        diagramEditor_->showItem(item);
    }
}

void MainWindow::saveWindowLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
}

void MainWindow::restoreWindowLayout()
{
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
    toolWindows_->normalize();
}

void MainWindow::chooseScopeTemporaryDirectory()
{
    QSettings settings;
    const auto currentDirectory = settings.value(
        QStringLiteral("scope/temporaryDirectory"), QDir::tempPath()).toString();
    const auto directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose Scope temporary data directory"), currentDirectory);
    if (directory.isEmpty()) {
        return;
    }
    if (!QDir(directory).exists()) {
        QMessageBox::critical(this, QStringLiteral("Invalid directory"),
                              QStringLiteral("The selected Scope temporary data directory does not exist."));
        return;
    }
    settings.setValue(QStringLiteral("scope/temporaryDirectory"), directory);
    statusBar()->showMessage(QStringLiteral("Scope temporary data directory set to %1").arg(directory), 5000);
}

void MainWindow::appendLog(const QString& message)
{
    if (logPanel_) {
        logPanel_->append(message);
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onStatusChanged(const QString& status)
{
    if (observersPanel_) {
        const auto configuration = runner_->activeConfiguration();
        observersPanel_->setRunState(runner_->state(), runner_->completedTicks(), configuration ? configuration->dt : 0.);
    }
    if (shuttingDown_) return;
    if (runPanel_) {
        // A state notification may have been queued before another command.
        runPanel_->setStatus(QString::fromLatin1(runStateName(runner_->state())));
        if (const auto configuration = runner_->activeConfiguration()) {
            runPanel_->setConfigurationSummary(QStringLiteral("Run %1 — %2 Hz, %3. Edits apply to the next run.")
                .arg(configuration->id).arg(1.0 / configuration->dt, 0, 'g', 10)
                .arg(configuration->ticks < 0 ? QStringLiteral("continuous") : QStringLiteral("%1 steps").arg(configuration->ticks)));
        } else runPanel_->setConfigurationSummary(QString());
    }
    (void)status;
    refreshActions();
}

void MainWindow::onRunCompleted(bool success, const QString& error)
{
    if (shuttingDown_) return;
    onScopeSamplesAvailable(); // final flush/error status, even with no pending preview notification
    if (const auto configuration = runner_->activeConfiguration()) observersPanel_->setRunState(runner_->state(), runner_->completedTicks(), configuration->dt, error);
    if (success) {
        appendLog("[studio] run completed");
        runPanel_->append(QStringLiteral("Run completed."));
    } else if (!error.isEmpty()) {
        appendLog(QString("[studio] run failed: %1").arg(error));
        runPanel_->append(QStringLiteral("Run failed: %1").arg(error));
        // A failed run must not look like an empty one.
        toolWindows_->show(runDock_);
    }
    refreshActions();
}

void MainWindow::onTickProgress(const long completed, const long total)
{
    if (runPanel_) {
        const auto configuration = runner_->activeConfiguration();
        runPanel_->setProgress(completed, total, configuration ? static_cast<double>(completed)*configuration->dt : 0.);
        observersPanel_->setRunState(runner_->state(), completed, configuration ? configuration->dt : 0.);
    }
}

void MainWindow::onElementNodeDoubleClicked(const QString& elementId,
                                            const QString& elementType,
                                            const int /*channelCount*/)
{
    auto* scene = activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr;
    if (!scene) {
        return;
    }
    if (elementType == QStringLiteral("TextFrame")) {
        for (const auto& frame : scene->textFrames()) {
            if (QString::fromStdString(frame->id()) != elementId) {
                continue;
            }
            auto params = frame->params();
            bool accepted = false;
            const QString editedText = QInputDialog::getMultiLineText(
                this,
                QStringLiteral("Edit Text Frame"),
                QStringLiteral("Text"),
                QString::fromStdString(json_utils::stringValue(params, "text", "")),
                &accepted);
            if (!accepted) {
                return;
            }
            params["text"] = editedText.toStdString();
            frame->setParams(params);
            appendLog(QStringLiteral("[studio] updated text frame: %1").arg(elementId));
            syncTextFrameStyleControls();
            return;
        }
        return;
    }
    const auto elementNode = scene->elementNode(elementId.toStdString());
    if (!elementNode) {
        return;
    }

    if (viewers::ViewerRegistry::instance().find(elementType)) {
        if (activeDiagramEditor()!=diagramEditor_) {
            appendLog(QStringLiteral("[studio] Nested tabs edit schema structure; open runtime observers from the root diagram."));
            return;
        }
        if (!observerMatchesActiveRun(elementId)) {
            appendLog(QStringLiteral("[studio] This observer was added or changed after the run snapshot. Run again to observe its new configuration."));
            return;
        }
        viewers_->openObserver(elementType, QString::fromStdString(elementNode->id()));
        return;
    }

    const auto* descriptor = s7::ElementRegistry::instance().find(elementType.toStdString());
    if (elementNode->element().schema.is_object() || (descriptor && descriptor->metadata().schema_definition)) {
        try {schemaTabs_->open(activeDiagramEditor(),elementNode->id());}
        catch(const std::exception& e) {QMessageBox::warning(this,QStringLiteral("Open schema"),QString::fromUtf8(e.what()));}
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("dialog.elementProperties"));
    dialog.setWindowTitle(QStringLiteral("Element Properties - %1").arg(elementId));
    dialog.resize(720, 680);
    auto* layout = new QVBoxLayout(&dialog);
    auto* splitter = new QSplitter(Qt::Vertical, &dialog);
    splitter->setObjectName(QStringLiteral("element.propertiesSplitter"));
    auto* documentation = new QWidget(splitter);
    auto* documentationLayout = new QVBoxLayout(documentation);
    documentationLayout->setContentsMargins(0, 0, 0, 0);
    auto* identity = new QLabel(QStringLiteral("%1 — %2").arg(elementId, elementType), documentation);
    identity->setTextFormat(Qt::PlainText); identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    documentationLayout->addWidget(identity);
    documentationLayout->addWidget(ui::createElementDocumentation(
        ui::elementDocumentation(s7::ElementRegistry::instance(), elementType), documentation), 1);
    splitter->addWidget(documentation);
    auto* settings = new QWidget(splitter);
    auto* settingsLayout = new QVBoxLayout(settings);
    settingsLayout->setContentsMargins(0, 0, 0, 0);

    // Ports and deployment remain available without crowding routine tuning.
    auto* detailsButton = new QToolButton(&dialog);
    detailsButton->setText(QStringLiteral("Ports and deployment"));
    detailsButton->setCheckable(true); detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsButton->setArrowType(Qt::RightArrow);
    auto* details = new QWidget(&dialog); auto* form = new QFormLayout(details);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto* library = new QLabel(elementLibraryForType(elementType), details); library->setWordWrap(true);
    form->addRow(QStringLiteral("Library"), library);
    const QString currentPlacement = QString::fromStdString(elementNode->element().placement);
    auto* nodeCombo = new QComboBox(details);
    nodeCombo->addItem(QStringLiteral("(not placed)"), QString());
    if (headlessNodesPanel_) for (const auto& known : headlessNodesPanel_->knownNodes())
        nodeCombo->addItem(QStringLiteral("%1  %2  (%3)").arg(known.nodeName, known.endpoint, known.nodeUuid.left(8)), known.nodeUuid);
    if (!currentPlacement.isEmpty() && nodeCombo->findData(currentPlacement) < 0) nodeCombo->addItem(currentPlacement, currentPlacement);
    nodeCombo->setCurrentIndex(std::max(0, nodeCombo->findData(currentPlacement)));
    nodeCombo->setAccessibleName(QStringLiteral("Deployment node"));
    nodeCombo->setToolTip(QStringLiteral("Used only when deploying to a runtime node. Local simulation does not require placement."));
    form->addRow(QStringLiteral("Deployment node"), nodeCombo);
    if (descriptor) {
        auto* inputs = new QLabel(portListSummary(descriptor->metadata().input_ports), details); inputs->setWordWrap(true); inputs->setTextFormat(Qt::PlainText);
        auto* outputs = new QLabel(portListSummary(descriptor->metadata().output_ports), details); outputs->setWordWrap(true); outputs->setTextFormat(Qt::PlainText);
        form->addRow(QStringLiteral("Inputs"), inputs); form->addRow(QStringLiteral("Outputs"), outputs);
    }
    details->setVisible(false);
    connect(detailsButton, &QToolButton::toggled, details, &QWidget::setVisible);
    connect(detailsButton, &QToolButton::toggled, &dialog, [detailsButton](bool checked) { detailsButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow); });
    settingsLayout->addWidget(detailsButton); settingsLayout->addWidget(details);
    auto* editor = new ui::ParameterEditor(descriptor ? std::optional{s7::ElementMetadata(descriptor->metadata())} : std::nullopt,
                                           elementNode->element().params, &dialog);
    settingsLayout->addWidget(editor, 1);
    splitter->addWidget(settings);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 2);
    splitter->setSizes({200, 400});
    layout->addWidget(splitter, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    boost::json::object params;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString error;
        if (!editor->readParameters(params, error)) { editor->showError(error); return; }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        auto element = elementNode->element(); element.params = std::move(params);
        element.placement = nodeCombo->currentData().toString().toStdString();
        scene->changeElement(elementNode, std::move(element));
        refreshPlacementLabels();
        appendLog(QStringLiteral("[studio] updated element params: %1").arg(elementId));
    }
}

void MainWindow::onScopeSamplesAvailable()
{
    if (shuttingDown_ || !runner_) return;
    const auto batch = runner_->takeScopeSamples();
    const auto configuration = runner_->activeConfiguration();
    if (!configuration || batch.runId != configuration->id) return;
    // Capture already happened on the worker; this only attaches read-only data.
    viewers_->presentLive(runner_->liveSources());
    viewers_->present(runner_->recordedSource(), batch.dropped,
        [this](const QString& observerId) { return observerViewerType(observerId); });
    if (batch.dropped > 0) {
        // The runtime produced samples faster than the UI took them; the oldest were dropped.
        const auto nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastScopeDropLogMs_ >= 1000) {
            lastScopeDropLogMs_ = nowMs;
            appendLog(QStringLiteral("[studio] preview skipped %1 updates; authoritative recording is unaffected")
                          .arg(viewers_->droppedSampleCount()));
        }
    }
}

auto MainWindow::observerViewerType(const QString& observerId) const -> QString
{
    if (observersFrozen_) return runObserverTypes_.value(observerId);
    const auto node = diagramEditor_ && diagramEditor_->scene()
        ? diagramEditor_->scene()->elementNode(observerId.toStdString()) : nullptr;
    if (node) {
        const auto type = QString::fromStdString(node->element().type);
        if (viewers::ViewerRegistry::instance().find(type)) return type;
    }
    return QStringLiteral("Scope");
}

void MainWindow::applyTriggerPreset(const QString& observerId, bool reportInvalid)
{
    const auto node = diagramEditor_ && diagramEditor_->scene() ? diagramEditor_->scene()->elementNode(observerId.toStdString()) : nullptr;
    if (!node || node->element().type != "LiveOscilloscope") return;
    // An observer saved without a preset uses the defaults, as it always did.
    observation::TriggerSettings settings;
    if (const auto* preset = json_utils::findObjectValue(node->element().params, "trigger")) {
        QString error;
        const auto json = QJsonDocument::fromJson(QByteArray::fromStdString(boost::json::serialize(*preset)));
        const auto parsed = preset->is_object() ? observation::triggerSettingsFromJson(json.object(), &error) : std::nullopt;
        if (!parsed) {
            if (reportInvalid)
                appendLog(QStringLiteral("[studio] %1: ignoring saved trigger settings: %2")
                    .arg(observerId, preset->is_object() ? error : QStringLiteral("trigger must be an object")));
            return;
        }
        settings = *parsed;
    }
    QString error;
    if (!viewers_->setObserverTrigger(observerId, settings, &error) && reportInvalid) appendLog(QStringLiteral("[studio] %1 trigger: %2").arg(observerId, error));
}

void MainWindow::storeTriggerPreset(const QString& observerId)
{
    auto* live = dynamic_cast<ui::LiveOscilloscopeWindow*>(viewers_->find(QStringLiteral("LiveOscilloscope"), observerId));
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    const auto node = live && scene ? scene->elementNode(observerId.toStdString()) : nullptr;
    if (!node || node->element().type != "LiveOscilloscope") return;
    const auto json = QJsonDocument(observation::triggerSettingsToJson(live->triggerSettings())).toJson(QJsonDocument::Compact);
    auto element = node->element();
    auto preset = boost::json::parse(json.toStdString());
    if (const auto* old = element.params.if_contains("trigger"); old && *old == preset) return;
    element.params["trigger"] = std::move(preset);
    // Trigger settings act on the running acquisition at once, unlike other edits.
    storingTriggerPreset_ = true;
    scene->changeElement(node, std::move(element));
    storingTriggerPreset_ = false;
}

bool MainWindow::observerMatchesActiveRun(const QString& observerId) const
{
    if (!observersFrozen_) return true;
    const auto configuration = runner_->activeConfiguration();
    if (!configuration || !runObserverTypes_.contains(observerId)) return false;
    const auto node = diagramEditor_->scene()->elementNode(observerId.toStdString());
    if (node && viewers::ViewerRegistry::instance().find(QString::fromStdString(node->element().type))
        && runObserverTypes_.value(observerId) != QString::fromStdString(node->element().type)) return false;
    auto select = [&](const std::vector<RunProbe>& probes) {
        std::vector<RunProbe> selected;
        for (const auto& probe : probes)
            if (viewers::ViewerManager::observerIdOf(QString::fromStdString(probe.name)) == observerId) selected.push_back(probe);
        return selected;
    };
    return select(configuration->probes) == select(currentRunProbes());
}

void MainWindow::refreshActions()
{
    const auto state = controller_->actionState();
    if (runAction_) {
        runAction_->setEnabled(state.runEnabled);
    }
    if (pauseAction_) {
        pauseAction_->setEnabled(state.pauseEnabled);
    }
    if (resumeAction_) {
        resumeAction_->setEnabled(state.resumeEnabled);
    }
    if (stopAction_) {
        stopAction_->setEnabled(state.stopEnabled);
    }
    if (resetAction_) {
        resetAction_->setEnabled(state.resetEnabled);
    }
}

void MainWindow::zoomDiagramIn()
{
    if (activeDiagramEditor()) {
        activeDiagramEditor()->zoomIn();
    }
}

void MainWindow::zoomDiagramOut()
{
    if (activeDiagramEditor()) {
        activeDiagramEditor()->zoomOut();
    }
}

void MainWindow::resetDiagramZoom()
{
    if (activeDiagramEditor()) {
     activeDiagramEditor()->resetZoom();
     }
 }

 void MainWindow::setDiagramMode(const int mode)
{
    if (activeDiagramEditor() && activeDiagramEditor()->scene()) {
        activeDiagramEditor()->scene()->setMode(mode);
        const QSignalBlocker wire(modeWireAction_), normal(modeNormalAction_);
        modeWireAction_->setChecked(mode == diagram::DiagramScene::DrawMode);
        modeNormalAction_->setChecked(mode == QSchematic::Scene::NormalMode);
    }
}

void MainWindow::connectSceneSignals()
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (!scene) {
        return;
    }

    connect(scene, &QSchematic::Scene::modeChanged, this, [this](int mode) {
        if (modeWireAction_) {
            const QSignalBlocker blockerWire(modeWireAction_);
            modeWireAction_->setChecked(mode == diagram::DiagramScene::DrawMode);
            modeNormalAction_->setChecked(mode == QSchematic::Scene::NormalMode);
        }
    });
    connect(scene, &diagram::DiagramScene::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });

    connect(scene, &QGraphicsScene::selectionChanged,
            this, &MainWindow::syncTextFrameStyleControls,
            Qt::UniqueConnection);

    // Wire labels show resolved types; re-resolve shortly after the diagram changes.
    if (!wireTypeTimer_) {
        wireTypeTimer_ = new QTimer(this);
        wireTypeTimer_->setSingleShot(true);
        wireTypeTimer_->setInterval(150);
        connect(wireTypeTimer_, &QTimer::timeout, this, &MainWindow::refreshWireTypeLabels);
    }
    connect(scene, &diagram::DiagramScene::diagramChanged, wireTypeTimer_, qOverload<>(&QTimer::start),
            Qt::UniqueConnection);
    // A probe's wire label depends on resolved types too (it is drawn on the
    // net's first wire, same as the type label), so probe changes go through
    // the same debounced refresh.
    connect(scene, &diagram::DiagramScene::probesChanged, wireTypeTimer_, qOverload<>(&QTimer::start),
            Qt::UniqueConnection);
    connect(scene, &diagram::DiagramScene::probesChanged, this, &MainWindow::onProbesChanged, Qt::UniqueConnection);
    connect(scene, &diagram::DiagramScene::diagramChanged, this, &MainWindow::refreshObserversPanel, Qt::UniqueConnection);
    connect(scene, &diagram::DiagramScene::probesChanged, this, &MainWindow::refreshObserversPanel, Qt::UniqueConnection);
    connect(scene, &diagram::DiagramScene::observersPanelChanged, this, &MainWindow::refreshObserversPanel, Qt::UniqueConnection);
    // Undo/redo of a stored preset updates the open window.
    connect(scene, &diagram::DiagramScene::diagramChanged, this, [this, scene] {
        if (storingTriggerPreset_) return;
        for (const auto& node : scene->elementNodes())
            if (node->element().type == "LiveOscilloscope") applyTriggerPreset(QString::fromStdString(node->id()), false);
    });
    const auto reportNextRunEdit = [this] {
        if (!shuttingDown_ && !storingTriggerPreset_ && runner_->isRunning())
            statusBar()->showMessage(QStringLiteral("Edits apply to the next run; the active simulation keeps its starting configuration."), 6000);
    };
    connect(scene, &diagram::DiagramScene::diagramChanged, this, reportNextRunEdit);
    connect(scene, &diagram::DiagramScene::probesChanged, this, reportNextRunEdit);
    wireTypeTimer_->start();
    onProbesChanged();
    refreshObserversPanel();
    auto* modifiedTimer = new QTimer(this);
    modifiedTimer->setSingleShot(true);
    modifiedTimer->setInterval(150);
    connect(modifiedTimer, &QTimer::timeout, this, [this] { setWindowModified(documentModified()); });
    connect(scene, &diagram::DiagramScene::diagramChanged, modifiedTimer, qOverload<>(&QTimer::start));
    connect(scene, &diagram::DiagramScene::probesChanged, modifiedTimer, qOverload<>(&QTimer::start));
}

void MainWindow::refreshWireTypeLabels()
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        return;
    }
    QString error;
    static_cast<void>(diagram::updateWireTypeLabels(*diagramEditor_->scene(), &error));
    if (error != lastWireTypeError_) {
        lastWireTypeError_ = error;
        if (!error.isEmpty()) {
            appendLog(QStringLiteral("[studio] types: %1").arg(error));
        }
    }
    refreshProblems();
}

void MainWindow::onProbesChanged()
{
    refreshProbesPanel();
}

void MainWindow::refreshProbesPanel()
{
    if (!probesPanel_) {
        return;
    }
    std::vector<ui::ProbeRow> rows;
    if (diagramEditor_ && diagramEditor_->scene()) {
        for (const auto& probe : diagramEditor_->scene()->probes()) {
            rows.push_back(ui::ProbeRow{probe.name, probe.point.element, probe.point.port, probe.unit});
        }
    }
    probesPanel_->setProbes(std::move(rows));
}

QVector<viewers::ObserverDescriptor> MainWindow::observerDescriptors() const
{
    QVector<viewers::ObserverDescriptor> result;
    if (!diagramEditor_ || !diagramEditor_->scene()) return result;
    const auto probes = currentRunProbes();
    for (const auto& node : diagramEditor_->scene()->elementNodes()) {
        const auto id = QString::fromStdString(node->id()), type = QString::fromStdString(node->element().type);
        if (!viewers::ViewerRegistry::instance().find(type)) continue;
        viewers::ObserverDescriptor descriptor{id, type, {}};
        for (const auto& probe : probes) if (QString::fromStdString(probe.name).startsWith(id+"/"))
            descriptor.inputs.push_back({QString::fromStdString(probe.name), QString::fromStdString(probe.unit), QString::fromStdString(probe.element), probe.port});
        if (diagram::isAxisObserverElementType(node->element().type))
            std::sort(descriptor.inputs.begin(), descriptor.inputs.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        result.push_back(std::move(descriptor));
    }
    return result;
}
void MainWindow::refreshObserversPanel()
{
    if (!observersPanel_ || !diagramEditor_ || !diagramEditor_->scene()) return;
    observersPanel_->setDocument(schemaDisplayName(runtimeSchema_, "untitled"), observerDescriptors(), diagramEditor_->scene()->observersPanel());
}

auto MainWindow::currentRunProbes() const -> std::vector<RunProbe>
{
    std::vector<RunProbe> probes;
    if (diagramEditor_ && diagramEditor_->scene()) {
        const auto* scene = diagramEditor_->scene();
        for (const auto& probe : scene->probes()) {
            probes.push_back(RunProbe{probe.name.toStdString(), probe.point.element, probe.point.port, probe.unit.toStdString()});
        }
        const diagram::NetModel model(*scene);
        for (const auto& connection : model.observationConnections()) {
            auto channelName = std::string("signal") + std::to_string(connection.to.port);
            if (const auto scope = scene->elementNode(connection.to.element)) {
                if (diagram::isAxisObserverElementType(scope->element().type)) {
                    static const std::array<std::string, 3> axes{"x", "y", "z"};
                    if (connection.to.port >= 0 && connection.to.port < static_cast<int>(axes.size())) {
                        channelName = axes[static_cast<std::size_t>(connection.to.port)];
                    }
                }
                const auto* names = json_utils::findObjectValue(scope->element().params, "channel_names");
                if (!diagram::isAxisObserverElementType(scope->element().type) &&
                    names && names->is_array() && connection.to.port < static_cast<int>(names->as_array().size()) &&
                    names->as_array()[connection.to.port].is_string()) {
                    channelName = names->as_array()[connection.to.port].as_string().c_str();
                }
            }
            std::string unit;
            for (const auto& probe : scene->probes()) if (probe.point == connection.from) {
                unit = probe.unit.toStdString(); break;
            }
            const auto observer = scene->elementNode(connection.to.element);
            const bool live = observer && observer->element().type == "LiveOscilloscope";
            if (observer) {
                const auto* units = json_utils::findObjectValue(observer->element().params, "channel_units");
                if (units && units->is_array() && connection.to.port >= 0 &&
                    connection.to.port < static_cast<int>(units->as_array().size()) && units->as_array()[connection.to.port].is_string())
                    unit = units->as_array()[connection.to.port].as_string().c_str();
            }
            probes.push_back(RunProbe{connection.to.element + "/" + channelName, connection.from.element,
                connection.from.port, unit, !live, live ? connection.to.element : std::string{}});
        }
    }
    return probes;
}

void MainWindow::onProbeOpenScopeRequested(const QString& probeName)
{
    if (!observerMatchesActiveRun(viewers::ViewerManager::observerIdOf(probeName))) {
        appendLog(QStringLiteral("[studio] This probe changed after the run snapshot; run again to observe it."));
        return;
    }
    viewers_->open(QStringLiteral("Scope"), {probeName, {}});
}

void MainWindow::onProbeOpenPhasePortraitRequested(const QString& xProbeName, const QString& yProbeName)
{
    if (!observerMatchesActiveRun(viewers::ViewerManager::observerIdOf(xProbeName))
        || !observerMatchesActiveRun(viewers::ViewerManager::observerIdOf(yProbeName))) {
        appendLog(QStringLiteral("[studio] These probes changed after the run snapshot; run again to observe them."));
        return;
    }
    // Ad-hoc portraits of two probes are keyed by the pair.
    viewers_->open(QStringLiteral("PhasePortrait"),
                   {QStringLiteral("%1|%2").arg(xProbeName, yProbeName), {xProbeName, yProbeName}});
}

void MainWindow::onProbeRenameRequested(const QString& probeName, const QString& newName)
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (!scene) {
        return;
    }
    for (const auto& probe : scene->probes()) {
        if (probe.name == probeName) {
            scene->renameProbe(probe.point, newName);
            return;
        }
    }
}

void MainWindow::onProbeDeleteRequested(const QString& probeName)
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (!scene) {
        return;
    }
    for (const auto& probe : scene->probes()) {
        if (probe.name == probeName) {
            scene->removeProbe(probe.point);
            return;
        }
    }
}

void MainWindow::addTextFrame()
{
    auto* scene = activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr;
    if (!scene) {
        return;
    }
    const auto frameId = nextTextFrameId(*scene);
    const auto params = diagram::TextFrameNode::defaultParams();
    const QSizeF size(params.at("width").to_number<double>(), params.at("height").to_number<double>());
    const auto frame = scene->addTextFrame(frameId, params, activeDiagramEditor()->suggestedNewItemPosition(size));
    scene->clearSelection();
    frame->setSelected(true);
    setDiagramMode(QSchematic::Scene::NormalMode);
    syncTextFrameStyleControls();
    appendLog(QStringLiteral("[studio] added diagram text frame: %1").arg(QString::fromStdString(frameId)));
}

void MainWindow::setSelectedTextFrameTextColor()
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) {
        return;
    }

    auto params = frame->params();
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "text_color", "#202020")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select text color"));
    if (!selected.isValid()) {
        return;
    }

    params["text_color"] = selected.name(QColor::HexRgb).toStdString();
    frame->setParams(params);
    syncTextFrameStyleControls();
}

void MainWindow::chooseTextFrameFont()
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("dialog.textFrameFont"));
    dialog.setWindowTitle(QStringLiteral("Text Frame Font and Size"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* family = new QFontComboBox(&dialog);
    family->setObjectName(QStringLiteral("frame.fontFamily"));
    family->setCurrentFont(QFont(QString::fromStdString(json_utils::stringValue(frame->params(), "font_family", "Sans Serif"))));
    auto* size = new QSpinBox(&dialog);
    size->setObjectName(QStringLiteral("frame.fontSize"));
    size->setRange(diagram::TextFrameNode::kMinimumFontPoints, 72); size->setValue(json_utils::intValue(frame->params(), "font_size", diagram::TextFrameNode::kDefaultFontPoints));
    form->addRow(QStringLiteral("Font family"), family);
    form->addRow(QStringLiteral("Size (points)"), size);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    auto params = frame->params();
    params["font_family"] = family->currentFont().family().toStdString();
    params["font_size"] = size->value();
    frame->setParams(params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameLineColor()
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) {
        return;
    }

    auto params = frame->params();
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "line_color", "#444444")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select line color"));
    if (!selected.isValid()) {
        return;
    }

    params["line_color"] = selected.name(QColor::HexRgb).toStdString();
    frame->setParams(params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameFillColor()
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) {
        return;
    }

    auto params = frame->params();
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "fill_color", "#fff4cc")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select fill color"));
    if (!selected.isValid()) {
        return;
    }

    params["fill_color"] = selected.name(QColor::HexRgb).toStdString();
    frame->setParams(params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameFont(const QFont& font)
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) {
        return;
    }
    auto params = frame->params();
    params["font_family"] = font.family().toStdString();
    frame->setParams(params);
}

void MainWindow::setSelectedTextFrameFontSize(const int pointSize)
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    if (!frame) {
        return;
    }
    auto params = frame->params();
    params["font_size"] = std::max(diagram::TextFrameNode::kMinimumFontPoints, pointSize);
    frame->setParams(params);
}

void MainWindow::syncTextFrameStyleControls()
{
    const auto frame = selectedTextFrame(activeDiagramEditor() ? activeDiagramEditor()->scene() : nullptr);
    const bool enabled = static_cast<bool>(frame);

    if (textFrameFontAction_) textFrameFontAction_->setEnabled(enabled);
    if (textColorAction_) textColorAction_->setEnabled(enabled);
    if (lineColorAction_) lineColorAction_->setEnabled(enabled);
    if (fillColorAction_) fillColorAction_->setEnabled(enabled);
    if (textFrameFontCombo_) textFrameFontCombo_->setEnabled(enabled);
    if (textFrameFontSizeSpin_) textFrameFontSizeSpin_->setEnabled(enabled);

    if (!enabled) {
        return;
    }

    const auto& params = frame->params();
    if (textFrameFontCombo_) {
        const QSignalBlocker blocker(textFrameFontCombo_);
        textFrameFontCombo_->setCurrentFont(QFont(QString::fromStdString(json_utils::stringValue(params, "font_family", "Sans Serif"))));
    }
    if (textFrameFontSizeSpin_) {
        const QSignalBlocker blocker(textFrameFontSizeSpin_);
        textFrameFontSizeSpin_->setValue(std::max(diagram::TextFrameNode::kMinimumFontPoints, json_utils::intValue(params, "font_size", diagram::TextFrameNode::kDefaultFontPoints)));
    }
}

void MainWindow::applyPaperSize(const double widthMm, const double heightMm)
{
    if (!activeDiagramEditor()) {
        return;
    }

    auto pageLayout = activeDiagramEditor()->pageLayout();
    pageLayout.widthMm = widthMm;
    pageLayout.heightMm = heightMm;
    activeDiagramEditor()->setPageLayout(pageLayout);
    activeDiagramEditor()->fitPageInView();
    syncPageActions();
}

void MainWindow::setPaperA5()
{
    applyPaperSize(kPaperA5WidthMm, kPaperA5HeightMm);
}

void MainWindow::setPaperA4()
{
    applyPaperSize(kPaperA4WidthMm, kPaperA4HeightMm);
}

void MainWindow::setPaperA3()
{
    applyPaperSize(kPaperA3WidthMm, kPaperA3HeightMm);
}

void MainWindow::setPaperLetter()
{
    applyPaperSize(kPaperLetterWidthMm, kPaperLetterHeightMm);
}

void MainWindow::setPagePortrait()
{
    if (!activeDiagramEditor()) {
        return;
    }

    auto pageLayout = activeDiagramEditor()->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Portrait;
    activeDiagramEditor()->setPageLayout(pageLayout);
    activeDiagramEditor()->fitPageInView();
    syncPageActions();
}

void MainWindow::setPageLandscape()
{
    if (!activeDiagramEditor()) {
        return;
    }

    auto pageLayout = activeDiagramEditor()->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Landscape;
    activeDiagramEditor()->setPageLayout(pageLayout);
    activeDiagramEditor()->fitPageInView();
    syncPageActions();
}

void MainWindow::syncPageActions()
{
    if (!activeDiagramEditor()) {
        return;
    }

    const auto pageLayout = activeDiagramEditor()->pageLayout();
    const double maxSide = std::max(pageLayout.widthMm, pageLayout.heightMm);
    const double minSide = std::min(pageLayout.widthMm, pageLayout.heightMm);

    auto nearlyEqual = [](const double lhs, const double rhs) {
        return std::abs(lhs - rhs) < 0.5;
    };

    paperA5Action_->setChecked(nearlyEqual(maxSide, kPaperA5WidthMm) && nearlyEqual(minSide, kPaperA5HeightMm));
    paperA4Action_->setChecked(nearlyEqual(maxSide, kPaperA4WidthMm) && nearlyEqual(minSide, kPaperA4HeightMm));
    paperA3Action_->setChecked(nearlyEqual(maxSide, kPaperA3WidthMm) && nearlyEqual(minSide, kPaperA3HeightMm));
    paperLetterAction_->setChecked(nearlyEqual(maxSide, kPaperLetterWidthMm) && nearlyEqual(minSide, kPaperLetterHeightMm));

    portraitPageAction_->setChecked(pageLayout.orientation == diagram::PageOrientation::Portrait);
    landscapePageAction_->setChecked(pageLayout.orientation == diagram::PageOrientation::Landscape);
}

void MainWindow::closeViewers()
{
    if (observersPanel_) observersPanel_->clearRun();
    viewers_->closeAll();
}

void MainWindow::clearViewerSamples()
{
    viewers_->clearSamples();
}

bool MainWindow::documentModified() const
{
    return hasSavedDocument_ && currentDocument() != savedDocument_;
}

void MainWindow::markDocumentSaved()
{
    refreshObserversPanel();
    savedDocument_ = currentDocument();
    hasSavedDocument_ = true;
    setWindowModified(false);
}

bool MainWindow::confirmDocumentReplacement()
{
    // Nested edits normally propagate immediately; a failed propagation must
    // never be interpreted as a clean document.
    const bool committed = !schemaTabs_ || schemaTabs_->commitAll();
    if (committed && !documentModified()) return true;
    const auto choice = QMessageBox::warning(this, QStringLiteral("Unsaved changes"),
        QStringLiteral("Save changes to the current diagram before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice == QMessageBox::Discard) return true;
    if (choice != QMessageBox::Save || !committed) return false;
    saveDocument();
    return !documentModified();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (shuttingDown_) {
        QMainWindow::closeEvent(event);
        return;
    }

    if (!confirmDocumentReplacement()) {
        event->ignore();
        return;
    }
    shuttingDown_ = true;
    retireRunForShutdown();
    saveWindowLayout();
    if (diagramEditor_ && diagramEditor_->scene()) {
        disconnect(diagramEditor_->scene(), nullptr, this, nullptr);
    }
    if (runner_) {
        disconnect(runner_, nullptr, this, nullptr);
    }

    closeViewers();
    QMainWindow::closeEvent(event);
}

void MainWindow::openRunParametersDialog()
{
    configureRun(false);
}

auto MainWindow::configureRun(const bool launchAfterAccept) -> bool
{
    if (overlayScopeTracesAction_) {
        clearScopesBeforeRun_ = !overlayScopeTracesAction_->isChecked();
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("dialog.runConfiguration"));
    dialog.setWindowTitle(QStringLiteral("Run Configuration"));
    dialog.resize(420, 260);

    auto* layout = new QVBoxLayout(&dialog);
    if (runner_->isRunning()) {
        auto* notice = new QLabel(QStringLiteral("Changes apply to the next run. The active run keeps its starting configuration."), &dialog);
        notice->setObjectName(QStringLiteral("run.editNotice"));
        notice->setWordWrap(true);
        layout->addWidget(notice);
    }
    auto* form = new QFormLayout();

    auto* runtimeSecondsSpin = new QDoubleSpinBox(&dialog);
    runtimeSecondsSpin->setObjectName(QStringLiteral("run.duration"));
    runtimeSecondsSpin->setDecimals(9);
    runtimeSecondsSpin->setRange(-1, 604800);
    runtimeSecondsSpin->setSingleStep(0.1);
    runtimeSecondsSpin->setSpecialValueText(QStringLiteral("-1 (continuous)"));
    runtimeSecondsSpin->setValue(std::clamp(runtimeSeconds_, -1.0, 604800.0));

    auto* continuous = new QCheckBox(QStringLiteral("Run continuously until stopped"), &dialog);
    continuous->setObjectName(QStringLiteral("run.continuous"));
    continuous->setChecked(runtimeSeconds_ == -1.0);
    runtimeSecondsSpin->setEnabled(!continuous->isChecked());
    connect(continuous, &QCheckBox::toggled, &dialog, [runtimeSecondsSpin](bool enabled) {
        runtimeSecondsSpin->setEnabled(!enabled);
        if (enabled) runtimeSecondsSpin->setValue(-1.0);
        else if (runtimeSecondsSpin->value() == -1.0) runtimeSecondsSpin->setValue(60.0);
    });
    connect(runtimeSecondsSpin, &QDoubleSpinBox::valueChanged, &dialog, [continuous](double value) {
        continuous->setChecked(value == -1.0);
    });
    form->addRow(continuous);
    const auto captureProbes = currentRunProbes();
    const auto recordingChannels = std::ranges::count_if(captureProbes, [](const auto& probe) { return probe.record; });
    const auto liveChannels = std::ranges::count_if(captureProbes, [](const auto& probe) { return !probe.liveGroup.empty(); });
    auto* captureSummary = new QLabel(QStringLiteral("Live acquisition: %1 channels, bounded RAM. Full-run recording: %2 channels (Scope, phase viewers and named probes). Recording retains the entire run and grows on disk.")
        .arg(liveChannels).arg(recordingChannels), &dialog);
    captureSummary->setWordWrap(true);
    captureSummary->setObjectName(QStringLiteral("run.captureSummary"));
    form->addRow(captureSummary);

    auto* frequencySpin = new QDoubleSpinBox(&dialog);
    frequencySpin->setObjectName(QStringLiteral("run.frequency"));
    frequencySpin->setDecimals(6);
    frequencySpin->setRange(0.001, 1'000'000.0);
    frequencySpin->setSingleStep(10.0);
    frequencySpin->setValue(std::max(0.001, discretizationFrequencyHz_));
    frequencySpin->setSuffix(QStringLiteral(" Hz"));

    auto* dtLabel = new QLabel(&dialog);
    auto refreshDtLabel = [dtLabel](const double frequencyHz) {
        const double dtSeconds = 1.0 / std::max(frequencyHz, 1e-12);
        dtLabel->setText(QStringLiteral("%1 s").arg(dtSeconds, 0, 'g', 10));
    };
    refreshDtLabel(frequencySpin->value());
    connect(frequencySpin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            &dialog,
            [refreshDtLabel](const double valueHz) { refreshDtLabel(valueHz); });

    auto* clearSamplesCheck = new QCheckBox(QStringLiteral("Keep Previous Scope Traces"), &dialog);
    clearSamplesCheck->setObjectName(QStringLiteral("run.keepScopeTraces"));
    clearSamplesCheck->setChecked(!clearScopesBeforeRun_);
    clearSamplesCheck->setToolTip(QStringLiteral("On the next run, keep the preceding Scope recording as a labelled comparison. Phase views always restart."));

    form->addRow(QStringLiteral("Model duration (s)"), runtimeSecondsSpin);
    form->addRow(QStringLiteral("Model step rate"), frequencySpin);
    form->addRow(QStringLiteral("Model step dt"), dtLabel);
    runtimeSecondsSpin->setAccessibleName(QStringLiteral("Model duration in seconds"));
    runtimeSecondsSpin->setToolTip(QStringLiteral("Simulated duration, not wall-clock time. -1 runs continuously until stopped."));
    frequencySpin->setAccessibleName(QStringLiteral("Model steps per second"));
    frequencySpin->setToolTip(QStringLiteral("Sets the local simulation rate and effective dt of rate-dependent elements. This is not a real-time pacing limit."));
    layout->addLayout(form);
    layout->addWidget(clearSamplesCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* durationNotice = new QLabel(&dialog);
    durationNotice->setObjectName(QStringLiteral("run.durationNotice"));
    durationNotice->setWordWrap(true);
    layout->addWidget(durationNotice);
    const auto validateDuration = [runtimeSecondsSpin, frequencySpin, durationNotice, buttons] {
        try {
            const auto steps = ui::ModelRunController::computeTicks(runtimeSecondsSpin->value(), 1.0 / frequencySpin->value());
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
            durationNotice->setText(steps >= 0 && steps < 100
                ? QStringLiteral("Warning: this simulation runs for %1 steps, fewer than 100. Increase the duration or model step rate for more samples.").arg(steps)
                : QString{});
            durationNotice->setVisible(steps >= 0 && steps < 100);
        } catch (const std::exception& ex) {
            durationNotice->setText(QStringLiteral("Invalid duration: %1").arg(QString::fromUtf8(ex.what())));
            durationNotice->show();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
    };
    connect(runtimeSecondsSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, validateDuration);
    connect(frequencySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, validateDuration);
    validateDuration();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    runtimeSeconds_ = runtimeSecondsSpin->value();
    clearScopesBeforeRun_ = !clearSamplesCheck->isChecked();
    if (overlayScopeTracesAction_ && overlayScopeTracesAction_->isChecked() == clearScopesBeforeRun_) {
        const QSignalBlocker blocker(overlayScopeTracesAction_);
        overlayScopeTracesAction_->setChecked(!clearScopesBeforeRun_);
    }

    applyDiscretizationFrequency(frequencySpin->value());
    setWindowModified(documentModified());

    if (launchAfterAccept) {
        launchRunWithCurrentConfiguration();
    }

    return true;
}

void MainWindow::launchRunWithCurrentConfiguration()
{
    if (schemaTabs_ && !schemaTabs_->commitAll()) {
        appendLog(QStringLiteral("[studio] Apply or undo the pending schema-tab edits before continuing."));
        return;
    }

    refreshWireTypeLabels();
    if (const auto problem = diagramProblem(); !problem.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("The diagram cannot run: %1").arg(problem), 8000);
        toolWindows_->show(problemsDock_);
        return;
    }
    if (runner_->isRunning()) return;
    const double dtSeconds = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);
    try {
        static_cast<void>(ui::ModelRunController::computeTicks(runtimeSeconds_, dtSeconds));
    } catch (const std::exception& ex) {
        const auto message = QStringLiteral("Cannot start run: %1").arg(QString::fromUtf8(ex.what()));
        appendLog(message);
        runPanel_->append(message);
        toolWindows_->show(runDock_);
        return;
    }
    const auto previousRun = runner_->activeConfiguration();
    onScopeSamplesAvailable(); // consume final samples before retiring their generation
    resetLocalRun();
    runPanel_->clearOutput();
    runPanel_->append(QStringLiteral("Running at %1 Hz, %2.")
                          .arg(discretizationFrequencyHz_)
                          .arg(runtimeSeconds_ < 0 ? QStringLiteral("continuously") : QStringLiteral("%1 s").arg(runtimeSeconds_)));
    viewers_->beginRun(previousRun ? QStringLiteral("Run %1").arg(previousRun->id) : QStringLiteral("Previous run"),
                       !clearScopesBeforeRun_);

    if (diagramEditor_ && diagramEditor_->scene()) {
        static_cast<void>(syncSchemaFromScene());
        updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
    }

    const auto probes = currentRunProbes();
    preparedObservers_ = observerDescriptors();
    for (const auto& probe : probes) {
        const auto observerId = viewers::ViewerManager::observerIdOf(QString::fromStdString(probe.name));
        runObserverTypes_.insert(observerId, observerViewerType(observerId));
    }
    observersFrozen_ = true;
    auto* focus = QApplication::focusWidget();
    const QPointer<QWidget> previousFocus(focus);
    toolWindows_->show(observersDock_);
    if (previousFocus) previousFocus->setFocus(Qt::OtherFocusReason);
    observersPanel_->setRunState(RunState::Preparing, 0, dtSeconds);
    controller_->run(runtimeSchema_, dtSeconds, ui::RunExecutionConfig{runtimeSeconds_}, probes);
    viewers_->presentLive(runner_->liveSources());
    refreshActions();
}

void MainWindow::applyDiscretizationFrequency(const double frequencyHz)
{
    if (frequencyHz <= 0.0) {
        return;
    }

    discretizationFrequencyHz_ = frequencyHz;
    const double dtSeconds = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);

    if (diagramEditor_ && diagramEditor_->scene()) {
        static_cast<void>(syncSchemaFromScene());
    }
    updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
    applySchemaParamsToScene();
}

auto MainWindow::updateSchemaDiscretizationParams(boost::json::value& schema,
                                                  const double frequencyHz,
                                                  const double dtSeconds) -> bool
{
    (void)frequencyHz;
    return s7::configure_schema_discretization(schema,dtSeconds);
}

bool MainWindow::loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel)
{
    if (runner_) resetLocalRun();
    if (schemaTabs_) schemaTabs_->reset();

    closeViewers();
    clearViewerSamples();

    runtimeSchema_ = schemaJson;
    runtimeSeconds_ = 60;
    clearScopesBeforeRun_ = true;
    if (overlayScopeTracesAction_) overlayScopeTracesAction_->setChecked(false);
    deploymentRuntimeNodes_.clear();
    deploymentStreams_.clear();

    const auto elementCheck = prepareElementTypes(runtimeSchema_);
    if (diagramEditor_) {
        diagramEditor_->loadRuntimeSchema(runtimeSchema_);
        syncPageActions();
        syncTextFrameStyleControls();
        refreshPlacementLabels();
    }
    reportElementTypes(elementCheck, sourceLabel);

    const auto name = schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded schema: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded schema: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1[*]").arg(name));
    markDocumentSaved();
    return true;
}

bool MainWindow::loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel)
{
    const auto document = diagram::DiagramDocument::fromJson(diagramJson);
    if (runner_) resetLocalRun();
    if (schemaTabs_) schemaTabs_->reset();

    closeViewers();
    clearViewerSamples();

    QProgressDialog loading(QStringLiteral("Preparing diagram…"), QString(), 0, 100, this);
    loading.setWindowTitle(QStringLiteral("Loading diagram"));
    loading.setWindowModality(Qt::WindowModal);
    loading.setCancelButton(nullptr);
    loading.setMinimumDuration(0);
    loading.setAutoClose(false);
    loading.setAutoReset(false);
    loading.show();
    int lastProgress = -1;
    const auto updateProgress = [&](const int value) {
        const int bounded = std::clamp(value, 0, 100);
        if (bounded != lastProgress) {
            lastProgress = bounded;
            loading.setValue(bounded);
        }
    };
    updateProgress(0);

    runtimeSchema_ = document.runtimeSchema;
    deploymentRuntimeNodes_ = document.runtimeNodes;
    deploymentStreams_ = document.streams;

    runtimeSeconds_ = document.runConfiguration.contains("runtime_seconds")
        ? document.runConfiguration.at("runtime_seconds").to_number<double>() : 60;
    clearScopesBeforeRun_ = !document.runConfiguration.contains("clear_scope_traces")
        || document.runConfiguration.at("clear_scope_traces").as_bool();
    if (overlayScopeTracesAction_) overlayScopeTracesAction_->setChecked(!clearScopesBeforeRun_);

    if (document.dt > 0.0) {
        discretizationFrequencyHz_ = 1.0 / document.dt;
    }

    const auto elementCheck = prepareElementTypes(document.runtimeSchema);
    loading.setLabelText(QStringLiteral("Building diagram and restoring wire routes…"));
    updateProgress(8);
    if (diagramEditor_) {
        diagramEditor_->loadDocument(document, [&](const int value) { updateProgress(8 + value * 87 / 100); });
        syncPageActions();
        syncTextFrameStyleControls();
        refreshPlacementLabels();
    }
    updateProgress(97);
    reportElementTypes(elementCheck, sourceLabel);

    const auto name = !document.id.isEmpty() ? document.id : schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded diagram: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded diagram: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1[*]").arg(name));
    updateProgress(100);
    loading.close();
    markDocumentSaved();
    return true;
}

void MainWindow::refreshRecentDiagrams()
{
    recentDiagramsMenu_->clear();
    const auto paths = QSettings{}.value(QStringLiteral("files/recentDiagrams")).toStringList().mid(0, 10);
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const QFileInfo info(paths[i]);
        const auto label = QStringLiteral("%1 %2 — %3")
            .arg(i < 9 ? QStringLiteral("&%1").arg(i + 1) : QString::number(i + 1),
                 info.fileName().replace('&', QStringLiteral("&&")),
                 info.absolutePath().replace('&', QStringLiteral("&&")));
        auto* action = recentDiagramsMenu_->addAction(label);
        action->setData(paths[i]);
        action->setToolTip(paths[i]);
        action->setStatusTip(paths[i]);
        action->setEnabled(info.isFile());
        connect(action, &QAction::triggered, this, [this, path = paths[i]] {
            static_cast<void>(openRecentDiagram(path, true));
        });
    }
    if (paths.isEmpty()) {
        auto* empty = recentDiagramsMenu_->addAction(QStringLiteral("No recent diagrams"));
        empty->setEnabled(false);
    }
    recentDiagramsMenu_->addSeparator();
    auto* clear = recentDiagramsMenu_->addAction(QStringLiteral("Clear recent diagrams"));
    clear->setObjectName(QStringLiteral("action.clearRecentDiagrams"));
    clear->setEnabled(!paths.isEmpty());
    connect(clear, &QAction::triggered, this, [this] {
        QSettings{}.remove(QStringLiteral("files/recentDiagrams"));
        refreshRecentDiagrams();
    });
}

void MainWindow::rememberRecentDiagram(const QString& path)
{
    const QFileInfo info(path);
    const auto normalized = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    QSettings settings;
    auto paths = settings.value(QStringLiteral("files/recentDiagrams")).toStringList();
    paths.removeAll(normalized);
    paths.prepend(normalized);
    settings.setValue(QStringLiteral("files/recentDiagrams"), paths.mid(0, 10));
    refreshRecentDiagrams();
}

bool MainWindow::openRecentDiagram(const QString& path, bool interactive)
{
    try {
        auto parsed = json_utils::parseFile(path);
        if (!isDiagramDocument(parsed)) throw std::runtime_error("The recent file is no longer a diagram");
        static_cast<void>(diagram::DiagramDocument::fromJson(parsed));
        if (!confirmDocumentReplacement()) return false;
        parsed = json_utils::parseFile(path);
        if (!loadDiagramJson(parsed, QFileInfo(path).fileName())) return false;
        currentDiagramPath_ = QFileInfo(path).absoluteFilePath();
        rememberRecentDiagram(path);
        return true;
    } catch (const std::exception& ex) {
        appendLog(QStringLiteral("[studio] cannot reopen %1: %2").arg(path, QString::fromUtf8(ex.what())));
        if (interactive) QMessageBox::critical(this, QStringLiteral("Open failed"), QString::fromUtf8(ex.what()));
        return false;
    }
}

void MainWindow::restoreStartupDiagram()
{
    const auto paths = QSettings{}.value(QStringLiteral("files/recentDiagrams")).toStringList().mid(0, 10);
    for (const auto& path : paths) {
        if (openRecentDiagram(path, false)) return;
    }
    loadDemoDiagram();
}

void MainWindow::openDocumentFile()
{
    openJsonDocument(
        QStringLiteral("Open Diagram"),
        QStringLiteral("Diagram, schema, or library files (*.json *.s7diagram *.s7library);;All files (*)"),
        true,
        QStringLiteral("Open failed"));
}

void MainWindow::closeCurrentDiagram()
{
    if (!confirmDocumentReplacement()) return;
    if (schemaTabs_) schemaTabs_->reset();

    if (runner_) resetLocalRun();

    closeViewers();
    clearViewerSamples();

    runtimeSchema_ = boost::json::object{
        {"name", "untitled"},
        {"elements", boost::json::array{}},
        {"connections", boost::json::array{}}
    };
    currentDiagramPath_.clear();
    runtimeSeconds_ = 60;
    clearScopesBeforeRun_ = true;
    if (overlayScopeTracesAction_) overlayScopeTracesAction_->setChecked(false);
    deploymentRuntimeNodes_.clear();
    deploymentStreams_.clear();

    if (diagramEditor_) {
        diagramEditor_->clearDiagram();
        diagramEditor_->fitPageInView();
        syncPageActions();
        syncTextFrameStyleControls();
    }

    markDocumentSaved();
    setWindowTitle(QStringLiteral("s7studio - untitled[*]"));
    appendLog(QStringLiteral("[studio] closed diagram"));
    statusBar()->showMessage(QStringLiteral("Diagram closed"), 4000);
    onStatusChanged(QStringLiteral("idle"));
}

void MainWindow::saveDocument()
{
    if (currentDiagramPath_.isEmpty()) {
        saveDocumentAs();
        return;
    }

    saveDocumentToPath(currentDiagramPath_);
}

void MainWindow::saveDocumentAs()
{
    QString selectedFilter;
    const auto filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Diagram"),
        currentDiagramPath_.isEmpty() ? QStringLiteral("diagram.s7diagram") : currentDiagramPath_,
        QStringLiteral("Diagram files (*.s7diagram);;JSON files (*.json);;All files (*)"),
        &selectedFilter);
    if (filePath.isEmpty()) {
        return;
    }

    QString normalizedPath = filePath;
    if (QFileInfo(normalizedPath).suffix().isEmpty()) {
        normalizedPath += selectedFilter.startsWith(QStringLiteral("JSON"))
            ? QStringLiteral(".json")
            : QStringLiteral(".s7diagram");
    }

    saveDocumentToPath(normalizedPath);
}

void MainWindow::openJsonDocument(const QString& dialogTitle,
                                  const QString& filter,
                                  const bool preferDiagram,
                                  const QString& errorTitle)
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        dialogTitle,
        QString(),
        filter);
    if (filePath.isEmpty()) {
        return;
    }
    openPath(filePath, preferDiagram, errorTitle);
}

void MainWindow::openPath(const QString& filePath, const bool preferDiagram, const QString& errorTitle)
{
    try {
        auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isElementLibraryDocument(parsed)) {
            loadElementLibraryJson(parsed, sourceLabel);
            return;
        }
        const bool diagramDocument = isDiagramDocument(parsed);
        if (diagramDocument) static_cast<void>(diagram::DiagramDocument::fromJson(parsed));
        if (!confirmDocumentReplacement()) return;
        parsed = json_utils::parseFile(filePath);
        if (diagramDocument) {
            if (!loadDiagramJson(parsed, sourceLabel)) return;
            currentDiagramPath_ = QFileInfo(filePath).absoluteFilePath();
            rememberRecentDiagram(filePath);
        } else {
            if (!loadSchemaJson(parsed, sourceLabel)) return;
            currentDiagramPath_.clear();
            if (preferDiagram) {
                appendLog(QString("[studio] file '%1' is a runtime schema; showing synthesized diagram view").arg(sourceLabel));
            }
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, errorTitle, QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to open document: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadElementLibraryJson(const boost::json::value& libraryJson, const QString& sourceLabel)
{
    try {
        const auto library = s7::SchemaLibraryLoader::load_from_json(libraryJson);
        auto& registry = s7::ElementRegistry::instance();
        s7::SchemaLibraryLoader::register_library(library, registry);

        if (elementPickerPanel_) {
            elementPickerPanel_->refreshCatalog();
        }

        appendLog(QString("[studio] loaded element library: %1").arg(sourceLabel));
        statusBar()->showMessage(QString("Loaded element library: %1").arg(QString::fromStdString(library.name)), 5000);
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("Load library failed"), QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to load element library: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadElementLibrary()
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Load Element Library"),
        QString(),
        QStringLiteral("Element libraries (*.s7library *.json);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    try {
        const auto parsed = json_utils::parseFile(filePath);
        loadElementLibraryJson(parsed, QFileInfo(filePath).fileName());
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("Load library failed"), QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to load element library: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadDemoDiagram()
{
    if (!confirmDocumentReplacement()) return;
    try {
        const auto filePath = QString::fromUtf8(kBundledDemoDiagramPath);
        const auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isDiagramDocument(parsed)) {
            if (!loadDiagramJson(parsed, sourceLabel)) return;
        } else {
            if (!loadSchemaJson(parsed, sourceLabel)) return;
        }
    } catch (const std::exception& ex) {
        appendLog(QStringLiteral("[studio] failed to load bundled demo: %1").arg(QString::fromUtf8(ex.what())));
        loadSchemaJson(boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}},
                       QStringLiteral("empty schema"));
    }
    currentDiagramPath_.clear();
}

void MainWindow::chooseExampleDiagram()
{
    const QDir app(QCoreApplication::applicationDirPath());
    QString directory = QStringLiteral(S7STUDIO_EXAMPLES_DIR);
    for (const auto& base : {QStringLiteral("../share/s7"), QStringLiteral("share/s7")}) {
        if (!QFileInfo(app.filePath(base)).isDir()) continue;
        directory = QDir::cleanPath(app.filePath(base + "/examples"));
        break;
    }
    if (!QFileInfo(directory).isDir()) {
        QMessageBox::critical(this, QStringLiteral("Examples Not Found"),
            QStringLiteral("The bundled examples directory is missing: %1").arg(QDir::toNativeSeparators(directory)));
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Example Diagrams"), directory,
        QStringLiteral("Diagram files (*.s7diagram);;All files (*)"));
    if (!path.isEmpty()) static_cast<void>(openExampleDiagram(path));
}

bool MainWindow::openExampleDiagram(const QString& path)
{
    try {
        // Stage parsing and layout before asking to retire the current document
        // or run. Keep these exact bytes across the Save/Discard/Cancel prompt.
        const auto parsed = json_utils::parseFile(path);
        if (!isDiagramDocument(parsed)) throw std::invalid_argument("The example is not an S7 diagram document.");
        const auto document = diagram::DiagramDocument::fromJson(parsed);
        if (!document.runtimeSchema.is_object()) throw std::invalid_argument("The example has no schema object.");
        static_cast<void>(prepareElementTypes(document.runtimeSchema));
        diagram::DiagramScene staged;
        diagram::SceneSerializer{}.deserialize(document, staged);
        if (!confirmDocumentReplacement()) return false;
        if (!loadDiagramJson(parsed, QFileInfo(path).fileName())) return false;
        // A bundled example is a working copy, never an implicit Save target.
        currentDiagramPath_.clear();
        markDocumentSaved();
        return true;
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("Open Example Failed"), QString::fromUtf8(ex.what()));
        appendLog(QStringLiteral("[studio] failed to open example: %1").arg(QString::fromUtf8(ex.what())));
        return false;
    }
}

void MainWindow::saveSchemaLibrary()
{
    if (schemaTabs_ && !schemaTabs_->commitAll()) {
        appendLog(QStringLiteral("[studio] Apply or undo the pending schema-tab edits before continuing."));
        return;
    }

    if (!diagramEditor_ || !diagramEditor_->scene()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Diagram scene is not available."));
        return;
    }

    auto document = syncSchemaFromScene();
    if (schemaTabs_ && activeDiagramEditor()!=diagramEditor_) document.runtimeSchema=schemaTabs_->activeSchema();
    const auto schemaJson = document.runtimeSchema;
    if (!schemaJson.is_object()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Current diagram does not contain a valid runtime schema."));
        return;
    }

    QString selectedFilter;
    const auto filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Diagram as Library Element"),
        QStringLiteral("schema_library.s7library"),
        QStringLiteral("Element libraries (*.s7library *.json);;All files (*)"),
        &selectedFilter);
    if (filePath.isEmpty()) {
        return;
    }

    QString normalizedPath = filePath;
    if (QFileInfo(normalizedPath).suffix().isEmpty()) {
        normalizedPath += selectedFilter.startsWith(QStringLiteral("JSON"))
            ? QStringLiteral(".json")
            : QStringLiteral(".s7library");
    }

    const auto defaultTypeName = schemaDisplayName(schemaJson, QFileInfo(normalizedPath).baseName());

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Schema Library Element"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* typeEdit = new QLineEdit(defaultTypeName, &dialog);
    auto* displayEdit = new QLineEdit(defaultTypeName, &dialog);
    auto* descriptionEdit = new QLineEdit(QStringLiteral("Schema-based library element"), &dialog);
    auto* categoryEdit = new QLineEdit(QFileInfo(normalizedPath).baseName(), &dialog);
    auto* versionEdit = new QLineEdit(QStringLiteral("1.0.0"), &dialog);

    form->addRow(QStringLiteral("Type name"), typeEdit);
    form->addRow(QStringLiteral("Display name"), displayEdit);
    form->addRow(QStringLiteral("Description"), descriptionEdit);
    form->addRow(QStringLiteral("Category"), categoryEdit);
    form->addRow(QStringLiteral("Version"), versionEdit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto typeName = typeEdit->text().trimmed();
    if (typeName.isEmpty()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Type name cannot be empty."));
        return;
    }

    s7::SchemaLibraryDefinition library;
    const auto outputFile = normalizedPath;

    if (QFile::exists(outputFile)) {
        try {
            library = s7::SchemaLibraryLoader::load_from_file(outputFile.toStdString());
        } catch (const std::exception& ex) {
            QMessageBox::critical(this, QStringLiteral("Save library failed"), QString::fromUtf8(ex.what()));
            return;
        }
    } else {
        library.name = QFileInfo(outputFile).baseName().toStdString();
    }

    s7::SchemaLibraryElementDefinition element;
    element.type_name = typeName.toStdString();
    element.display_name = displayEdit->text().trimmed().isEmpty() ? element.type_name : displayEdit->text().trimmed().toStdString();
    element.description = descriptionEdit->text().trimmed().toStdString();
    element.category = categoryEdit->text().trimmed().isEmpty() ? library.name : categoryEdit->text().trimmed().toStdString();
    element.version = versionEdit->text().trimmed().isEmpty() ? std::string{"1.0.0"} : versionEdit->text().trimmed().toStdString();
    element.schema = schemaJson;

    for (const auto& existing : library.elements) {
        if (existing.type_name == element.type_name) {
            QMessageBox::critical(this,
                                  QStringLiteral("Save library failed"),
                                  QStringLiteral("The library already contains an element named '%1'.").arg(typeName));
            return;
        }
    }

    if (s7::ElementRegistry::instance().find(element.type_name)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("An element type with that name is already registered. Choose a different type name."));
        return;
    }

    library.elements.push_back(element);

    const auto payload = library.to_json();
    const auto json = boost::json::serialize(payload);
    QString error;
    if (!writeFileAtomically(outputFile, QByteArray::fromStdString(json) + '\n', error)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Failed to write '%1': %2").arg(outputFile, error));
        return;
    }

    try {
        s7::SchemaLibraryDefinition registrationOnly;
        registrationOnly.name = library.name;
        registrationOnly.elements.push_back(element);
        s7::SchemaLibraryLoader::register_library(registrationOnly, s7::ElementRegistry::instance());
    } catch (const std::exception& ex) {
        appendLog(QString("[studio] library saved but registration skipped: %1").arg(QString::fromUtf8(ex.what())));
    }

    if (elementPickerPanel_) {
        elementPickerPanel_->refreshCatalog();
    }

    appendLog(QString("[studio] saved schema library element: %1").arg(typeName));
    statusBar()->showMessage(QString("Saved schema library: %1").arg(QFileInfo(outputFile).fileName()), 5000);
}

auto MainWindow::saveDocumentToPath(const QString& filePath) -> bool
{
    if (schemaTabs_ && !schemaTabs_->commitAll()) {
        QMessageBox::warning(this,QStringLiteral("Save failed"),QStringLiteral("A schema tab could not apply its edits. See the log."));
        return false;
    }
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QStringLiteral("Diagram scene is not available."));
        return false;
    }

    auto document = syncSchemaFromScene();
    document.runtimeNodes = deploymentRuntimeNodes_;
    document.streams = deploymentStreams_;

    const auto payload = document.toJson();
    const auto json = boost::json::serialize(payload);
    QString error;
    if (!writeFileAtomically(filePath, QByteArray::fromStdString(json) + '\n', error)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save failed"),
                              QStringLiteral("Failed to write '%1': %2").arg(filePath, error));
        return false;
    }

    runtimeSchema_ = document.runtimeSchema;
    markDocumentSaved();
    currentDiagramPath_ = QFileInfo(filePath).absoluteFilePath();
    rememberRecentDiagram(filePath);
    appendLog(QString("[studio] saved diagram: %1").arg(QFileInfo(filePath).fileName()));
    statusBar()->showMessage(QString("Saved diagram: %1").arg(QFileInfo(filePath).fileName()), 5000);
    return true;
}

void MainWindow::fitDiagramToView()
{
    if (activeDiagramEditor()) {
        activeDiagramEditor()->fitPageInView();
    }
}

auto MainWindow::serializeScene() const -> diagram::DiagramDocument
{
    diagram::SerializationOptions options;
    options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("untitled"));
    options.dt = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);
    const auto pageLayout = diagramEditor_->pageLayout();
    options.sheetOrientation = pageLayout.orientation;
    options.sheetWidthMm = pageLayout.widthMm;
    options.sheetHeightMm = pageLayout.heightMm;
    auto document = diagram::SceneSerializer{}.serialize(*diagramEditor_->scene(), options);
    document.runConfiguration = {{"runtime_seconds", runtimeSeconds_}, {"clear_scope_traces", clearScopesBeforeRun_}};
    // The plugins the elements come from, so a runtime (or Studio) can find them.
    auto& schema = document.runtimeSchema.as_object();
    if (auto requirements = pluginLoader_ ? pluginRequirements(schema, *pluginLoader_) : boost::json::array{}; requirements.empty()) {
        schema.erase("requires");
    } else {
        schema["requires"] = std::move(requirements);
    }
    return document;
}

auto MainWindow::syncSchemaFromScene() -> diagram::DiagramDocument
{
    if (schemaTabs_) schemaTabs_->commitAll();
    auto document = serializeScene();
    auto schema = runtimeSchema_.is_object() ? runtimeSchema_.as_object() : boost::json::object{{"name", "untitled"}};
    const auto& fromScene = document.runtimeSchema.as_object();
    schema["elements"] = fromScene.contains("elements") ? fromScene.at("elements") : boost::json::array{};
    schema["connections"] = fromScene.contains("connections") ? fromScene.at("connections") : boost::json::array{};
    if (const auto* requirements = fromScene.if_contains("requires")) {
        schema["requires"] = *requirements;
    } else {
        schema.erase("requires");
    }
    runtimeSchema_ = schema;
    document.runtimeSchema = std::move(schema);
    return document;
}

void MainWindow::applySchemaParamsToScene()
{
    if (!diagramEditor_ || !diagramEditor_->scene() || !runtimeSchema_.is_object()) {
        return;
    }
    const auto* elements = runtimeSchema_.as_object().if_contains("elements");
    if (!elements || !elements->is_array()) {
        return;
    }
    for (const auto& value : elements->as_array()) {
        if (!value.is_object()) {
            continue;
        }
        const auto& object = value.as_object();
        const auto node = diagramEditor_->scene()->elementNode(json_utils::stringValue(object, "id"));
        const auto* params = object.if_contains("params");
        if (!node) continue;
        auto element = node->element();
        bool changed=false;
        if (params && params->is_object() && element.params!=params->as_object()) {
            element.params=params->as_object();
            changed=true;
        }
        if (const auto* nested=object.if_contains("schema"); nested && nested->is_object() && element.schema!=*nested) {
            element.schema=*nested;
            changed=true;
        }
        if (changed) node->setElement(element);
    }
    if (schemaTabs_) schemaTabs_->refresh();
}

auto MainWindow::prepareElementTypes(const boost::json::value& schema) -> ElementCheck
{
    if (!pluginLoader_ || !schema.is_object()) {
        return {};
    }
    std::vector<std::filesystem::path> buildTreePlugins;
    for (const auto& candidate : developmentPluginCandidates()) {
        buildTreePlugins.emplace_back(candidate);
    }
    const auto before = pluginLoader_->loaded_count();
    auto check = ensureElementTypes(schema.as_object(), *pluginLoader_, s7::ElementRegistry::instance(), buildTreePlugins);
    if (pluginLoader_->loaded_count() != before) {
        appendLog(QStringLiteral("[studio] plugins loaded: %1").arg(pluginLoader_->loaded_count()));
        if (elementPickerPanel_) {
            elementPickerPanel_->refreshCatalog();
        }
    }
    return check;
}

void MainWindow::reportElementTypes(const ElementCheck& check, const QString& sourceLabel)
{
    if (diagramEditor_ && diagramEditor_->scene()) {
        for (const auto& [id, problem] : check.elementProblems) {
            if (const auto node = diagramEditor_->scene()->elementNode(id)) {
                node->setProblem(problem);
            }
        }
    }
    loadWarnings_ = check.warnings;
    for (const auto& warning : check.warnings) {
        appendLog(QStringLiteral("[studio] %1").arg(warning));
    }
    for (const auto& error : check.errors) {
        appendLog(QStringLiteral("[studio] error: %1").arg(error));
    }
    refreshProblems();
    if (!check.errors.empty()) {
        // Missing plugins or element types: the affected elements are listed under Problems.
        statusBar()->showMessage(QStringLiteral("%1 uses element types that are not available; see Problems.").arg(sourceLabel), 8000);
        toolWindows_->show(problemsDock_);
    }
}

auto MainWindow::diagramProblem() const -> QString
{
    if (!problemsPanel_) {
        return {};
    }
    for (const auto& problem : problemsPanel_->problems()) {
        if (problem.severity == ui::Problem::Severity::Error) {
            return problem.element.isEmpty() ? problem.message : QStringLiteral("%1: %2").arg(problem.element, problem.message);
        }
    }
    return {};
}

auto MainWindow::currentDocument() const -> boost::json::value
{
    auto document = serializeScene();
    document.runtimeNodes = deploymentRuntimeNodes_;
    document.streams = deploymentStreams_;
    return document.toJson();
}

void MainWindow::refreshPlacementLabels()
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        return;
    }
    QMap<QString, QString> names;
    for (const auto& value : deploymentRuntimeNodes_) {
        if (value.is_object()) {
            const auto& node = value.as_object();
            names[QString::fromStdString(json_utils::stringValue(node, "node_uuid", ""))] =
                QString::fromStdString(json_utils::stringValue(node, "name", ""));
        }
    }
    if (headlessNodesPanel_) {
        for (const auto& known : headlessNodesPanel_->knownNodes()) {
            names[known.nodeUuid] = known.nodeName;
        }
    }
    diagram::updatePlacementLabels(*diagramEditor_->scene(), names);
}

auto MainWindow::startNetworkTask(std::function<std::function<void()>()> work) -> bool
{
    if (networkTaskRunning_) {
        QMessageBox::information(this, QStringLiteral("Busy"), QStringLiteral("A deployment request is still running."));
        return false;
    }
    if (networkTask_.joinable()) {
        networkTask_.join();
    }
    networkTaskRunning_ = true;
    deployAction_->setEnabled(false);
    openDeployedAction_->setEnabled(false);
    networkTask_ = std::thread([this, work = std::move(work)] {
        auto done = work();
        QMetaObject::invokeMethod(this, [this, done = std::move(done)] {
            networkTaskRunning_ = false;
            deployAction_->setEnabled(true);
            openDeployedAction_->setEnabled(true);
            if (!shuttingDown_) {
                done();
            }
        }, Qt::QueuedConnection);
    });
    return true;
}

void MainWindow::deployToNodes()
{
    if (schemaTabs_ && !schemaTabs_->commitAll()) {
        appendLog(QStringLiteral("[studio] Apply or undo the pending schema-tab edits before continuing."));
        return;
    }

    if (!diagramEditor_ || !diagramEditor_->scene() || !headlessNodesPanel_) {
        return;
    }
    refreshWireTypeLabels();
    if (const auto problem = diagramProblem(); !problem.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("The diagram cannot be deployed: %1").arg(problem), 8000);
        toolWindows_->show(problemsDock_);
        return;
    }
    QMap<QString, diagram::NodeAddress> known;
    for (const auto& node : headlessNodesPanel_->knownNodes()) {
        known[node.nodeUuid] = diagram::NodeAddress{node.nodeName, node.endpoint, node.dataEndpoint, node.dataSecurity};
    }
    boost::json::object deployment;
    s7::deployment::OutputTypeLookup types;
    try {
        deployment = diagram::deploymentDiagram(diagram::DiagramDocument::fromJson(currentDocument()), known);
        if (s7::deployment::placed_nodes(deployment).empty()) {
            QMessageBox::information(this, QStringLiteral("Deploy"),
                                     QStringLiteral("No element is placed on a node. Choose a node in each element's properties "
                                                    "(nodes come from the Headless Nodes tool window)."));
            return;
        }
        types = diagram::studioOutputTypes(deployment);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QStringLiteral("Deploy"), QStringLiteral("The diagram cannot be deployed: %1").arg(ex.what()));
        return;
    }
    const auto tls = headlessNodesPanel_->clientTls();
    if (!tls.enabled) {
        QMessageBox::warning(this, QStringLiteral("Deploy"),
                             QStringLiteral("Deployments need the Studio certificate: set CA, certificate and key in the Headless Nodes tool window."));
        return;
    }
    appendLog(QStringLiteral("[deploy] deploying to %1 node(s)...").arg(s7::deployment::placed_nodes(deployment).size()));
    deployPanel_->setRunning(QStringLiteral("Deploying to %1 node(s)...").arg(s7::deployment::placed_nodes(deployment).size()));
    toolWindows_->show(deployDock_);
    startNetworkTask([this, deployment, types, tls]() -> std::function<void()> {
        s7::deployment::DeployResult result;
        try {
            s7::deployment::BeastControlRequester requester(tls);
            result = s7::deployment::deploy_diagram(deployment, types, [&](const auto& node, const auto& message) {
                return requester(node, message);
            });
        } catch (const std::exception& ex) {
            result.errors.push_back(ex.what());
        }
        return [this, result = std::move(result)] {
            QStringList lines;
            for (const auto& node : result.nodes) {
                lines << QStringLiteral("%1 %2: %3%4")
                             .arg(QString::fromStdString(node.node_uuid).left(8), QString::fromStdString(node.endpoint),
                                  QString::fromStdString(node.status),
                                  node.message.empty() ? QString() : QStringLiteral(" — ") + QString::fromStdString(node.message));
            }
            for (const auto& error : result.errors) {
                appendLog(QStringLiteral("[deploy] error: %1").arg(QString::fromStdString(error)));
            }
            for (const auto& line : lines) {
                appendLog(QStringLiteral("[deploy] ") + line);
            }
            if (!result.diagram.empty()) {
                // Keep the streams so the next deployment continues them.
                deploymentStreams_ = result.diagram.at("streams").as_array();
                deploymentRuntimeNodes_ = result.diagram.at("runtime_nodes").as_array();
                refreshPlacementLabels();
            }
            std::vector<ui::DeployPanel::NodeRow> rows;
            QMap<QString, QString> names;
            if (headlessNodesPanel_) {
                for (const auto& known : headlessNodesPanel_->knownNodes()) {
                    names[known.nodeUuid] = known.nodeName;
                }
            }
            for (const auto& node : result.nodes) {
                const auto uuid = QString::fromStdString(node.node_uuid);
                rows.push_back({names.value(uuid, uuid.left(8)), QString::fromStdString(node.endpoint), QString::fromStdString(node.status),
                                QString::fromStdString(node.message)});
            }
            std::vector<QString> errors;
            for (const auto& error : result.errors) {
                errors.push_back(QString::fromStdString(error));
            }
            if (result.committed) {
                appendLog(QStringLiteral("[deploy] committed %1 revision %2")
                              .arg(QString::fromStdString(result.deployment_id))
                              .arg(result.revision));
                statusBar()->showMessage(QStringLiteral("Deployed revision %1 to %2 node(s)").arg(result.revision).arg(result.nodes.size()),
                                         8000);
                deployPanel_->setResult(QStringLiteral("Deployed revision %1 of %2 to %3 node(s).")
                                            .arg(result.revision)
                                            .arg(QString::fromStdString(result.deployment_id))
                                            .arg(result.nodes.size()),
                                        true, rows, errors);
            } else {
                deployPanel_->setResult(QStringLiteral("Deployment failed: nothing was committed."), false, rows, errors);
                toolWindows_->show(deployDock_);
            }
        };
    });
}

void MainWindow::openDeployedDiagram()
{
    if (!headlessNodesPanel_) {
        return;
    }
    const auto nodes = headlessNodesPanel_->knownNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Open Deployed Diagram"),
                                 QStringLiteral("No node is known yet. Add a seed in the Headless Nodes tool window and refresh."));
        return;
    }
    QStringList labels;
    for (const auto& node : nodes) {
        labels << QStringLiteral("%1  %2  (%3)").arg(node.nodeName, node.endpoint, node.nodeUuid.left(8));
    }
    bool accepted = false;
    const auto chosen = QInputDialog::getItem(this, QStringLiteral("Open Deployed Diagram"), QStringLiteral("Node"), labels, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const auto node = nodes.at(static_cast<int>(labels.indexOf(chosen)));
    const auto tls = headlessNodesPanel_->clientTls();
    startNetworkTask([this, node, tls]() -> std::function<void()> {
        boost::json::object response;
        QString error;
        try {
            s7::deployment::BeastControlRequester requester(tls);
            response = requester(s7::deployment::RuntimeNode{node.nodeUuid.toStdString(), {}, node.endpoint.toStdString()},
                                 boost::json::object{{"type", "GET_NETWORK_CONFIGURATION"}, {"request_id", "studio-config"}, {"protocol_version", 1}});
        } catch (const std::exception& ex) {
            error = QString::fromUtf8(ex.what());
        }
        return [this, node, response = std::move(response), error] {
            if (error.isEmpty() && json_utils::stringValue(response, "type", "") != "NETWORK_CONFIGURATION") {
                QMessageBox::information(this, QStringLiteral("Open Deployed Diagram"),
                                         QStringLiteral("%1 has no deployed diagram: %2")
                                             .arg(node.nodeName, QString::fromStdString(json_utils::stringValue(response, "message", ""))));
                return;
            }
            if (!error.isEmpty() || !response.contains("diagram")) {
                QMessageBox::warning(this, QStringLiteral("Open Deployed Diagram"), QStringLiteral("Request failed: %1").arg(error));
                return;
            }
            if (!confirmDocumentReplacement()) return;
            const auto revision = response.at("revision").to_number<std::uint64_t>();
            try {
                if (!loadDiagramJson(response.at("diagram"), QStringLiteral("deployed on %1, revision %2").arg(node.nodeName).arg(revision))) return;
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, QStringLiteral("Open Deployed Diagram"), QString::fromUtf8(ex.what()));
            }
            currentDiagramPath_.clear();
        };
    });
}

} // namespace s7studio

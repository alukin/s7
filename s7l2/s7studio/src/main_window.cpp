#include "main_window.hpp"

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
#include "tool_windows.hpp"
#include "ui_icons.hpp"
#include "scene_serializer.hpp"
#include "scope_view_window.hpp"
#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_catalog.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/schema_library.hpp>

#include "s7/plugin_loader.hpp"
#include "wire_type_labels.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/scene.hpp>

#include <QDateTime>
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
constexpr const char* kBundledDemoDiagramPath = S7STUDIO_SOURCE_DIR "/resources/demo_startup.s7diagram";


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

auto elementDescriptionForType(const QString& type) -> QString
{
    if (const auto* descriptor = s7::ElementRegistry::instance().find(type.toStdString())) {
        const auto& metadata = descriptor->metadata();
        if (!metadata.description.empty()) {
            return QString::fromStdString(metadata.description);
        }
        if (metadata.schema_definition.has_value()) {
            return QStringLiteral("Schema-based library element.");
        }
    }

    if (type == "Constant") return QStringLiteral("Constant source: outputs a fixed value.");
    if (type == "Gain") return QStringLiteral("Gain: multiplies input by a factor.");
    if (type == "Adder") return QStringLiteral("Adder: weighted sum of N inputs.");
    if (type == "Multiply") return QStringLiteral("Multiply: product of N inputs.");
    if (type == "Delay") return QStringLiteral("Delay: outputs previous input sample (stateful).");
    if (type == "Accumulator") return QStringLiteral("Accumulator: running sum y[n] = y[n-1] + x[n] (no time step).");
    if (type == "EulerIntegrator") return QStringLiteral("Euler integrator: rectangle rule y[n+1] = y[n] + dt*x[n]; can close feedback loops.");
    if (type == "TrapezoidIntegrator") return QStringLiteral("Trapezoid integrator: y[n] = y[n-1] + dt/2*(x[n] + x[n-1]).");
    if (type == "AnalogOscillator") return QStringLiteral("Analog oscillator: sinusoidal source.");
    if (type == "FirFilter") return QStringLiteral("FIR filter: finite impulse response filter.");
    if (type == "IirFilter") return QStringLiteral("IIR filter: recursive filter.");
    if (type == "PreviousValue") return QStringLiteral("PreviousValue: unit-delay output element.");
    if (type == "ReImToComplex") return QStringLiteral("ReImToComplex: combines real/imaginary inputs.");
    if (type == "ComplexToReIm") return QStringLiteral("ComplexToReIm: splits complex input into real and imaginary outputs.");
    if (type == "Convert") return QStringLiteral("Convert: explicit type conversion (mode value or normalized, rounding, overflow).");
    if (type == "ComplexToMagnitudePhase") return QStringLiteral("ComplexToMagnitudePhase: outputs magnitude and phase.");
    return QStringLiteral("No built-in description available for this element type.");
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

auto nextElementId(const diagram::DiagramScene& scene, const QString& elementType) -> QString
{
    const QString base = elementType.toLower() + QStringLiteral("_");
    int suffix = 1;
    QString candidate;
    do {
        candidate = base + QString::number(suffix++);
    } while (scene.elementNode(candidate.toStdString()));
    return candidate;
}

auto parseEditableParams(const QString& text, boost::json::object& paramsOut, QString& error) -> bool
{
    boost::system::error_code ec;
    const auto parsed = boost::json::parse(text.toStdString(), ec);
    if (ec) {
        error = QStringLiteral("Invalid JSON: %1").arg(QString::fromUtf8(ec.message()));
        return false;
    }
    if (!parsed.is_object()) {
        error = QStringLiteral("Parameters must be a JSON object, e.g. {\"value\": 1.0}.");
        return false;
    }

    paramsOut = parsed.as_object();
    return true;
}

auto withDefaultedParams(const s7::ElementMetadata& metadata, const boost::json::object& params) -> boost::json::object
{
    auto effective = params;
    for (const auto& parameter : metadata.parameters) {
        // Optional parameters without a default (e.g. a generic element's
        // `type`) stay absent: null is not a valid value for them.
        if (effective.find(parameter.name) == effective.end() && !parameter.default_value.is_null()) {
            effective[parameter.name] = parameter.default_value;
        }
    }
    return effective;
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

auto parameterListSummary(const std::vector<s7::ParameterSpec>& parameters) -> QString
{
    if (parameters.empty()) {
        return QStringLiteral("none");
    }

    QStringList lines;
    lines.reserve(static_cast<int>(parameters.size()));
    for (const auto& parameter : parameters) {
        lines.push_back(QStringLiteral("- %1 [%2], default=%3: %4")
                            .arg(QString::fromStdString(parameter.name),
                                 QString::fromStdString(parameter.type),
                                 QString::fromStdString(boost::json::serialize(parameter.default_value)),
                                 QString::fromStdString(parameter.description.empty() ? std::string("No description") : parameter.description)));
    }
    return lines.join('\n');
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , runtimeSchema_(boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}})
{
    runner_ = new LocalSchemaRunner(this);
    controller_ = std::make_unique<ui::ModelRunController>(runner_);

    buildActions();
    buildUi();

    connectRunnerSignals();
    if (diagramEditor_) {
        connect(diagramEditor_, &diagram::DiagramEditor::elementNodeDoubleClicked,
                this,
                &MainWindow::onElementNodeDoubleClicked);
    }

    appendLog("[studio] Local embedded runtime ready");
    restoreWindowLayout();
    loadDemoDiagram();
    refreshActions();
    onStatusChanged("idle");
}

MainWindow::~MainWindow()
{
    shuttingDown_ = true;
    if (networkTask_.joinable()) {
        networkTask_.join(); // bounded by the control request timeouts
    }
    if (diagramEditor_ && diagramEditor_->scene()) {
        disconnect(diagramEditor_->scene(), nullptr, this, nullptr);
    }
    if (runner_) {
        disconnect(runner_, nullptr, this, nullptr);
    }
    for (const auto& window : scopeWindows_) {
        if (window) {
            disconnect(window, nullptr, this, nullptr);
        }
    }
    closeScopeWindows();
}

void MainWindow::connectRunnerSignals()
{
    connect(runner_, &LocalSchemaRunner::logMessage, this, [this](const QString& message) {
        appendLog(message);
        if (runPanel_) {
            runPanel_->append(message);
        }
    });
    connect(runner_, &LocalSchemaRunner::statusChanged, this, &MainWindow::onStatusChanged);
    connect(runner_, &LocalSchemaRunner::runCompleted, this, &MainWindow::onRunCompleted);
    connect(runner_, &LocalSchemaRunner::ticksProgress, this, &MainWindow::onTickProgress);
    connect(runner_, &LocalSchemaRunner::scopeSamplesAvailable, this, &MainWindow::onScopeSamplesAvailable);
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
    loadDemoAction_ = actions.loadDemoAction;
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
    zoomOutAction_ = new QAction("Zoom Out", this);
    resetZoomAction_ = new QAction("Actual Size", this);
    fitPageAction_ = new QAction("Fit Page", this);
    overlayScopeTracesAction_ = new QAction("Overlay Scope Traces", this);
    overlayScopeTracesAction_->setCheckable(true);
    overlayScopeTracesAction_->setChecked(false);
    overlayScopeTracesAction_->setToolTip("Keep previous scope traces when starting a new run");

    modeNormalAction_ = new QAction("Normal Mode", this);
    modeNormalAction_->setCheckable(true);
    modeNormalAction_->setChecked(true);
    modeNormalAction_->setToolTip("Switch to Normal mode (move/select elements)");
    const QColor drawIconColor = palette().color(QPalette::ButtonText);
    modeNormalAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/pointer.svg"), drawIconColor));

    modeWireAction_ = new QAction("Wire Mode", this);
    modeWireAction_->setCheckable(true);
    modeWireAction_->setText(QStringLiteral("Wire Draw"));
    modeWireAction_->setToolTip("Draw wires click by click: a port or wire, bends, then a port or wire (Esc cancels). "
                                "In normal mode, drag from a port to connect, or Alt+drag from a wire.");
    modeWireAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    modeWireAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/vector-bezier-2.svg"), drawIconColor));

    addTextFrameAction_ = new QAction("Text Frame", this);
    addTextFrameAction_->setToolTip("Insert a diagram-only text frame");
    addTextFrameAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/text-caption.svg"), drawIconColor));

    textColorAction_ = new QAction("Text Color", this);
    textColorAction_->setToolTip("Set selected text frame text color");
    textColorAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/text-color.svg"), drawIconColor));
    lineColorAction_ = new QAction("Line Color", this);
    lineColorAction_->setToolTip("Set selected text frame border color");
    lineColorAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/border-style-2.svg"), drawIconColor));
    fillColorAction_ = new QAction("Fill Color", this);
    fillColorAction_->setToolTip("Set selected text frame background color");
    fillColorAction_->setIcon(ui::themedIcon(QStringLiteral(":/icons/tabler/bucket.svg"), drawIconColor));

    paperA5Action_ = new QAction("A5", this);
    paperA4Action_ = new QAction("A4", this);
    paperA3Action_ = new QAction("A3", this);
    paperLetterAction_ = new QAction("Letter", this);
    portraitPageAction_ = new QAction("Portrait", this);
    landscapePageAction_ = new QAction("Landscape", this);

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
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
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
    connect(loadDemoAction_, &QAction::triggered, this, &MainWindow::loadDemoDiagram);
    connect(fitDiagramAction_, &QAction::triggered, this, [this] {
        if (diagramEditor_) {
            diagramEditor_->fitDiagramInView();
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
        controller_->reset();
        clearScopeSamples();
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
    actions.loadDemoAction = loadDemoAction_;
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

    ui::MainWindowBuilder::buildMenuAndToolbar(this, actions);

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
    textFrameFontSizeSpin_->setRange(6, 72);
    textFrameFontSizeSpin_->setPrefix(QStringLiteral("Pt "));
    diagramDrawToolBar_->addWidget(textFrameFontSizeSpin_);

    connect(textFrameFontCombo_, &QFontComboBox::currentFontChanged,
            this, &MainWindow::setSelectedTextFrameFont);
    connect(textFrameFontSizeSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::setSelectedTextFrameFontSize);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
    viewMenu->addAction(resetZoomAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fitDiagramAction_);
    viewMenu->addAction(fitPageAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(overlayScopeTracesAction_);

    auto* pageMenu = menuBar()->addMenu("&Page");
    pageMenu->addAction(paperA5Action_);
    pageMenu->addAction(paperA4Action_);
    pageMenu->addAction(paperA3Action_);
    pageMenu->addAction(paperLetterAction_);
    pageMenu->addSeparator();
    pageMenu->addAction(portraitPageAction_);
    pageMenu->addAction(landscapePageAction_);

    auto* settingsMenu = menuBar()->addMenu("&Settings");
    auto* scopeTempDirectoryAction = settingsMenu->addAction(QStringLiteral("Scope temporary data directory…"));
    connect(scopeTempDirectoryAction, &QAction::triggered,
            this, &MainWindow::chooseScopeTemporaryDirectory);

    const auto widgets = ui::MainWindowBuilder::buildCentralWidgets(this);
    diagramEditor_ = widgets.diagramEditor;
    if (diagramEditor_ && diagramEditor_->scene()) {
        auto* stack = diagramEditor_->scene()->undoStack();
        undoAction_ = stack->createUndoAction(this, QStringLiteral("&Undo"));
        undoAction_->setShortcut(QKeySequence::Undo);
        undoAction_->setIcon(ui::icon(QStringLiteral("arrow-back-up")));
        redoAction_ = stack->createRedoAction(this, QStringLiteral("&Redo"));
        redoAction_->setShortcut(QKeySequence::Redo);
        redoAction_->setIcon(ui::icon(QStringLiteral("arrow-forward-up")));
        diagramDrawToolBar_->insertAction(diagramDrawToolBar_->actions().value(0), redoAction_);
        diagramDrawToolBar_->insertAction(redoAction_, undoAction_);
        diagramDrawToolBar_->insertSeparator(diagramDrawToolBar_->actions().value(2));
        auto* editMenu = new QMenu(QStringLiteral("&Edit"), this);
        editMenu->addAction(undoAction_);
        editMenu->addAction(redoAction_);
        const auto menus = menuBar()->actions();
        menuBar()->insertMenu(menus.size() > 1 ? menus.at(1) : nullptr, editMenu);
    }
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
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

void MainWindow::onElementRequested(const QString& elementType, const QString& /*displayName*/)
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (elementType.isEmpty() || !scene) {
        return;
    }
    const bool scopeObserver = elementType == QStringLiteral("StudioObserver:Scope");
    const bool phasePortraitObserver = elementType == QStringLiteral("StudioObserver:PhasePortrait");
    const bool phaseTrajectory3DObserver = elementType == QStringLiteral("StudioObserver:PhaseTrajectory3D");
    const bool observer = scopeObserver || phasePortraitObserver || phaseTrajectory3DObserver;
    const QString actualType = scopeObserver ? QStringLiteral("Scope")
                              : phasePortraitObserver ? QStringLiteral("PhasePortrait")
                              : phaseTrajectory3DObserver ? QStringLiteral("PhaseTrajectory3D") : elementType;
    const auto* descriptor = observer ? nullptr : s7::ElementRegistry::instance().find(actualType.toStdString());
    diagram::ElementNode::Element element;
    element.id = nextElementId(*scene, actualType).toStdString();
    element.type = actualType.toStdString();
    if (scopeObserver) {
        element.params["channel_count"] = 0;
    }
    if (descriptor && descriptor->metadata().schema_definition.has_value()) {
        element.schema = *descriptor->metadata().schema_definition;
    }
    const auto ports = diagram::elementPorts(element.type, element.params, element.schema);
    const int portRows = static_cast<int>(std::max(ports.inputs.size(), ports.outputs.size()));
    const QSizeF size(diagram::kElementWidthSteps * diagram::kGridStep,
                      std::max(5, 2 * portRows + 1) * diagram::kGridStep);
    scene->addElement(element, diagramEditor_->suggestedNewItemPosition(size));
    setDiagramMode(QSchematic::Scene::NormalMode);
    refreshPlacementLabels();
    appendLog(QStringLiteral("[studio] added %1 %2").arg(observer ? QStringLiteral("observer") : QStringLiteral("element"),
                                                               QString::fromStdString(element.id)));
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

    probesPanel_ = new ui::ProbesPanel(this);
    connect(probesPanel_, &ui::ProbesPanel::openScopeRequested, this, &MainWindow::onProbeOpenScopeRequested);
    connect(probesPanel_, &ui::ProbesPanel::openPhasePortraitRequested, this, &MainWindow::onProbeOpenPhasePortraitRequested);
    connect(probesPanel_, &ui::ProbesPanel::renameRequested, this, &MainWindow::onProbeRenameRequested);
    connect(probesPanel_, &ui::ProbesPanel::deleteRequested, this, &MainWindow::onProbeDeleteRequested);
    probesDock_ = toolWindows_->add(QStringLiteral("probes"), QStringLiteral("Probes"), QStringLiteral("pointer"), probesPanel_,
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
    if (runPanel_) {
        runPanel_->setStatus(status);
    }
    refreshActions();
}

void MainWindow::onRunCompleted(bool success, const QString& error)
{
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
        runPanel_->setProgress(total < 0 ? QString("%1 / continuous").arg(completed) : QString("%1 / %2").arg(completed).arg(total));
    }
}

void MainWindow::onElementNodeDoubleClicked(const QString& elementId,
                                            const QString& elementType,
                                            const int /*channelCount*/)
{
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
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

    if (elementType == QStringLiteral("Scope")) {
        openScopeWindow(QString::fromStdString(elementNode->id()));
        return;
    }
    if (elementType == QStringLiteral("PhasePortrait")) {
        const auto id = QString::fromStdString(elementNode->id());
        openPhasePortraitWindow(id + QStringLiteral("/x"), id + QStringLiteral("/y"));
        return;
    }
    if (elementType == QStringLiteral("PhaseTrajectory3D")) {
        const auto id = QString::fromStdString(elementNode->id());
        openPhaseTrajectory3DWindow(id + QStringLiteral("/x"), id + QStringLiteral("/y"),
                                    id + QStringLiteral("/z"));
        return;
    }

    const auto* descriptor = s7::ElementRegistry::instance().find(elementType.toStdString());

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Element Properties - %1").arg(elementId));
    dialog.resize(620, 460);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* idEdit = new QLineEdit(elementId, &dialog);
    idEdit->setReadOnly(true);
    form->addRow(QStringLiteral("ID"), idEdit);

    auto* typeEdit = new QLineEdit(elementType, &dialog);
    typeEdit->setReadOnly(true);
    form->addRow(QStringLiteral("Type"), typeEdit);

    auto* libraryEdit = new QLineEdit(elementLibraryForType(elementType), &dialog);
    libraryEdit->setReadOnly(true);
    form->addRow(QStringLiteral("Library"), libraryEdit);

    auto* descriptionLabel = new QLabel(elementDescriptionForType(elementType), &dialog);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setMinimumHeight(48);
    form->addRow(QStringLiteral("Description"), descriptionLabel);

    // Placement: the runtime node this element runs on when the diagram is deployed.
    const QString currentPlacement = QString::fromStdString(elementNode->element().placement);
    auto* nodeCombo = new QComboBox(&dialog);
    nodeCombo->addItem(QStringLiteral("(not placed)"), QString());
    if (headlessNodesPanel_) {
        for (const auto& known : headlessNodesPanel_->knownNodes()) {
            nodeCombo->addItem(QStringLiteral("%1  %2  (%3)").arg(known.nodeName, known.endpoint, known.nodeUuid.left(8)), known.nodeUuid);
        }
    }
    if (!currentPlacement.isEmpty() && nodeCombo->findData(currentPlacement) < 0) {
        nodeCombo->addItem(currentPlacement, currentPlacement);
    }
    nodeCombo->setCurrentIndex(std::max(0, nodeCombo->findData(currentPlacement)));
    nodeCombo->setToolTip(QStringLiteral("Runtime node this element runs on when the diagram is deployed (nodes from the Headless Nodes tool window)."));
    form->addRow(QStringLiteral("Node"), nodeCombo);

    if (descriptor) {
        const auto& metadata = descriptor->metadata();
        auto* inputsLabel = new QLabel(portListSummary(metadata.input_ports), &dialog);
        inputsLabel->setWordWrap(true);
        form->addRow(QStringLiteral("Inputs"), inputsLabel);

        auto* outputsLabel = new QLabel(portListSummary(metadata.output_ports), &dialog);
        outputsLabel->setWordWrap(true);
        form->addRow(QStringLiteral("Outputs"), outputsLabel);

        if (metadata.variadic_inputs.enabled) {
            auto* variadicLabel = new QLabel(
                QStringLiteral("enabled, min=%1, max=%2, template=%3")
                    .arg(static_cast<int>(metadata.variadic_inputs.min_count))
                    .arg(static_cast<int>(metadata.variadic_inputs.max_count))
                    .arg(QString::fromStdString(metadata.variadic_inputs.port_name_template)),
                &dialog);
            variadicLabel->setWordWrap(true);
            form->addRow(QStringLiteral("Variadic inputs"), variadicLabel);
        }
    }

    layout->addLayout(form);

    if (descriptor) {
        const auto& metadata = descriptor->metadata();
        auto* paramsInfoLabel = new QLabel(parameterListSummary(metadata.parameters), &dialog);
        paramsInfoLabel->setWordWrap(true);
        paramsInfoLabel->setMinimumHeight(56);
        layout->addWidget(new QLabel(QStringLiteral("Parameter specs:"), &dialog));
        layout->addWidget(paramsInfoLabel);
    }

    auto* paramsLabel = new QLabel(QStringLiteral("Parameters (effective JSON object):"), &dialog);
    layout->addWidget(paramsLabel);

    boost::json::object paramsForDialog = elementNode->element().params;
    if (descriptor) {
        paramsForDialog = withDefaultedParams(descriptor->metadata(), paramsForDialog);
    }
    const QString currentParamsText = QString::fromStdString(boost::json::serialize(paramsForDialog));

    auto* paramsEdit = new QPlainTextEdit(currentParamsText, &dialog);
    paramsEdit->setPlaceholderText(QStringLiteral("{\n  \"value\": 1.0\n}"));
    paramsEdit->setMinimumHeight(180);
    layout->addWidget(paramsEdit, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    while (dialog.exec() == QDialog::Accepted) {
        boost::json::object paramsObject;
        QString parseError;
        if (!parseEditableParams(paramsEdit->toPlainText().trimmed(), paramsObject, parseError)) {
            QMessageBox::warning(this, QStringLiteral("Invalid parameters"), parseError);
            continue;
        }

        // A null optional parameter means "not set".
        if (descriptor) {
            for (const auto& parameter : descriptor->metadata().parameters) {
                if (const auto it = paramsObject.find(parameter.name); !parameter.required && it != paramsObject.end() && it->value().is_null()) {
                    paramsObject.erase(it);
                }
            }
        }
        auto element = elementNode->element();
        element.params = paramsObject;
        element.placement = nodeCombo->currentData().toString().toStdString();
        scene->changeElement(elementNode, element);
        refreshPlacementLabels();

        appendLog(QStringLiteral("[studio] updated element params: %1").arg(elementId));
        break;
    }
}

void MainWindow::onScopeSamplesAvailable()
{
    if (!runner_) {
        return;
    }
    const auto batch = runner_->takeScopeSamples();

    for (const auto& sample : batch.samples) {
        const auto probeName = QString::fromStdString(sample.probeName);
        const auto separator = probeName.indexOf(QLatin1Char('/'));
        const auto scopeId = separator < 0 ? probeName : probeName.left(separator);
        if (const auto window = ensureScopeWindow(scopeId)) {
            const auto channel = separator < 0 ? probeName : probeName.mid(separator + 1);
            window->appendSample(channel, sample.timeSeconds, sample.value);
        }
        for (auto it = phasePortraitWindows_.cbegin(); it != phasePortraitWindows_.cend(); ++it) {
            const auto& window = it.value();
            if (!window) {
                continue;
            }
            if (window->xProbeName() == probeName) {
                window->appendXSample(sample.step, sample.value);
            }
            if (window->yProbeName() == probeName) {
                window->appendYSample(sample.step, sample.value);
            }
        }
        for (auto it = phaseTrajectory3DWindows_.cbegin(); it != phaseTrajectory3DWindows_.cend(); ++it) {
            const auto& window = it.value();
            if (!window) {
                continue;
            }
            if (window->xProbeName() == probeName) {
                window->appendXSample(sample.step, sample.value);
            }
            if (window->yProbeName() == probeName) {
                window->appendYSample(sample.step, sample.value);
            }
            if (window->zProbeName() == probeName) {
                window->appendZSample(sample.step, sample.value);
            }
        }
    }
    if (batch.dropped > 0) {
        // The runtime produced samples faster than the UI took them; the oldest were dropped.
        scopeSamplesDropped_ += batch.dropped;
        for (const auto& window : std::as_const(scopeWindows_)) {
            if (window) {
                window->setDroppedSampleCount(scopeSamplesDropped_);
            }
        }
        for (const auto& window : std::as_const(phasePortraitWindows_)) {
            if (window) {
                window->setDroppedSampleCount(scopeSamplesDropped_);
            }
        }
        for (const auto& window : std::as_const(phaseTrajectory3DWindows_)) {
            if (window) {
                window->setDroppedSampleCount(scopeSamplesDropped_);
            }
        }
        const auto nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastScopeDropLogMs_ >= 1000) {
            lastScopeDropLogMs_ = nowMs;
            appendLog(QStringLiteral("[studio] scope display could not keep up; %1 samples dropped so far (oldest first)")
                          .arg(scopeSamplesDropped_));
        }
    }
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
    if (diagramEditor_) {
        diagramEditor_->zoomIn();
    }
}

void MainWindow::zoomDiagramOut()
{
    if (diagramEditor_) {
        diagramEditor_->zoomOut();
    }
}

void MainWindow::resetDiagramZoom()
{
    if (diagramEditor_) {
     diagramEditor_->resetZoom();
     }
 }

 void MainWindow::setDiagramMode(const int mode)
{
    if (diagramEditor_ && diagramEditor_->scene()) {
        diagramEditor_->scene()->setMode(mode);
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
    wireTypeTimer_->start();
    onProbesChanged();
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

auto MainWindow::currentRunProbes() const -> std::vector<RunProbe>
{
    std::vector<RunProbe> probes;
    if (diagramEditor_ && diagramEditor_->scene()) {
        const auto* scene = diagramEditor_->scene();
        for (const auto& probe : scene->probes()) {
            probes.push_back(RunProbe{probe.name.toStdString(), probe.point.element, probe.point.port});
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
            probes.push_back(RunProbe{connection.to.element + "/" + channelName, connection.from.element, connection.from.port});
        }
    }
    return probes;
}

void MainWindow::onProbeOpenScopeRequested(const QString& probeName)
{
    openScopeWindow(probeName);
}

void MainWindow::onProbeOpenPhasePortraitRequested(const QString& xProbeName, const QString& yProbeName)
{
    openPhasePortraitWindow(xProbeName, yProbeName);
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
    auto* scene = diagramEditor_ ? diagramEditor_->scene() : nullptr;
    if (!scene) {
        return;
    }
    const auto frameId = nextTextFrameId(*scene);
    const auto params = diagram::TextFrameNode::defaultParams();
    const QSizeF size(params.at("width").to_number<double>(), params.at("height").to_number<double>());
    const auto frame = scene->addTextFrame(frameId, params, diagramEditor_->suggestedNewItemPosition(size));
    scene->clearSelection();
    frame->setSelected(true);
    setDiagramMode(QSchematic::Scene::NormalMode);
    syncTextFrameStyleControls();
    appendLog(QStringLiteral("[studio] added diagram text frame: %1").arg(QString::fromStdString(frameId)));
}

void MainWindow::setSelectedTextFrameTextColor()
{
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
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

void MainWindow::setSelectedTextFrameLineColor()
{
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
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
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
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
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!frame) {
        return;
    }
    auto params = frame->params();
    params["font_family"] = font.family().toStdString();
    frame->setParams(params);
}

void MainWindow::setSelectedTextFrameFontSize(const int pointSize)
{
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!frame) {
        return;
    }
    auto params = frame->params();
    params["font_size"] = std::max(6, pointSize);
    frame->setParams(params);
}

void MainWindow::syncTextFrameStyleControls()
{
    const auto frame = selectedTextFrame(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    const bool enabled = static_cast<bool>(frame);

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
        textFrameFontSizeSpin_->setValue(std::max(6, json_utils::intValue(params, "font_size", 12)));
    }
}

void MainWindow::applyPaperSize(const double widthMm, const double heightMm)
{
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.widthMm = widthMm;
    pageLayout.heightMm = heightMm;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
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
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Portrait;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
    syncPageActions();
}

void MainWindow::setPageLandscape()
{
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Landscape;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
    syncPageActions();
}

void MainWindow::syncPageActions()
{
    if (!diagramEditor_) {
        return;
    }

    const auto pageLayout = diagramEditor_->pageLayout();
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

auto MainWindow::ensureScopeWindow(const QString& probeName) -> ui::ScopeViewWindow*
{
    if (const auto existing = scopeWindows_.value(probeName)) {
        return existing;
    }
    // Record from the first delivered sample, even before the user opens the
    // view. Closing hides the window and retains its disk-backed recording.
    auto* window = new ui::ScopeViewWindow(
        probeName,
        nullptr,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    scopeWindows_.insert(probeName, window);
    window->setDroppedSampleCount(scopeSamplesDropped_);
    return window;
}

void MainWindow::openScopeWindow(const QString& probeName)
{
    if (probeName.isEmpty()) {
        return;
    }
    auto* window = ensureScopeWindow(probeName);
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::openPhasePortraitWindow(const QString& xProbeName, const QString& yProbeName)
{
    if (xProbeName.isEmpty() || yProbeName.isEmpty() || xProbeName == yProbeName) {
        return;
    }
    const auto key = QStringLiteral("%1|%2").arg(xProbeName, yProbeName);

    if (const auto existing = phasePortraitWindows_.value(key)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* window = new ui::PhasePortraitViewWindow(
        xProbeName,
        yProbeName,
        nullptr,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    phasePortraitWindows_.insert(key, window);
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::openPhaseTrajectory3DWindow(const QString& xProbeName,
                                             const QString& yProbeName,
                                             const QString& zProbeName)
{
    if (xProbeName.isEmpty() || yProbeName.isEmpty() || zProbeName.isEmpty() ||
        xProbeName == yProbeName || xProbeName == zProbeName || yProbeName == zProbeName) {
        return;
    }
    const auto key = QStringLiteral("%1|%2|%3").arg(xProbeName, yProbeName, zProbeName);
    if (const auto existing = phaseTrajectory3DWindows_.value(key)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* window = new ui::PhaseTrajectory3DViewWindow(
        xProbeName, yProbeName, zProbeName, nullptr,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    phaseTrajectory3DWindows_.insert(key, window);
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::closeScopeWindows()
{
    const auto existingWindows = scopeWindows_.values();
    scopeWindows_.clear();
    for (const auto& window : existingWindows) {
        if (window) {
            delete window.data();
        }
    }
    const auto existingPhasePortraits = phasePortraitWindows_.values();
    phasePortraitWindows_.clear();
    for (const auto& window : existingPhasePortraits) {
        if (window) {
            window->close();
        }
    }
    const auto existingPhaseTrajectories3D = phaseTrajectory3DWindows_.values();
    phaseTrajectory3DWindows_.clear();
    for (const auto& window : existingPhaseTrajectories3D) {
        if (window) {
            window->close();
        }
    }
}

void MainWindow::clearScopeSamples()
{
    scopeSamplesDropped_ = 0;
    for (const auto& window : scopeWindows_) {
        if (window) {
            window->clearSamples();
        }
    }
    for (const auto& window : phasePortraitWindows_) {
        if (window) {
            window->clearSamples();
        }
    }
    for (const auto& window : phaseTrajectory3DWindows_) {
        if (window) {
            window->clearSamples();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (shuttingDown_) {
        QMainWindow::closeEvent(event);
        return;
    }

    shuttingDown_ = true;
    saveWindowLayout();
    if (diagramEditor_ && diagramEditor_->scene()) {
        disconnect(diagramEditor_->scene(), nullptr, this, nullptr);
    }
    if (runner_) {
        disconnect(runner_, nullptr, this, nullptr);
    }
    for (const auto& window : scopeWindows_) {
        if (window) {
            disconnect(window, nullptr, this, nullptr);
        }
    }

    closeScopeWindows();
    if (runner_) {
        runner_->reset();
    }
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
    dialog.setWindowTitle(QStringLiteral("Run Configuration"));
    dialog.resize(420, 260);

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* runtimeSecondsSpin = new QSpinBox(&dialog);
    runtimeSecondsSpin->setRange(-1, 604800);
    runtimeSecondsSpin->setSpecialValueText(QStringLiteral("-1 (continuous)"));
    runtimeSecondsSpin->setValue(static_cast<int>(std::clamp<long>(runtimeSeconds_, -1, 604800)));

    auto* frequencySpin = new QDoubleSpinBox(&dialog);
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

    auto* clearSamplesCheck = new QCheckBox(QStringLiteral("Clear scope traces before run"), &dialog);
    clearSamplesCheck->setChecked(clearScopesBeforeRun_);

    form->addRow(QStringLiteral("Run time (s)"), runtimeSecondsSpin);
    form->addRow(QStringLiteral("Discretization frequency"), frequencySpin);
    form->addRow(QStringLiteral("Computed dt"), dtLabel);
    layout->addLayout(form);
    layout->addWidget(clearSamplesCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    runtimeSeconds_ = runtimeSecondsSpin->value();
    clearScopesBeforeRun_ = clearSamplesCheck->isChecked();
    if (overlayScopeTracesAction_ && overlayScopeTracesAction_->isChecked() == clearScopesBeforeRun_) {
        const QSignalBlocker blocker(overlayScopeTracesAction_);
        overlayScopeTracesAction_->setChecked(!clearScopesBeforeRun_);
    }

    applyDiscretizationFrequency(frequencySpin->value());

    if (launchAfterAccept) {
        launchRunWithCurrentConfiguration();
    }

    return true;
}

void MainWindow::launchRunWithCurrentConfiguration()
{
    refreshWireTypeLabels();
    if (const auto problem = diagramProblem(); !problem.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("The diagram cannot run: %1").arg(problem), 8000);
        toolWindows_->show(problemsDock_);
        return;
    }
    runPanel_->clearOutput();
    runPanel_->append(QStringLiteral("Running at %1 Hz, %2.")
                          .arg(discretizationFrequencyHz_)
                          .arg(runtimeSeconds_ < 0 ? QStringLiteral("continuously") : QStringLiteral("%1 s").arg(runtimeSeconds_)));
    if (clearScopesBeforeRun_) {
        clearScopeSamples();
    }

    const double dtSeconds = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);
    if (diagramEditor_ && diagramEditor_->scene()) {
        static_cast<void>(syncSchemaFromScene());
        updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
    }

    controller_->run(runtimeSchema_, dtSeconds, ui::RunExecutionConfig{runtimeSeconds_}, currentRunProbes());
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
    if (!schema.is_object()) {
        return false;
    }

    auto& root = schema.as_object();
    const auto elementsIt = root.find("elements");
    if (elementsIt == root.end() || !elementsIt->value().is_array()) {
        return false;
    }

    bool changed = false;
    for (auto& elementValue : elementsIt->value().as_array()) {
        if (!elementValue.is_object()) {
            continue;
        }

        auto& elementObject = elementValue.as_object();
        const auto type = json_utils::stringValue(elementObject, "type");
        bool needsSampleRate = false;
        bool needsDt = false;
        if (const auto* descriptor = s7::ElementRegistry::instance().find(type)) {
            for (const auto& parameter : descriptor->metadata().parameters) {
                if (parameter.name == "sample_rate_hz") {
                    needsSampleRate = true;
                }
                if (parameter.name == "dt") {
                    needsDt = true;
                }
            }
        }

        boost::json::object* params = nullptr;
        if (const auto paramsIt = elementObject.find("params");
            paramsIt != elementObject.end() && paramsIt->value().is_object()) {
            params = &paramsIt->value().as_object();
        }

        if (!needsSampleRate && !needsDt && params) {
            needsSampleRate = params->contains("sample_rate_hz");
            needsDt = params->contains("dt");
        }

        if (!needsSampleRate && !needsDt) {
            continue;
        }

        if (!params) {
            elementObject["params"] = boost::json::object{};
            params = &elementObject["params"].as_object();
        }

        if (needsSampleRate) {
            (*params)["sample_rate_hz"] = frequencyHz;
            changed = true;
        }
        if (needsDt) {
            (*params)["dt"] = dtSeconds;
            changed = true;
        }
    }

    return changed;
}

void MainWindow::loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel)
{
    closeScopeWindows();
    clearScopeSamples();

    runtimeSchema_ = schemaJson;
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
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel)
{
    closeScopeWindows();
    clearScopeSamples();

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

    const auto document = diagram::DiagramDocument::fromJson(diagramJson);
    runtimeSchema_ = document.runtimeSchema;
    deploymentRuntimeNodes_ = document.runtimeNodes;
    deploymentStreams_ = document.streams;

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
    setWindowTitle(QString("s7studio - %1").arg(name));
    updateProgress(100);
    loading.close();
}

void MainWindow::openDocumentFile()
{
    openJsonDocument(
        QStringLiteral("Open diagram"),
        QStringLiteral("Diagram, schema, or library files (*.json *.s7diagram *.s7library);;All files (*)"),
        true,
        QStringLiteral("Open failed"));
}

void MainWindow::closeCurrentDiagram()
{
    if (runner_ && runner_->isRunning()) {
        runner_->reset();
    }

    closeScopeWindows();
    clearScopeSamples();

    runtimeSchema_ = boost::json::object{
        {"name", "untitled"},
        {"elements", boost::json::array{}},
        {"connections", boost::json::array{}}
    };
    currentDiagramPath_.clear();
    deploymentRuntimeNodes_.clear();
    deploymentStreams_.clear();

    if (diagramEditor_) {
        diagramEditor_->clearDiagram();
        diagramEditor_->fitPageInView();
        syncPageActions();
        syncTextFrameStyleControls();
    }

    setWindowTitle(QStringLiteral("s7studio - untitled"));
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
        QStringLiteral("Save diagram"),
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
        const auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isElementLibraryDocument(parsed)) {
            loadElementLibraryJson(parsed, sourceLabel);
            return;
        }
        const bool diagramDocument = isDiagramDocument(parsed);
        if (diagramDocument) {
            loadDiagramJson(parsed, sourceLabel);
            currentDiagramPath_ = filePath;
        } else {
            loadSchemaJson(parsed, sourceLabel);
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
        QStringLiteral("Load element library"),
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
    try {
        const auto filePath = QString::fromUtf8(kBundledDemoDiagramPath);
        const auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isDiagramDocument(parsed)) {
            loadDiagramJson(parsed, sourceLabel);
        } else {
            loadSchemaJson(parsed, sourceLabel);
        }
    } catch (const std::exception& ex) {
        appendLog(QStringLiteral("[studio] failed to load bundled demo: %1").arg(QString::fromUtf8(ex.what())));
        loadSchemaJson(boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}},
                       QStringLiteral("empty schema"));
    }
    currentDiagramPath_.clear();
}

void MainWindow::saveSchemaLibrary()
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Diagram scene is not available."));
        return;
    }

    auto document = syncSchemaFromScene();
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
        QStringLiteral("Save schema library"),
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
    QFile file(outputFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save library failed"),
                              QStringLiteral("Failed to write '%1': %2").arg(outputFile, file.errorString()));
        return;
    }

    const auto json = boost::json::serialize(payload);
    file.write(QByteArray::fromStdString(json));
    file.write("\n");
    file.close();

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
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QStringLiteral("Diagram scene is not available."));
        return false;
    }

    auto document = syncSchemaFromScene();
    document.runtimeNodes = deploymentRuntimeNodes_;
    document.streams = deploymentStreams_;

    const auto payload = document.toJson();
    const auto json = boost::json::serialize(payload);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save failed"),
                              QStringLiteral("Failed to write '%1': %2").arg(filePath, file.errorString()));
        return false;
    }

    file.write(QByteArray::fromStdString(json));
    file.write("\n");
    file.close();

    runtimeSchema_ = document.runtimeSchema;
    currentDiagramPath_ = filePath;
    appendLog(QString("[studio] saved diagram: %1").arg(QFileInfo(filePath).fileName()));
    statusBar()->showMessage(QString("Saved diagram: %1").arg(QFileInfo(filePath).fileName()), 5000);
    return true;
}

void MainWindow::fitDiagramToView()
{
    if (diagramEditor_) {
        diagramEditor_->fitPageInView();
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
        if (node && params && params->is_object() && node->element().params != params->as_object()) {
            auto element = node->element();
            element.params = params->as_object();
            node->setElement(element);
        }
    }
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
        known[node.nodeUuid] = diagram::NodeAddress{node.nodeName, node.endpoint};
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
            const auto revision = response.at("revision").to_number<std::uint64_t>();
            try {
                loadDiagramJson(response.at("diagram"), QStringLiteral("deployed on %1, revision %2").arg(node.nodeName).arg(revision));
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, QStringLiteral("Open Deployed Diagram"), QString::fromUtf8(ex.what()));
            }
            currentDiagramPath_.clear();
        };
    });
}

} // namespace s7studio

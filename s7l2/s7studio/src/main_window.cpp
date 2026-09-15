#include "main_window.hpp"

#include "diagram_document.hpp"
#include "diagram_editor.hpp"
#include "diagram_node_roles.hpp"
#include "element_picker_panel.hpp"
#include "json_utils.hpp"
#include "main_window_builder.hpp"
#include "scene_serializer.hpp"
#include "scope_view_window.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_catalog.hpp>
#include <s7/element_registry.hpp>

#include "s7/plugin_loader.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/scene.hpp>

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QSpinBox>
#include <QStatusBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
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
constexpr int kMaxScopeHistoryPointsPerScope = 20000;
constexpr const char* kBundledDemoDiagramPath = S7STUDIO_SOURCE_DIR "/resources/demo_startup.s7diagram";

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
    }

    if (type == "Constant") return QStringLiteral("Constant source: outputs a fixed value.");
    if (type == "Gain") return QStringLiteral("Gain: multiplies input by a factor.");
    if (type == "Adder") return QStringLiteral("Adder: weighted sum of N inputs.");
    if (type == "Multiply") return QStringLiteral("Multiply: product of N inputs.");
    if (type == "Delay") return QStringLiteral("Delay: outputs previous input sample (stateful).");
    if (type == "Integrator") return QStringLiteral("Integrator: accumulates input over time.");
    if (type == "AnalogOscillator") return QStringLiteral("Analog oscillator: sinusoidal source.");
    if (type == "Scope") return QStringLiteral("Scope observer: records selected channels for plotting.");
    if (type == "FirFilter") return QStringLiteral("FIR filter: finite impulse response filter.");
    if (type == "IirFilter") return QStringLiteral("IIR filter: recursive filter.");
    if (type == "PreviousValue") return QStringLiteral("PreviousValue: unit-delay output element.");
    if (type == "ReImToComplex") return QStringLiteral("ReImToComplex: combines real/imaginary inputs.");
    if (type == "ComplexToReIm") return QStringLiteral("ComplexToReIm: splits complex input into real and imaginary outputs.");
    if (type == "Int12ToDouble") return QStringLiteral("Int12ToDouble: converts 12-bit integer signal to double.");
    if (type == "DoubleToInt12") return QStringLiteral("DoubleToInt12: converts double signal to 12-bit integer range.");
    if (type == "ComplexToMagnitudePhase") return QStringLiteral("ComplexToMagnitudePhase: outputs magnitude and phase.");
    return QStringLiteral("No built-in description available for this element type.");
}

auto elementLibraryForType(const QString& type) -> QString
{
    if (type == "Scope") {
        return QStringLiteral("s7studio (embedded observer in executable)");
    }
    if (const auto* descriptor = s7::ElementRegistry::instance().find(type.toStdString())) {
        const auto& metadata = descriptor->metadata();
        if (!metadata.category.empty()) {
            return QStringLiteral("registry category: %1").arg(QString::fromStdString(metadata.category));
        }
        return QStringLiteral("registry");
    }
    return QStringLiteral("s7core (.so at runtime link: libs7core)");
}

auto nextElementId(boost::json::value& runtimeSchema, const QString& elementType) -> QString
{
    const QString base = elementType.toLower() + QStringLiteral("_");
    int suffix = 1;

    auto hasId = [&runtimeSchema](const QString& probeId) {
        if (!runtimeSchema.is_object()) {
            return false;
        }
        auto& root = runtimeSchema.as_object();
        const auto elementsIt = root.find("elements");
        if (elementsIt == root.end() || !elementsIt->value().is_array()) {
            return false;
        }
        for (const auto& item : elementsIt->value().as_array()) {
            if (!item.is_object()) {
                continue;
            }
            const auto id = json_utils::stringValue(item.as_object(), "id");
            if (QString::fromUtf8(id.c_str()) == probeId) {
                return true;
            }
        }
        return false;
    };

    QString candidate;
    do {
        candidate = base + QString::number(suffix++);
    } while (hasId(candidate));

    return candidate;
}

auto suggestedNewNodePosition(const QSchematic::Scene& scene) -> QPointF
{
    constexpr double kOffsetX = 240.0;

    const auto nodes = scene.items<QSchematic::Items::Node>();
    if (nodes.empty()) {
        return QPointF(40.0, 40.0);
    }

    double maxX = 40.0;
    double yAtMaxX = 40.0;
    for (const auto& node : nodes) {
        if (!node) {
            continue;
        }

        if (node->pos().x() >= maxX) {
            maxX = node->pos().x();
            yAtMaxX = node->pos().y();
        }
    }

    return QPointF(maxX + kOffsetX, yAtMaxX);
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

auto findElementParamsObject(boost::json::value& runtimeSchema, const QString& elementId) -> boost::json::object*
{
    if (!runtimeSchema.is_object()) {
        return nullptr;
    }

    auto& root = runtimeSchema.as_object();
    const auto elementsIt = root.find("elements");
    if (elementsIt == root.end() || !elementsIt->value().is_array()) {
        return nullptr;
    }

    auto& elements = elementsIt->value().as_array();
    for (auto& elementValue : elements) {
        if (!elementValue.is_object()) {
            continue;
        }

        auto& elementObject = elementValue.as_object();
        const auto id = json_utils::stringValue(elementObject, "id");
        if (QString::fromUtf8(id.c_str()) != elementId) {
            continue;
        }

        if (const auto it = elementObject.find("params"); it != elementObject.end() && it->value().is_object()) {
            return &it->value().as_object();
        }

        elementObject["params"] = boost::json::object{};
        return &elementObject["params"].as_object();
    }

    return nullptr;
}

auto findElementParamsObjectView(const boost::json::value& runtimeSchema, const QString& elementId) -> const boost::json::object*
{
    if (!runtimeSchema.is_object()) {
        return nullptr;
    }

    const auto& root = runtimeSchema.as_object();
    const auto elementsIt = root.find("elements");
    if (elementsIt == root.end() || !elementsIt->value().is_array()) {
        return nullptr;
    }

    const auto& elements = elementsIt->value().as_array();
    for (const auto& elementValue : elements) {
        if (!elementValue.is_object()) {
            continue;
        }

        const auto& elementObject = elementValue.as_object();
        const auto id = json_utils::stringValue(elementObject, "id");
        if (QString::fromUtf8(id.c_str()) != elementId) {
            continue;
        }

        if (const auto it = elementObject.find("params"); it != elementObject.end() && it->value().is_object()) {
            return &it->value().as_object();
        }

        return nullptr;
    }

    return nullptr;
}

auto withDefaultedParams(const s7::ElementMetadata& metadata, const boost::json::object& params) -> boost::json::object
{
    auto effective = params;
    for (const auto& parameter : metadata.parameters) {
        if (effective.find(parameter.name) == effective.end()) {
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
        connect(diagramEditor_, &diagram::DiagramEditor::scopeNodeDoubleClicked,
                this,
                &MainWindow::onScopeNodeDoubleClicked);
        connect(diagramEditor_, &diagram::DiagramEditor::elementNodeDoubleClicked,
                this,
                &MainWindow::onElementNodeDoubleClicked);
    }

    appendLog("[studio] Local embedded runtime ready");
    loadDemoDiagram();
    refreshActions();
    onStatusChanged("idle");
}

MainWindow::~MainWindow()
{
    closeScopeWindows();
}

void MainWindow::connectRunnerSignals()
{
    connect(runner_, &LocalSchemaRunner::logMessage, this, &MainWindow::appendLog);
    connect(runner_, &LocalSchemaRunner::statusChanged, this, &MainWindow::onStatusChanged);
    connect(runner_, &LocalSchemaRunner::runCompleted, this, &MainWindow::onRunCompleted);
    connect(runner_, &LocalSchemaRunner::ticksProgress, this, &MainWindow::onTickProgress);
    connect(runner_, &LocalSchemaRunner::scopeSample, this, &MainWindow::onScopeSample);
}

void MainWindow::buildActions()
{
    const auto actions = ui::MainWindowBuilder::createActions(this);
    openAction_ = actions.openAction;
    closeDiagramAction_ = actions.closeDiagramAction;
    saveAction_ = actions.saveAction;
    saveAsAction_ = actions.saveAsAction;
    loadDemoAction_ = actions.loadDemoAction;
    fitDiagramAction_ = actions.fitDiagramAction;
    exitAction_ = actions.exitAction;
    runAction_ = actions.runAction;
    pauseAction_ = actions.pauseAction;
    resumeAction_ = actions.resumeAction;
    stopAction_ = actions.stopAction;
    resetAction_ = actions.resetAction;

    zoomInAction_ = new QAction("Zoom In", this);
    zoomOutAction_ = new QAction("Zoom Out", this);
    resetZoomAction_ = new QAction("Actual Size", this);
    fitPageAction_ = new QAction("Fit Page", this);
    showRuntimePanelAction_ = new QAction("Show Runtime Panel", this);
    overlayScopeTracesAction_ = new QAction("Overlay Scope Traces", this);
    overlayScopeTracesAction_->setCheckable(true);
    overlayScopeTracesAction_->setChecked(false);
    overlayScopeTracesAction_->setToolTip("Keep previous scope traces when starting a new run");

    modeNormalAction_ = new QAction("Normal Mode", this);
    modeNormalAction_->setCheckable(true);
    modeNormalAction_->setChecked(true);
    modeNormalAction_->setToolTip("Switch to Normal mode (move/select elements)");

    modeWireAction_ = new QAction("Wire Mode", this);
    modeWireAction_->setCheckable(true);
    modeWireAction_->setToolTip("Switch to Wire mode (draw connections)");

    QActionGroup* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addAction(modeNormalAction_);
    modeGroup->addAction(modeWireAction_);

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

    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    resetZoomAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fitPageAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));

    openAction_->setShortcut(QKeySequence::Open);
    closeDiagramAction_->setShortcut(QKeySequence::Close);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);

    connect(openAction_, &QAction::triggered, this, &MainWindow::openDocumentFile);
    connect(closeDiagramAction_, &QAction::triggered, this, &MainWindow::closeCurrentDiagram);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveDocument);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveDocumentAs);
    connect(loadDemoAction_, &QAction::triggered, this, &MainWindow::loadDemoDiagram);
    connect(fitDiagramAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    connect(runAction_, &QAction::triggered, this, [this] {
        if (!overlayScopeTracesAction_ || !overlayScopeTracesAction_->isChecked()) {
            clearScopeSamples();
        }
        controller_->run(runtimeSchema_, dtSpin_->value(), selectedRunDuration());
        refreshActions();
    });
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
    connect(showRuntimePanelAction_, &QAction::toggled, this, &MainWindow::setRuntimePanelVisible);
    connect(modeNormalAction_, &QAction::triggered, this, [this] {
        setDiagramMode(QSchematic::Scene::NormalMode);
    });
    connect(modeWireAction_, &QAction::triggered, this, [this] {
        setDiagramMode(QSchematic::Scene::WireMode);
    });

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
    actions.loadDemoAction = loadDemoAction_;
    actions.fitDiagramAction = fitDiagramAction_;
    actions.exitAction = exitAction_;
    actions.runAction = runAction_;
    actions.pauseAction = pauseAction_;
    actions.resumeAction = resumeAction_;
    actions.stopAction = stopAction_;
    actions.resetAction = resetAction_;

    ui::MainWindowBuilder::buildMenuAndToolbar(this, actions);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(modeNormalAction_);
    viewMenu->addAction(modeWireAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
    viewMenu->addAction(resetZoomAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fitPageAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(showRuntimePanelAction_);
    viewMenu->addAction(overlayScopeTracesAction_);

    auto* pageMenu = menuBar()->addMenu("&Page");
    pageMenu->addAction(paperA5Action_);
    pageMenu->addAction(paperA4Action_);
    pageMenu->addAction(paperA3Action_);
    pageMenu->addAction(paperLetterAction_);
    pageMenu->addSeparator();
    pageMenu->addAction(portraitPageAction_);
    pageMenu->addAction(landscapePageAction_);

    const auto widgets = ui::MainWindowBuilder::buildCentralWidgets(this);
    diagramEditor_ = widgets.diagramEditor;
    dtSpin_ = widgets.dtSpin;
    hoursSpin_ = widgets.hoursSpin;
    minutesSpin_ = widgets.minutesSpin;
    secondsSpin_ = widgets.secondsSpin;
    statusLabel_ = widgets.statusLabel;
    progressLabel_ = widgets.progressLabel;
    logView_ = widgets.logView;
    mainSplitter_ = widgets.splitter;
    runtimePanel_ = widgets.runtimePanel;
    setRuntimePanelVisible(false);

    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
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
    elementPickerDock_ = new QDockWidget(QStringLiteral("Element Library"), this);
    elementPickerDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    elementPickerDock_->setWidget(elementPickerPanel_);
    addDockWidget(Qt::RightDockWidgetArea, elementPickerDock_);

    connect(elementPickerPanel_, &ui::ElementPickerPanel::elementRequested,
            this, &MainWindow::onElementRequested);

    if (pluginLoader_) {
        appendLog(QStringLiteral("[studio] plugins loaded: %1").arg(pluginLoader_->loaded_count()));
    }

    if (diagramEditor_ && diagramEditor_->scene()) {
        connect(diagramEditor_->scene(), &QSchematic::Scene::modeChanged, this, [this](int mode) {
            if (modeNormalAction_ && modeWireAction_) {
                const QSignalBlocker blockerNormal(modeNormalAction_);
                const QSignalBlocker blockerWire(modeWireAction_);
                modeNormalAction_->setChecked(mode == QSchematic::Scene::NormalMode);
                modeWireAction_->setChecked(mode == QSchematic::Scene::WireMode);
            }
        });
    }

    syncPageActions();
}

void MainWindow::onElementRequested(const QString& elementType, const QString& /*displayName*/)
{
    if (elementType.isEmpty()) {
        return;
    }

    if (!runtimeSchema_.is_object()) {
        runtimeSchema_ = boost::json::object{
            {"name", "untitled"},
            {"elements", boost::json::array{}},
            {"connections", boost::json::array{}}
        };
    }

    auto& root = runtimeSchema_.as_object();
    if (!root.contains("elements") || !root.at("elements").is_array()) {
        root["elements"] = boost::json::array{};
    }

    const QString elementId = nextElementId(runtimeSchema_, elementType);

    boost::json::object elementObj;
    elementObj["id"] = elementId.toStdString();
    elementObj["type"] = elementType.toStdString();
    elementObj["params"] = boost::json::object{};
    root["elements"].as_array().push_back(elementObj);

    if (diagramEditor_) {
        if (auto* scene = diagramEditor_->scene()) {
            diagram::SerializationOptions options;
            options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("untitled"));
            options.dt = dtSpin_ ? dtSpin_->value() : 0.001;
            const auto pageLayout = diagramEditor_->pageLayout();
            options.sheetOrientation = pageLayout.orientation;
            options.sheetWidthMm = pageLayout.widthMm;
            options.sheetHeightMm = pageLayout.heightMm;

            diagram::SceneSerializer serializer;
            auto document = serializer.serialize(*scene, options);
            document.runtimeSchema = runtimeSchema_;

            if (!document.diagram.contains("nodes") || !document.diagram.at("nodes").is_array()) {
                document.diagram["nodes"] = boost::json::array{};
            }

            const auto position = suggestedNewNodePosition(*scene);
            boost::json::object positionJson;
            positionJson["x"] = position.x();
            positionJson["y"] = position.y();

            boost::json::object nodeJson;
            nodeJson["id"] = elementId.toStdString();
            nodeJson["type"] = elementType.toStdString();
            nodeJson["name"] = elementId.toStdString();
            nodeJson["position"] = positionJson;
            if (elementType == QStringLiteral("Scope")) {
                nodeJson["channel_count"] = 1;
            }
            document.diagram["nodes"].as_array().push_back(nodeJson);

            diagramEditor_->loadDocument(document);
        } else {
            diagramEditor_->loadRuntimeSchema(runtimeSchema_);
        }
        syncPageActions();
    }

    appendLog(QStringLiteral("[studio] added element %1 (%2)").arg(elementId, elementType));
}

void MainWindow::appendLog(const QString& message)
{
    if (logView_) {
        logView_->appendPlainText(message);
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onStatusChanged(const QString& status)
{
    if (statusLabel_) {
        statusLabel_->setText(status);
    }
    refreshActions();
}

void MainWindow::onRunCompleted(bool success, const QString& error)
{
    if (success) {
        appendLog("[studio] run completed");
    } else if (!error.isEmpty()) {
        appendLog(QString("[studio] run failed: %1").arg(error));
    }
    refreshActions();
}

void MainWindow::onTickProgress(const long completed, const long total)
{
    if (progressLabel_) {
        progressLabel_->setText(QString("%1 / %2").arg(completed).arg(total));
    }
}

void MainWindow::onScopeNodeDoubleClicked(const QString& scopeId, const int channelCount)
{
    openScopeWindow(scopeId, channelCount);
}

void MainWindow::onElementNodeDoubleClicked(const QString& elementId,
                                            const QString& elementType,
                                            const int channelCount)
{
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

    boost::json::object paramsForDialog;
    if (const auto* schemaParams = findElementParamsObjectView(runtimeSchema_, elementId)) {
        paramsForDialog = *schemaParams;
    }
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

        const QString encodedParams = QString::fromStdString(boost::json::serialize(paramsObject));
        if (diagramEditor_ && diagramEditor_->scene()) {
            for (const auto& nodePtr : diagramEditor_->scene()->items<QSchematic::Items::Node>()) {
                if (!nodePtr || nodePtr->data(diagram::kNodeIdRole).toString() != elementId) {
                    continue;
                }

                nodePtr->setData(diagram::kNodeParamsRole, encodedParams);
                if (elementType == QStringLiteral("Scope")) {
                    nodePtr->setData(diagram::kNodeChannelCountRole, std::max(1, json_utils::intValue(paramsObject, "channel_count", channelCount)));
                }
                break;
            }
        }

        if (auto* schemaParams = findElementParamsObject(runtimeSchema_, elementId)) {
            *schemaParams = paramsObject;
        }

        if ((elementType == QStringLiteral("Adder") ||
             elementType == QStringLiteral("Multiply") ||
             elementType == QStringLiteral("Scope")) && diagramEditor_) {
            diagramEditor_->loadRuntimeSchema(runtimeSchema_);
            syncPageActions();
        }

        appendLog(QStringLiteral("[studio] updated element params: %1").arg(elementId));
        break;
    }
}

void MainWindow::onScopeSample(const QString& scopeId,
                               const int channel,
                               const long /*step*/,
                               const double timeSeconds,
                               const double value)
{
    auto& history = scopeSampleHistory_[scopeId];
    history.push_back(ScopeSamplePoint{channel, timeSeconds, value});
    if (history.size() > kMaxScopeHistoryPointsPerScope) {
        const int overflow = history.size() - kMaxScopeHistoryPointsPerScope;
        history.erase(history.begin(), history.begin() + overflow);
    }

    if (const auto window = scopeWindows_.value(scopeId)) {
        window->appendSample(channel, timeSeconds, value);
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

 void MainWindow::setRuntimePanelVisible(const bool visible)
{
    if (showRuntimePanelAction_ && showRuntimePanelAction_->isChecked() != visible) {
        const QSignalBlocker blocker(showRuntimePanelAction_);
        showRuntimePanelAction_->setChecked(visible);
    }

    if (!runtimePanel_ || !mainSplitter_) {
        return;
    }

    runtimePanel_->setVisible(visible);
    if (!visible) {
        mainSplitter_->setSizes({std::max(mainSplitter_->width(), 1), 0});
        return;
    }

    const int totalWidth = std::max(mainSplitter_->width(), 900);
    const int panelWidth = std::clamp(totalWidth / 5, 220, 320);
    mainSplitter_->setSizes({std::max(totalWidth - panelWidth, 400), panelWidth});
}

void MainWindow::setDiagramMode(const int mode)
{
    if (diagramEditor_ && diagramEditor_->scene()) {
        diagramEditor_->scene()->setMode(mode);
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

void MainWindow::openScopeWindow(const QString& scopeId, const int channelCount)
{
    if (scopeId.isEmpty()) {
        return;
    }

    if (const auto existing = scopeWindows_.value(scopeId)) {
        replayScopeSamples(scopeId, *existing);
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* window = new ui::ScopeViewWindow(
        scopeId,
        channelCount,
        nullptr,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    scopeWindows_.insert(scopeId, window);
    connect(window, &QObject::destroyed, this, [this, scopeId] { scopeWindows_.remove(scopeId); });
    connect(window, &ui::ScopeViewWindow::configureRequested, this, [this](const QString& id, const int channels) {
        onElementNodeDoubleClicked(id, QStringLiteral("Scope"), channels);
    });
    replayScopeSamples(scopeId, *window);
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
            window->close();
        }
    }
}

void MainWindow::clearScopeSamples()
{
    scopeSampleHistory_.clear();
    for (const auto& window : scopeWindows_) {
        if (window) {
            window->clearSamples();
        }
    }
}

void MainWindow::replayScopeSamples(const QString& scopeId, ui::ScopeViewWindow& window)
{
    window.clearSamples();
    const auto historyIt = scopeSampleHistory_.find(scopeId);
    if (historyIt == scopeSampleHistory_.end()) {
        return;
    }

    for (const auto& point : historyIt.value()) {
        window.appendSample(point.channel, point.timeSeconds, point.value);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    closeScopeWindows();
    if (runner_) {
        runner_->reset();
    }
    QMainWindow::closeEvent(event);
}

auto MainWindow::selectedRunDuration() const -> ui::RunDuration
{
    ui::RunDuration duration;
    duration.hours = hoursSpin_ ? hoursSpin_->value() : 0;
    duration.minutes = minutesSpin_ ? minutesSpin_->value() : 1;
    duration.seconds = secondsSpin_ ? secondsSpin_->value() : 0;
    return duration;
}

void MainWindow::loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel)
{
    closeScopeWindows();
    clearScopeSamples();

    runtimeSchema_ = schemaJson;

    if (diagramEditor_) {
        diagramEditor_->loadRuntimeSchema(runtimeSchema_);
        syncPageActions();
    }

    const auto name = schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded schema: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded schema: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel)
{
    closeScopeWindows();
    clearScopeSamples();

    const auto document = diagram::DiagramDocument::fromJson(diagramJson);
    runtimeSchema_ = document.runtimeSchema;

    if (dtSpin_ && document.dt > 0.0) {
        dtSpin_->setValue(document.dt);
    }

    if (diagramEditor_) {
        diagramEditor_->loadDocument(document);
        syncPageActions();
    }

    const auto name = !document.id.isEmpty() ? document.id : schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded diagram: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded diagram: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::openDocumentFile()
{
    openJsonDocument(
        QStringLiteral("Open diagram"),
        QStringLiteral("Diagram or JSON files (*.json *.s7diagram);;All files (*)"),
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

    if (diagramEditor_) {
        diagramEditor_->clearDiagram();
        diagramEditor_->fitPageInView();
        syncPageActions();
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

    try {
        const auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
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

auto MainWindow::saveDocumentToPath(const QString& filePath) -> bool
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QStringLiteral("Diagram scene is not available."));
        return false;
    }

    diagram::SerializationOptions options;
    options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("s7studio_diagram"));
    options.dt = dtSpin_ ? dtSpin_->value() : 0.001;
    if (diagramEditor_) {
        const auto pageLayout = diagramEditor_->pageLayout();
        options.sheetOrientation = pageLayout.orientation;
        options.sheetWidthMm = pageLayout.widthMm;
        options.sheetHeightMm = pageLayout.heightMm;
    }

    diagram::SceneSerializer serializer;
    auto document = serializer.serialize(*diagramEditor_->scene(), options);

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

} // namespace s7studio

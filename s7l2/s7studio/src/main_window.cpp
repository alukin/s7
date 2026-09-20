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
#include <s7/net.hpp>
#include <s7/schema_library.hpp>

#include "s7/plugin_loader.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/scene.hpp>

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
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
#include <QCheckBox>
#include <QSignalBlocker>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
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
#include <QStringList>
#include <QSpinBox>
#include <QStatusBar>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
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
constexpr int kMaxScopeHistoryPointsPerScope = 20000;
constexpr const char* kBundledDemoDiagramPath = S7STUDIO_SOURCE_DIR "/resources/demo_startup.s7diagram";
constexpr double kDefaultTextFrameWidth = 260.0;
constexpr double kDefaultTextFrameHeight = 120.0;

auto parseEncodedParams(const QString& encodedParams) -> boost::json::object
{
    if (encodedParams.isEmpty()) {
        return {};
    }

    boost::system::error_code ec;
    const auto parsed = boost::json::parse(encodedParams.toStdString(), ec);
    if (ec || !parsed.is_object()) {
        return {};
    }

    return parsed.as_object();
}

auto defaultTextFrameParams() -> boost::json::object
{
    return boost::json::object{
        {"text", "Text frame"},
        {"text_color", "#202020"},
        {"line_color", "#444444"},
        {"fill_color", "#fff4cc"},
        {"font_family", "Sans Serif"},
        {"font_size", 12},
        {"width", kDefaultTextFrameWidth},
        {"height", kDefaultTextFrameHeight},
    };
}

auto mergedTextFrameParams(const boost::json::object& custom) -> boost::json::object
{
    auto merged = defaultTextFrameParams();
    for (const auto& [key, value] : custom) {
        merged[key] = value;
    }
    return merged;
}

auto themedToolbarSvgIcon(const QString& resourcePath, const QColor& color) -> QIcon
{
    QIcon source(resourcePath);
    if (source.isNull()) {
        return source;
    }

    QIcon themed;
    const std::vector<int> sizes{16, 18, 20, 24, 28, 32};
    for (const int size : sizes) {
        QPixmap base = source.pixmap(size, size);
        if (base.isNull()) {
            continue;
        }

        QPixmap normal = base;
        {
            QPainter painter(&normal);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(normal.rect(), color);
        }
        themed.addPixmap(normal, QIcon::Normal, QIcon::Off);
        themed.addPixmap(normal, QIcon::Active, QIcon::Off);
        themed.addPixmap(normal, QIcon::Selected, QIcon::Off);

        QPixmap disabled = base;
        {
            QColor disabledColor = color;
            disabledColor.setAlpha(110);
            QPainter painter(&disabled);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(disabled.rect(), disabledColor);
        }
        themed.addPixmap(disabled, QIcon::Disabled, QIcon::Off);
    }

    return themed.isNull() ? source : themed;
}

auto applyTextFrameVisuals(QSchematic::Items::Node& node, const boost::json::object& rawParams) -> boost::json::object
{
    auto params = mergedTextFrameParams(rawParams);
    const auto* widthValue = json_utils::findObjectValue(params, "width");
    const auto* heightValue = json_utils::findObjectValue(params, "height");
    const double width = std::max(80.0, json_utils::numberValue(widthValue ? *widthValue : boost::json::value{}, kDefaultTextFrameWidth));
    const double height = std::max(50.0, json_utils::numberValue(heightValue ? *heightValue : boost::json::value{}, kDefaultTextFrameHeight));
    params["width"] = width;
    params["height"] = height;

    const auto text = QString::fromStdString(json_utils::stringValue(params, "text", "Text frame"));
    const auto lineColor = QColor(QString::fromStdString(json_utils::stringValue(params, "line_color", "#444444")));
    const auto fillColor = QColor(QString::fromStdString(json_utils::stringValue(params, "fill_color", "#fff4cc")));
    const auto textColor = QColor(QString::fromStdString(json_utils::stringValue(params, "text_color", "#202020")));
    const auto fontFamily = QString::fromStdString(json_utils::stringValue(params, "font_family", "Sans Serif"));
    const int fontSize = std::max(6, json_utils::intValue(params, "font_size", 12));
    params["font_size"] = fontSize;

    node.setSize(width, height);

    QGraphicsRectItem* frameRect = nullptr;
    QGraphicsTextItem* frameText = nullptr;
    for (auto* child : node.childItems()) {
        if (!frameRect) {
            frameRect = dynamic_cast<QGraphicsRectItem*>(child);
        }
        if (!frameText) {
            frameText = dynamic_cast<QGraphicsTextItem*>(child);
        }
    }

    if (!frameRect) {
        frameRect = new QGraphicsRectItem(&node);
    }
    frameRect->setRect(0.0, 0.0, width, height);
    frameRect->setPen(QPen(lineColor, 1.4));
    frameRect->setBrush(fillColor);

    if (!frameText) {
        frameText = new QGraphicsTextItem(&node);
    }
    frameText->setPlainText(text);
    frameText->setDefaultTextColor(textColor);
    QFont font(fontFamily);
    font.setPointSize(fontSize);
    frameText->setFont(font);
    frameText->setTextWidth(width - 20.0);
    frameText->setPos(10.0, 8.0);

    node.setData(diagram::kNodeTypeRole, QStringLiteral("TextFrame"));
    node.setData(diagram::kNodeParamsRole, QString::fromStdString(boost::json::serialize(params)));
    node.setData(diagram::kTextFrameTextRole, text);
    node.setData(diagram::kTextFrameTextColorRole, textColor.name(QColor::HexRgb));
    node.setData(diagram::kTextFrameLineColorRole, lineColor.name(QColor::HexRgb));
    node.setData(diagram::kTextFrameFillColorRole, fillColor.name(QColor::HexRgb));
    node.setData(diagram::kTextFrameFontFamilyRole, fontFamily);
    node.setData(diagram::kTextFrameFontSizeRole, fontSize);
    return params;
}

auto selectedTextFrameNode(QSchematic::Scene* scene) -> std::shared_ptr<QSchematic::Items::Node>
{
    if (!scene) {
        return {};
    }

    for (const auto& node : scene->items<QSchematic::Items::Node>()) {
        if (!node || !node->isSelected()) {
            continue;
        }
        if (node->data(diagram::kNodeTypeRole).toString() == QStringLiteral("TextFrame")) {
            return node;
        }
    }

    return {};
}

auto nextTextFrameId(const QSchematic::Scene& scene) -> QString
{
    int maxSuffix = 0;
    for (const auto& node : scene.items<QSchematic::Items::Node>()) {
        if (!node) {
            continue;
        }
        const auto id = node->data(diagram::kNodeIdRole).toString();
        if (!id.startsWith(QStringLiteral("text_frame_"))) {
            continue;
        }
        bool ok = false;
        const int suffix = id.mid(QStringLiteral("text_frame_").size()).toInt(&ok);
        if (ok) {
            maxSuffix = std::max(maxSuffix, suffix);
        }
    }
    return QStringLiteral("text_frame_%1").arg(maxSuffix + 1);
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
    shuttingDown_ = true;
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
    const QColor drawIconColor = palette().color(QPalette::ButtonText);
    modeNormalAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/pointer.svg"), drawIconColor));

    modeWireAction_ = new QAction("Wire Mode", this);
    modeWireAction_->setCheckable(true);
    modeWireAction_->setText(QStringLiteral("Wire Draw"));
    modeWireAction_->setToolTip("Toggle wire drawing mode");
    modeWireAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    modeWireAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/vector-bezier-2.svg"), drawIconColor));

    addTextFrameAction_ = new QAction("Text Frame", this);
    addTextFrameAction_->setToolTip("Insert a diagram-only text frame");
    addTextFrameAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/text-caption.svg"), drawIconColor));

    textColorAction_ = new QAction("Text Color", this);
    textColorAction_->setToolTip("Set selected text frame text color");
    textColorAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/text-color.svg"), drawIconColor));
    lineColorAction_ = new QAction("Line Color", this);
    lineColorAction_->setToolTip("Set selected text frame border color");
    lineColorAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/border-style-2.svg"), drawIconColor));
    fillColorAction_ = new QAction("Fill Color", this);
    fillColorAction_->setToolTip("Set selected text frame background color");
    fillColorAction_->setIcon(themedToolbarSvgIcon(QStringLiteral(":/icons/tabler/bucket.svg"), drawIconColor));

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
    connect(fitDiagramAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    connect(runAction_, &QAction::triggered, this, [this] {
        configureRun(true);
    });
    connect(runParametersAction_, &QAction::triggered, this, &MainWindow::openRunParametersDialog);
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
    connect(modeWireAction_, &QAction::toggled, this, [this](const bool checked) {
        setDiagramMode(checked ? QSchematic::Scene::WireMode : QSchematic::Scene::NormalMode);
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

    ui::MainWindowBuilder::buildMenuAndToolbar(this, actions);

    diagramDrawToolBar_ = addToolBar(QStringLiteral("Diagram Drawing"));
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

    if (dtSpin_) {
        discretizationFrequencyHz_ = 1.0 / std::max(dtSpin_->value(), 1e-12);
        connect(dtSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](const double dtSeconds) {
            if (dtSeconds <= 0.0) {
                return;
            }
            applyDiscretizationFrequency(1.0 / dtSeconds);
        });
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
    elementPickerDock_ = new QDockWidget(QStringLiteral("Element Library"), this);
    elementPickerDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    elementPickerDock_->setWidget(elementPickerPanel_);
    addDockWidget(Qt::RightDockWidgetArea, elementPickerDock_);

    connect(elementPickerPanel_, &ui::ElementPickerPanel::elementRequested,
            this, &MainWindow::onElementRequested);

    if (pluginLoader_) {
        appendLog(QStringLiteral("[studio] plugins loaded: %1").arg(pluginLoader_->loaded_count()));
    }

    connectSceneSignals();
    syncPageActions();
    syncTextFrameStyleControls();
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
    const auto* descriptor = s7::ElementRegistry::instance().find(elementType.toStdString());
    const auto* schemaDefinition = descriptor && descriptor->metadata().schema_definition.has_value()
        ? &descriptor->metadata().schema_definition.value()
        : nullptr;

    boost::json::object elementObj;
    elementObj["id"] = elementId.toStdString();
    elementObj["type"] = elementType.toStdString();
    elementObj["params"] = boost::json::object{};
    if (schemaDefinition) {
        elementObj["schema"] = *schemaDefinition;
    }
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
            if (schemaDefinition) {
                nodeJson["schema"] = *schemaDefinition;
            }
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
        progressLabel_->setText(total < 0
            ? QString("%1 / continuous").arg(completed)
            : QString("%1 / %2").arg(completed).arg(total));
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
    if (elementType == QStringLiteral("TextFrame")) {
        if (!diagramEditor_ || !diagramEditor_->scene()) {
            return;
        }

        for (const auto& node : diagramEditor_->scene()->items<QSchematic::Items::Node>()) {
            if (!node || node->data(diagram::kNodeIdRole).toString() != elementId) {
                continue;
            }

            auto params = parseEncodedParams(node->data(diagram::kNodeParamsRole).toString());
            const auto defaults = mergedTextFrameParams(params);
            bool accepted = false;
            const QString editedText = QInputDialog::getMultiLineText(
                this,
                QStringLiteral("Edit Text Frame"),
                QStringLiteral("Text"),
                QString::fromStdString(json_utils::stringValue(defaults, "text", "Text frame")),
                &accepted);
            if (!accepted) {
                return;
            }

            params["text"] = editedText.toStdString();
            applyTextFrameVisuals(*node, params);
            appendLog(QStringLiteral("[studio] updated text frame: %1").arg(elementId));
            syncTextFrameStyleControls();
            return;
        }
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
                    const int updatedChannels = std::max(1, json_utils::intValue(paramsObject, "channel_count", channelCount));
                    nodePtr->setData(diagram::kNodeChannelCountRole, updatedChannels);
                    if (const auto window = scopeWindows_.value(elementId); window && window->channelCount() != updatedChannels) {
                        window->setChannelCount(updatedChannels);
                        replayScopeSamples(elementId, *window);
                    }
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
                diagramEditor_->loadDocument(document);
            } else {
                diagramEditor_->loadRuntimeSchema(runtimeSchema_);
            }
            syncPageActions();
            syncTextFrameStyleControls();
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

void MainWindow::connectSceneSignals()
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        return;
    }

    connect(diagramEditor_->scene(), &QSchematic::Scene::modeChanged, this, [this](int mode) {
        if (modeWireAction_) {
            const QSignalBlocker blockerWire(modeWireAction_);
            modeWireAction_->setChecked(mode == QSchematic::Scene::WireMode);
        }
    });

    connect(diagramEditor_->scene(), &QGraphicsScene::selectionChanged,
            this, &MainWindow::syncTextFrameStyleControls,
            Qt::UniqueConnection);
}

void MainWindow::addTextFrame()
{
    if (!diagramEditor_ || !diagramEditor_->scene()) {
        return;
    }

    auto* scene = diagramEditor_->scene();
    auto node = std::make_shared<QSchematic::Items::Node>();
    node->setAllowMouseRotate(false);
    node->setConnectorsMovable(false);
    node->setPos(suggestedNewNodePosition(*scene));

    const auto frameId = nextTextFrameId(*scene);
    node->setData(diagram::kNodeIdRole, frameId);
    node->setData(diagram::kNodeTitleRole, QStringLiteral("Text frame"));

    applyTextFrameVisuals(*node, defaultTextFrameParams());
    scene->addItem(node);

    for (const auto& item : scene->selectedItems()) {
        if (item) {
            item->setSelected(false);
        }
    }
    node->setSelected(true);
    setDiagramMode(QSchematic::Scene::NormalMode);
    syncTextFrameStyleControls();
    appendLog(QStringLiteral("[studio] added diagram text frame: %1").arg(frameId));
}

void MainWindow::setSelectedTextFrameTextColor()
{
    auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!node) {
        return;
    }

    auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "text_color", "#202020")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select text color"));
    if (!selected.isValid()) {
        return;
    }

    params["text_color"] = selected.name(QColor::HexRgb).toStdString();
    applyTextFrameVisuals(*node, params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameLineColor()
{
    auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!node) {
        return;
    }

    auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "line_color", "#444444")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select line color"));
    if (!selected.isValid()) {
        return;
    }

    params["line_color"] = selected.name(QColor::HexRgb).toStdString();
    applyTextFrameVisuals(*node, params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameFillColor()
{
    auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!node) {
        return;
    }

    auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
    const auto current = QColor(QString::fromStdString(json_utils::stringValue(params, "fill_color", "#fff4cc")));
    const auto selected = QColorDialog::getColor(current, this, QStringLiteral("Select fill color"));
    if (!selected.isValid()) {
        return;
    }

    params["fill_color"] = selected.name(QColor::HexRgb).toStdString();
    applyTextFrameVisuals(*node, params);
    syncTextFrameStyleControls();
}

void MainWindow::setSelectedTextFrameFont(const QFont& font)
{
    auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!node) {
        return;
    }

    auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
    params["font_family"] = font.family().toStdString();
    applyTextFrameVisuals(*node, params);
}

void MainWindow::setSelectedTextFrameFontSize(const int pointSize)
{
    auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    if (!node) {
        return;
    }

    auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
    params["font_size"] = std::max(6, pointSize);
    applyTextFrameVisuals(*node, params);
}

void MainWindow::syncTextFrameStyleControls()
{
    const auto node = selectedTextFrameNode(diagramEditor_ ? diagramEditor_->scene() : nullptr);
    const bool enabled = static_cast<bool>(node);

    if (textColorAction_) textColorAction_->setEnabled(enabled);
    if (lineColorAction_) lineColorAction_->setEnabled(enabled);
    if (fillColorAction_) fillColorAction_->setEnabled(enabled);
    if (textFrameFontCombo_) textFrameFontCombo_->setEnabled(enabled);
    if (textFrameFontSizeSpin_) textFrameFontSizeSpin_->setEnabled(enabled);

    if (!enabled) {
        return;
    }

    const auto params = mergedTextFrameParams(parseEncodedParams(node->data(diagram::kNodeParamsRole).toString()));
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

void MainWindow::openScopeWindow(const QString& scopeId, const int channelCount)
{
    if (scopeId.isEmpty()) {
        return;
    }

    if (const auto existing = scopeWindows_.value(scopeId)) {
        if (existing->channelCount() != channelCount) {
            existing->setChannelCount(channelCount);
        }
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
    if (shuttingDown_) {
        QMainWindow::closeEvent(event);
        return;
    }

    shuttingDown_ = true;
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
    if (clearScopesBeforeRun_) {
        clearScopeSamples();
    }

    const double dtSeconds = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);
    if (diagramEditor_ && diagramEditor_->scene()) {
        diagram::SerializationOptions options;
        options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("untitled"));
        options.dt = dtSeconds;
        const auto pageLayout = diagramEditor_->pageLayout();
        options.sheetOrientation = pageLayout.orientation;
        options.sheetWidthMm = pageLayout.widthMm;
        options.sheetHeightMm = pageLayout.heightMm;

        diagram::SceneSerializer serializer;
        const auto document = serializer.serialize(*diagramEditor_->scene(), options);
        runtimeSchema_ = document.runtimeSchema;
        updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
    }

    controller_->run(runtimeSchema_, dtSeconds, ui::RunExecutionConfig{runtimeSeconds_});
    refreshActions();
}

void MainWindow::applyDiscretizationFrequency(const double frequencyHz)
{
    if (frequencyHz <= 0.0) {
        return;
    }

    discretizationFrequencyHz_ = frequencyHz;
    const double dtSeconds = 1.0 / std::max(discretizationFrequencyHz_, 1e-12);
    if (dtSpin_) {
        const QSignalBlocker blocker(dtSpin_);
        dtSpin_->setValue(dtSeconds);
    }

    if (diagramEditor_ && diagramEditor_->scene()) {
        diagram::SerializationOptions options;
        options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("untitled"));
        options.dt = dtSeconds;
        const auto pageLayout = diagramEditor_->pageLayout();
        options.sheetOrientation = pageLayout.orientation;
        options.sheetWidthMm = pageLayout.widthMm;
        options.sheetHeightMm = pageLayout.heightMm;

        diagram::SceneSerializer serializer;
        auto document = serializer.serialize(*diagramEditor_->scene(), options);
        runtimeSchema_ = document.runtimeSchema;
        updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
        document.runtimeSchema = runtimeSchema_;
        diagramEditor_->loadDocument(document);
        syncPageActions();
        syncTextFrameStyleControls();
        return;
    }

    updateSchemaDiscretizationParams(runtimeSchema_, discretizationFrequencyHz_, dtSeconds);
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
        syncTextFrameStyleControls();
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
        const QSignalBlocker blocker(dtSpin_);
        dtSpin_->setValue(document.dt);
        discretizationFrequencyHz_ = 1.0 / document.dt;
    }

    if (diagramEditor_) {
        diagramEditor_->loadDocument(document);
        syncPageActions();
        syncTextFrameStyleControls();
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

    diagram::SerializationOptions options;
    options.schemaName = schemaDisplayName(runtimeSchema_, QStringLiteral("schema_library_element"));
    options.dt = dtSpin_ ? dtSpin_->value() : 0.001;
    if (diagramEditor_) {
        const auto pageLayout = diagramEditor_->pageLayout();
        options.sheetOrientation = pageLayout.orientation;
        options.sheetWidthMm = pageLayout.widthMm;
        options.sheetHeightMm = pageLayout.heightMm;
    }

    diagram::SceneSerializer serializer;
    const auto document = serializer.serialize(*diagramEditor_->scene(), options);
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

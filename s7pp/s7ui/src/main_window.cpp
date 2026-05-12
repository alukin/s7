#include "main_window.hpp"

#include "elements/basic_node_item.hpp"
#include "elements/sink_node_item.hpp"
#include "remote_nodes_widget.hpp"
#include "schema_compiler.hpp"
#include "schema_session.hpp"

#include <s7factory.hpp>

#include <boost/json.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/view.hpp>

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHash>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QKeySequence>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr int kRoleTypeName = Qt::UserRole + 1;
constexpr int kRoleCategory = Qt::UserRole + 2;
constexpr int kRoleInputPorts = Qt::UserRole + 3;
constexpr int kRoleOutputPorts = Qt::UserRole + 4;
constexpr int kRoleVisualSink = Qt::UserRole + 5;
constexpr int kRoleNodeId = Qt::UserRole + 10;
constexpr int kRoleNodeType = Qt::UserRole + 11;
constexpr int kRoleNodeName = Qt::UserRole + 12;
constexpr int kRolePaperOverlay = Qt::UserRole + 200;

constexpr double kPaperMarginMm = 10.0;
constexpr double kTitleBlockWidthMm = 90.0;
constexpr double kTitleBlockHeightMm = 28.0;

template <typename T>
auto jsonValueOr(const boost::json::object& obj, const char* key, T defaultValue) -> T
{
    if (const auto* value = obj.if_contains(key))
    {
        return boost::json::value_to<T>(*value);
    }
    return defaultValue;
}

auto jsonContains(const boost::json::value& root, const char* key) -> bool
{
    return root.is_object() && root.as_object().if_contains(key);
}

auto jsonPoint(const QPointF& point) -> boost::json::value;
auto pointFromJson(const boost::json::value& value) -> QPointF;

auto defaultScenePosForIndex(const int index) -> QPointF
{
    return {static_cast<qreal>(20.0 + (index % 6) * 70.0),
            static_cast<qreal>(20.0 + (index / 6) * 50.0)};
}

struct SheetSizeMm
{
    double width = 297.0;
    double height = 210.0;
    const char* label = "A4 landscape";
};

auto sheetSizeForPreset(const MainWindow::SheetPreset preset) -> SheetSizeMm
{
    switch (preset)
    {
    case MainWindow::SheetPreset::A1Landscape:
        return SheetSizeMm{841.0, 594.0, "A1 landscape"};
    case MainWindow::SheetPreset::A2Landscape:
        return SheetSizeMm{594.0, 420.0, "A2 landscape"};
    case MainWindow::SheetPreset::A3Landscape:
        return SheetSizeMm{420.0, 297.0, "A3 landscape"};
    case MainWindow::SheetPreset::A4Landscape:
    default:
        return SheetSizeMm{297.0, 210.0, "A4 landscape"};
    }
}

auto sheetStandardName(const MainWindow::SheetPreset preset) -> const char*
{
    switch (preset)
    {
    case MainWindow::SheetPreset::A1Landscape: return "A1";
    case MainWindow::SheetPreset::A2Landscape: return "A2";
    case MainWindow::SheetPreset::A3Landscape: return "A3";
    case MainWindow::SheetPreset::A4Landscape:
    default: return "A4";
    }
}

auto sheetPresetFromStandard(const QString& standard) -> MainWindow::SheetPreset
{
    const auto s = standard.trimmed().toUpper();
    if (s == "A1") return MainWindow::SheetPreset::A1Landscape;
    if (s == "A2") return MainWindow::SheetPreset::A2Landscape;
    if (s == "A3") return MainWindow::SheetPreset::A3Landscape;
    return MainWindow::SheetPreset::A4Landscape;
}

struct ConnectorEndpoint
{
    QString nodeId;
    QString connectorName;

    static auto fromJson(const boost::json::value& value) -> ConnectorEndpoint
    {
        const auto& obj = value.as_object();
        return ConnectorEndpoint{
            .nodeId = QString::fromStdString(boost::json::value_to<std::string>(obj.at("node"))),
            .connectorName = QString::fromStdString(boost::json::value_to<std::string>(obj.at("connector")))};
    }

    auto toJson() const -> boost::json::object
    {
        return boost::json::object{{"node", nodeId.toStdString()}, {"connector", connectorName.toStdString()}};
    }
};

struct LoadedNodeSpec
{
    QString id;
    QString typeName;
    QString name;
    QPointF position;
    boost::json::value params = boost::json::object();
    int inputPorts = -1;
    int outputPorts = -1;
    bool visualSink = false;

    static auto fromJson(const boost::json::value& nodeJson) -> LoadedNodeSpec
    {
        const auto& nodeObj = nodeJson.as_object();
        LoadedNodeSpec spec;
        spec.id = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "id", std::string{}));
        spec.typeName = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "type", std::string{"Constant"}));
        spec.name = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "name", spec.typeName.toStdString()));
        if (const auto* inputPorts = nodeObj.if_contains("input_ports")) {
            spec.inputPorts = boost::json::value_to<int>(*inputPorts);
        } else if (const auto* inputs = nodeObj.if_contains("inputs")) {
            spec.inputPorts = boost::json::value_to<int>(*inputs);
        }
        if (const auto* outputPorts = nodeObj.if_contains("output_ports")) {
            spec.outputPorts = boost::json::value_to<int>(*outputPorts);
        } else if (const auto* outputs = nodeObj.if_contains("outputs")) {
            spec.outputPorts = boost::json::value_to<int>(*outputs);
        }
        const bool isVisualType = (spec.typeName == "ScopeSink" || spec.typeName == "ComplexScopeSink");
        spec.visualSink = jsonValueOr<bool>(nodeObj, "visual", isVisualType);
        if (const auto* position = nodeObj.if_contains("position")) {
            spec.position = pointFromJson(*position);
        } else {
            spec.position = {jsonValueOr<double>(nodeObj, "x", 0.0), jsonValueOr<double>(nodeObj, "y", 0.0)};
        }
        if (const auto* params = nodeObj.if_contains("params")) {
            spec.params = *params;
        }
        return spec;
    }

    auto toJson() const -> boost::json::object
    {
        boost::json::object obj;
        obj["id"] = id.toStdString();
        obj["type"] = typeName.toStdString();
        obj["name"] = name.toStdString();
        obj["visual"] = visualSink;
        obj["position"] = jsonPoint(position);
        if (inputPorts >= 0) obj["input_ports"] = inputPorts;
        if (outputPorts >= 0) obj["output_ports"] = outputPorts;
        obj["params"] = params;
        return obj;
    }
};

struct LoadedWireSpec
{
    QString name;
    ConnectorEndpoint from;
    ConnectorEndpoint to;
    QVector<QPointF> points;

    static auto fromJson(const boost::json::value& wireJson) -> LoadedWireSpec
    {
        const auto& wireObj = wireJson.as_object();
        LoadedWireSpec spec;
        spec.name = QString::fromStdString(jsonValueOr<std::string>(wireObj, "name", std::string{}));
        spec.from = ConnectorEndpoint::fromJson(wireObj.at("from"));
        spec.to = ConnectorEndpoint::fromJson(wireObj.at("to"));
        if (const auto* points = wireObj.if_contains("points")) {
            if (!points->is_array()) {
                throw std::runtime_error("Schema wire field 'points' must be an array");
            }
            for (const auto& pointJson : points->as_array()) {
                spec.points.push_back(pointFromJson(pointJson));
            }
        }
        return spec;
    }

    auto toJson() const -> boost::json::object
    {
        boost::json::object obj;
        obj["name"] = name.toStdString();
        obj["from"] = from.toJson();
        obj["to"] = to.toJson();
        boost::json::array pts;
        for (const auto& p : points) {
            pts.push_back(jsonPoint(p));
        }
        obj["points"] = std::move(pts);
        return obj;
    }
};

auto jsonPoint(const QPointF& point) -> boost::json::value
{
    return boost::json::object{{"x", point.x()}, {"y", point.y()}};
}

auto pointFromJson(const boost::json::value& value) -> QPointF
{
    if (value.is_array() && value.as_array().size() >= 2)
    {
        const auto& arr = value.as_array();
        return {
            boost::json::value_to<double>(arr.at(0)),
            boost::json::value_to<double>(arr.at(1))};
    }

    if (value.is_object())
    {
        const auto& obj = value.as_object();
        return {
            jsonValueOr<double>(obj, "x", 0.0),
            jsonValueOr<double>(obj, "y", 0.0)};
    }

    throw std::runtime_error("Point value must be an array [x, y] or an object with x/y fields");
}

auto endpointFromJson(const boost::json::value& value) -> ConnectorEndpoint
{
    return ConnectorEndpoint::fromJson(value);
}

auto nextCreatedNodeCounterFromId(const QString& id) -> int
{
    const auto suffix = id.section('_', -1);
    bool ok = false;
    const int parsed = suffix.toInt(&ok);
    return ok ? parsed : 0;
}

auto connectorIsOutput(const QString& connectorName) -> bool
{
    return connectorName.startsWith("out");
}

auto findConnectorByName(const std::shared_ptr<QSchematic::Items::Node>& node, const QString& connectorName)
    -> std::shared_ptr<QSchematic::Items::Connector>
{
    if (!node)
    {
        return nullptr;
    }

    for (const auto& connector : node->connectors())
    {
        if (connector && connector->text() == connectorName)
        {
            return connector;
        }
    }
    return nullptr;
}

auto findFirstConnector(const std::shared_ptr<QSchematic::Items::Node>& node, const bool output)
    -> std::shared_ptr<QSchematic::Items::Connector>
{
    if (!node)
    {
        return nullptr;
    }

    for (const auto& connector : node->connectors())
    {
        if (connector && connectorIsOutput(connector->text()) == output)
        {
            return connector;
        }
    }
    return nullptr;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    scene_ = new QSchematic::Scene(this);
    QSchematic::Settings cadSettings;
    cadSettings.gridSize = 5;           // 5 mm grid spacing
    cadSettings.gridPointSize = 2;
    cadSettings.showGrid = true;
    scene_->setSettings(cadSettings);

    // Connect scene mode change to update wire mode action
    connect(scene_, QOverload<int>::of(&QSchematic::Scene::modeChanged),
            this, &MainWindow::onSceneModeChanged);

    runStatusTimer_ = new QTimer(this);
    runStatusTimer_->setInterval(200); // 5 Hz
    connect(runStatusTimer_, &QTimer::timeout, this, &MainWindow::onRunStatusPoll);

    createActions();
    createMenus();
    createToolbars();
    createCentralEditor();
    applySheetPreset(sheetPreset_);
    createDocks();
    populatePalette();
    // Start with a blank canvas — no seed schema loaded.
    updateModelActionStates();
    setModelStatus("idle");
    updateWindowTitle();

    // Set a comfortable initial zoom centered at the canvas origin.
    if (view_)
    {
        view_->setZoomValue(1.0);
        view_->centerOn(0.0, 0.0);
    }

    resize(1600, 950);
    appendLog("[s7ui] Ready — blank canvas");
}

void MainWindow::createActions()
{
    actionOpenSchema_ = new QAction("Open Schema...", this);
    actionOpenSchema_->setShortcut(QKeySequence::Open);
    connect(actionOpenSchema_, &QAction::triggered, this, &MainWindow::loadSchema);

    actionSaveSchema_ = new QAction("Save Schema", this);
    actionSaveSchema_->setShortcut(QKeySequence::Save);
    connect(actionSaveSchema_, &QAction::triggered, this, &MainWindow::saveSchema);

    actionSaveSchemaAs_ = new QAction("Save Schema As...", this);
    actionSaveSchemaAs_->setShortcut(QKeySequence::SaveAs);
    connect(actionSaveSchemaAs_, &QAction::triggered, this, &MainWindow::saveSchemaAs);

    actionAddNode_ = new QAction("Add Node", this);
    connect(actionAddNode_, &QAction::triggered, this, &MainWindow::addSelectedPaletteNode);

    actionFitView_ = new QAction("Fit View", this);
    connect(actionFitView_, &QAction::triggered, [this] {
        if (view_)
        {
            view_->fitInView();
        }
    });

    actionToggleWireMode_ = new QAction("Wire Mode", this);
    actionToggleWireMode_->setShortcut(Qt::CTRL | Qt::Key_W);
    actionToggleWireMode_->setCheckable(true);
    actionToggleWireMode_->setToolTip("Toggle wire drawing mode (Ctrl+W)");
    connect(actionToggleWireMode_, &QAction::triggered, this, &MainWindow::toggleWireMode);

    actionRunModel_ = new QAction("Run", this);
    connect(actionRunModel_, &QAction::triggered, this, &MainWindow::runSchemaModel);

    actionPauseModel_ = new QAction("Pause", this);
    connect(actionPauseModel_, &QAction::triggered, this, &MainWindow::pauseSchemaModel);

    actionResumeModel_ = new QAction("Resume", this);
    connect(actionResumeModel_, &QAction::triggered, this, &MainWindow::resumeSchemaModel);

    actionStopModel_ = new QAction("Stop", this);
    connect(actionStopModel_, &QAction::triggered, this, &MainWindow::stopSchemaModel);

    actionResetModel_ = new QAction("Reset", this);
    connect(actionResetModel_, &QAction::triggered, this, &MainWindow::resetSchemaModel);

    createSheetActions();
}

void MainWindow::createSheetActions()
{
    auto* sheetGroup = new QActionGroup(this);
    sheetGroup->setExclusive(true);

    actionSheetA4_ = new QAction("A4 landscape (297 x 210 mm)", this);
    actionSheetA4_->setCheckable(true);
    actionSheetA4_->setChecked(true);
    sheetGroup->addAction(actionSheetA4_);
    connect(actionSheetA4_, &QAction::triggered, this, [this] { applySheetPreset(SheetPreset::A4Landscape); });

    actionSheetA3_ = new QAction("A3 landscape (420 x 297 mm)", this);
    actionSheetA3_->setCheckable(true);
    sheetGroup->addAction(actionSheetA3_);
    connect(actionSheetA3_, &QAction::triggered, this, [this] { applySheetPreset(SheetPreset::A3Landscape); });

    actionSheetA2_ = new QAction("A2 landscape (594 x 420 mm)", this);
    actionSheetA2_->setCheckable(true);
    sheetGroup->addAction(actionSheetA2_);
    connect(actionSheetA2_, &QAction::triggered, this, [this] { applySheetPreset(SheetPreset::A2Landscape); });

    actionSheetA1_ = new QAction("A1 landscape (841 x 594 mm)", this);
    actionSheetA1_->setCheckable(true);
    sheetGroup->addAction(actionSheetA1_);
    connect(actionSheetA1_, &QAction::triggered, this, [this] { applySheetPreset(SheetPreset::A1Landscape); });
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(actionOpenSchema_);
    fileMenu->addAction(actionSaveSchema_);
    fileMenu->addAction(actionSaveSchemaAs_);

    auto* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(actionToggleWireMode_);

    auto* insertMenu = menuBar()->addMenu("&Insert");
    insertMenu->addAction(actionAddNode_);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(actionFitView_);
    auto* sheetMenu = viewMenu->addMenu("Sheet Size (mm)");
    sheetMenu->addAction(actionSheetA4_);
    sheetMenu->addAction(actionSheetA3_);
    sheetMenu->addAction(actionSheetA2_);
    sheetMenu->addAction(actionSheetA1_);

    auto* modelMenu = menuBar()->addMenu("&Model");
    modelMenu->addAction(actionRunModel_);
    modelMenu->addAction(actionPauseModel_);
    modelMenu->addAction(actionResumeModel_);
    modelMenu->addAction(actionStopModel_);
    modelMenu->addAction(actionResetModel_);
}

void MainWindow::createToolbars()
{
    auto* toolbar = addToolBar("Main");
    toolbar->addAction(actionAddNode_);
    toolbar->addAction(actionFitView_);
    toolbar->addSeparator();
    toolbar->addAction(actionToggleWireMode_);

    auto* modelToolbar = addToolBar("Model");
    modelToolbar->addAction(actionRunModel_);
    modelToolbar->addAction(actionPauseModel_);
    modelToolbar->addAction(actionResumeModel_);
    modelToolbar->addAction(actionStopModel_);
    modelToolbar->addAction(actionResetModel_);

    runHoursSpin_ = new QSpinBox(modelToolbar);
    runHoursSpin_->setRange(0, 23);
    runHoursSpin_->setValue(0);
    runHoursSpin_->setToolTip("Run hours");
    auto* hoursAction = new QWidgetAction(modelToolbar);
    hoursAction->setDefaultWidget(runHoursSpin_);
    modelToolbar->addAction(hoursAction);

    runMinutesSpin_ = new QSpinBox(modelToolbar);
    runMinutesSpin_->setRange(0, 59);
    runMinutesSpin_->setValue(1);
    runMinutesSpin_->setToolTip("Run minutes");
    auto* minutesAction = new QWidgetAction(modelToolbar);
    minutesAction->setDefaultWidget(runMinutesSpin_);
    modelToolbar->addAction(minutesAction);

    runSecondsSpin_ = new QSpinBox(modelToolbar);
    runSecondsSpin_->setRange(0, 59);
    runSecondsSpin_->setValue(0);
    runSecondsSpin_->setToolTip("Run seconds");
    auto* secondsAction = new QWidgetAction(modelToolbar);
    secondsAction->setDefaultWidget(runSecondsSpin_);
    modelToolbar->addAction(secondsAction);

    modelStatusLabel_ = new QLabel("model: idle", modelToolbar);
    modelStatusLabel_->setMinimumWidth(220);
    auto* statusAction = new QWidgetAction(modelToolbar);
    statusAction->setDefaultWidget(modelStatusLabel_);
    modelToolbar->addAction(statusAction);
}

void MainWindow::createCentralEditor()
{
    view_ = new QSchematic::View(this);
    view_->setScene(scene_);
    view_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCentralWidget(view_);
}

void MainWindow::createDocks()
{
    auto* paletteDock = new QDockWidget("S7 Node Palette", this);
    auto* paletteHost = new QWidget(paletteDock);
    auto* paletteLayout = new QVBoxLayout(paletteHost);

    paletteTree_ = new QTreeWidget(paletteHost);
    paletteTree_->setHeaderLabel("Node Types");
    paletteTree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    connect(paletteTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item && item->childCount() == 0)
        {
            addSelectedPaletteNode();
        }
    });

    auto* addNodeButton = new QPushButton("Add Selected Node", paletteHost);
    connect(addNodeButton, &QPushButton::clicked, this, &MainWindow::addSelectedPaletteNode);

    paletteLayout->addWidget(paletteTree_);
    paletteLayout->addWidget(addNodeButton);
    paletteDock->setWidget(paletteHost);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    auto* remoteDock = new QDockWidget("Remote Models", this);
    remoteNodes_ = new RemoteNodesWidget(remoteDock);
    connect(remoteNodes_, &RemoteNodesWidget::logMessage, this, &MainWindow::appendLog);
    remoteDock->setWidget(remoteNodes_);
    addDockWidget(Qt::RightDockWidgetArea, remoteDock);

    auto* logDock = new QDockWidget("Runtime Log", this);
    logView_ = new QTextEdit(logDock);
    logView_->setReadOnly(true);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    auto* schemaDock = new QDockWidget("Schema Properties", this);
    auto* schemaHost = new QWidget(schemaDock);
    auto* schemaForm = new QFormLayout(schemaHost);

    schemaNameEdit_ = new QLineEdit(schemaName_, schemaHost);
    schemaDtSpin_ = new QDoubleSpinBox(schemaHost);
    schemaDtSpin_->setDecimals(9);
    schemaDtSpin_->setRange(1e-9, 10.0);
    schemaDtSpin_->setSingleStep(0.0001);
    schemaDtSpin_->setValue(0.001);

    schemaForm->addRow("Name", schemaNameEdit_);
    schemaForm->addRow("Sampling Interval (dt)", schemaDtSpin_);

    connect(schemaNameEdit_, &QLineEdit::editingFinished, this, &MainWindow::schemaPropertiesEdited);
    connect(schemaDtSpin_, &QDoubleSpinBox::editingFinished, this, &MainWindow::schemaPropertiesEdited);

    schemaDock->setWidget(schemaHost);
    addDockWidget(Qt::RightDockWidgetArea, schemaDock);
}

void MainWindow::populatePalette()
{
    ensureNodeLibrariesRegistered();
    ensureVisualCatalogLoaded();

    paletteTree_->clear();

    QMap<QString, QTreeWidgetItem*> roots;

    // Build palette from TypeDescriptors in the registry.
    const auto descriptors = s7::ElementRegistry::instance().allDescriptors();
    for (const auto& desc : descriptors)
    {
        if (desc.hidden) continue;
        if (desc.category == "Internal") continue;

        const QString cat = QString::fromStdString(desc.category);
        if (cat.isEmpty()) continue;

        if (!roots.contains(cat))
        {
            auto* root = new QTreeWidgetItem();
            root->setText(0, cat);
            roots.insert(cat, root);
            paletteTree_->addTopLevelItem(root);
        }

        const QString typeName = QString::fromStdString(desc.type_name);
        const auto visualSpec = visualCatalog_.specForType(typeName);
        const QString displayName = (visualSpec && !visualSpec->displayName.trimmed().isEmpty())
            ? visualSpec->displayName
            : typeName;
        QString prefix = typeName.toLower();
        prefix.remove(' ');

        auto* child = new QTreeWidgetItem();
        child->setText(0, displayName);
        const QString visualHint = (visualSpec ? QString("\nshape: %1").arg(visualSpec->shape) : QString());
        child->setToolTip(0, QString::fromStdString(desc.description) + visualHint);
        child->setData(0, kRoleTypeName, typeName);
        child->setData(0, kRoleCategory, prefix);
        child->setData(0, kRoleInputPorts, desc.default_input_count);
        child->setData(0, kRoleOutputPorts, desc.default_output_count);
        child->setData(0, kRoleVisualSink, false);
        roots[cat]->addChild(child);
    }

    // Always add the visual ScopeSink under the "Visual" category.
    {
        const QString cat = "Visual";
        if (!roots.contains(cat))
        {
            auto* root = new QTreeWidgetItem();
            root->setText(0, cat);
            roots.insert(cat, root);
            paletteTree_->addTopLevelItem(root);
        }
        auto* child = new QTreeWidgetItem();
        child->setText(0, "ScopeSink");
        child->setToolTip(0, "Oscilloscope-style visual sink");
        child->setData(0, kRoleTypeName, QString("ScopeSink"));
        child->setData(0, kRoleCategory, QString("scope"));
        child->setData(0, kRoleInputPorts, 2);
        child->setData(0, kRoleOutputPorts, 0);
        child->setData(0, kRoleVisualSink, true);
        roots[cat]->addChild(child);

        auto* complexChild = new QTreeWidgetItem();
        complexChild->setText(0, "ComplexScopeSink");
        complexChild->setToolTip(0, "Complex oscilloscope sink (Re/Im)");
        complexChild->setData(0, kRoleTypeName, QString("ComplexScopeSink"));
        complexChild->setData(0, kRoleCategory, QString("complex_scope"));
        complexChild->setData(0, kRoleInputPorts, 1);
        complexChild->setData(0, kRoleOutputPorts, 0);
        complexChild->setData(0, kRoleVisualSink, true);
        roots[cat]->addChild(complexChild);
    }

    paletteTree_->expandAll();
    if (paletteTree_->topLevelItemCount() > 0 && paletteTree_->topLevelItem(0)->childCount() > 0)
    {
        paletteTree_->setCurrentItem(paletteTree_->topLevelItem(0)->child(0));
    }
}

void MainWindow::ensureVisualCatalogLoaded()
{
    if (visualCatalogLoaded_)
    {
        return;
    }

    const QString catalogDir = QStringLiteral(S7UI_SOURCE_DIR "/s7ui/visuals");
    visualCatalog_.loadFromDir(catalogDir, [this](const QString& msg) {
        appendLog(msg);
    });
    visualCatalogLoaded_ = true;
}

void MainWindow::seedStarterScene()
{
    const QString startupPath = QStringLiteral(S7UI_SOURCE_DIR "/s7ui/resources/oscillator_agreement.json");
    try
    {
        QFile file(startupPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            throw std::runtime_error(QString("Cannot open startup schema: %1").arg(startupPath).toStdString());
        }

        const QByteArray data = file.readAll();
        const auto root = boost::json::parse(std::string(data.constData(), static_cast<std::size_t>(data.size())));
        loadSchemaFromJson(root, startupPath);
        appendLog(QString("[schema] startup schema loaded from %1").arg(startupPath));
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[schema] startup schema load failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadSchema()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(currentSchemaPath_).absolutePath();

    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Schema",
        startDir,
        "Schema Files (*.json);;All Files (*)");
    if (path.isEmpty())
    {
        return;
    }

    try
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            throw std::runtime_error(QString("Cannot open file: %1").arg(path).toStdString());
        }

        const QByteArray data = file.readAll();
        const auto root = boost::json::parse(std::string(data.constData(), static_cast<std::size_t>(data.size())));
        loadSchemaFromJson(root, path);
    }
    catch (const std::exception& ex)
    {
        QMessageBox::warning(this, "Open Schema Failed", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::saveSchema()
{
    if (currentSchemaPath_.isEmpty())
    {
        saveSchemaAs();
        return;
    }

    if (saveSchemaToFile(currentSchemaPath_))
    {
        appendLog(QString("[schema] saved to %1").arg(currentSchemaPath_));
    }
}

void MainWindow::saveSchemaAs()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(currentSchemaPath_).absolutePath();

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save Schema As",
        startDir,
        "Schema Files (*.json);;All Files (*)");
    if (path.isEmpty())
    {
        return;
    }

    if (saveSchemaToFile(path))
    {
        appendLog(QString("[schema] saved to %1").arg(path));
    }
}

bool MainWindow::saveSchemaToFile(const QString& path)
{
    try
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            throw std::runtime_error(QString("Cannot open file for writing: %1").arg(path).toStdString());
        }

        const auto root = buildSchemaJsonFromScene();
        const QByteArray payload = QByteArray::fromStdString(boost::json::serialize(root));
        if (file.write(payload) != payload.size())
        {
            throw std::runtime_error(QString("Failed to write file: %1").arg(path).toStdString());
        }
        if (!file.commit())
        {
            throw std::runtime_error(QString("Failed to commit file: %1").arg(path).toStdString());
        }

        currentSchemaPath_ = path;
        updateWindowTitle();
        return true;
    }
    catch (const std::exception& ex)
    {
        QMessageBox::warning(this, "Save Schema Failed", QString::fromUtf8(ex.what()));
        return false;
    }
}

void MainWindow::loadSchemaFromJson(const boost::json::value& root, const QString& sourcePath)
{
    if (!root.is_object())
    {
        throw std::runtime_error("Schema file root must be a JSON object");
    }

    const auto& rootObj = root.as_object();
    const boost::json::value* diagramRoot = &root;
    if (const auto* diagram = rootObj.if_contains("diagram"); diagram && diagram->is_object())
    {
        diagramRoot = diagram;
    }

    std::vector<LoadedNodeSpec> nodes;
    if (const auto* nodesValue = diagramRoot->as_object().if_contains("nodes"))
    {
        if (!nodesValue->is_array())
        {
            throw std::runtime_error("Schema field 'nodes' must be an array");
        }

        for (const auto& nodeJson : nodesValue->as_array())
        {
            nodes.push_back(LoadedNodeSpec::fromJson(nodeJson));
        }
    }

    std::vector<LoadedWireSpec> wires;
    if (const auto* wiresValue = diagramRoot->as_object().if_contains("wires"))
    {
        if (!wiresValue->is_array())
        {
            throw std::runtime_error("Schema field 'wires' must be an array");
        }

        for (const auto& wireJson : wiresValue->as_array())
        {
            wires.push_back(LoadedWireSpec::fromJson(wireJson));
        }
    }

    if (wires.empty())
    {
        const auto* netsValue = diagramRoot->as_object().if_contains("nets");
        if (!netsValue)
        {
            // no fallback nets available
        }
        else if (!netsValue->is_array())
        {
            throw std::runtime_error("Schema field 'nets' must be an array");
        }
        else for (const auto& netJson : netsValue->as_array())
        {
            const auto& netObj = netJson.as_object();
            const QString netName = QString::fromStdString(jsonValueOr<std::string>(netObj, "name", std::string{}));
            const auto* connections = netObj.if_contains("connections");
            if (!connections || !connections->is_array())
            {
                continue;
            }

            ConnectorEndpoint output;
            std::vector<ConnectorEndpoint> inputs;
            for (const auto& connectionJson : connections->as_array())
            {
                const auto endpoint = endpointFromJson(connectionJson);
                if (connectorIsOutput(endpoint.connectorName))
                {
                    output = endpoint;
                }
                else
                {
                    inputs.push_back(endpoint);
                }
            }

            if (output.nodeId.isEmpty() || inputs.empty())
            {
                continue;
            }

            for (const auto& input : inputs)
            {
                wires.push_back(LoadedWireSpec{
                    .name = netName,
                    .from = output,
                    .to = input
                });
            }
        }
    }

    scene_->clear();

    // Stop any active session before replacing scene state.
    if (schemaSession_)
    {
        unlinkSinkProbes();
        schemaSession_->stop();
        schemaSession_->waitForCompletion();
        schemaSession_.reset();
    }
    if (runStatusTimer_) runStatusTimer_->stop();

    currentSchemaPath_ = sourcePath;
    runId_.clear();
    schemaHandle_.clear();
    runActive_ = false;
    runPaused_ = false;
    modelLoaded_ = false;
    updateModelActionStates();

    schemaName_ = QString::fromStdString(jsonValueOr<std::string>(rootObj, "id", std::string{"s7ui_schema"}));
    if (schemaName_.trimmed().isEmpty())
    {
        schemaName_ = QFileInfo(sourcePath).baseName();
        if (schemaName_.trimmed().isEmpty())
        {
            schemaName_ = "s7ui_schema";
        }
    }
    if (schemaNameEdit_)
    {
        const QSignalBlocker blocker(schemaNameEdit_);
        schemaNameEdit_->setText(schemaName_);
    }

    if (schemaDtSpin_)
    {
        const QSignalBlocker blocker(schemaDtSpin_);
        schemaDtSpin_->setValue(jsonValueOr<double>(rootObj, "dt", 0.001));
    }

    if (const auto* sheetVal = rootObj.if_contains("sheet"); sheetVal && sheetVal->is_object())
    {
        const auto& sheetObj = sheetVal->as_object();
        const QString standard = QString::fromStdString(jsonValueOr<std::string>(sheetObj, "standard", std::string{"A4"}));
        applySheetPreset(sheetPresetFromStandard(standard));
    }

    createdNodeCounter_ = 0;
    QMap<QString, std::shared_ptr<QSchematic::Items::Node>> nodeById;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& spec = nodes[i];
        auto nodeTemplate = templateForType(spec.typeName);
        if (spec.inputPorts >= 0)
        {
            nodeTemplate.inputPorts = spec.inputPorts;
        }
        if (spec.outputPorts >= 0)
        {
            nodeTemplate.outputPorts = spec.outputPorts;
        }
        nodeTemplate.visualSink = spec.visualSink || nodeTemplate.visualSink;
        const bool isVisualSink = nodeTemplate.visualSink;

        const auto item = createItemFromTemplate(nodeTemplate, static_cast<int>(i));
        const auto node = std::dynamic_pointer_cast<QSchematic::Items::Node>(item);
        if (!node)
        {
            continue;
        }

        scene_->addItem(item);

        if (auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(item))
        {
            basic->setTypeName(spec.typeName);
            basic->setNodeId(spec.id);
            basic->setNodeName(spec.name);
            basic->setParamsJson(QString::fromStdString(boost::json::serialize(spec.params)));
        }

        if (auto sink = std::dynamic_pointer_cast<s7ui::elements::SinkNodeItem>(item))
        {
            sink->setInputCount(std::max(1, nodeTemplate.inputPorts));
        }

        if (!isVisualSink)
        {
            if (auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(item))
            {
                basic->setPortLayout(std::max(0, nodeTemplate.inputPorts), std::max(0, nodeTemplate.outputPorts));
            }
        }

        item->setScenePos(spec.position);
        item->setData(kRoleNodeId, spec.id);
        item->setData(kRoleNodeName, spec.name);
        item->setData(kRoleNodeType, spec.typeName);
        item->setData(kRoleVisualSink, isVisualSink);
        nodeById.insert(spec.id, node);

        createdNodeCounter_ = std::max(createdNodeCounter_, nextCreatedNodeCounterFromId(spec.id));
    }

    if (createdNodeCounter_ == 0 && !nodes.empty())
    {
        createdNodeCounter_ = static_cast<int>(nodes.size());
    }

    for (const auto& wireSpec : wires)
    {
        const auto fromIt = nodeById.find(wireSpec.from.nodeId);
        const auto toIt = nodeById.find(wireSpec.to.nodeId);
        if (fromIt == nodeById.end() || toIt == nodeById.end())
        {
            continue;
        }

        const auto fromNode = fromIt.value();
        const auto toNode = toIt.value();
        if (!fromNode || !toNode)
        {
            continue;
        }

        const auto fromConnector = findConnectorByName(fromNode, wireSpec.from.connectorName);
        const auto toConnector = findConnectorByName(toNode, wireSpec.to.connectorName);
        const auto fromFallback = findFirstConnector(fromNode, true);
        const auto toFallback = findFirstConnector(toNode, false);
        const auto resolvedFrom = fromConnector ? fromConnector : fromFallback;
        const auto resolvedTo = toConnector ? toConnector : toFallback;
        if (!resolvedFrom || !resolvedTo)
        {
            continue;
        }

        auto wire = std::make_shared<QSchematic::Items::Wire>();
        if (!wireSpec.points.isEmpty())
        {
            for (const auto& point : wireSpec.points)
            {
                wire->append_point(point);
            }
        }
        else
        {
            wire->append_point(resolvedFrom->scenePos());
            wire->append_point(resolvedTo->scenePos());
        }

        wire->simplify();
        scene_->addWire(wire);
    }

    refreshViewAfterLayoutChange();
    rebuildPaperOverlay();
    updateWindowTitle();
    appendLog(QString("[schema] loaded %1 node(s), %2 wire(s)")
        .arg(nodes.size())
        .arg(wires.size()));
}

MainWindow::NodeTemplate MainWindow::selectedTemplate() const
{
    NodeTemplate fallback{"Core", "Constant", "const", 0, 1, false};

    auto* item = paletteTree_->currentItem();
    if (!item || item->childCount() > 0)
    {
        return fallback;
    }

    NodeTemplate nodeTemplate;
    nodeTemplate.typeName = item->data(0, kRoleTypeName).toString();
    nodeTemplate.defaultIdPrefix = item->data(0, kRoleCategory).toString();
    nodeTemplate.inputPorts = item->data(0, kRoleInputPorts).toInt();
    nodeTemplate.outputPorts = item->data(0, kRoleOutputPorts).toInt();
    nodeTemplate.visualSink = item->data(0, kRoleVisualSink).toBool();
    return nodeTemplate;
}

MainWindow::NodeTemplate MainWindow::templateForType(const QString& typeName) const
{
    // Check registry TypeDescriptors first.
    if (const auto desc = s7::ElementRegistry::instance().getDescriptor(typeName.toStdString()))
    {
        QString prefix = typeName.toLower();
        prefix.remove(' ');
        return NodeTemplate{
            QString::fromStdString(desc->category),
            typeName,
            prefix,
            desc->default_input_count,
            desc->default_output_count,
            false
        };
    }

    // Visual sink special case.
    if (typeName == "ScopeSink")
    {
        return NodeTemplate{"Visual", "ScopeSink", "scope", 2, 0, true};
    }
    if (typeName == "ComplexScopeSink")
    {
        return NodeTemplate{"Visual", "ComplexScopeSink", "complex_scope", 1, 0, true};
    }

    QString fallbackPrefix = typeName.toLower();
    fallbackPrefix.remove(' ');
    if (fallbackPrefix.isEmpty())
    {
        fallbackPrefix = "node";
    }
    QString fallbackCategory = "Imported";
    if (const auto visualSpec = visualCatalog_.specForType(typeName);
        visualSpec && !visualSpec->category.trimmed().isEmpty())
    {
        fallbackCategory = visualSpec->category;
    }
    return NodeTemplate{fallbackCategory, typeName, fallbackPrefix, 1, 1, false};
}

std::shared_ptr<QSchematic::Items::Item> MainWindow::createItemFromTemplate(const NodeTemplate& nodeTemplate, const int index) const
{
    if (nodeTemplate.visualSink)
    {
        auto sink = std::make_shared<s7ui::elements::SinkNodeItem>(nodeTemplate.typeName, std::max(1, nodeTemplate.inputPorts));
        const QString nodeId = QString("%1_%2").arg(nodeTemplate.defaultIdPrefix).arg(index + 1);
        const QString nodeName = QString("%1 %2").arg(nodeTemplate.typeName).arg(index + 1);
        sink->setNodeId(nodeId);
        sink->setNodeName(nodeName);
        sink->setParamsJson(QString("{\"inputs\": %1}").arg(std::max(1, nodeTemplate.inputPorts)));
        sink->setData(kRoleNodeId, nodeId);
        sink->setData(kRoleNodeName, nodeName);
        sink->setData(kRoleNodeType, nodeTemplate.typeName);
        sink->setData(kRoleVisualSink, true);
        sink->setScenePos(defaultScenePosForIndex(index));
        return sink;
    }

    auto node = std::make_shared<s7ui::elements::BasicNodeItem>(nodeTemplate.typeName);
    node->setSize(50, 30);
    node->setScenePos(defaultScenePosForIndex(index));

    const QString nodeId = QString("%1_%2").arg(nodeTemplate.defaultIdPrefix).arg(index + 1);
    const QString nodeName = QString("%1 %2").arg(nodeTemplate.typeName).arg(index + 1);
    node->setNodeId(nodeId);
    node->setNodeName(nodeName);
    node->setParamsJson("{}");
    node->setData(kRoleNodeId, nodeId);
    node->setData(kRoleNodeName, nodeName);
    node->setData(kRoleNodeType, nodeTemplate.typeName);
    node->setData(kRoleVisualSink, false);
    node->setPortLayout(nodeTemplate.inputPorts, nodeTemplate.outputPorts);
    if (const auto visualSpec = visualCatalog_.specForType(nodeTemplate.typeName))
    {
        node->setVisualStyle(visualSpec->shape, QColor(visualSpec->color));
    }

    return node;
}

void MainWindow::addSelectedPaletteNode()
{
    const auto nodeTemplate = selectedTemplate();
    const auto item = createItemFromTemplate(nodeTemplate, createdNodeCounter_++);

    // Override the placeholder position: always place at the center of whatever
    // the user is currently looking at, with a small cascade offset per node so
    // successive additions don't land exactly on top of each other.
    if (view_ && item)
    {
        const QPoint vpCenter = view_->viewport()->rect().center();
        QPointF sceneCenter = view_->mapToScene(vpCenter);
        // Offset each successive node slightly so they don't stack up exactly.
        const int slot = (createdNodeCounter_ - 1) % 5;
        sceneCenter += QPointF(slot * 20.0, slot * 20.0);
        item->setScenePos(sceneCenter);
    }

    if (scene_->addItem(item))
    {
        appendLog(QString("[scene] Added node: %1").arg(nodeTemplate.typeName));
        // Ensure the new node is visible in the view.
        if (view_ && item)
        {
            const QPointF p = item->scenePos();
            const QRectF b = item->boundingRect();
            view_->ensureVisible(QRectF(p.x(), p.y(), b.width(), b.height()), 20, 20);
        }
    }
}

void MainWindow::appendLog(const QString& message)
{
    if (logView_)
    {
        logView_->append(message);
    }
}

void MainWindow::runSchemaModel()
{
    if (runActive_)
    {
        appendLog("[model] Run skipped: model is already running");
        return;
    }

    ensureNodeLibrariesRegistered();

    boost::json::value runtimeJson;
    try
    {
        runtimeJson = buildRuntimeJsonFromScene();
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[compile] %1").arg(QString::fromUtf8(ex.what())));
        setModelStatus("error: invalid diagram");
        return;
    }

    const auto diagramJson = buildSchemaJsonFromScene();
    const auto diagramReport = s7ui::compiler::validateDiagramConnections(diagramJson);
    for (const auto& d : diagramReport.diagnostics)
    {
        const QString prefix = (d.severity == s7ui::compiler::Diagnostic::Severity::Error)
            ? "[diagram:error]"
            : "[diagram:warn]";
        appendLog(QString("%1 %2").arg(prefix).arg(QString::fromStdString(d.message)));
    }
    if (diagramReport.hasErrors())
    {
        setModelStatus("error: invalid diagram");
        return;
    }

    const auto& runtimeObj = runtimeJson.as_object();
    const auto* runtimeNodes = runtimeObj.if_contains("nodes");
    if (!runtimeNodes || !runtimeNodes->is_array() || runtimeNodes->as_array().empty())
    {
        appendLog("[model] No runtime nodes found in scene");
        setModelStatus("error: empty runtime graph");
        return;
    }

    const auto report = s7ui::compiler::validateRuntimeGraph(runtimeJson);
    for (const auto& d : report.diagnostics)
    {
        const QString prefix = (d.severity == s7ui::compiler::Diagnostic::Severity::Error)
            ? "[compile:error]"
            : "[compile:warn]";
        appendLog(QString("%1 %2").arg(prefix).arg(QString::fromStdString(d.message)));
    }
    if (report.hasErrors())
    {
        setModelStatus("error: invalid graph");
        return;
    }

    // Collect visual-sink item IDs (they become Probe nodes in the engine).
    std::vector<std::string> sinkItemIds;
    for (const auto& node : scene_->items<QSchematic::Items::Node>())
    {
        if (node && node->data(kRoleVisualSink).toBool())
        {
            const QString id = node->data(kRoleNodeId).toString();
            if (!id.isEmpty())
            {
                sinkItemIds.push_back(id.toStdString());
            }
        }
    }

    try
    {
        schemaSession_ = s7ui::SchemaSession::build(runtimeJson, sinkItemIds);
        modelLoaded_ = true;

        const int    hours   = runHoursSpin_   ? runHoursSpin_->value()   : 0;
        const int    minutes = runMinutesSpin_ ? runMinutesSpin_->value() : 1;
        const int    seconds = runSecondsSpin_ ? runSecondsSpin_->value() : 0;
        const long   totalSeconds = std::max<long>(1,
            static_cast<long>(hours) * 3600L +
            static_cast<long>(minutes) * 60L +
            static_cast<long>(seconds));
        const double dt    = schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
        const long   ticks = std::max<long>(1,
            static_cast<long>(std::llround(static_cast<double>(totalSeconds) / std::max(1e-12, dt))));

        runActive_ = true;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("running");

        // Link SinkNodeItems to their probes before the background thread starts
        // writing data so the QTimer begins polling immediately.
        linkSinkProbes();

        // Completion callback runs on the background thread: use invokeMethod to
        // marshal the UI update back to the GUI thread.
        // Capture a weak_ptr to the session that started this run so the callback
        // is safe even if the user resets or loads a new schema before completion.
        std::weak_ptr<s7ui::SchemaSession> weakSession = schemaSession_;
        schemaSession_->runAsync(ticks,
            [this, weakSession](bool succeeded, const std::string& error)
            {
                QMetaObject::invokeMethod(this, [this, weakSession, succeeded, error] {
                    runActive_  = false;
                    runPaused_  = false;
                    if (runStatusTimer_) runStatusTimer_->stop();
                    updateModelActionStates();
                    if (succeeded)
                    {
                        setModelStatus("completed");
                        const long ticks = weakSession.lock()
                            ? weakSession.lock()->completedTicks()
                            : 0;
                        appendLog(QString("[model] Run completed (%1 ticks)").arg(ticks));
                    }
                    else
                    {
                        setModelStatus("error");
                        appendLog(QString("[model] Run failed: %1")
                            .arg(QString::fromStdString(error)));
                    }
                    // Keep probes alive a moment so the last frame is visible.
                }, Qt::QueuedConnection);
            });

        if (runStatusTimer_) runStatusTimer_->start();

        appendLog(QString("[model] Run started: %1 ticks (dt=%2 s)")
            .arg(ticks).arg(dt));
    }
    catch (const std::exception& ex)
    {
        schemaSession_.reset();
        runActive_   = false;
        runPaused_   = false;
        modelLoaded_ = false;
        updateModelActionStates();
        setModelStatus("error");
        appendLog(QString("[model] Run failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::pauseSchemaModel()
{
    if (!schemaSession_ || !runActive_ || runPaused_)
    {
        appendLog("[model] Pause skipped");
        return;
    }
    schemaSession_->pause();
    runPaused_ = true;
    updateModelActionStates();
    setModelStatus("paused");
    appendLog("[model] Paused");
}

void MainWindow::resumeSchemaModel()
{
    if (!schemaSession_ || !runPaused_)
    {
        appendLog("[model] Resume skipped");
        return;
    }
    schemaSession_->resume();
    runPaused_ = false;
    updateModelActionStates();
    setModelStatus("running");
    appendLog("[model] Resumed");
}

void MainWindow::stopSchemaModel()
{
    if (!schemaSession_ || !runActive_)
    {
        appendLog("[model] Stop skipped");
        return;
    }
    schemaSession_->stop();
    // The completion callback will update UI state asynchronously.
    appendLog("[model] Stop requested");
}

void MainWindow::resetSchemaModel()
{
    unlinkSinkProbes();
    schemaSession_.reset();
    modelLoaded_ = false;
    runActive_   = false;
    runPaused_   = false;
    if (runStatusTimer_) runStatusTimer_->stop();
    updateModelActionStates();
    setModelStatus("idle");
    appendLog("[model] Reset");
}

void MainWindow::onRunStatusPoll()
{
    if (!schemaSession_) return;
    // Update tick progress indicator.
    const long done  = schemaSession_->completedTicks();
    const long total = schemaSession_->totalTicks();
    if (total > 0)
    {
        setModelStatus(QString("running %1/%2").arg(done).arg(total));
    }
}

void MainWindow::linkSinkProbes()
{
    if (!schemaSession_) return;

    for (const auto& node : scene_->items<QSchematic::Items::Node>())
    {
        if (!node || !node->data(kRoleVisualSink).toBool()) continue;
        const QString id = node->data(kRoleNodeId).toString();
        if (id.isEmpty()) continue;

        if (auto sink = std::dynamic_pointer_cast<s7ui::elements::SinkNodeItem>(node))
        {
            const auto typeName = node->data(kRoleNodeType).toString();
            if (typeName == "ComplexScopeSink")
            {
                auto cprobe = schemaSession_->complexProbe(id.toStdString());
                if (!cprobe) continue;
                sink->setComplexProbe(cprobe);
                appendLog(QString("[probe] Linked complex probe '%1' (%2 channels)")
                    .arg(id).arg(cprobe->channelCount()));
            }
            else
            {
                auto probe = schemaSession_->probe(id.toStdString());
                if (!probe) continue;
                sink->setProbe(probe);
                appendLog(QString("[probe] Linked probe '%1' (%2 channels)")
                    .arg(id).arg(probe->channelCount()));
            }
        }
    }
}

void MainWindow::unlinkSinkProbes()
{
    for (const auto& node : scene_->items<QSchematic::Items::Node>())
    {
        if (!node || !node->data(kRoleVisualSink).toBool()) continue;
        if (auto sink = std::dynamic_pointer_cast<s7ui::elements::SinkNodeItem>(node))
        {
            sink->clearProbe();
        }
    }
}

void MainWindow::schemaPropertiesEdited()
{
    schemaName_ = schemaNameEdit_ ? schemaNameEdit_->text().trimmed() : schemaName_;
    if (schemaName_.isEmpty())
    {
        schemaName_ = "s7ui_schema";
        if (schemaNameEdit_)
        {
            schemaNameEdit_->setText(schemaName_);
        }
    }

    updateModelActionStates();
    rebuildPaperOverlay();
    appendLog(QString("[schema] properties updated: name=%1 dt=%2")
        .arg(schemaName_)
        .arg(schemaDtSpin_ ? schemaDtSpin_->value() : 0.001, 0, 'g', 9));
}

boost::json::value MainWindow::buildSchemaJsonFromScene() const
{
    boost::json::object root;
    root["schema_version"] = 1;
    root["diagram_version"] = 1;
    root["units"] = "mm";
    root["sheet"] = boost::json::object{
        {"standard", sheetStandardName(sheetPreset_)},
        {"orientation", "landscape"},
        {"width_mm", sheetSizeForPreset(sheetPreset_).width},
        {"height_mm", sheetSizeForPreset(sheetPreset_).height}
    };
    root["id"] = schemaName_.toStdString();
    root["dt"] = schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
    root["nodes"] = boost::json::array();
    root["nets"] = boost::json::array();
    root["wires"] = boost::json::array();
    root["diagram"] = boost::json::object{{"version", 1},
                                           {"nodes", boost::json::array()},
                                           {"nets", boost::json::array()},
                                           {"wires", boost::json::array()}};

    auto& rootNodes = root["nodes"].as_array();
    auto& rootNets = root["nets"].as_array();
    auto& rootWires = root["wires"].as_array();
    auto& diagram = root["diagram"].as_object();
    auto& diagramNodes = diagram["nodes"].as_array();
    auto& diagramNets = diagram["nets"].as_array();
    auto& diagramWires = diagram["wires"].as_array();

    auto nodeItems = scene_->items<QSchematic::Items::Node>();
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpoint> runtimeEndpointByConnector;
    const auto wireManager = scene_->wire_manager();

    for (const auto& node : nodeItems)
    {
        if (!node) continue;

        const bool isVisualSink = node->data(kRoleVisualSink).toBool();
        const QString nodeId    = node->data(kRoleNodeId).toString();
        const QString typeName  = node->data(kRoleNodeType).toString();
        if (nodeId.isEmpty() || typeName.isEmpty()) continue;

        boost::json::object nodeJson;
        nodeJson["id"] = nodeId.toStdString();

        auto buildPorts = [&]() -> boost::json::array {
            boost::json::array ports;
            const bool complexNode = typeName.contains("Complex", Qt::CaseInsensitive);
            for (const auto& connector : node->connectors())
            {
                if (!connector) continue;
                const bool isOutput = connectorIsOutput(connector->text());
                boost::json::object p;
                p["name"] = connector->text().toStdString();
                p["direction"] = isOutput ? "output" : "input";
                p["signal_type"] = complexNode ? "complex" : "real";
                p["domain"] = "stream";
                p["max_connections"] = isOutput ? 1024 : 1;
                ports.push_back(std::move(p));
            }
            return ports;
        };

        if (isVisualSink)
        {
            // Visual sink elements become probe nodes in the computation engine.
            int inputCount = 0;
            for (const auto& connector : node->connectors())
            {
                if (!connectorIsOutput(connector->text())) ++inputCount;
            }
            nodeJson["type"]   = (typeName == "ComplexScopeSink") ? "ComplexProbe" : "Probe";
            nodeJson["params"] = boost::json::object{{"channels", std::max(1, inputCount)}};
        }
        else
        {
            nodeJson["type"] = typeName.toStdString();

            QString params = "{}";
            if (const auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(node))
            {
                params = basic->paramsJson();
            }
            try
            {
                nodeJson["params"] = boost::json::parse(params.toStdString());
            }
            catch (...)
            {
                nodeJson["params"] = boost::json::object();
            }
        }

        boost::json::object diagramNodeJson;
        diagramNodeJson["id"] = nodeId.toStdString();
        diagramNodeJson["type"] = typeName.toStdString();
        diagramNodeJson["name"] = node->data(kRoleNodeName).toString().isEmpty()
            ? typeName.toStdString()
            : node->data(kRoleNodeName).toString().toStdString();
        diagramNodeJson["visual"] = isVisualSink;
        diagramNodeJson["position"] = jsonPoint(node->scenePos());
        int inputPorts = 0;
        int outputPorts = 0;
        for (const auto& connector : node->connectors())
        {
            if (connectorIsOutput(connector->text())) ++outputPorts;
            else ++inputPorts;
        }
        diagramNodeJson["input_ports"] = inputPorts;
        diagramNodeJson["output_ports"] = outputPorts;
        diagramNodeJson["ports"] = buildPorts();
        diagramNodeJson["params"] = nodeJson["params"];

        diagramNodes.push_back(std::move(diagramNodeJson));
        rootNodes.push_back(std::move(nodeJson));

        // Register all connectors (including visual-sink connectors) for wire resolution.
        for (const auto& connector : node->connectors())
        {
            runtimeEndpointByConnector[connector.get()] = ConnectorEndpoint{
                .nodeId = nodeId,
                .connectorName = connector->text()
            };
        }
    }

    // Wire serialization (same for both sink and non-sink nodes).
    auto wireItems = scene_->items<QSchematic::Items::Wire>();
    for (const auto& wire : wireItems)
    {
        if (!wire) continue;

        ConnectorEndpoint from;
        ConnectorEndpoint to;
        bool fromSet = false;
        bool toSet   = false;

        for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
        {
            const auto attached = wireManager ? wireManager->attached_wire(connectorPtr) : std::nullopt;
            if (!attached || attached->wire != wire.get()) continue;

            if (connectorIsOutput(endpoint.connectorName) && !fromSet)
            {
                from = endpoint;
                fromSet = true;
            }
            else if (!connectorIsOutput(endpoint.connectorName) && !toSet)
            {
                to = endpoint;
                toSet = true;
            }
        }

        if (!fromSet || !toSet) continue;

        boost::json::object wireJson;
        wireJson["from"] = boost::json::object{{"node", from.nodeId.toStdString()}, {"connector", from.connectorName.toStdString()}};
        wireJson["to"]   = boost::json::object{{"node", to.nodeId.toStdString()},   {"connector", to.connectorName.toStdString()}};
        wireJson["points"] = boost::json::array();
        for (const auto& point : wire->pointsAbsolute())
        {
            wireJson["points"].as_array().push_back(jsonPoint(point));
        }
        diagramWires.push_back(wireJson);
        rootWires.push_back(std::move(wireJson));
    }

    // Net serialization.
    if (wireManager)
    {
        for (const auto& net : wireManager->nets())
        {
            std::unordered_set<const wire_system::wire*> netWires;
            for (const auto& wire : net->wires())
            {
                netWires.insert(wire.get());
            }

            boost::json::array connectionsJson;
            int outputs = 0;
            int inputs  = 0;

            for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
            {
                const auto attached = wireManager->attached_wire(connectorPtr);
                if (!attached || !attached->wire || !netWires.contains(attached->wire)) continue;

                connectionsJson.push_back(boost::json::object{{"node", endpoint.nodeId.toStdString()},
                                                              {"connector", endpoint.connectorName.toStdString()}});

                if (connectorIsOutput(endpoint.connectorName)) ++outputs;
                else ++inputs;
            }

            QString netName = net->name();
            if (netName.trimmed().isEmpty())
            {
                netName = QString("net_%1").arg(rootNets.size());
            }

            if (outputs == 1 && inputs >= 1)
            {
                diagramNets.push_back(boost::json::object{{"name", netName.toStdString()},
                                                          {"connections", connectionsJson}});
                rootNets.push_back(boost::json::object{{"name", netName.toStdString()},
                                                       {"connections", std::move(connectionsJson)}});
            }
            // Structural validation is handled by schema_compiler::validateRuntimeGraph.
        }
    }

    return root;
}

boost::json::value MainWindow::buildRuntimeJsonFromScene() const
{
    boost::json::object root;
    root["schema_version"] = 1;
    root["id"] = schemaName_.toStdString();
    root["dt"] = schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
    root["nodes"] = boost::json::array();
    root["nets"] = boost::json::array();
    root["wires"] = boost::json::array();
    auto& rootNodes = root["nodes"].as_array();
    auto& rootNets = root["nets"].as_array();
    auto& rootWires = root["wires"].as_array();

    auto nodeItems = scene_->items<QSchematic::Items::Node>();
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpoint> runtimeEndpointByConnector;
    const auto wireManager = scene_->wire_manager();

    for (const auto& node : nodeItems)
    {
        if (!node) continue;

        const bool isVisualSink = node->data(kRoleVisualSink).toBool();
        const QString nodeId    = node->data(kRoleNodeId).toString();
        const QString typeName  = node->data(kRoleNodeType).toString();
        if (nodeId.isEmpty() || typeName.isEmpty()) continue;

        boost::json::object nodeJson;
        nodeJson["id"] = nodeId.toStdString();

        if (isVisualSink)
        {
            // Visual sink elements become probe nodes in the computation engine.
            int inputCount = 0;
            for (const auto& connector : node->connectors())
            {
                if (!connectorIsOutput(connector->text())) ++inputCount;
            }
            nodeJson["type"]   = (typeName == "ComplexScopeSink") ? "ComplexProbe" : "Probe";
            nodeJson["params"] = boost::json::object{{"channels", std::max(1, inputCount)}};
        }
        else
        {
            nodeJson["type"] = typeName.toStdString();

            QString params = "{}";
            if (const auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(node))
            {
                params = basic->paramsJson();
            }
            try
            {
                nodeJson["params"] = boost::json::parse(params.toStdString());
            }
            catch (...)
            {
                nodeJson["params"] = boost::json::object();
            }
        }

        rootNodes.push_back(std::move(nodeJson));

        // Register all connectors (including visual-sink connectors) for wire resolution.
        for (const auto& connector : node->connectors())
        {
            runtimeEndpointByConnector[connector.get()] = ConnectorEndpoint{
                .nodeId = nodeId,
                .connectorName = connector->text()
            };
        }
    }

    // Wire serialization (same for both sink and non-sink nodes).
    auto wireItems = scene_->items<QSchematic::Items::Wire>();
    for (const auto& wire : wireItems)
    {
        if (!wire) continue;

        ConnectorEndpoint from;
        ConnectorEndpoint to;
        bool fromSet = false;
        bool toSet   = false;

        for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
        {
            const auto attached = wireManager ? wireManager->attached_wire(connectorPtr) : std::nullopt;
            if (!attached || attached->wire != wire.get()) continue;

            if (connectorIsOutput(endpoint.connectorName) && !fromSet)
            {
                from = endpoint;
                fromSet = true;
            }
            else if (!connectorIsOutput(endpoint.connectorName) && !toSet)
            {
                to = endpoint;
                toSet = true;
            }
        }

        if (!fromSet || !toSet) continue;

        boost::json::object wireJson;
        wireJson["from"] = boost::json::object{{"node", from.nodeId.toStdString()}, {"connector", from.connectorName.toStdString()}};
        wireJson["to"]   = boost::json::object{{"node", to.nodeId.toStdString()},   {"connector", to.connectorName.toStdString()}};
        wireJson["points"] = boost::json::array();
        for (const auto& point : wire->pointsAbsolute())
        {
            wireJson["points"].as_array().push_back(jsonPoint(point));
        }
        rootWires.push_back(std::move(wireJson));
    }

    // Net serialization.
    if (wireManager)
    {
        for (const auto& net : wireManager->nets())
        {
            std::unordered_set<const wire_system::wire*> netWires;
            for (const auto& wire : net->wires())
            {
                netWires.insert(wire.get());
            }

            boost::json::array connectionsJson;
            int outputs = 0;
            int inputs  = 0;

            for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
            {
                const auto attached = wireManager->attached_wire(connectorPtr);
                if (!attached || !attached->wire || !netWires.contains(attached->wire)) continue;

                connectionsJson.push_back(boost::json::object{{"node", endpoint.nodeId.toStdString()},
                                                              {"connector", endpoint.connectorName.toStdString()}});

                if (connectorIsOutput(endpoint.connectorName)) ++outputs;
                else ++inputs;
            }

            QString netName = net->name();
            if (netName.trimmed().isEmpty())
            {
                netName = QString("net_%1").arg(rootNets.size());
            }

            if (outputs == 1 && inputs >= 1)
            {
                rootNets.push_back(boost::json::object{{"name", netName.toStdString()},
                                                       {"connections", std::move(connectionsJson)}});
            }
            // Structural validation is handled by schema_compiler::validateRuntimeGraph.
        }
    }

    return root;
}

void MainWindow::ensureNodeLibrariesRegistered()
{
    if (librariesRegistered_)
    {
        return;
    }
    s7::registerBuiltins();
    s7::registerFileIO();
    s7::registerMemIO();
    s7::registerNetIO();
    s7::registerProbe();
    librariesRegistered_ = true;
}

void MainWindow::refreshViewAfterLayoutChange()
{
    if (view_)
    {
        view_->fitInView();
    }
}

void MainWindow::applySheetPreset(const SheetPreset preset)
{
    sheetPreset_ = preset;

    if (actionSheetA4_) actionSheetA4_->setChecked(sheetPreset_ == SheetPreset::A4Landscape);
    if (actionSheetA3_) actionSheetA3_->setChecked(sheetPreset_ == SheetPreset::A3Landscape);
    if (actionSheetA2_) actionSheetA2_->setChecked(sheetPreset_ == SheetPreset::A2Landscape);
    if (actionSheetA1_) actionSheetA1_->setChecked(sheetPreset_ == SheetPreset::A1Landscape);

    const auto sheet = sheetSizeForPreset(sheetPreset_);

    // CAD-style: scene units are millimeters.
    scene_->setSceneRect(0.0, 0.0, sheet.width, sheet.height);

    if (view_)
    {
        view_->setSceneRect(scene_->sceneRect());
        view_->centerOn(sheet.width * 0.5, sheet.height * 0.5);
    }

    rebuildPaperOverlay();

    appendLog(QString("[sheet] %1 (%2 x %3 mm), units=mm")
        .arg(sheet.label)
        .arg(sheet.width, 0, 'f', 0)
        .arg(sheet.height, 0, 'f', 0));
}

void MainWindow::rebuildPaperOverlay()
{
    if (!scene_)
    {
        return;
    }

    // Remove previous paper overlay primitives.
    const auto rawItems = scene_->QGraphicsScene::items();
    for (auto* item : rawItems)
    {
        if (!item) continue;
        if (item->data(kRolePaperOverlay).toInt() == 1)
        {
            scene_->QGraphicsScene::removeItem(item);
            delete item;
        }
    }

    const QRectF sheet = scene_->sceneRect();
    if (sheet.isEmpty())
    {
        return;
    }

    const auto mkOverlay = [](QGraphicsItem* item) {
        item->setData(kRolePaperOverlay, 1);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        item->setFlag(QGraphicsItem::ItemIsMovable, false);
        item->setZValue(-500.0);
    };

    auto* border = new QGraphicsRectItem(sheet);
    border->setPen(QPen(QColor(30, 30, 30), 0.7));
    border->setBrush(Qt::NoBrush);
    mkOverlay(border);
    scene_->QGraphicsScene::addItem(border);

    const QRectF printMargin = sheet.adjusted(kPaperMarginMm, kPaperMarginMm,
                                              -kPaperMarginMm, -kPaperMarginMm);
    auto* marginRect = new QGraphicsRectItem(printMargin);
    QPen marginPen(QColor(90, 90, 90), 0.4);
    marginPen.setStyle(Qt::DashLine);
    marginRect->setPen(marginPen);
    marginRect->setBrush(Qt::NoBrush);
    mkOverlay(marginRect);
    scene_->QGraphicsScene::addItem(marginRect);

    QRectF titleBlock(
        sheet.right() - kPaperMarginMm - kTitleBlockWidthMm,
        sheet.bottom() - kPaperMarginMm - kTitleBlockHeightMm,
        kTitleBlockWidthMm,
        kTitleBlockHeightMm);
    auto* titleRect = new QGraphicsRectItem(titleBlock);
    titleRect->setPen(QPen(QColor(45, 45, 45), 0.5));
    titleRect->setBrush(Qt::NoBrush);
    mkOverlay(titleRect);
    scene_->QGraphicsScene::addItem(titleRect);

    const auto sheetInfo = sheetSizeForPreset(sheetPreset_);
    auto* titleText = new QGraphicsSimpleTextItem(
        QString("%1\n%2\nunits: mm")
            .arg(schemaName_.trimmed().isEmpty() ? QStringLiteral("untitled") : schemaName_.trimmed())
            .arg(sheetInfo.label));
    QFont font = titleText->font();
    font.setPointSizeF(7.5);
    titleText->setFont(font);
    titleText->setBrush(QBrush(QColor(20, 20, 20)));
    titleText->setPos(titleBlock.left() + 3.0, titleBlock.top() + 2.0);
    mkOverlay(titleText);
    titleText->setZValue(-490.0);
    scene_->QGraphicsScene::addItem(titleText);
}

void MainWindow::updateModelActionStates()
{
    if (actionRunModel_)
    {
        actionRunModel_->setEnabled(!runActive_);
    }
    if (actionPauseModel_)
    {
        actionPauseModel_->setEnabled(runActive_ && !runPaused_);
    }
    if (actionResumeModel_)
    {
        actionResumeModel_->setEnabled(runActive_ && runPaused_);
    }
    if (actionStopModel_)
    {
        actionStopModel_->setEnabled(runActive_);
    }
    if (actionResetModel_)
    {
        actionResetModel_->setEnabled(modelLoaded_);
    }
}

void MainWindow::setModelStatus(const QString& status)
{
    if (modelStatusLabel_)
    {
        modelStatusLabel_->setText(QString("model: %1").arg(status));
    }
}

void MainWindow::updateWindowTitle()
{
    QString title = QString("s7ui - %1").arg(schemaName_.trimmed().isEmpty() ? QStringLiteral("untitled") : schemaName_.trimmed());
    if (!currentSchemaPath_.isEmpty())
    {
        title += QString(" [%1]").arg(QFileInfo(currentSchemaPath_).fileName());
    }

    setWindowTitle(title);
}

void MainWindow::toggleWireMode()
{
    if (!scene_)
    {
        return;
    }

    const int currentMode = scene_->mode();
    if (currentMode == QSchematic::Scene::WireMode)
    {
        scene_->setMode(QSchematic::Scene::NormalMode);
        appendLog("[editor] Wire mode disabled");
    }
    else
    {
        scene_->setMode(QSchematic::Scene::WireMode);
        appendLog("[editor] Wire mode enabled - click and drag from connectors to draw wires");
    }
}

void MainWindow::onSceneModeChanged(int mode)
{
    if (actionToggleWireMode_)
    {
        const QSignalBlocker blocker(actionToggleWireMode_);
        actionToggleWireMode_->setChecked(mode == QSchematic::Scene::WireMode);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* evt)
{
    if (!evt) return;

    // Delete pressed: remove selected items
    if (evt->key() == Qt::Key_Delete || evt->key() == Qt::Key_Backspace)
    {
        if (scene_)
        {
            // Get all selected items (these are shared_ptrs to QSchematic::Items::Item)
            auto selectedItems = scene_->selectedItems();
            int deletedCount = 0;

            // Try to remove each selected item
            for (const auto& item : selectedItems)
            {
                if (scene_->removeItem(item))
                {
                    ++deletedCount;
                }
            }

            if (deletedCount > 0)
            {
                appendLog(QString("[editor] Deleted %1 item(s)").arg(deletedCount));
            }
        }
        evt->accept();
        return;
    }

    // Forward to parent for other keys
    QMainWindow::keyPressEvent(evt);
}


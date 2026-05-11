#include "main_window.hpp"

#include "elements/basic_node_item.hpp"
#include "elements/sink_node_item.hpp"
#include "remote_nodes_widget.hpp"

#include <s7factory.hpp>

#include <nlohmann/json.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/view.hpp>

#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
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

struct CatalogEntry
{
    QString category;
    QString typeName;
    QString defaultIdPrefix;
    int inputPorts = 1;
    int outputPorts = 1;
    bool visualSink = false;
};

auto paletteCatalog() -> const std::vector<CatalogEntry>&
{
    static const std::vector<CatalogEntry> catalog = {
        {"Core", "Constant", "const", 0, 1, false},
        {"Core", "Sum", "sum", 2, 1, false},
        {"Core", "Integrator", "int", 1, 1, false},
        {"Core", "ToComplex", "to_complex", 2, 1, false},
        {"Core", "ToReals", "to_reals", 1, 2, false},
        {"Core", "Vca", "vca", 2, 1, false},
        {"Core", "DigitalOscillator", "dosc", 0, 1, false},
        {"Core", "HarmonicOscillator", "harm", 0, 1, false},
        {"IO", "RealFileSink", "file_sink", 1, 0, false},
        {"IO", "RealMemSink", "mem_sink", 1, 0, false},
        {"IO", "RealNetSink", "net_sink", 1, 0, false},
        {"Visual", "ScopeSink", "scope", 2, 0, true},
    };
    return catalog;
}

auto defaultScenePosForIndex(const int index) -> QPointF
{
    return {static_cast<qreal>((index % 6) * 240), static_cast<qreal>((index / 6) * 170)};
}

struct ConnectorEndpoint
{
    QString nodeId;
    QString connectorName;
};

auto connectorIsOutput(const QString& connectorName) -> bool
{
    return connectorName.startsWith("out");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    scene_ = new QSchematic::Scene(this);
    scene_->setSceneRect(-6000, -4000, 12000, 8000);

    createActions();
    createMenus();
    createToolbars();
    createCentralEditor();
    createDocks();
    populatePalette();
    seedStarterScene();
    refreshViewAfterLayoutChange();
    startupFitQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        if (startupFitQueued_)
        {
            refreshViewAfterLayoutChange();
            startupFitQueued_ = false;
        }
    });
    updateModelActionStates();
    setModelStatus("idle");

    setWindowTitle("s7ui - control/compute/visualize skeleton");
    resize(1600, 950);
    appendLog("[s7ui] UI skeleton initialized");
}

void MainWindow::createActions()
{
    actionAddNode_ = new QAction("Add Node", this);
    connect(actionAddNode_, &QAction::triggered, this, &MainWindow::addSelectedPaletteNode);

    actionFitView_ = new QAction("Fit View", this);
    connect(actionFitView_, &QAction::triggered, [this] {
        if (view_)
        {
            view_->fitInView();
        }
    });

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
}

void MainWindow::createMenus()
{
    auto* insertMenu = menuBar()->addMenu("&Insert");
    insertMenu->addAction(actionAddNode_);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(actionFitView_);

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
    paletteTree_->clear();

    QMap<QString, QTreeWidgetItem*> roots;
    for (const auto& entry : paletteCatalog())
    {
        if (!roots.contains(entry.category))
        {
            auto* root = new QTreeWidgetItem();
            root->setText(0, entry.category);
            roots.insert(entry.category, root);
            paletteTree_->addTopLevelItem(root);
        }

        auto* child = new QTreeWidgetItem();
        child->setText(0, entry.typeName);
        child->setData(0, kRoleTypeName, entry.typeName);
        child->setData(0, kRoleCategory, entry.defaultIdPrefix);
        child->setData(0, kRoleInputPorts, entry.inputPorts);
        child->setData(0, kRoleOutputPorts, entry.outputPorts);
        child->setData(0, kRoleVisualSink, entry.visualSink);
        roots[entry.category]->addChild(child);
    }

    paletteTree_->expandAll();
    if (paletteTree_->topLevelItemCount() > 0 && paletteTree_->topLevelItem(0)->childCount() > 0)
    {
        paletteTree_->setCurrentItem(paletteTree_->topLevelItem(0)->child(0));
    }
}

void MainWindow::seedStarterScene()
{
    const auto sourceA = createItemFromTemplate(NodeTemplate{"Core", "Constant", "const", 0, 1, false}, createdNodeCounter_++);
    const auto sourceB = createItemFromTemplate(NodeTemplate{"Core", "Constant", "const", 0, 1, false}, createdNodeCounter_++);
    const auto sink = createItemFromTemplate(NodeTemplate{"Visual", "ScopeSink", "scope", 2, 0, true}, createdNodeCounter_++);

    sourceA->setScenePos(140, 120);
    sourceB->setScenePos(140, 290);
    sink->setScenePos(540, 160);

    scene_->addItem(sourceA);
    scene_->addItem(sourceB);
    scene_->addItem(sink);

    if (const auto c1 = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(sourceA))
    {
        c1->setParamsJson(R"({"value": 0.7})");
    }
    if (const auto c2 = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(sourceB))
    {
        c2->setParamsJson(R"({"value": -0.4})");
    }
    if (const auto scope = std::dynamic_pointer_cast<s7ui::elements::SinkNodeItem>(sink))
    {
        scope->setPreviewLevels({0.7, -0.4});
    }

    const auto srcANode = std::dynamic_pointer_cast<QSchematic::Items::Node>(sourceA);
    const auto srcBNode = std::dynamic_pointer_cast<QSchematic::Items::Node>(sourceB);
    const auto sinkNode = std::dynamic_pointer_cast<QSchematic::Items::Node>(sink);
    if (!srcANode || !srcBNode || !sinkNode)
    {
        return;
    }

    auto wire1 = std::make_shared<QSchematic::Items::Wire>();
    wire1->append_point(srcANode->connectors().at(0)->scenePos());
    wire1->append_point(sinkNode->connectors().at(0)->scenePos());
    wire1->simplify();
    scene_->addWire(wire1);

    auto wire2 = std::make_shared<QSchematic::Items::Wire>();
    wire2->append_point(srcBNode->connectors().at(0)->scenePos());
    wire2->append_point(sinkNode->connectors().at(1)->scenePos());
    wire2->simplify();
    scene_->addWire(wire2);
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
    for (const auto& entry : paletteCatalog())
    {
        if (entry.typeName == typeName)
        {
            return NodeTemplate{entry.category, entry.typeName, entry.defaultIdPrefix, entry.inputPorts, entry.outputPorts, entry.visualSink};
        }
    }

    QString fallbackPrefix = typeName.toLower();
    fallbackPrefix.remove(' ');
    if (fallbackPrefix.isEmpty())
    {
        fallbackPrefix = "node";
    }
    return NodeTemplate{"Imported", typeName, fallbackPrefix, 1, 1, false};
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
    node->setSize(180, 100);
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

    return node;
}

void MainWindow::addSelectedPaletteNode()
{
    const auto nodeTemplate = selectedTemplate();
    const auto item = createItemFromTemplate(nodeTemplate, createdNodeCounter_++);
    if (scene_->addItem(item))
    {
        appendLog(QString("[scene] Added node: %1").arg(nodeTemplate.typeName));
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

    const auto runtimeJson = buildRuntimeJsonFromScene();
    if (!runtimeJson.contains("nodes") || runtimeJson["nodes"].empty())
    {
        appendLog("[model] No runtime nodes found in scene");
        setModelStatus("error: empty runtime graph");
        return;
    }

    try
    {
        runActive_ = true;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("running");

        const auto loadResult = runtimeService_.loadSchema(
            schemaName_.toStdString(),
            runtimeJson.dump());
        schemaHandle_ = QString::fromStdString(loadResult.schema_handle);
        modelLoaded_ = true;
        appendLog(QString("[model] Loaded schema '%1' (nodes=%2, connections=%3)")
            .arg(schemaName_)
            .arg(loadResult.num_elements)
            .arg(loadResult.num_connections));

        const int hours = runHoursSpin_ ? runHoursSpin_->value() : 0;
        const int minutes = runMinutesSpin_ ? runMinutesSpin_->value() : 1;
        const int seconds = runSecondsSpin_ ? runSecondsSpin_->value() : 0;
        const long totalSeconds = std::max<long>(1, static_cast<long>(hours) * 3600L + static_cast<long>(minutes) * 60L + static_cast<long>(seconds));
        const double dt = schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
        const long ticks = std::max<long>(1, static_cast<long>(std::llround(static_cast<double>(totalSeconds) / std::max(1e-12, dt))));

        const auto runResult = runtimeService_.runSchema(schemaHandle_.toStdString(), ticks);
        runId_ = QString::fromStdString(runResult.run_id);
        runActive_ = false;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("completed");
        appendLog(QString("[model] Run completed, run_id=%1, duration=%2:%3:%4, ticks=%5")
            .arg(runId_)
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(ticks));
    }
    catch (const std::exception& ex)
    {
        runActive_ = false;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("error");
        appendLog(QString("[model] Run failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::pauseSchemaModel()
{
    if (runId_.isEmpty())
    {
        appendLog("[model] Pause skipped: no active run id");
        return;
    }
    try
    {
        runtimeService_.pauseRun(runId_.toStdString());
        runPaused_ = true;
        updateModelActionStates();
        setModelStatus("paused");
        appendLog(QString("[model] Paused run %1").arg(runId_));
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[model] Pause failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::resumeSchemaModel()
{
    if (runId_.isEmpty())
    {
        appendLog("[model] Resume skipped: no active run id");
        return;
    }
    try
    {
        runtimeService_.resumeRun(runId_.toStdString());
        runPaused_ = false;
        runActive_ = true;
        updateModelActionStates();
        setModelStatus("running");
        appendLog(QString("[model] Resumed run %1").arg(runId_));
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[model] Resume failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::stopSchemaModel()
{
    if (runId_.isEmpty())
    {
        appendLog("[model] Stop skipped: no active run id");
        return;
    }
    try
    {
        runtimeService_.stopRun(runId_.toStdString());
        runActive_ = false;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("stopped");
        appendLog(QString("[model] Stopped run %1").arg(runId_));
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[model] Stop failed: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::resetSchemaModel()
{
    if (schemaHandle_.isEmpty())
    {
        appendLog("[model] Reset skipped: no loaded schema handle");
        return;
    }
    try
    {
        runtimeService_.resetSchema(schemaHandle_.toStdString());
        runActive_ = false;
        runPaused_ = false;
        updateModelActionStates();
        setModelStatus("reset");
        appendLog(QString("[model] Reset schema %1").arg(schemaHandle_));
    }
    catch (const std::exception& ex)
    {
        appendLog(QString("[model] Reset failed: %1").arg(QString::fromUtf8(ex.what())));
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
    appendLog(QString("[schema] properties updated: name=%1 dt=%2")
        .arg(schemaName_)
        .arg(schemaDtSpin_ ? schemaDtSpin_->value() : 0.001, 0, 'g', 9));
}

nlohmann::json MainWindow::buildRuntimeJsonFromScene() const
{
    nlohmann::json root;
    root["id"] = schemaName_.toStdString();
    root["dt"] = schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
    root["nodes"] = nlohmann::json::array();
    root["nets"] = nlohmann::json::array();

    auto nodeItems = scene_->items<QSchematic::Items::Node>();
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpoint> runtimeEndpointByConnector;

    for (const auto& node : nodeItems)
    {
        if (node->data(kRoleVisualSink).toBool())
        {
            continue;
        }

        const QString nodeId = node->data(kRoleNodeId).toString();
        const QString typeName = node->data(kRoleNodeType).toString();
        if (nodeId.isEmpty() || typeName.isEmpty())
        {
            continue;
        }

        QString params = "{}";
        if (const auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(node))
        {
            params = basic->paramsJson();
        }

        nlohmann::json nodeJson;
        nodeJson["id"] = nodeId.toStdString();
        nodeJson["type"] = typeName.toStdString();
        try
        {
            nodeJson["params"] = nlohmann::json::parse(params.toStdString());
        }
        catch (...)
        {
            nodeJson["params"] = nlohmann::json::object();
        }
        root["nodes"].push_back(std::move(nodeJson));
        for (const auto& connector : node->connectors())
        {
            runtimeEndpointByConnector[connector.get()] = ConnectorEndpoint{
                .nodeId = nodeId,
                .connectorName = connector->text()
            };
        }
    }

    if (auto wireManager = scene_->wire_manager())
    {
        for (const auto& net : wireManager->nets())
        {
            std::unordered_set<const wire_system::wire*> netWires;
            for (const auto& wire : net->wires())
            {
                netWires.insert(wire.get());
            }

            nlohmann::json connectionsJson = nlohmann::json::array();
            int outputs = 0;
            int inputs = 0;

            for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
            {
                const auto attached = wireManager->attached_wire(connectorPtr);
                if (!attached || !attached->wire || !netWires.contains(attached->wire))
                {
                    continue;
                }

                connectionsJson.push_back({
                    {"node", endpoint.nodeId.toStdString()},
                    {"connector", endpoint.connectorName.toStdString()}
                });

                if (connectorIsOutput(endpoint.connectorName))
                {
                    ++outputs;
                }
                else
                {
                    ++inputs;
                }
            }

            if (outputs == 1 && inputs >= 1)
            {
                QString netName = net->name();
                if (netName.trimmed().isEmpty())
                {
                    netName = QString("net_%1").arg(root["nets"].size());
                }
                root["nets"].push_back({
                    {"name", netName.toStdString()},
                    {"connections", std::move(connectionsJson)}
                });
            }
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
    librariesRegistered_ = true;
}

void MainWindow::refreshViewAfterLayoutChange()
{
    if (view_)
    {
        view_->fitInView();
    }
}

void MainWindow::updateModelActionStates()
{
    if (actionRunModel_)
    {
        actionRunModel_->setEnabled(!runActive_);
    }
    if (actionPauseModel_)
    {
        actionPauseModel_->setEnabled(runActive_ && !runPaused_ && !runId_.isEmpty());
    }
    if (actionResumeModel_)
    {
        actionResumeModel_->setEnabled(runActive_ && runPaused_ && !runId_.isEmpty());
    }
    if (actionStopModel_)
    {
        actionStopModel_->setEnabled(runActive_ && !runId_.isEmpty());
    }
    if (actionResetModel_)
    {
        actionResetModel_->setEnabled(modelLoaded_ && !schemaHandle_.isEmpty());
    }
}

void MainWindow::setModelStatus(const QString& status)
{
    if (modelStatusLabel_)
    {
        modelStatusLabel_->setText(QString("model: %1").arg(status));
    }
}


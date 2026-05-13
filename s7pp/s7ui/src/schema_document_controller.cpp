#include "schema_document_controller.hpp"
#include "i_schema_runner.hpp"
#include "node_factory.hpp"
#include "scene_roles.hpp"
#include "sheet_presenter.hpp"
#include "s7ui_json_dto.hpp"
#include "elements/basic_node_item.hpp"
#include "elements/sink_node_item.hpp"
#include <qschematic/items/connector.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSignalBlocker>
#include <algorithm>
#include <memory>
#include <vector>
namespace s7ui::ui {
namespace {
using ConnectorEndpoint = s7ui::dto::ConnectorEndpoint;
using LoadedNodeSpec    = s7ui::dto::LoadedNodeSpec;
using LoadedWireSpec    = s7ui::dto::LoadedWireSpec;
auto connectorIsOutput(const QString& name) -> bool
{
    return name.startsWith("out");
}
auto nextCounterFromId(const QString& id) -> int
{
    const auto suffix = id.section('_', -1);
    bool ok = false;
    const int n = suffix.toInt(&ok);
    return ok ? n : 0;
}
auto findConnectorByName(const std::shared_ptr<QSchematic::Items::Node>& node,
                          const QString& name)
    -> std::shared_ptr<QSchematic::Items::Connector>
{
    if (!node) return nullptr;
    for (const auto& c : node->connectors())
        if (c && c->text() == name) return c;
    return nullptr;
}
auto findFirstConnector(const std::shared_ptr<QSchematic::Items::Node>& node,
                         const bool output)
    -> std::shared_ptr<QSchematic::Items::Connector>
{
    if (!node) return nullptr;
    for (const auto& c : node->connectors())
        if (c && connectorIsOutput(c->text()) == output) return c;
    return nullptr;
}
} // namespace
// ---------------------------------------------------------------------------
SchemaDocumentController::SchemaDocumentController(QSchematic::Scene*   scene,
                                                    NodeFactory*         nodeFactory,
                                                    SheetPresenter*      sheetPresenter,
                                                    s7ui::ISchemaRunner* runner,
                                                    QObject*             parent)
    : QObject(parent)
    , scene_(scene)
    , nodeFactory_(nodeFactory)
    , sheetPresenter_(sheetPresenter)
    , runner_(runner)
{
}
LoadedSchemaResult SchemaDocumentController::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error(
            QString("Cannot open file: %1").arg(path).toStdString());
    const QByteArray data = file.readAll();
    const auto root = boost::json::parse(
        std::string(data.constData(), static_cast<std::size_t>(data.size())));
    return loadFromJson(root, path);
}
LoadedSchemaResult SchemaDocumentController::loadFromJson(
    const boost::json::value& root, const QString& sourcePath)
{
    if (!root.is_object())
        throw std::runtime_error("Schema file root must be a JSON object");
    const auto& rootObj = root.as_object();
    // Determine diagram sub-object (unified or legacy).
    const boost::json::value* diagramRoot = &root;
    if (const auto* d = rootObj.if_contains("diagram");
        d && d->is_object()) {
        diagramRoot = d;
    }
    // --- Parse nodes ---
    std::vector<LoadedNodeSpec> nodes;
    if (const auto* v = diagramRoot->as_object().if_contains("nodes")) {
        if (!v->is_array())
            throw std::runtime_error("Schema field 'nodes' must be an array");
        for (const auto& j : v->as_array())
            nodes.push_back(LoadedNodeSpec::fromJson(j));
    }
    // --- Parse wires (with legacy nets fallback) ---
    std::vector<LoadedWireSpec> wires;
    if (const auto* v = diagramRoot->as_object().if_contains("wires")) {
        if (!v->is_array())
            throw std::runtime_error("Schema field 'wires' must be an array");
        for (const auto& j : v->as_array())
            wires.push_back(LoadedWireSpec::fromJson(j));
    }
    if (wires.empty()) {
        if (const auto* v = diagramRoot->as_object().if_contains("nets");
            v && v->is_array())
        {
            for (const auto& netJson : v->as_array()) {
                const auto& netObj  = netJson.as_object();
                const QString netName = QString::fromStdString(
                    s7ui::dto::jsonValueOr<std::string>(netObj, "name", {}));
                const auto* conns = netObj.if_contains("connections");
                if (!conns || !conns->is_array()) continue;
                ConnectorEndpoint output;
                std::vector<ConnectorEndpoint> inputs;
                for (const auto& c : conns->as_array()) {
                    auto ep = ConnectorEndpoint::fromJson(c);
                    if (connectorIsOutput(ep.connectorName)) output = ep;
                    else                                      inputs.push_back(ep);
                }
                if (output.nodeId.isEmpty() || inputs.empty()) continue;
                for (const auto& inp : inputs)
                    wires.push_back(LoadedWireSpec{netName, output, inp, {}});
            }
        }
    }
    // --- Tear down existing state ---
    if (runner_) runner_->stop();
    scene_->clear();
    // --- Apply schema metadata ---
    LoadedSchemaResult result;
    result.nodeCount  = static_cast<int>(nodes.size());
    result.wireCount  = static_cast<int>(wires.size());
    result.dt         = s7ui::dto::jsonValueOr<double>(rootObj, "dt", 0.001);
    result.sheetStandard = "A4";
    result.schemaName = QString::fromStdString(
        s7ui::dto::jsonValueOr<std::string>(rootObj, "id", std::string{}));
    if (result.schemaName.trimmed().isEmpty()) {
        result.schemaName = QFileInfo(sourcePath).baseName();
        if (result.schemaName.trimmed().isEmpty())
            result.schemaName = "s7ui_schema";
    }
    if (const auto* sv = rootObj.if_contains("sheet");
        sv && sv->is_object()) {
        const auto& so = sv->as_object();
        result.sheetStandard = QString::fromStdString(
            s7ui::dto::jsonValueOr<std::string>(so, "standard", std::string{"A4"}));
    }
    // Apply sheet preset (rebuilds overlay)
    if (sheetPresenter_) {
        const auto preset = SheetPresenter::presetFromStandard(result.sheetStandard);
        sheetPresenter_->applyPreset(preset, [this](const QString& msg) {
            emit logged(msg);
        });
        sheetPresenter_->setSchemaName(result.schemaName);
    }
    // --- Populate scene ---
    int nodeCounter = 0;
    QMap<QString, std::shared_ptr<QSchematic::Items::Node>> nodeById;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& spec = nodes[i];
        auto tmpl = nodeFactory_->templateForType(spec.typeName);
        if (spec.inputPorts  >= 0) tmpl.inputPorts  = spec.inputPorts;
        if (spec.outputPorts >= 0) tmpl.outputPorts  = spec.outputPorts;
        tmpl.visualSink = spec.visualSink || tmpl.visualSink;
        const auto item = nodeFactory_->createItemFromTemplate(
            tmpl, static_cast<int>(i), spec.position);
        if (!item) continue;
        const auto node = std::dynamic_pointer_cast<QSchematic::Items::Node>(item);
        if (!node) continue;
        if (auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(item)) {
            basic->setTypeName(spec.typeName);
            basic->setNodeId(spec.id);
            basic->setNodeName(spec.name);
            basic->setParamsJson(QString::fromStdString(boost::json::serialize(spec.params)));
        }
        if (auto sink = std::dynamic_pointer_cast<s7ui::elements::SinkNodeItem>(item))
            sink->setInputCount(std::max(1, tmpl.inputPorts));
        item->setData(kRoleNodeId,    spec.id);
        item->setData(kRoleNodeName,  spec.name);
        item->setData(kRoleNodeType,  spec.typeName);
        item->setData(kRoleVisualSink, tmpl.visualSink);
        scene_->addItem(item);
        nodeById.insert(spec.id, node);
        nodeCounter = std::max(nodeCounter, nextCounterFromId(spec.id));
    }
    if (nodeCounter == 0 && !nodes.empty())
        nodeCounter = static_cast<int>(nodes.size());
    result.nextNodeCounter = nodeCounter;
    // --- Instantiate wires ---
    for (const auto& ws : wires) {
        const auto fromIt = nodeById.find(ws.from.nodeId);
        const auto toIt   = nodeById.find(ws.to.nodeId);
        if (fromIt == nodeById.end() || toIt == nodeById.end()) continue;
        const auto& fromNode = fromIt.value();
        const auto& toNode   = toIt.value();
        if (!fromNode || !toNode) continue;
        const auto fromC = findConnectorByName(fromNode, ws.from.connectorName);
        const auto toC   = findConnectorByName(toNode,   ws.to.connectorName);
        const auto fromF = findFirstConnector(fromNode, true);
        const auto toF   = findFirstConnector(toNode,   false);
        const auto resolvedFrom = fromC ? fromC : fromF;
        const auto resolvedTo   = toC   ? toC   : toF;
        if (!resolvedFrom || !resolvedTo) continue;
        auto wire = std::make_shared<QSchematic::Items::Wire>();
        if (!ws.points.isEmpty()) {
            for (const auto& pt : ws.points)
                wire->append_point(pt);
        } else {
            wire->append_point(resolvedFrom->scenePos());
            wire->append_point(resolvedTo->scenePos());
        }
        wire->simplify();
        scene_->addWire(wire);
    }
    emit logged(QString("[schema] loaded %1 node(s), %2 wire(s)")
                    .arg(result.nodeCount).arg(result.wireCount));
    return result;
}
} // namespace s7ui::ui

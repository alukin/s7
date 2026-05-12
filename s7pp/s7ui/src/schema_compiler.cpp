#include "schema_compiler.hpp"

#include <s7factory.hpp>

#include <unordered_map>
#include <unordered_set>

namespace s7ui::compiler {
namespace {

auto connectorIsOutput(const std::string& connectorName) -> bool { return connectorName.rfind("out", 0) == 0; }
auto connectorIsInput(const std::string& connectorName) -> bool { return connectorName.rfind("in", 0) == 0; }

struct PortSpec {
    std::string direction;
    std::string signalType = "any";
    std::string domain = "stream";
    int maxConnections = 1;
};

using PortMap = std::unordered_map<std::string, PortSpec>; // key: nodeId:connector

auto portKey(const std::string& nodeId, const std::string& connector) -> std::string
{
    return nodeId + ":" + connector;
}

void appendError(CompileReport& report, const std::string& msg)
{
    report.diagnostics.push_back({Diagnostic::Severity::Error, msg});
}

void appendWarning(CompileReport& report, const std::string& msg)
{
    report.diagnostics.push_back({Diagnostic::Severity::Warning, msg});
}

auto toIntOr(const boost::json::object& obj, const char* key, int fallback) -> int
{
    if (const auto* v = obj.if_contains(key); v && v->is_int64()) return static_cast<int>(v->as_int64());
    if (const auto* v = obj.if_contains(key); v && v->is_uint64()) return static_cast<int>(v->as_uint64());
    return fallback;
}

void synthesizePorts(const boost::json::object& node, PortMap& ports)
{
    const auto* idVal = node.if_contains("id");
    if (!idVal || !idVal->is_string()) return;
    const auto nodeId = boost::json::value_to<std::string>(*idVal);

    if (node.if_contains("ports")) {
        return;
    }

    const int inCount = toIntOr(node, "input_ports", 0);
    const int outCount = toIntOr(node, "output_ports", 0);
    for (int i = 0; i < inCount; ++i) {
        ports[portKey(nodeId, "in" + std::to_string(i))] = PortSpec{"input", "any", "stream", 1};
    }
    for (int i = 0; i < outCount; ++i) {
        ports[portKey(nodeId, "out" + std::to_string(i))] = PortSpec{"output", "any", "stream", 1};
    }
}

void loadDeclaredPorts(const boost::json::object& node, PortMap& ports)
{
    const auto* idVal = node.if_contains("id");
    const auto* portsVal = node.if_contains("ports");
    if (!idVal || !idVal->is_string() || !portsVal || !portsVal->is_array()) return;

    const auto nodeId = boost::json::value_to<std::string>(*idVal);
    for (const auto& p : portsVal->as_array()) {
        if (!p.is_object()) continue;
        const auto& po = p.as_object();
        const auto* name = po.if_contains("name");
        const auto* direction = po.if_contains("direction");
        if (!name || !name->is_string() || !direction || !direction->is_string()) continue;

        PortSpec spec;
        spec.direction = boost::json::value_to<std::string>(*direction);
        if (const auto* st = po.if_contains("signal_type"); st && st->is_string()) {
            spec.signalType = boost::json::value_to<std::string>(*st);
        }
        if (const auto* dm = po.if_contains("domain"); dm && dm->is_string()) {
            spec.domain = boost::json::value_to<std::string>(*dm);
        }
        spec.maxConnections = toIntOr(po, "max_connections", spec.direction == "input" ? 1 : 1024);
        ports[portKey(nodeId, boost::json::value_to<std::string>(*name))] = std::move(spec);
    }
}

} // namespace

auto CompileReport::hasErrors() const -> bool
{
    for (const auto& d : diagnostics) {
        if (d.severity == Diagnostic::Severity::Error) return true;
    }
    return false;
}

auto validateRuntimeGraph(const boost::json::value& runtimeGraph) -> CompileReport
{
    CompileReport report;
    if (!runtimeGraph.is_object()) {
        report.diagnostics.push_back({Diagnostic::Severity::Error, "Runtime graph root must be an object"});
        return report;
    }

    const auto& root = runtimeGraph.as_object();
    const auto* nodesValue = root.if_contains("nodes");
    const auto* netsValue = root.if_contains("nets");
    if (!nodesValue || !nodesValue->is_array()) {
        report.diagnostics.push_back({Diagnostic::Severity::Error, "Runtime graph must contain array field 'nodes'"});
        return report;
    }
    if (!netsValue || !netsValue->is_array()) {
        report.diagnostics.push_back({Diagnostic::Severity::Error, "Runtime graph must contain array field 'nets'"});
        return report;
    }

    std::unordered_set<std::string> nodeIds;
    for (const auto& nodeJson : nodesValue->as_array()) {
        if (!nodeJson.is_object()) continue;
        const auto& nodeObj = nodeJson.as_object();
        const auto* id = nodeObj.if_contains("id");
        if (id && id->is_string()) {
            nodeIds.insert(boost::json::value_to<std::string>(*id));
        }
    }

    std::unordered_map<std::string, int> inputUseCount;
    std::size_t netIndex = 0;
    for (const auto& netJson : netsValue->as_array()) {
        ++netIndex;
        if (!netJson.is_object()) {
            report.diagnostics.push_back({Diagnostic::Severity::Error, "Net entry must be an object"});
            continue;
        }

        const auto& netObj = netJson.as_object();
        const auto* connections = netObj.if_contains("connections");
        if (!connections || !connections->is_array()) {
            report.diagnostics.push_back({Diagnostic::Severity::Error,
                "Net #" + std::to_string(netIndex) + " must contain array field 'connections'"});
            continue;
        }

        int outputs = 0;
        int inputs = 0;
        for (const auto& endpointJson : connections->as_array()) {
            if (!endpointJson.is_object()) {
                report.diagnostics.push_back({Diagnostic::Severity::Error,
                    "Net #" + std::to_string(netIndex) + " endpoint must be an object"});
                continue;
            }
            const auto& endpointObj = endpointJson.as_object();
            const auto* node = endpointObj.if_contains("node");
            const auto* connector = endpointObj.if_contains("connector");
            if (!node || !node->is_string() || !connector || !connector->is_string()) {
                report.diagnostics.push_back({Diagnostic::Severity::Error,
                    "Net #" + std::to_string(netIndex) + " endpoint must contain string 'node' and 'connector'"});
                continue;
            }

            const auto connectorName = boost::json::value_to<std::string>(*connector);
            const auto nodeId = boost::json::value_to<std::string>(*node);
            if (!nodeIds.contains(nodeId)) {
                report.diagnostics.push_back({Diagnostic::Severity::Error,
                    "Net #" + std::to_string(netIndex) + " references unknown node '" + nodeId + "'"});
                continue;
            }
            if (connectorIsOutput(connectorName)) {
                ++outputs;
            } else if (connectorIsInput(connectorName)) {
                ++inputs;
                ++inputUseCount[nodeId + ":" + connectorName];
            } else {
                report.diagnostics.push_back({Diagnostic::Severity::Error,
                    "Net #" + std::to_string(netIndex) + " connector must be prefixed with 'out' or 'in'"});
            }
        }

        if (outputs != 1 || inputs < 1) {
            report.diagnostics.push_back({Diagnostic::Severity::Error,
                "Net #" + std::to_string(netIndex) + " must have exactly one output and at least one input"});
        }
    }

    for (const auto& [key, count] : inputUseCount) {
        if (count > 1) {
            report.diagnostics.push_back({Diagnostic::Severity::Error,
                "Input port '" + key + "' has " + std::to_string(count) + " connections (max 1)"});
        }
    }

    return report;
}

auto validateDiagramConnections(const boost::json::value& diagramDocument) -> CompileReport
{
    CompileReport report;
    if (!diagramDocument.is_object()) {
        appendError(report, "Diagram document root must be an object");
        return report;
    }

    const auto& doc = diagramDocument.as_object();
    const auto* diagramVal = doc.if_contains("diagram");
    const auto* root = (diagramVal && diagramVal->is_object()) ? &diagramVal->as_object() : &doc;

    const auto* nodesVal = root->if_contains("nodes");
    const auto* wiresVal = root->if_contains("wires");
    if (!nodesVal || !nodesVal->is_array()) {
        appendError(report, "Diagram must contain array field 'nodes'");
        return report;
    }
    if (!wiresVal || !wiresVal->is_array()) {
        appendWarning(report, "Diagram has no 'wires' array; skipping diagram-level type/domain validation");
        return report;
    }

    PortMap ports;
    std::unordered_set<std::string> nodeIds;
    std::unordered_set<std::string> connectedNodeIds;
    for (const auto& nodeVal : nodesVal->as_array()) {
        if (!nodeVal.is_object()) continue;
        const auto& node = nodeVal.as_object();
        if (const auto* idVal = node.if_contains("id"); idVal && idVal->is_string()) {
            const auto id = boost::json::value_to<std::string>(*idVal);
            if (!nodeIds.insert(id).second) {
                appendError(report, "Duplicate node id: '" + id + "'");
            }
        }
        if (const auto* typeVal = node.if_contains("type"); typeVal && typeVal->is_string()) {
            const auto typeName = boost::json::value_to<std::string>(*typeVal);
            const bool isKnownVisualBuiltin = (typeName == "ScopeSink" || typeName == "ComplexScopeSink");
            if (!isKnownVisualBuiltin && !s7::ElementRegistry::instance().hasType(typeName)) {
                appendWarning(report, "Unknown element type: '" + typeName + "' (fallback visual is expected)");
            }
        }
        synthesizePorts(node, ports);
        loadDeclaredPorts(node, ports);
    }

    std::unordered_map<std::string, int> inputUseCount;
    for (const auto& wireVal : wiresVal->as_array()) {
        if (!wireVal.is_object()) continue;
        const auto& wire = wireVal.as_object();
        const auto* from = wire.if_contains("from");
        const auto* to = wire.if_contains("to");
        if (!from || !from->is_object() || !to || !to->is_object()) {
            appendError(report, "Wire must contain object fields 'from' and 'to'");
            continue;
        }

        const auto& fromObj = from->as_object();
        const auto& toObj = to->as_object();
        const auto* fromNode = fromObj.if_contains("node");
        const auto* fromConn = fromObj.if_contains("connector");
        const auto* toNode = toObj.if_contains("node");
        const auto* toConn = toObj.if_contains("connector");
        if (!fromNode || !fromNode->is_string() || !fromConn || !fromConn->is_string() ||
            !toNode || !toNode->is_string() || !toConn || !toConn->is_string()) {
            appendError(report, "Wire endpoints must contain string 'node' and 'connector'");
            continue;
        }

        const std::string fromNodeId = boost::json::value_to<std::string>(*fromNode);
        const std::string fromConnector = boost::json::value_to<std::string>(*fromConn);
        const std::string toNodeId = boost::json::value_to<std::string>(*toNode);
        const std::string toConnector = boost::json::value_to<std::string>(*toConn);

        if (!nodeIds.contains(fromNodeId)) {
            appendError(report, "Wire source references unknown node '" + fromNodeId + "'");
            continue;
        }
        if (!nodeIds.contains(toNodeId)) {
            appendError(report, "Wire target references unknown node '" + toNodeId + "'");
            continue;
        }

        const auto fromKey = portKey(fromNodeId, fromConnector);
        const auto toKey = portKey(toNodeId, toConnector);
        const auto fromIt = ports.find(fromKey);
        const auto toIt = ports.find(toKey);

        if (fromIt == ports.end()) {
            appendError(report, "Wire references unknown source port '" + fromKey + "'");
            continue;
        }
        if (toIt == ports.end()) {
            appendError(report, "Wire references unknown target port '" + toKey + "'");
            continue;
        }

        connectedNodeIds.insert(fromNodeId);
        connectedNodeIds.insert(toNodeId);

        const bool fromIsOutput = fromIt->second.direction == "output";
        const bool toIsInput = toIt->second.direction == "input";
        if (!fromIsOutput || !toIsInput) {
            appendError(report, "Wire must connect output -> input ('" + fromKey + "' -> '" + toKey + "')");
        }

        const auto maxConn = toIt->second.maxConnections;
        const int used = ++inputUseCount[toKey];
        if (used > maxConn) {
            appendError(report, "Input port '" + toKey + "' exceeds max_connections=" + std::to_string(maxConn));
        }

        const auto& outType = fromIt->second.signalType;
        const auto& inType = toIt->second.signalType;
        if (outType != "any" && inType != "any" && outType != inType) {
            appendError(report, "Signal type mismatch: '" + fromKey + "'(" + outType + ") -> '" + toKey + "'(" + inType + ")");
        }
        const auto& outDomain = fromIt->second.domain;
        const auto& inDomain = toIt->second.domain;
        if (outDomain != inDomain) {
            appendError(report, "Domain mismatch: '" + fromKey + "'(" + outDomain + ") -> '" + toKey + "'(" + inDomain + ")");
        }
    }

    for (const auto& nodeVal : nodesVal->as_array()) {
        if (!nodeVal.is_object()) continue;
        const auto& node = nodeVal.as_object();
        const auto* idVal = node.if_contains("id");
        if (!idVal || !idVal->is_string()) continue;
        const auto id = boost::json::value_to<std::string>(*idVal);
        const auto* visualVal = node.if_contains("visual");
        const bool isVisual = visualVal && visualVal->is_bool() && visualVal->as_bool();
        if (isVisual && !connectedNodeIds.contains(id)) {
            appendWarning(report, "Orphan visual node: '" + id + "' has no wire connections");
        }
    }

    return report;
}

} // namespace s7ui::compiler

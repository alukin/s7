#include "schema_compiler.hpp"
#include "s7ui_json_dto.hpp"

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

void synthesizePorts(const s7ui::dto::CompilerNodeDto& node, PortMap& ports)
{
    if (node.id.empty()) {
        return;
    }
    if (!node.ports.empty()) {
        return;
    }

    const auto& nodeId = node.id;
    const int inCount = node.inputPorts;
    const int outCount = node.outputPorts;
    for (int i = 0; i < inCount; ++i) {
        ports[portKey(nodeId, "in" + std::to_string(i))] = PortSpec{"input", "any", "stream", 1};
    }
    for (int i = 0; i < outCount; ++i) {
        ports[portKey(nodeId, "out" + std::to_string(i))] = PortSpec{"output", "any", "stream", 1};
    }
}

void loadDeclaredPorts(const s7ui::dto::CompilerNodeDto& node, PortMap& ports)
{
    if (node.id.empty()) {
        return;
    }
    for (const auto& p : node.ports) {
        PortSpec spec;
        spec.direction = p.direction;
        spec.signalType = p.signalType;
        spec.domain = p.domain;
        spec.maxConnections = p.maxConnections;
        ports[portKey(node.id, p.name)] = std::move(spec);
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
    s7ui::dto::CompilerRuntimeGraphDto graph;
    try {
        graph = s7ui::dto::CompilerRuntimeGraphDto::fromJson(runtimeGraph);
    } catch (const std::exception& ex) {
        report.diagnostics.push_back({Diagnostic::Severity::Error, ex.what()});
        return report;
    }

    std::unordered_set<std::string> nodeIds;
    for (const auto& node : graph.nodes) {
        if (!node.id.empty()) {
            nodeIds.insert(node.id);
        }
    }

    std::unordered_map<std::string, int> inputUseCount;
    std::size_t netIndex = 0;
    for (const auto& net : graph.nets) {
        ++netIndex;
        int outputs = 0;
        int inputs = 0;
        for (const auto& endpoint : net.connections) {
            const auto& connectorName = endpoint.connector;
            const auto& nodeId = endpoint.node;
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
    s7ui::dto::CompilerDiagramDto diagram;
    try {
        diagram = s7ui::dto::CompilerDiagramDto::fromJson(diagramDocument);
    } catch (const std::exception& ex) {
        appendError(report, ex.what());
        return report;
    }
    if (diagram.wires.empty()) {
        appendWarning(report, "Diagram has no 'wires' array; skipping diagram-level type/domain validation");
        return report;
    }

    PortMap ports;
    std::unordered_set<std::string> nodeIds;
    std::unordered_set<std::string> connectedNodeIds;
    for (const auto& node : diagram.nodes) {
        if (!node.id.empty() && !nodeIds.insert(node.id).second) {
            appendError(report, "Duplicate node id: '" + node.id + "'");
        }
        if (!node.type.empty()) {
            const bool isKnownVisualBuiltin = (node.type == "ScopeSink" || node.type == "ComplexScopeSink");
            if (!isKnownVisualBuiltin && !s7::ElementRegistry::instance().hasType(node.type)) {
                appendWarning(report, "Unknown element type: '" + node.type + "' (fallback visual is expected)");
            }
        }
        synthesizePorts(node, ports);
        loadDeclaredPorts(node, ports);
    }

    std::unordered_map<std::string, int> inputUseCount;
    for (const auto& wire : diagram.wires) {
        const std::string fromNodeId = wire.from.node;
        const std::string fromConnector = wire.from.connector;
        const std::string toNodeId = wire.to.node;
        const std::string toConnector = wire.to.connector;

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

    for (const auto& node : diagram.nodes) {
        if (node.visual && !node.id.empty() && !connectedNodeIds.contains(node.id)) {
            appendWarning(report, "Orphan visual node: '" + node.id + "' has no wire connections");
        }
    }

    return report;
}

} // namespace s7ui::compiler

#pragma once

#include <boost/json.hpp>

#include <string>
#include <vector>

namespace s7ui::compiler {

struct Diagnostic {
    enum class Severity {
        Error,
        Warning,
    };

    Severity severity = Severity::Error;
    std::string message;
};

struct CompileReport {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] auto hasErrors() const -> bool;
};

// Validates runtime graph structure generated from diagram.
auto validateRuntimeGraph(const boost::json::value& runtimeGraph) -> CompileReport;

// Validates diagram-level connections (direction/type/domain/max_connections).
auto validateDiagramConnections(const boost::json::value& diagramDocument) -> CompileReport;

} // namespace s7ui::compiler

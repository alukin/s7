// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "document_session.hpp"

#include <utility>

namespace s7studio {

DocumentSession::DocumentSession()
    : schema_(boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}})
{
}

void DocumentSession::openSchema(boost::json::value schema, QString sourceLabel)
{
    schema_ = std::move(schema);
    description_.clear();
    sourceLabel_ = std::move(sourceLabel);
    path_.clear();
    runtimeNodes_.clear();
    streams_.clear();
    runtimeSeconds_ = 60.0;
    clearScopesBeforeRun_ = true;
    hasSavedDocument_ = false;
}

void DocumentSession::openDiagram(const diagram::DiagramDocument& document, QString sourceLabel)
{
    schema_ = document.runtimeSchema;
    description_ = document.description;
    sourceLabel_ = std::move(sourceLabel);
    path_.clear();
    runtimeNodes_ = document.runtimeNodes;
    streams_ = document.streams;
    runtimeSeconds_ =
        document.runConfiguration.contains("runtime_seconds") ? document.runConfiguration.at("runtime_seconds").to_number<double>() : 60.0;
    clearScopesBeforeRun_ =
        !document.runConfiguration.contains("clear_scope_traces") || document.runConfiguration.at("clear_scope_traces").as_bool();
    if (document.dt > 0.0)
        frequencyHz_ = 1.0 / document.dt;
    hasSavedDocument_ = false;
}

void DocumentSession::reset()
{
    schema_ = boost::json::object{{"name", "untitled"}, {"elements", boost::json::array{}}, {"connections", boost::json::array{}}};
    description_.clear();
    sourceLabel_.clear();
    path_.clear();
    runtimeNodes_.clear();
    streams_.clear();
    runtimeSeconds_ = 60.0;
    clearScopesBeforeRun_ = true;
    hasSavedDocument_ = false;
}

void DocumentSession::setDeployment(boost::json::array runtimeNodes, boost::json::array streams)
{
    runtimeNodes_ = std::move(runtimeNodes);
    streams_ = std::move(streams);
}

void DocumentSession::markSaved(boost::json::value document)
{
    savedDocument_ = std::move(document);
    hasSavedDocument_ = true;
}

bool DocumentSession::modified(const boost::json::value& current) const { return hasSavedDocument_ && current != savedDocument_; }

auto DocumentSession::complete(diagram::DiagramDocument document) const -> diagram::DiagramDocument
{
    document.description = description_;
    document.runConfiguration = {{"runtime_seconds", runtimeSeconds_}, {"clear_scope_traces", clearScopesBeforeRun_}};
    document.runtimeNodes = runtimeNodes_;
    document.streams = streams_;
    return document;
}

} // namespace s7studio

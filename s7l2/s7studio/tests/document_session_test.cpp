// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "document_session.hpp"

int main()
{
    s7studio::DocumentSession session;
    session.openSchema(boost::json::object{{"name", "plant"}}, QStringLiteral("plant.json"));
    if (session.path() != QString{} || session.runtimeSeconds() != 60.0 || !session.clearScopesBeforeRun()) return 1;

    s7studio::diagram::DiagramDocument diagram;
    diagram.runtimeSchema = boost::json::object{{"name", "network"}};
    diagram.description = QStringLiteral("A deployed model");
    diagram.runConfiguration = {{"runtime_seconds", 12.5}, {"clear_scope_traces", false}};
    diagram.dt = 0.002;
    diagram.runtimeNodes = {boost::json::object{{"node_uuid", "node-a"}}};
    diagram.streams = {boost::json::object{{"stream_id", "stream-a"}}};
    session.openDiagram(diagram, QStringLiteral("network.s7diagram"));
    if (session.description() != diagram.description || session.runtimeSeconds() != 12.5 ||
        session.clearScopesBeforeRun() || session.frequencyHz() != 500.0) return 2;
    if (session.complete({}).runtimeNodes != diagram.runtimeNodes || session.complete({}).streams != diagram.streams) return 3;

    const auto savePoint = boost::json::object{{"revision", 1}};
    session.markSaved(savePoint);
    if (session.modified(savePoint) || !session.modified(boost::json::object{{"revision", 2}})) return 4;
    session.setDeployment({boost::json::object{{"node_uuid", "node-b"}}}, {});
    if (session.complete({}).runtimeNodes == diagram.runtimeNodes || !session.complete({}).streams.empty()) return 5;
    session.reset();
    if (!session.path().isEmpty() || !session.runtimeNodes().empty() || !session.streams().empty() ||
        session.runtimeSeconds() != 60.0 || !session.clearScopesBeforeRun()) return 6;
    return 0;
}

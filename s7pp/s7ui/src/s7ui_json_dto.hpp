#pragma once

#include "visual_catalog.hpp"

#include <boost/json.hpp>

#include <QPointF>
#include <QString>
#include <QVector>

#include <string>
#include <vector>

namespace s7ui::dto {

template <typename T>
auto jsonValueOr(const boost::json::object& obj, const char* key, T defaultValue) -> T
{
    if (const auto* value = obj.if_contains(key)) {
        return boost::json::value_to<T>(*value);
    }
    return defaultValue;
}

struct ConnectorEndpoint {
    QString nodeId;
    QString connectorName;

    static auto fromJson(const boost::json::value& value) -> ConnectorEndpoint;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct LoadedNodeSpec {
    QString id;
    QString typeName;
    QString name;
    QPointF position;
    boost::json::value params = boost::json::object();
    int inputPorts = -1;
    int outputPorts = -1;
    bool visualSink = false;

    static auto fromJson(const boost::json::value& nodeJson) -> LoadedNodeSpec;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct LoadedWireSpec {
    QString name;
    ConnectorEndpoint from;
    ConnectorEndpoint to;
    QVector<QPointF> points;

    static auto fromJson(const boost::json::value& wireJson) -> LoadedWireSpec;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct VisualSpecDto {
    static auto fromJson(const boost::json::value& root) -> visual::VisualSpec;
};

struct JsonPoint {
    static auto toJson(const QPointF& point) -> boost::json::value;
    static auto fromJson(const boost::json::value& value) -> QPointF;
};

struct JsonParamsObject {
    static auto validateObject(const QString& text) -> void;
};

struct CompilerPortSpecDto {
    std::string name;
    std::string direction;
    std::string signalType = "any";
    std::string domain = "stream";
    int maxConnections = 1;

    static auto fromJson(const boost::json::value& value) -> CompilerPortSpecDto;
};

struct CompilerNodeDto {
    std::string id;
    std::string type;
    bool visual = false;
    int inputPorts = 0;
    int outputPorts = 0;
    std::vector<CompilerPortSpecDto> ports;

    static auto fromJson(const boost::json::value& value) -> CompilerNodeDto;
};

struct CompilerEndpointDto {
    std::string node;
    std::string connector;

    static auto fromJson(const boost::json::value& value) -> CompilerEndpointDto;
};

struct CompilerWireDto {
    CompilerEndpointDto from;
    CompilerEndpointDto to;

    static auto fromJson(const boost::json::value& value) -> CompilerWireDto;
};

struct CompilerNetDto {
    std::vector<CompilerEndpointDto> connections;

    static auto fromJson(const boost::json::value& value) -> CompilerNetDto;
};

struct CompilerRuntimeGraphDto {
    std::vector<CompilerNodeDto> nodes;
    std::vector<CompilerNetDto> nets;

    static auto fromJson(const boost::json::value& value) -> CompilerRuntimeGraphDto;
};

struct CompilerDiagramDto {
    std::vector<CompilerNodeDto> nodes;
    std::vector<CompilerWireDto> wires;

    static auto fromJson(const boost::json::value& value) -> CompilerDiagramDto;
};

} // namespace s7ui::dto




#pragma once

#include "s7netlist.hpp"

#include <boost/json.hpp>

#include <string>

namespace s7::dto {

struct SchemaLoadedEvent {
    std::string schema_handle;
    std::string schema_id;
    int nodes = 0;
    int connections = 0;
    std::string path;

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct SchemaHandleEvent {
    std::string schema_handle;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RunStartedEvent {
    std::string run_id;
    std::string schema_handle;
    long ticks = 0;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RunIdEvent {
    std::string run_id;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RunCompletedEvent {
    std::string run_id;
    long ticks = 0;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RunFailedEvent {
    std::string run_id;
    std::string error;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

class NetlistJsonMapper {
public:
    static auto paramValueFromJson(const boost::json::value& j) -> ParamValue;
    static auto paramMapFromJson(const boost::json::value& params_obj) -> ParamMap;
    static auto parseNetlist(const boost::json::value& j) -> NetlistDocument;
};

} // namespace s7::dto


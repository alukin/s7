#include "s7core_json_dto.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace s7::dto {
namespace {

struct ConnectorRef {
    int port_index = 0;
    bool is_output = false;
};

auto parseConnectorName(const std::string& connector_name) -> ConnectorRef
{
    if (connector_name.size() < 3) {
        throw std::runtime_error("Connector name must be inN or outN. Got: '" + connector_name + "'");
    }

    const bool is_input = connector_name.starts_with("in");
    const bool is_output = connector_name.starts_with("out");
    if (!is_input && !is_output) {
        throw std::runtime_error("Connector name must start with 'in' or 'out'. Got: '" + connector_name + "'");
    }

    const std::size_t digits_offset = is_output ? 3 : 2;
    if (digits_offset >= connector_name.size()) {
        throw std::runtime_error("Connector name is missing numeric index: '" + connector_name + "'");
    }

    for (std::size_t i = digits_offset; i < connector_name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(connector_name[i]))) {
            throw std::runtime_error("Connector index must be numeric in connector '" + connector_name + "'");
        }
    }

    return ConnectorRef{.port_index = std::stoi(connector_name.substr(digits_offset)), .is_output = is_output};
}

} // namespace

auto SchemaLoadedEvent::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["schema_handle"] = schema_handle;
    obj["schema_id"] = schema_id;
    obj["nodes"] = nodes;
    obj["connections"] = connections;
    if (!path.empty()) {
        obj["path"] = path;
    }
    return obj;
}

auto SchemaHandleEvent::toJson() const -> boost::json::value
{
    return boost::json::object{{"schema_handle", schema_handle}};
}

auto RunStartedEvent::toJson() const -> boost::json::value
{
    return boost::json::object{{"run_id", run_id}, {"schema_handle", schema_handle}, {"ticks", ticks}};
}

auto RunIdEvent::toJson() const -> boost::json::value
{
    return boost::json::object{{"run_id", run_id}};
}

auto RunCompletedEvent::toJson() const -> boost::json::value
{
    return boost::json::object{{"run_id", run_id}, {"ticks", ticks}};
}

auto RunFailedEvent::toJson() const -> boost::json::value
{
    return boost::json::object{{"run_id", run_id}, {"error", error}};
}

auto NetlistJsonMapper::paramValueFromJson(const boost::json::value& j) -> ParamValue
{
    if (j.is_string()) {
        return ParamValue{boost::json::value_to<std::string>(j)};
    }
    if (j.is_number()) {
        return ParamValue{boost::json::value_to<Real>(j)};
    }
    if (j.is_object()) {
        const auto& obj = j.as_object();
        if (obj.if_contains("real") && obj.if_contains("imag")) {
            return ParamValue{Complex{boost::json::value_to<Real>(obj.at("real")), boost::json::value_to<Real>(obj.at("imag"))}};
        }
        throw std::runtime_error(
            "JSON object used as ParamValue must have 'real' and 'imag' fields. Got: " + boost::json::serialize(j));
    }
    if (j.is_array()) {
        throw std::runtime_error(
            "Cannot parse JSON array as ParamValue. Supported: string, number, or {real,imag}: " + boost::json::serialize(j));
    }
    throw std::runtime_error("Cannot convert JSON value to ParamValue: " + boost::json::serialize(j));
}

auto NetlistJsonMapper::paramMapFromJson(const boost::json::value& params_obj) -> ParamMap
{
    ParamMap params;
    if (params_obj.is_null()) {
        return params;
    }
    if (!params_obj.is_object()) {
        throw std::runtime_error("Element 'params' field must be a JSON object. Got: " + boost::json::serialize(params_obj));
    }

    for (const auto& kv : params_obj.as_object()) {
        params[std::string(kv.key())] = paramValueFromJson(kv.value());
    }
    return params;
}

auto NetlistJsonMapper::parseNetlist(const boost::json::value& j) -> NetlistDocument
{
    if (!j.is_object()) {
        throw std::runtime_error("Netlist root must be a JSON object");
    }
    const auto& root = j.as_object();

    NetlistDocument doc;
    if (const auto* id = root.if_contains("id"); id && id->is_string()) {
        doc.schema_id = boost::json::value_to<std::string>(*id);
    }
    if (const auto* dt = root.if_contains("dt"); dt && dt->is_number()) {
        doc.dt = boost::json::value_to<double>(*dt);
    }

    const auto* nodes = root.if_contains("nodes");
    if (!nodes || !nodes->is_array()) {
        throw std::runtime_error("Netlist must contain array field 'nodes'");
    }

    for (const auto& node_json : nodes->as_array()) {
        const auto& node_obj = node_json.as_object();
        NetlistNodeSpec node_spec;
        node_spec.id = boost::json::value_to<std::string>(node_obj.at("id"));
        node_spec.type = boost::json::value_to<std::string>(node_obj.at("type"));
        if (const auto* params = node_obj.if_contains("params")) {
            node_spec.params = paramMapFromJson(*params);
        }
        doc.nodes.push_back(std::move(node_spec));
    }

    if (const auto* nets = root.if_contains("nets"); nets && nets->is_array()) {
        for (const auto& net_json : nets->as_array()) {
            const auto& net_obj = net_json.as_object();
            const auto* connections = net_obj.if_contains("connections");
            if (!connections || !connections->is_array()) {
                throw std::runtime_error("Each net must contain a 'connections' array");
            }

            struct Endpoint {
                std::string node_id;
                int port_index = 0;
                bool is_output = false;
            };

            std::vector<Endpoint> outputs;
            std::vector<Endpoint> inputs;

            for (const auto& endpoint_json : connections->as_array()) {
                const auto& endpoint_obj = endpoint_json.as_object();
                const auto node_id = boost::json::value_to<std::string>(endpoint_obj.at("node"));
                const auto connector_name = boost::json::value_to<std::string>(endpoint_obj.at("connector"));
                const auto connector_ref = parseConnectorName(connector_name);

                Endpoint endpoint{.node_id = node_id, .port_index = connector_ref.port_index, .is_output = connector_ref.is_output};
                if (endpoint.is_output) {
                    outputs.push_back(std::move(endpoint));
                } else {
                    inputs.push_back(std::move(endpoint));
                }
            }

            if (outputs.empty() && inputs.empty()) {
                continue;
            }
            if (outputs.size() != 1) {
                throw std::runtime_error("Each net must define exactly one output connector");
            }

            for (const auto& input : inputs) {
                NetlistConnectionSpec conn;
                conn.from.node_id = outputs.front().node_id;
                conn.from.port_index = outputs.front().port_index;
                conn.to.node_id = input.node_id;
                conn.to.port_index = input.port_index;
                doc.connections.push_back(std::move(conn));
            }
        }
    }

    return doc;
}

} // namespace s7::dto


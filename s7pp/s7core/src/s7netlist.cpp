//
// s7netlist.cpp — SchemaBuilder and JSON ↔ ParamValue helpers
//

#include "s7netlist.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace s7 {

namespace {

struct ConnectorRef
{
    int port_index = 0;
    bool is_output = false;
};

auto parseConnectorName(const std::string& connector_name) -> ConnectorRef
{
    if (connector_name.size() < 3)
    {
        throw std::runtime_error("Connector name must be inN or outN. Got: '" + connector_name + "'");
    }

    const bool is_input = connector_name.starts_with("in");
    const bool is_output = connector_name.starts_with("out");
    if (!is_input && !is_output)
    {
        throw std::runtime_error("Connector name must start with 'in' or 'out'. Got: '" + connector_name + "'");
    }

    const std::size_t digits_offset = is_output ? 3 : 2;
    if (digits_offset >= connector_name.size())
    {
        throw std::runtime_error("Connector name is missing numeric index: '" + connector_name + "'");
    }

    for (std::size_t i = digits_offset; i < connector_name.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(connector_name[i])))
        {
            throw std::runtime_error("Connector index must be numeric in connector '" + connector_name + "'");
        }
    }

    return ConnectorRef{
        .port_index = std::stoi(connector_name.substr(digits_offset)),
        .is_output = is_output
    };
}

} // namespace

auto paramValueFromJson(const nlohmann::json& j) -> ParamValue
{
    if (j.is_string())
    {
        return ParamValue{j.get<std::string>()};
    }

    if (j.is_number())
    {
        return ParamValue{j.get<Real>()};
    }

    if (j.is_object())
    {
        if (j.contains("real") && j.contains("imag"))
        {
            return ParamValue{Complex{j["real"].get<Real>(), j["imag"].get<Real>()}};
        }
        throw std::runtime_error(
            "JSON object used as ParamValue must have 'real' and 'imag' fields. Got: " + j.dump());
    }

    if (j.is_array())
    {
        throw std::runtime_error(
            "Cannot parse JSON array as ParamValue. Supported: string, number, or {real,imag}: " + j.dump());
    }

    throw std::runtime_error("Cannot convert JSON value to ParamValue: " + j.dump());
}

auto paramMapFromJson(const nlohmann::json& params_obj) -> ParamMap
{
    ParamMap params;
    if (params_obj.is_null())
    {
        return params;
    }
    if (!params_obj.is_object())
    {
        throw std::runtime_error(
            "Element 'params' field must be a JSON object. Got: " + params_obj.dump());
    }

    for (const auto& [key, value] : params_obj.items())
    {
        params[key] = paramValueFromJson(value);
    }
    return params;
}

auto SchemaBuilder::parseJson(const nlohmann::json& j) -> NetlistDocument
{
    NetlistDocument doc;
    doc.schema_id = j.value("id", "");
    doc.dt        = j.value("dt", 1.0);

    for (const auto& node_json : j.at("nodes"))
    {
        NetlistNodeSpec node_spec;
        node_spec.id   = node_json.at("id").get<std::string>();
        node_spec.type = node_json.at("type").get<std::string>();
        if (node_json.contains("params"))
        {
            node_spec.params = paramMapFromJson(node_json["params"]);
        }
        doc.nodes.push_back(std::move(node_spec));
    }

    if (j.contains("nets"))
    {
        for (const auto& net_json : j["nets"])
        {
            if (!net_json.contains("connections") || !net_json["connections"].is_array())
            {
                throw std::runtime_error("Each net must contain a 'connections' array");
            }

            struct Endpoint
            {
                std::string node_id;
                int port_index = 0;
                bool is_output = false;
            };

            std::vector<Endpoint> outputs;
            std::vector<Endpoint> inputs;

            for (const auto& endpoint_json : net_json["connections"])
            {
                const auto node_id = endpoint_json.at("node").get<std::string>();
                const auto connector_name = endpoint_json.at("connector").get<std::string>();
                const auto connector_ref = parseConnectorName(connector_name);

                Endpoint endpoint{
                    .node_id = node_id,
                    .port_index = connector_ref.port_index,
                    .is_output = connector_ref.is_output
                };
                if (endpoint.is_output)
                {
                    outputs.push_back(std::move(endpoint));
                }
                else
                {
                    inputs.push_back(std::move(endpoint));
                }
            }

            if (outputs.empty() && inputs.empty())
            {
                continue;
            }
            if (outputs.size() != 1)
            {
                throw std::runtime_error("Each net must define exactly one output connector");
            }

            for (const auto& input : inputs)
            {
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

auto SchemaBuilder::parseJsonString(const std::string& json_str) -> NetlistDocument
{
    return parseJson(nlohmann::json::parse(json_str));
}

auto SchemaBuilder::parseJsonFile(const std::string& path) -> NetlistDocument
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open netlist file: " + path);
    }
    return parseJson(nlohmann::json::parse(file));
}

auto SchemaBuilder::build(const NetlistDocument& doc) -> std::shared_ptr<Schema>
{
    return build(doc, ElementRegistry::instance());
}

auto SchemaBuilder::build(const NetlistDocument&  doc,
                          const ElementRegistry& registry) -> std::shared_ptr<Schema>
{
    auto schema = std::make_shared<Schema>(doc.dt, doc.schema_id);

    std::unordered_map<std::string, ElementPtr> id_to_node;

    for (const auto& spec : doc.nodes)
    {
        auto node = registry.create(spec.type, spec.id, spec.params);
        id_to_node[spec.id] = node;
        schema->insert(node);
    }

    for (const auto& conn : doc.connections)
    {
        const auto from_it = id_to_node.find(conn.from.node_id);
        if (from_it == id_to_node.end())
        {
            throw std::runtime_error(
                "Netlist connection references unknown source node: '" +
                conn.from.node_id + "'");
        }

        const auto to_it = id_to_node.find(conn.to.node_id);
        if (to_it == id_to_node.end())
        {
            throw std::runtime_error(
                "Netlist connection references unknown target node: '" +
                conn.to.node_id + "'");
        }

        schema->connect(from_it->second, conn.from.port_index,
                        to_it->second,  conn.to.port_index);
    }

    // Validate causal execution order while loading the netlist.
    schema->prepareExecution();

    return schema;
}

} // namespace s7


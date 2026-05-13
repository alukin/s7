//
// s7netlist.cpp — SchemaBuilder and JSON ↔ ParamValue helpers
//

#include "s7netlist.hpp"
#include "s7core_json_dto.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace s7 {

auto paramValueFromJson(const boost::json::value& j) -> ParamValue
{
    return dto::NetlistJsonMapper::paramValueFromJson(j);
}

auto paramMapFromJson(const boost::json::value& params_obj) -> ParamMap
{
    return dto::NetlistJsonMapper::paramMapFromJson(params_obj);
}

auto SchemaBuilder::parseJson(const boost::json::value& j) -> NetlistDocument
{
    return dto::NetlistJsonMapper::parseNetlist(j);
}

auto SchemaBuilder::parseJsonString(const std::string& json_str) -> NetlistDocument
{
    return parseJson(boost::json::parse(json_str));
}

auto SchemaBuilder::parseJsonFile(const std::string& path) -> NetlistDocument
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open netlist file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parseJson(boost::json::parse(buffer.str()));
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


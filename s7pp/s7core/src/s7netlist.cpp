//
// s7netlist.cpp — SchemaBuilder and JSON ↔ ParamValue helpers
//

#include "s7netlist.hpp"

#include <fstream>
#include <stdexcept>

namespace s7 {

auto paramValueFromJson(const nlohmann::json& j) -> ParamValue
{
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
            "Cannot parse JSON array as ParamValue. Only number or {real,imag} are supported: " + j.dump());
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

    for (const auto& element_json : j.at("elements"))
    {
        NetlistElementSpec spec;
        spec.id   = element_json.at("id").get<std::string>();
        spec.type = element_json.at("type").get<std::string>();
        if (element_json.contains("params"))
        {
            spec.params = paramMapFromJson(element_json["params"]);
        }
        doc.elements.push_back(std::move(spec));
    }

    if (j.contains("connections"))
    {
        for (const auto& conn_json : j["connections"])
        {
            NetlistConnectionSpec conn;
            conn.from.element_id = conn_json["from"]["element"].get<std::string>();
            conn.from.port_index = conn_json["from"].value("port", 0);
            conn.to.element_id   = conn_json["to"]["element"].get<std::string>();
            conn.to.port_index   = conn_json["to"].value("port", 0);
            doc.connections.push_back(std::move(conn));
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

    std::unordered_map<std::string, ElementPtr> id_to_element;

    for (const auto& spec : doc.elements)
    {
        auto element = registry.create(spec.type, spec.id, spec.params);
        id_to_element[spec.id] = element;
        schema->insert(element);
    }

    for (const auto& conn : doc.connections)
    {
        const auto from_it = id_to_element.find(conn.from.element_id);
        if (from_it == id_to_element.end())
        {
            throw std::runtime_error(
                "Netlist connection references unknown source element: '" +
                conn.from.element_id + "'");
        }

        const auto to_it = id_to_element.find(conn.to.element_id);
        if (to_it == id_to_element.end())
        {
            throw std::runtime_error(
                "Netlist connection references unknown target element: '" +
                conn.to.element_id + "'");
        }

        schema->connect(from_it->second, conn.from.port_index,
                        to_it->second,  conn.to.port_index);
    }

    // Validate causal execution order while loading the netlist.
    schema->prepareExecution();

    return schema;
}

} // namespace s7


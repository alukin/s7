#pragma once

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <stdexcept>
#include <string_view>

namespace s7studio {

inline boost::json::value makeDemoSchemaJson()
{
    constexpr std::string_view schema = R"json({
        "name": "s7studio_demo",
        "elements": [
            { "id": "unit", "type": "Constant", "params": { "value": 2.0 } },
            { "id": "sum", "type": "Adder" },
            { "id": "memory", "type": "Delay", "params": { "initial_value": 1.0 } }
        ],
        "connections": [
            { "from_element": "unit", "from_port": 0, "to_element": "sum", "to_port": 0 },
            { "from_element": "memory", "from_port": 0, "to_element": "sum", "to_port": 1 },
            { "from_element": "sum", "from_port": 0, "to_element": "memory", "to_port": 0 }
        ]
    })json";

    boost::system::error_code ec;
    const auto value = boost::json::parse(schema, ec);
    if (ec) {
        throw std::runtime_error("Failed to parse embedded demo schema: " + ec.message());
    }

    return value;
}

} // namespace s7studio

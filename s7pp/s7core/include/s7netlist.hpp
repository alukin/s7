//
// s7netlist.hpp — JSON netlist document model and SchemaBuilder
//
// A NetlistDocument is a plain-data intermediate representation of a
// GUI-authored or hand-written JSON netlist. It can be inspected,
// validated, versioned, or transformed before being turned into a live
// Schema via SchemaBuilder::build().
//
// JSON shape expected by SchemaBuilder::parseJson():
//
//  {
//    "id":   "my_schema",          // optional, default ""
//    "dt":   1e-9,                 // discretisation time, default 1.0
//    "nodes": [
//      {
//        "id":     "c1",
//        "type":   "Constant",
//        "params": { "value": 1.0 }
//      },
//      {
//        "id":     "int1",
//        "type":   "Integrator",
//        "params": { "init_state": 0.0 }
//      }
//    ],
//    "nets": [
//      {
//        "name": "n0",
//        "connections": [
//          { "node": "c1",   "connector": "out0" },
//          { "node": "int1", "connector": "in0" }
//        ]
//      }
//    ]
//  }
//
// Connector names are directional and index-based:
//   "outN" for source connectors
//   "inN"  for destination connectors
//
// ParamValue parameter values can be:
//   number                    → Real
//   { "real": x, "imag": y } → Complex
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "s7.hpp"
#include "s7factory.hpp"

namespace s7 {

    // ------------------------------------------------------------------
    // Netlist document model
    // ------------------------------------------------------------------

    /// Reference to a specific port (input or output) on a named node.
    struct NetlistPortRef {
        std::string node_id;
        int         port_index = 0;
    };

    /// One wire in the netlist.
    struct NetlistConnectionSpec {
        NetlistPortRef from;   ///< Source output port
        NetlistPortRef to;     ///< Destination input port
    };

    /// Describes one node instance: id, library type name, and parameters.
    struct NetlistNodeSpec {
        std::string id;
        std::string type;
        ParamMap    params;
    };

    /// A fully parsed, self-contained netlist document ready to be built
    /// into a live Schema.
    struct NetlistDocument {
        std::string                        schema_id;
        double                             dt = 1.0;
        std::vector<NetlistNodeSpec>       nodes;
        std::vector<NetlistConnectionSpec> connections;
    };

    // ------------------------------------------------------------------
    // JSON ↔ ParamValue helpers
    // ------------------------------------------------------------------

    /// Convert a single JSON value to a ParamValue.
    /// Supports: string, number, complex object.
    ParamValue paramValueFromJson(const boost::json::value& j);

    /// Convert a JSON object of key→value pairs to a ParamMap.
    ParamMap paramMapFromJson(const boost::json::value& params_obj);

    // ------------------------------------------------------------------
    // SchemaBuilder
    // ------------------------------------------------------------------

    /// Converts NetlistDocuments into live Schema objects, and parses JSON
    /// into NetlistDocuments.
    class SchemaBuilder {
    public:
        /// Parse a boost::json::value into a NetlistDocument.
        static NetlistDocument parseJson(const boost::json::value& j);

        /// Parse a JSON string into a NetlistDocument.
        static NetlistDocument parseJsonString(const std::string& json_str);

        /// Parse a JSON file from disk into a NetlistDocument.
        static NetlistDocument parseJsonFile(const std::string& path);

        /// Build a Schema from a NetlistDocument using the global registry.
        static std::shared_ptr<Schema> build(const NetlistDocument& doc);

        /// Build a Schema from a NetlistDocument using an explicit registry.
        static std::shared_ptr<Schema> build(const NetlistDocument&  doc,
                                              const ElementRegistry& registry);
    };

} // namespace s7


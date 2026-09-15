#ifndef S7CORE_INCLUDE_S7_ELEMENT_METADATA_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_METADATA_HPP

#include "s7/element.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <boost/json.hpp>

namespace s7 {

// Parameter specification
struct ParameterSpec {
    std::string name;                       // Parameter name
    std::string type;                       // "real", "int", "string", "enum", "bool"
    std::string description;                // Human-readable description
    boost::json::value default_value;       // Default value
    std::optional<double> min_value;        // For numeric types
    std::optional<double> max_value;        // For numeric types
    std::vector<std::string> enum_values;   // For enum type
    bool required;                          // Must be provided?
};

// Port specification
struct PortSpec {
    std::string name;                       // Port name
    std::string description;                // Description
    SignalType type = SignalType::DOUBLE;
    bool optional;                          // Can be omitted?
};

// Visual representation hints
struct VisualHints {
    std::string shape;                      // "box", "circle", "triangle", "diamond"
    std::string color;                      // Hex color "#RRGGBB"
    int max_width;                          // Suggested width
    int max_height;                         // Suggested height
    std::string icon_name;                  // Icon identifier
};

// Variadic input specification
struct VariadicInputSpec {
    bool enabled;                           // Element supports variadic inputs?
    std::size_t min_count;                  // Minimum inputs
    std::size_t max_count;                  // Maximum inputs
    std::string port_name_template;         // "in_{index}" for naming
    SignalType signal_type = SignalType::DOUBLE;
};

// Complete element metadata
struct ElementMetadata {
    std::string type_name;                  // Unique identifier
    std::string display_name;               // Human-readable name
    std::string description;                // Long description
    std::string category;                   // "basic", "filters", "io", "network", "spectral"
    std::string version;                    // Semantic version
    std::vector<std::string> tags;          // Tags for filtering

    std::vector<PortSpec> input_ports;      // Input specifications
    std::vector<PortSpec> output_ports;     // Output specifications
    std::vector<ParameterSpec> parameters;  // Tunable parameters

    VisualHints visual;                     // For diagram rendering
    VariadicInputSpec variadic_inputs;      // If element supports variadic inputs
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_METADATA_HPP


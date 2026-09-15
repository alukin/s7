# Element Development Guide

Quick reference for creating and registering elements in the S7 platform.

## Quick Start: Creating a Custom Element

### 1. Define the Element Class

```cpp
// my_elements.hpp
#pragma once

#include "s7/element.hpp"

namespace my {

class MyCustomElement : public s7::Element {
public:
    MyCustomElement(double param1, double param2);
    void step() override;

private:
    double param1_;
    double param2_;
    double output_value_;
};

} // namespace my
```

### 2. Implement the Element

```cpp
// my_elements.cpp
#include "my_elements.hpp"

namespace my {

MyCustomElement::MyCustomElement(double param1, double param2)
    : s7::Element("MyCustomElement", false),  // false = stateless
      param1_(param1),
      param2_(param2),
      output_value_(0.0)
{
    register_input(0, "signal", s7::SignalType::DOUBLE, nullptr);
    register_output(0, "result", s7::SignalType::DOUBLE, &output_value_);
}

void MyCustomElement::step() {
    double input = read_input<double>(0);
    output_value_ = (input * param1_) + param2_;
}

} // namespace my
```

### 3. Create Metadata

```cpp
// In my_elements.cpp, add this function

s7::ElementMetadata create_my_custom_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "MyCustomElement";
    meta.display_name = "My Custom Element";
    meta.description = "Applies linear transformation: output = input * param1 + param2";
    meta.category = "custom";
    meta.version = "1.0.0";
    meta.tags = {"stateless", "linear"};
    
    // Input ports
    meta.input_ports.push_back({
        .name = "signal",
        .description = "Input signal",
        .type = s7::SignalType::DOUBLE,
        .optional = false
    });
    
    // Output ports
    meta.output_ports.push_back({
        .name = "result",
        .description = "Transformed signal",
        .type = s7::SignalType::DOUBLE,
        .optional = false
    });
    
    // Parameters
    meta.parameters = {
        {
            .name = "param1",
            .type = "real",
            .description = "Multiplication factor",
            .default_value = 1.0,
            .min_value = -1e6,
            .max_value = 1e6,
            .required = true
        },
        {
            .name = "param2",
            .type = "real",
            .description = "Additive offset",
            .default_value = 0.0,
            .required = false
        }
    };
    
    // Visual hints for s7studio
    meta.visual.shape = "box";
    meta.visual.color = "#FF9800";
    meta.visual.max_width = 120;
    meta.visual.max_height = 50;
    meta.visual.icon_name = "calculator";
    
    return meta;
}
```

### 4. Register in Plugin

```cpp
// If building as a plugin, create plugin_my_elements.cpp

extern "C" {
    S7_PLUGIN_INFO {
        .api_version = 1,
        .element_count = 1,
        .plugin_name = "my-elements"
    };
    
    void s7_register_elements(s7::ElementRegistry* registry) {
        registry->register_element(s7::ElementDescriptor(
            "MyCustomElement",
            [](const s7::ElementCreationContext& ctx) {
                double param1 = ctx.get_real("param1");
                double param2 = ctx.get_real("param2");
                return std::make_unique<my::MyCustomElement>(param1, param2);
            },
            create_my_custom_metadata()
        ));
    }
}
```

## Element Metadata Reference

### Parameter Types

```cpp
// In ParameterSpec:
std::string type;  // One of:
                   // - "real"     (double, maps JSON number)
                   // - "int"      (int64_t, maps JSON integer)
                   // - "string"   (std::string, maps JSON string)
                   // - "bool"     (bool, maps JSON boolean)
                   // - "enum"     (uses enum_values array)
                   // - "array"    (JSON array of values)
```

### Example Parameters

```cpp
// Real number with range
{
    .name = "frequency_hz",
    .type = "real",
    .description = "Frequency in Hz",
    .default_value = 1000.0,
    .min_value = 0.1,
    .max_value = 100000.0,
    .required = true
}

// Integer parameter
{
    .name = "order",
    .type = "int",
    .description = "Filter order",
    .default_value = 4,
    .min_value = 1,
    .max_value = 16,
    .required = true
}

// Enumerated choice
{
    .name = "filter_type",
    .type = "enum",
    .description = "Type of filter",
    .default_value = "lowpass",
    .enum_values = {"lowpass", "highpass", "bandpass"},
    .required = true
}

// String parameter
{
    .name = "file_path",
    .type = "string",
    .description = "Path to output file",
    .required = true
}

// Boolean parameter
{
    .name = "normalize",
    .type = "bool",
    .description = "Normalize output",
    .default_value = false,
    .required = false
}
```

## Element Categories

Use these for consistent organization:

| Category | Use For | Example Color |
|----------|---------|---------------|
| `basic` | Arithmetic, constants, gains | `#4CAF50` (green) |
| `filters` | FIR, IIR, integrators | `#2196F3` (blue) |
| `io` | File/network I/O | `#FF9800` (orange) |
| `network` | Network communication | `#9C27B0` (purple) |
| `control` | Logic, multiplexing, switching | `#F44336` (red) |
| `conversion` | Type conversion, encoding | `#00BCD4` (cyan) |
| `simulation` | Test sources, probes | `#FFEB3B` (yellow) |
| `custom` | User-defined elements | `#795548` (brown) |

## Visual Shapes

| Shape | Use For | Example |
|-------|---------|---------|
| `box` | General processing elements | Filters, math |
| `circle` | Sources, no inputs | Oscillators, constants |
| `triangle` | Sinks, decision points | File output, network send |
| `diamond` | Conditional logic | Switches, gates |
| `trapezoid` | I/O elements | ADC/DAC converters |

## Using the Registry

### In Custom Code

```cpp
#include "s7/element_registry.hpp"

auto& registry = s7::ElementRegistry::instance();

// Find element
const auto* descriptor = registry.find("MyCustomElement");
if (!descriptor) {
    throw std::runtime_error("Element not found");
}

// List all elements
auto all = registry.element_types();
for (const auto& type : all) {
    std::cout << type << "\n";
}

// List by category
auto filters = registry.element_types_in_category("filters");

// Get metadata
const auto& metadata = descriptor->metadata();
std::cout << "Description: " << metadata.description << "\n";

// Create instance (without parameters)
auto element = descriptor->create();

// Create with parameters
s7::ElementCreationContext ctx{metadata, params_json};
auto element = descriptor->create(ctx);
```

## Parameter Extraction in Elements

```cpp
// In element factory lambda:

[](const s7::ElementCreationContext& ctx) {
    // Extract parameters with automatic type conversion
    double param1 = ctx.get_real("param1");      // double
    int64_t param2 = ctx.get_int("param2");      // int64_t
    std::string param3 = ctx.get_string("path"); // std::string
    
    // Check if parameter exists
    if (ctx.has_param("optional_param")) {
        double opt = ctx.get_real("optional_param");
    }
    
    // Create and return element
    return std::make_unique<MyElement>(param1, param2, param3);
}
```

## Stateful Elements (with state)

```cpp
class DelayBuffer : public s7::Element {
public:
    explicit DelayBuffer(int delay_samples)
        : s7::Element("DelayBuffer", true),  // NOTE: has_state = true
          delay_(delay_samples),
          buffer_(delay_samples, 0.0),
          output_value_(0.0)
    {
        register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
        register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
    }
    
    void step() override {
        // Update buffer
        double input = read_input<double>(0);
        output_value_ = buffer_.front();
        buffer_.pop_front();
        buffer_.push_back(input);
    }

private:
    int delay_;
    std::deque<double> buffer_;
    double output_value_;
};
```

## Variadic Input Elements

```cpp
class SignalMixer : public s7::Element {
public:
    SignalMixer() : s7::Element("SignalMixer") {
        register_output(0, "mix", s7::SignalType::DOUBLE, &output_value_);
    }
    
    bool supports_variadic_inputs() const override { return true; }
    std::size_t min_inputs() const override { return 2; }
    std::size_t max_inputs() const override { return 16; }
    
    void add_input(std::string_view name, s7::SignalType type) override {
        register_input(input_ports_.size(), name, type, nullptr);
    }
    
    void step() override {
        double sum = 0.0;
        for (std::size_t i = 0; i < input_ports_.size(); ++i) {
            sum += read_input<double>(i);
        }
        output_value_ = sum / static_cast<double>(input_ports_.size());
    }

private:
    double output_value_;
};

// Metadata for variadic element
s7::ElementMetadata create_mixer_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "SignalMixer";
    meta.display_name = "Signal Mixer";
    meta.description = "Mixes multiple inputs into average output";
    // ... other fields ...
    
    meta.variadic_inputs.enabled = true;
    meta.variadic_inputs.min_count = 2;
    meta.variadic_inputs.max_count = 16;
    meta.variadic_inputs.port_name_template = "in_{index}";
    meta.variadic_inputs.signal_type = s7::SignalType::DOUBLE;
    
    meta.output_ports.push_back({
        .name = "mix",
        .description = "Averaged signal",
        .type = s7::SignalType::DOUBLE
    });
    
    return meta;
}
```

## Complex Signal Elements

```cpp
class ComplexGain : public s7::Element {
public:
    explicit ComplexGain(s7::ComplexValue gain)
        : s7::Element("ComplexGain"),
          gain_(gain),
          output_value_(0.0)
    {
        register_input(0, "in", s7::SignalType::COMPLEX, nullptr);
        register_output(0, "out", s7::SignalType::COMPLEX, &output_value_);
    }
    
    void step() override {
        output_value_ = read_input<s7::ComplexValue>(0) * gain_;
    }

private:
    s7::ComplexValue gain_;
    s7::ComplexValue output_value_;
};
```

## Testing Elements

```cpp
// In test file
#include <catch2/catch.hpp>
#include "my_elements.hpp"

TEST_CASE("MyCustomElement basic functionality") {
    auto elem = std::make_unique<my::MyCustomElement>(2.0, 3.0);
    
    // Verify ports
    REQUIRE(elem->input_count() == 1);
    REQUIRE(elem->output_count() == 1);
    
    // Test computation
    // (This requires manual binding in test or schema context)
    double input_val = 5.0;
    double expected = (5.0 * 2.0) + 3.0;  // = 13.0
    
    // Execute step and verify output
}

TEST_CASE("MyCustomElement metadata") {
    auto meta = my::create_my_custom_metadata();
    
    REQUIRE(meta.type_name == "MyCustomElement");
    REQUIRE(meta.input_ports.size() == 1);
    REQUIRE(meta.output_ports.size() == 1);
    REQUIRE(meta.parameters.size() == 2);
    REQUIRE(meta.parameters[0].name == "param1");
}
```

## Schema JSON Example

```json
{
  "name": "MySchema",
  "elements": [
    {
      "id": "gen1",
      "type": "MyCustomElement",
      "params": {
        "param1": 2.5,
        "param2": -1.0
      }
    },
    {
      "id": "gain1",
      "type": "Gain",
      "params": {
        "factor": 0.5
      }
    }
  ],
  "connections": [
    {
      "from_element": "gen1",
      "from_port": 0,
      "to_element": "gain1",
      "to_port": 0
    }
  ]
}
```

## CMakeLists.txt for Custom Plugin

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_elements_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find s7core
find_package(s7core REQUIRED)

# Create plugin library
add_library(my_elements SHARED
    src/my_elements.cpp
    src/plugin_my_elements.cpp
)

target_include_directories(my_elements PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(my_elements PRIVATE
    s7core
)

# Set library version
set_target_properties(my_elements PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

# Install to standard plugin location
install(TARGETS my_elements
    LIBRARY DESTINATION lib/s7/plugins
)
```

## Checklist: Element Implementation

- [ ] Element class created with proper constructor
- [ ] `step()` method implemented
- [ ] All ports registered in constructor
- [ ] Input/output members initialized
- [ ] Metadata created with all fields
- [ ] Category chosen from standard list
- [ ] Visual hints specified
- [ ] Parameters documented with types and ranges
- [ ] Factory lambda captures parameters correctly
- [ ] Plugin entry point created (if plugin)
- [ ] CMakeLists.txt configured
- [ ] Unit tests written
- [ ] Documentation/comments added
- [ ] Registered with ElementRegistry

## Troubleshooting

### "Unknown element type" error
- Check element is registered via ElementRegistry
- Verify type name matches exactly (case-sensitive)
- Ensure plugin was loaded (check logs)

### Parameter validation failures
- Check parameter names match metadata exactly
- Verify parameter types match (real vs int vs string)
- Ensure required parameters are in JSON
- Check numeric values within min/max range

### Port connection errors
- Verify port index is correct (0-based)
- Check signal types match (or are compatible)
- Ensure source element has outputs, sink has inputs
- Verify from_element/to_element IDs exist in schema

### Symbol not found when loading plugin
- Check plugin linked against s7core shared library
- Verify s7core shared library in LD_LIBRARY_PATH
- Check plugin built with same C++ version
- Use `ldd` to verify dependencies resolved

## Resources

- `ELEMENT_LIBRARY_ANALYSIS.md` - Architecture overview
- `ELEMENT_LIBRARY_IMPLEMENTATION.md` - Implementation roadmap
- `s7core/include/s7/element.hpp` - Element base class
- `s7core/include/s7/element_metadata.hpp` - Metadata structures
- `s7runtime/examples/` - Working element examples


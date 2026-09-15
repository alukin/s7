# Element Development Reference

## Quick Start: Creating a Custom Element

### 1. Define the Element Class

```cpp
// my_elements.hpp
#pragma once

#include "s7/element.hpp"

namespace my {

class LinearTransform : public s7::Element {
public:
    LinearTransform(double scale, double offset);
    void step() override;

private:
    double scale_;
    double offset_;
    double output_value_;
};

} // namespace my
```

### 2. Implement the Element

```cpp
// my_elements.cpp
#include "my_elements.hpp"

namespace my {

LinearTransform::LinearTransform(double scale, double offset)
    : s7::Element("LinearTransform", false),
      scale_(scale),
      offset_(offset),
      output_value_(0.0)
{
    register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
    register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
}

void LinearTransform::step() {
    double input = read_input<double>(0);
    output_value_ = (input * scale_) + offset_;
}

} // namespace my
```

### 3. Create Metadata

```cpp
s7::ElementMetadata create_linear_transform_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "LinearTransform";
    meta.display_name = "Linear Transform";
    meta.description = "Applies y = x*scale + offset";
    meta.category = "basic";
    meta.version = "1.0.0";
    
    meta.input_ports.push_back({
        "in", "Input signal", s7::SignalType::DOUBLE, false
    });
    
    meta.output_ports.push_back({
        "out", "Output signal", s7::SignalType::DOUBLE, false
    });
    
    meta.parameters = {
        {"scale", "real", "Multiplication factor", 1.0, -1e6, 1e6, true},
        {"offset", "real", "Additive offset", 0.0, std::nullopt, std::nullopt, false}
    };
    
    meta.visual = {"box", "#4CAF50", 100, 50, "calculator"};
    return meta;
}
```

### 4. Register in Plugin

```cpp
// plugin_my_elements.cpp
extern "C" {
    const S7_PLUGIN_INFO = {
        .api_version = 1,
        .element_count = 1,
        .plugin_name = "my-elements"
    };
    
    void s7_register_elements(s7::ElementRegistry* registry) {
        registry->register_element(s7::ElementDescriptor(
            "LinearTransform",
            [](const s7::ElementCreationContext& ctx) {
                double scale = ctx.get_real("scale");
                double offset = ctx.get_real("offset");
                return std::make_unique<my::LinearTransform>(scale, offset);
            },
            create_linear_transform_metadata()
        ));
    }
}
```

## Parameter Types

### Real (Double)
```cpp
{
    .name = "frequency_hz",
    .type = "real",
    .description = "Frequency in Hz",
    .default_value = 1000.0,
    .min_value = 0.1,
    .max_value = 100000.0,
    .required = true
}
```

### Integer
```cpp
{
    .name = "order",
    .type = "int",
    .description = "Filter order",
    .default_value = 4,
    .min_value = 1,
    .max_value = 16,
    .required = true
}
```

### String
```cpp
{
    .name = "file_path",
    .type = "string",
    .description = "Path to file",
    .required = true
}
```

### Enumeration
```cpp
{
    .name = "filter_type",
    .type = "enum",
    .description = "Type of filter",
    .default_value = "lowpass",
    .enum_values = {"lowpass", "highpass", "bandpass"},
    .required = true
}
```

### Boolean
```cpp
{
    .name = "normalize",
    .type = "bool",
    .description = "Normalize output",
    .default_value = false,
    .required = false
}
```

## Element Categories

| Category | Use For | Color |
|----------|---------|-------|
| `basic` | Arithmetic, constants, gains | #4CAF50 |
| `filters` | FIR, IIR, integrators | #2196F3 |
| `io` | File/network I/O | #FF9800 |
| `network` | Network communication | #9C27B0 |
| `control` | Logic, multiplexing | #F44336 |
| `conversion` | Type conversion | #00BCD4 |
| `custom` | User-defined | #795548 |

## Registry Usage

```cpp
#include "s7/element_registry.hpp"

auto& registry = s7::ElementRegistry::instance();

// Find element
const auto* descriptor = registry.find("LinearTransform");
if (!descriptor) throw std::runtime_error("Not found");

// Get metadata
const auto& meta = descriptor->metadata();

// List all elements
auto all = registry.element_types();

// Filter by category
auto filters = registry.element_types_in_category("filters");

// Create element
s7::ElementCreationContext ctx{meta, params_json};
auto elem = descriptor->create(ctx);
```

## Stateful Elements

Elements with internal state set `has_state = true`:

```cpp
class DelayBuffer : public s7::Element {
public:
    explicit DelayBuffer(int delay_samples)
        : s7::Element("DelayBuffer", true),  // has_state = true
          delay_(delay_samples),
          buffer_(delay_samples, 0.0),
          output_value_(0.0)
    {
        register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
        register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
    }
    
    void step() override {
        output_value_ = buffer_.front();
        buffer_.pop_front();
        buffer_.push_back(read_input<double>(0));
    }

private:
    int delay_;
    std::deque<double> buffer_;
    double output_value_;
};
```

## Variadic Input Element

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
        register_input(input_ports_.size(), std::string(name), type, nullptr);
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

s7::ElementMetadata create_mixer_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "SignalMixer";
    meta.display_name = "Signal Mixer";
    meta.category = "basic";
    
    meta.variadic_inputs.enabled = true;
    meta.variadic_inputs.min_count = 2;
    meta.variadic_inputs.max_count = 16;
    meta.variadic_inputs.port_name_template = "in_{index}";
    meta.variadic_inputs.signal_type = s7::SignalType::DOUBLE;
    
    meta.output_ports.push_back({
        "mix", "Averaged output", s7::SignalType::DOUBLE
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

## CMakeLists.txt for Plugin

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_elements_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(s7core REQUIRED)

add_library(my_elements SHARED
    src/my_elements.cpp
    src/plugin_my_elements.cpp
)

target_include_directories(my_elements PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(my_elements PRIVATE s7core)

set_target_properties(my_elements PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

install(TARGETS my_elements
    LIBRARY DESTINATION lib/s7/plugins
)
```

## Schema JSON

```json
{
  "name": "my_schema",
  "elements": [
    {
      "id": "gen",
      "type": "AnalogOscillator",
      "params": {
        "frequency_hz": 1000,
        "amplitude": 1.0,
        "sample_rate_hz": 44100
      }
    },
    {
      "id": "transform",
      "type": "LinearTransform",
      "params": {
        "scale": 2.5,
        "offset": -1.0
      }
    }
  ],
  "connections": [
    {
      "from_element": "gen",
      "from_port": 0,
      "to_element": "transform",
      "to_port": 0
    }
  ]
}
```

## Troubleshooting

### "Unknown element type" error
- Check type name matches exactly (case-sensitive)
- Verify plugin loaded (check startup logs)
- Confirm registration called in s7_register_elements()

### Parameter validation failures
- Check parameter names match metadata exactly
- Verify parameter types (real vs int vs string)
- Ensure required parameters in JSON
- Check numeric ranges

### Port connection errors
- Verify port index correct (0-based)
- Check signal types match
- Confirm source has outputs, sink has inputs
- Verify element IDs exist in schema

### Symbol not found when loading plugin
- Check plugin linked against s7core shared library
- Verify s7core shared library in LD_LIBRARY_PATH
- Use `ldd` to verify dependencies

## Implementation Checklist

- [ ] Element class created and constructor working
- [ ] `step()` method implemented
- [ ] All ports registered in constructor
- [ ] Input/output members initialized
- [ ] Metadata created with all fields
- [ ] Factory lambda captures parameters correctly
- [ ] Plugin entry point created (if plugin)
- [ ] CMakeLists.txt configured
- [ ] Builds without errors
- [ ] Registers successfully with ElementRegistry


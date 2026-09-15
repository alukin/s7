# Plugin Interface Specification

## Overview

The plugin interface allows external shared libraries to provide custom elements to s7runtime without modifying core code. Plugins are loaded dynamically at startup and register their elements with the central ElementRegistry.

## C ABI Interface

Located in: `s7core/include/s7/element_plugin.hpp`

### Plugin Information Structure

```cpp
struct S7PluginInfo {
    uint32_t api_version;           // For compatibility checking
    uint32_t element_count;         // Number of elements provided
    const char* plugin_name;        // Human-readable name
    const char* plugin_version;     // Plugin version string
    const char* author;             // Author/organization
};
```

### Required C Export Functions

#### 1. s7_plugin_query()
```cpp
const S7PluginInfo* s7_plugin_query();
```

**Purpose:** Provide plugin metadata to loader
**Called:** Once during plugin discovery
**Returns:** Static S7PluginInfo structure
**Example:**
```cpp
static const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 3,
    .plugin_name = "s7spectral",
    .plugin_version = "1.0.0",
    .author = "S7 Project"
};

extern "C" {
const S7PluginInfo* s7_plugin_query() {
    return &plugin_info;
}
}
```

#### 2. s7_register_elements()
```cpp
void s7_register_elements(void* registry_ptr);
```

**Purpose:** Register all elements provided by plugin
**Called:** Once after plugin load verification
**Argument:** Pointer to s7::ElementRegistry (cast internally)
**Example:**
```cpp
extern "C" {
void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    
    registry->register_element(s7::ElementDescriptor(
        "MyElement",
        [](const s7::ElementCreationContext& ctx) {
            return std::make_unique<MyElement>();
        },
        create_my_element_metadata()
    ));
}
}
```

## Plugin Discovery and Loading

### Search Paths

Plugins are searched in this order:
1. `~/.s7/plugins/`
2. `/opt/s7/plugins/`
3. `./plugins/` (relative to s7runtime)
4. `$S7_PLUGIN_PATH` (environment variable)

### Naming Convention

- Linux/Unix: `libs7{name}.so` or `libs7{name}.so.1`
- macOS: `libs7{name}.dylib`
- Windows: `s7{name}.dll`

Example: s7spectral library → `libs7spectral.so`

### Loading Process

1. **Discovery**: Scan plugin directories for matching library names
2. **Load**: `dlopen()` the shared library
3. **Query**: Call `s7_plugin_query()` to get metadata
4. **Verify**: Check API version compatibility
5. **Register**: Call `s7_register_elements()` with registry pointer
6. **Complete**: Plugin elements now available in registry

### Failure Handling

If any step fails:
- Library unloaded with `dlclose()`
- Warning logged
- Runtime continues with other plugins
- Schema compilation fails only if required element not found

## Element Registration

### ElementDescriptor

Combines factory function with metadata:

```cpp
class ElementDescriptor {
public:
    ElementDescriptor(
        std::string type_name,
        ElementFactory factory,
        const ElementMetadata& metadata
    );
    
    const ElementMetadata& metadata() const;
    std::unique_ptr<Element> create() const;
    std::unique_ptr<Element> create(const ElementCreationContext& ctx) const;
};
```

### ElementFactory

Type definition:
```cpp
using ElementFactory = std::function<std::unique_ptr<Element>()>;
```

Lambda factory pattern:
```cpp
registry->register_element(s7::ElementDescriptor(
    "Gain",
    [](const s7::ElementCreationContext& ctx) {
        double factor = ctx.get_real("factor");
        return std::make_unique<s7::Gain>(factor);
    },
    create_gain_metadata()
));
```

### ElementMetadata

Complete element description:

```cpp
struct ElementMetadata {
    std::string type_name;          // "Gain"
    std::string display_name;       // "Gain / Amplifier"
    std::string description;        // Long description
    std::string category;           // "basic", "filters", "spectral"
    std::string version;            // "1.0.0"
    std::vector<std::string> tags;  // {"stateless", "linear"}
    
    struct Port {
        std::string name;           // "in", "out"
        std::string description;
        SignalType type;            // DOUBLE, COMPLEX, INT16
        bool optional;              // Can connection be omitted?
    };
    
    std::vector<Port> input_ports;
    std::vector<Port> output_ports;
    std::vector<ParameterSpec> parameters;
    
    struct VisualHints {
        std::string shape;          // "box", "circle", "triangle"
        std::string color;          // "#RRGGBB"
        int max_width;
        int max_height;
        std::string icon_name;
    };
    VisualHints visual;
};
```

### ElementCreationContext

Parameter extraction helper:

```cpp
struct ElementCreationContext {
    const ElementMetadata& metadata;
    const boost::json::object& params;
    
    double get_real(const std::string& name) const;
    int64_t get_int(const std::string& name) const;
    std::string get_string(const std::string& name) const;
    bool has_param(const std::string& name) const;
};
```

## Example: Complete Plugin

### Plugin Structure

```cpp
// plugin_example.cpp - Single file plugin

#include "s7/element.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"
#include <memory>

// ============================================================================
// Element Implementation
// ============================================================================

class MyCustomElement : public s7::Element {
public:
    explicit MyCustomElement(double parameter)
        : s7::Element("MyCustomElement", false),
          parameter_(parameter),
          output_value_(0.0)
    {
        register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
        register_output(0, "out", s7::SignalType::DOUBLE, &output_value_);
    }
    
    void step() override {
        output_value_ = read_input<double>(0) * parameter_;
    }

private:
    double parameter_;
    double output_value_;
};

// ============================================================================
// Metadata
// ============================================================================

s7::ElementMetadata create_my_custom_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "MyCustomElement";
    meta.display_name = "My Custom Element";
    meta.description = "Custom processing element";
    meta.category = "custom";
    meta.version = "1.0.0";
    
    meta.input_ports.push_back({
        "in", "Input", s7::SignalType::DOUBLE, false
    });
    meta.output_ports.push_back({
        "out", "Output", s7::SignalType::DOUBLE, false
    });
    meta.parameters.push_back({
        "parameter", "real", "Processing parameter", 1.0,
        -1e6, 1e6, true
    });
    
    meta.visual = {"box", "#FF9800", 100, 50, "calculator"};
    return meta;
}

// ============================================================================
// Plugin Information
// ============================================================================

static const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 1,
    .plugin_name = "example",
    .plugin_version = "1.0.0",
    .author = "Your Name"
};

// ============================================================================
// Plugin Interface Implementation
// ============================================================================

extern "C" {

const S7PluginInfo* s7_plugin_query() {
    return &plugin_info;
}

void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    
    registry->register_element(s7::ElementDescriptor(
        "MyCustomElement",
        [](const s7::ElementCreationContext& ctx) {
            double param = ctx.get_real("parameter");
            return std::make_unique<MyCustomElement>(param);
        },
        create_my_custom_metadata()
    ));
}

}
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(example_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(s7core REQUIRED)

add_library(s7example SHARED src/plugin_example.cpp)

target_include_directories(s7example PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(s7example PRIVATE s7core)

set_target_properties(s7example PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

install(TARGETS s7example
    LIBRARY DESTINATION lib/s7/plugins
)
```

## API Versioning

### Current Version
```cpp
#define S7_PLUGIN_API_VERSION 1
```

### Breaking Changes
- Increment `S7_PLUGIN_API_VERSION` when:
  - S7PluginInfo structure changes
  - Element/Registry interface changes
  - ParameterSpec format changes
  - SignalType enum modified

### Backward Compatibility
- Plugins built for version N will not load on version N+1
- Plugins can be rebuilt without source changes for patch versions
- Clear error message when version mismatch detected

## Symbol Resolution

### Required Symbols from s7core

Plugins must link against s7core shared library. Required symbols:
- `s7::Element` (base class)
- `s7::ElementRegistry::instance()`
- `s7::ElementMetadata`
- `s7::ElementDescriptor`
- `s7::SignalType` (enum)
- `s7::SpectralFrame` (if used)

### Symbol Visibility

All plugin symbols internal except:
```cpp
extern "C" {
    const S7PluginInfo* s7_plugin_query();
    void s7_register_elements(void* registry_ptr);
}
```

## Performance Considerations

### Plugin Loading
- Occurs once at s7runtime startup
- Typical time: 1-10 ms per plugin
- Parallelizable if multiple plugins

### Plugin Execution
- No overhead vs built-in elements
- Direct method calls (virtual dispatch)
- Same execution path as core elements

### Memory
- Plugin .so file loaded once
- Metadata remains in memory
- Element instances created on-demand

## Security Considerations

### Trust Model
- Plugins have full process access
- No sandboxing implemented
- Load only trusted plugins

### Future Hardening
- Option to disable plugin loading
- Plugin signature verification
- Restricted plugin directories

## Troubleshooting

### Plugin Not Found
- Check naming convention (libs7{name}.so)
- Verify plugin directory in search path
- Set `S7_PLUGIN_PATH` environment variable

### Version Mismatch
```
Error: Plugin s7example API version 1 doesn't match runtime version 2
```
- Rebuild plugin against current s7core

### Symbol Resolution Error
```
error: undefined reference to `s7::Element::Element(...)
```
- Verify plugin linked against s7core shared library
- Check LD_LIBRARY_PATH includes s7core location

### Crash on Load
- Plugin likely violates ABI contract
- Check C++ standard (must be 20)
- Verify no static initialization issues
- Use `gdb` to get backtrace

## Best Practices

1. **Single Responsibility**: Each plugin should provide related elements
2. **Metadata**: Complete and accurate metadata improves usability
3. **Error Handling**: Throw exceptions for invalid parameters
4. **Dependencies**: Document external library requirements
5. **Testing**: Test plugin loading independently
6. **Documentation**: Provide clear usage examples
7. **Version**: Increment version for releases
8. **CMakeLists**: Use standard installation paths

## Examples

Complete working examples:
- **s7spectral**: FFT-based spectral analysis
  - Path: `/home/al/projects/s7/s7l2/s7spectral/`
  - Elements: WindowedFFT, MagnitudeSpectrum, PowerSpectrumDB
  - Dependencies: FFTW3

Use s7spectral as template for new plugins.


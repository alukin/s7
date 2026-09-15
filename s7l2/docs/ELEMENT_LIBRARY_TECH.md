# Element Library Architecture

## Current System Overview

### Library Structure
- **s7core**: Base signal processing framework with element base class, templated elements, schema, compiler
- **s7io**: File I/O elements (sources and sinks with multiple format support)
- **s7net**: Network connectivity (NetSink, topology definitions)
- **s7runtime**: Main application that loads JSON schemas and instantiates elements

### Element Definition Model

**Element Base Class** (element.hpp):
- Owns member variables for signal storage
- Ports are lightweight references to those members
- Elements register input/output ports during construction
- Provides `step()` virtual interface for computation
- Uses type-erased bindings for runtime connections

**Template-Based Elements** (elements.hpp):
- `ConstantT<T>`, `GainT<T>`, `AdderT<T>`, `MultiplyT<T>`, `DelayT<T>`
- Type aliases for Real and Complex variants
- Real-only elements: FirFilter, IirFilter, Integrator, AnalogOscillator
- Type conversion elements: ReImToComplex, ComplexToReIm, ComplexToMagnitudePhase

**Factory Pattern** (schema.hpp):
- `ElementFactory = std::function<std::unique_ptr<Element>()>`
- Each element registered with factory lambda
- Schema compiles to ExecutableGraph with ordered elements

**Runtime Registration** (runtime_service.cpp):
- Parses JSON element definitions with params
- Creates factory lambdas with captured parameters

## Desired Features

### 1. Dynamic Shared Library Loading
- Compile s7core, s7io, s7net as shared libraries
- s7runtime loads these as separate shared objects
- Support external plugin libraries with same interface

### 2. Built-in Element Descriptions
- Metadata about each element: name, description, version
- Categorization (basic math, filters, I/O, network)
- Help/documentation strings

### 3. Tunable Parameters
- Standardized parameter schema for each element
- Type information (int, float, string, enum, array)
- Default values, min/max ranges
- Runtime parameter validation

### 4. Variable Number of Inputs
- Elements can accept variable number of inputs (variadic adders, mixers)
- Signal names descriptive, not just index-based
- Support optional/variadic ports

### 5. Visual Representation Hints
- Element categories for visual grouping
- Port layout hints
- Shape/icon suggestions for diagrams
- Color coding by signal type or category

## Proposed Architecture

### Three-Tier Design

**TIER 1: METADATA LAYER**
```cpp
struct ParameterSpec {
    std::string name;
    std::string type;              // "real", "int", "string", "enum"
    std::string description;
    boost::json::value default_value;
    std::optional<double> min_value;
    std::optional<double> max_value;
    bool required = false;
};

struct ElementMetadata {
    std::string type_name;
    std::string display_name;
    std::string description;
    std::string category;          // "basic", "filters", "io", "network"
    std::string version;
    std::vector<std::string> tags;
    
    struct Port {
        std::string name;
        std::string description;
        SignalType type;
        bool optional = false;
    };
    
    std::vector<Port> input_ports;
    std::vector<Port> output_ports;
    std::vector<ParameterSpec> parameters;
    
    struct VisualHints {
        std::string shape;          // "box", "circle", "triangle"
        std::string color;
        int max_width = 100;
        int max_height = 50;
        std::string icon_name;
    };
    VisualHints visual;
};
```

**TIER 2: REGISTRY LAYER**
```cpp
class ElementDescriptor {
public:
    ElementDescriptor(std::string type_name, ElementFactory factory, 
                      const ElementMetadata& metadata);
    
    [[nodiscard]] const ElementMetadata& metadata() const;
    [[nodiscard]] std::unique_ptr<Element> create() const;
    [[nodiscard]] std::unique_ptr<Element> create(const boost::json::object& params) const;

private:
    ElementFactory factory_;
    ElementMetadata metadata_;
};

class ElementRegistry {
public:
    static ElementRegistry& instance();  // Singleton
    
    void register_element(const ElementDescriptor& descriptor);
    [[nodiscard]] const ElementDescriptor* find(const std::string& type_name) const;
    [[nodiscard]] const std::vector<ElementDescriptor>& all_descriptors() const;
    [[nodiscard]] std::vector<std::string> element_types() const;
    [[nodiscard]] std::vector<std::string> element_types_in_category(const std::string& category) const;
    
private:
    std::unordered_map<std::string, ElementDescriptor> descriptors_;
};
```

**TIER 3: PLUGIN LAYER**
```cpp
// C ABI interface for plugins
extern "C" {
    struct S7ElementPluginInfo {
        uint32_t api_version;
        uint32_t element_count;
    };
    
    const S7ElementPluginInfo* s7_plugin_info();
    void s7_register_elements(s7::ElementRegistry* registry);
}

class ElementPlugin {
public:
    explicit ElementPlugin(const std::string& library_path);
    ~ElementPlugin();
    
    [[nodiscard]] bool is_loaded() const;
    void load();
    void unload();
    
    static std::vector<std::string> discover_plugins(const std::filesystem::path& plugin_dir);

private:
    std::string library_path_;
    void* handle_;
};
```

### Variadic Element Support

```cpp
class VariadicElement : public Element {
public:
    [[nodiscard]] bool supports_variadic_inputs() const override { return true; }
    [[nodiscard]] std::size_t min_inputs() const { return min_inputs_; }
    [[nodiscard]] std::size_t max_inputs() const { return max_inputs_; }
    
    void add_input(std::string_view name, SignalType type) override;
    void remove_input(std::size_t index) override;

protected:
    void set_variadic_constraints(std::size_t min_inputs, std::size_t max_inputs);

private:
    std::size_t min_inputs_ = 1;
    std::size_t max_inputs_ = std::numeric_limits<std::size_t>::max();
};
```

### Parameter Validation

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

## Build System Changes

Convert s7core, s7io, s7net to shared libraries:

```cmake
# Build as shared libraries (not static)
add_library(s7core SHARED ...)
add_library(s7io SHARED ...)
add_library(s7net SHARED ...)

# s7runtime links shared libs
target_link_libraries(s7runtime PRIVATE
    s7core
    s7io
    s7net
)

# Plugin example
add_library(s7elements_builtins SHARED
    src/builtin_elements.cpp
)
target_link_libraries(s7elements_builtins PRIVATE s7core)
```

## Implementation Roadmap

### Phase 1: Metadata Framework (Weeks 1-2)
- Create `s7core/include/s7/element_metadata.hpp`
- Create `s7core/include/s7/element_registry.hpp`
- Implement registry singleton
- Unit tests for registry operations

### Phase 2: Element Annotation (Weeks 2-3)
- Add metadata to all built-in elements (~20 elements)
- Metadata for I/O elements
- Metadata for network elements
- Proof of concept with 2-3 elements

### Phase 3: Plugin Infrastructure (Weeks 3-4)
- Implement `element_plugin.hpp` (dlopen/dlsym wrapper)
- C plugin interface (extern "C")
- Plugin discovery and loading
- Example plugin

### Phase 4: Build System (Weeks 4-5)
- Convert to shared libraries
- Add plugin search paths
- CMake configuration for plugins
- Installation setup

### Phase 5: Runtime Integration (Weeks 5-6)
- Update RuntimeService to use registry
- Remove hardcoded switch statement
- Parameter validation in build_schema()
- Plugin loading at startup

### Phase 6: Variadic Support (Weeks 6-7)
- Extend Element base class
- Implement VariadicAdder element
- Update compiler for variable ports
- Metadata for variadic constraints

### Phase 7: Visual Integration (Weeks 7-8)
- ElementCatalogService with metadata export
- JSON catalog format
- Integration points documented

## Element Metadata Specification

### Parameter Types
- `"real"`: Double-precision floating point
- `"int"`: 64-bit signed integer
- `"string"`: UTF-8 string
- `"bool"`: Boolean
- `"enum"`: Enumerated choice
- `"array"`: JSON array

### Element Categories
- `"basic"`: Arithmetic, constants, gains (color: #4CAF50)
- `"filters"`: FIR, IIR, integrators (color: #2196F3)
- `"io"`: File/network I/O (color: #FF9800)
- `"network"`: Network communication (color: #9C27B0)
- `"control"`: Logic, multiplexing (color: #F44336)
- `"conversion"`: Type conversion (color: #00BCD4)
- `"custom"`: User-defined (color: #795548)

### Visual Shapes
- `"box"`: General processing (Filters, math)
- `"circle"`: Sources (Oscillators, constants)
- `"triangle"`: Sinks (File output, network)
- `"diamond"`: Conditional logic
- `"trapezoid"`: I/O elements

## Example: Gain Element Registration

```cpp
// Metadata
ElementMetadata create_gain_metadata() {
    ElementMetadata meta;
    meta.type_name = "Gain";
    meta.display_name = "Gain / Amplifier";
    meta.description = "Multiplies input by constant factor";
    meta.category = "basic";
    meta.version = "1.0.0";
    
    meta.input_ports.push_back({
        "in", "Input signal", SignalType::DOUBLE, false
    });
    meta.output_ports.push_back({
        "out", "Output signal", SignalType::DOUBLE, false
    });
    meta.parameters.push_back({
        "factor", "real", "Multiplication factor", 1.0, 
        -1e6, 1e6, false
    });
    meta.visual = {"box", "#4CAF50", 100, 50, "multiply"};
    
    return meta;
}

// Factory
std::unique_ptr<Element> create_gain(const ElementCreationContext& ctx) {
    double factor = ctx.get_real("factor");
    return std::make_unique<s7::Gain>(factor);
}

// Registration
registry->register_element(ElementDescriptor(
    "Gain",
    create_gain,
    create_gain_metadata()
));
```

## JSON Schema Format

```json
{
  "name": "my_system",
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
      "id": "gain",
      "type": "Gain",
      "params": {
        "factor": 0.5
      }
    }
  ],
  "connections": [
    {
      "from_element": "gen",
      "from_port": 0,
      "to_element": "gain",
      "to_port": 0
    }
  ]
}
```

## Registry Lookup Performance

- Local element lookup by ID: O(1)
- Element lookup by type: O(1)
- Category filtering: O(n) where n = elements in category
- Registry lookup overhead: ~1 microsecond
- One-time cost at schema compilation

## Backward Compatibility

- All existing schemas work unchanged
- Element type names unchanged
- Parameter names unchanged
- Automatic UUID generation for elements without explicit UUIDs
- New registry coexists with existing code during migration


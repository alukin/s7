# Element Library Architecture Analysis

## Current System Overview

### Library Structure
- **s7core**: Base signal processing framework with element base class, templated elements, schema, compiler
- **s7io**: File I/O elements (sources and sinks with multiple format support)
- **s7net**: Network connectivity (NetSink, topology definitions)
- **s7runtime**: Main application that loads JSON schemas and instantiates elements
- All currently built as **STATIC libraries** linked into s7runtime executable

### Current Element Definition Model
1. **Element Base Class** (element.hpp)
   - Owns member variables for signal storage
   - Ports are lightweight references to those members
   - Elements register input/output ports during construction
   - Provides `step()` virtual interface for computation
   - Uses type-erased bindings for runtime connections

2. **Template-Based Elements** (elements.hpp)
   - `ConstantT<T>`, `GainT<T>`, `AdderT<T>`, `MultiplyT<T>`, `DelayT<T>`
   - Type aliases for Real and Complex variants
   - Real-only elements: FirFilter, IirFilter, Integrator, AnalogOscillator
   - Type conversion elements: ReImToComplex, ComplexToReIm, ComplexToMagnitudePhase

3. **Factory Pattern** (schema.hpp)
   - `ElementFactory` = `std::function<std::unique_ptr<Element>()>`
   - Each element registered with factory lambda
   - Schema compiles to ExecutableGraph with ordered elements

4. **Runtime Registration** (runtime_service.cpp)
   - Hardcoded element type switch statement (lines 270-341)
   - Parses JSON element definitions with params
   - Creates factory lambdas with captured parameters

## Desired Features

### 1. **Dynamic Shared Library Loading**
   - Compile s7core, s7io, s7net as shared libraries (not static)
   - s7runtime loads these as separate shared objects
   - Support external plugin libraries with same interface

### 2. **Built-in Element Descriptions**
   - Metadata about each element: name, description, version
   - Categorization (basic math, filters, I/O, network, etc.)
   - Help/documentation strings

### 3. **Tunable Parameters**
   - Standardized parameter schema for each element
   - Type information (int, float, string, enum, array)
   - Default values, min/max ranges
   - Runtime parameter validation

### 4. **Variable Number of Inputs**
   - Some elements need dynamic input counts (variadic adders, mixers, etc.)
   - Signal names should be descriptive, not just index-based
   - Support optional/variadic ports

### 5. **Visual Representation Hints**
   - Element categories for visual grouping
   - Port layout hints (inputs on left, outputs on right, etc.)
   - Shape/icon suggestions for s7studio diagrams
   - Color coding by signal type or category

## Proposed Architecture

### 1. **Element Metadata Framework**

Create `s7core/include/s7/element_metadata.hpp`:

```cpp
struct ParameterSpec {
    std::string name;
    std::string type;          // "real", "int", "string", "enum", "array"
    std::string description;
    boost::json::value default_value;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::vector<std::string> enum_values;  // For enum type
    bool required = false;
};

struct ElementMetadata {
    std::string type_name;
    std::string display_name;
    std::string description;
    std::string category;      // "basic", "filters", "io", "network", etc.
    std::string version;
    std::vector<std::string> tags;
    
    struct Port {
        std::string name;
        std::string description;
        SignalType type;
        bool optional = false;
    };
    
    std::vector<Port> input_ports;      // Fixed or descriptor for variadic
    std::vector<Port> output_ports;
    std::vector<ParameterSpec> parameters;
    
    struct VisualHints {
        std::string shape;              // "box", "circle", "triangle", etc.
        std::string color;
        int max_width = 100;
        int max_height = 50;
        std::string icon_name;
    };
    VisualHints visual;
};
```

### 2. **Element Registry Pattern**

Create `s7core/include/s7/element_registry.hpp`:

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

### 3. **Plugin/Dynamic Library Interface**

Create `s7core/include/s7/element_plugin.hpp`:

```cpp
// C ABI-compatible interface for plugins
extern "C" {
    struct S7ElementPluginInfo {
        uint32_t api_version;       // For compatibility checking
        uint32_t element_count;
    };
    
    // Plugin must export these functions
    const S7ElementPluginInfo* s7_plugin_info();
    void s7_register_elements(s7::ElementRegistry* registry);
}

// C++ wrapper for convenience
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
    void* handle_;  // dlopen handle
};
```

### 4. **Variadic Input Support**

Extend Element base class for elements with variable input counts:

```cpp
class VariadicElement : public Element {
public:
    // Add/remove inputs dynamically
    void add_input(std::string_view name, SignalType type);
    void remove_input(std::size_t index);
    
    [[nodiscard]] std::size_t max_inputs() const { return max_inputs_; }
    [[nodiscard]] std::size_t min_inputs() const { return min_inputs_; }
    
protected:
    void set_variadic_constraints(std::size_t min_inputs, std::size_t max_inputs);
    
private:
    std::size_t min_inputs_ = 1;
    std::size_t max_inputs_ = std::numeric_limits<std::size_t>::max();
};
```

### 5. **Runtime Parameter Application**

Enhance element factories to support parameter injection:

```cpp
struct ElementCreationContext {
    const ElementMetadata& metadata;
    const boost::json::object& params;
    
    double get_real(const std::string& name) const;
    int64_t get_int(const std::string& name) const;
    std::string get_string(const std::string& name) const;
    bool has_param(const std::string& name) const;
};

using ElementFactoryWithParams = 
    std::function<std::unique_ptr<Element>(const ElementCreationContext&)>;
```

### 6. **Build System Changes**

**CMakeLists.txt modifications**:

```cmake
# Build s7core, s7io, s7net as shared libraries (not static)
add_library(s7core SHARED ...)
add_library(s7io SHARED ...)
add_library(s7net SHARED ...)

# s7runtime links shared libs
target_link_libraries(s7runtime PRIVATE
    s7core
    s7io
    s7net
)

# Plugin example - builds separate shared library
add_library(s7elements_builtins SHARED
    src/builtin_elements.cpp
)
target_link_libraries(s7elements_builtins PRIVATE s7core)

# Plugins can be built and loaded at runtime
```

### 7. **Registration Pattern for Built-in Elements**

Create `s7core/src/builtin_elements.cpp`:

```cpp
// Each element provides metadata and factory
ElementMetadata create_gain_metadata() {
    ElementMetadata meta;
    meta.type_name = "Gain";
    meta.display_name = "Gain (Amplifier)";
    meta.category = "basic";
    meta.description = "Multiplies input by constant factor";
    
    meta.input_ports.push_back({"in", "Input signal", SignalType::DOUBLE});
    meta.output_ports.push_back({"out", "Output signal", SignalType::DOUBLE});
    
    meta.parameters.push_back({
        "factor",
        "real",
        "Gain factor",
        1.0,
        std::nullopt,
        std::nullopt
    });
    
    meta.visual.shape = "box";
    meta.visual.color = "#4CAF50";
    
    return meta;
}

ElementDescriptor create_gain_descriptor() {
    return ElementDescriptor(
        "Gain",
        [](const ElementCreationContext& ctx) {
            auto factor = ctx.get_real("factor");
            return std::make_unique<s7::Gain>(factor);
        },
        create_gain_metadata()
    );
}

// Plugin entry point
extern "C" {
const S7ElementPluginInfo* s7_plugin_info() {
    static S7ElementPluginInfo info{
        .api_version = 1,
        .element_count = <count>
    };
    return &info;
}

void s7_register_elements(s7::ElementRegistry* registry) {
    registry->register_element(create_gain_descriptor());
    registry->register_element(create_constant_descriptor());
    // ... register all built-in elements
}
}
```

### 8. **Schema Compilation Enhancement**

Runtime service updated to use registry:

```cpp
s7::Schema RuntimeService::build_schema() const {
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema{definition_.name};
    
    for (const auto& element : definition_.elements) {
        const auto* descriptor = registry.find(element.type);
        if (!descriptor) {
            throw std::invalid_argument("Unknown element type: " + element.type);
        }
        
        ElementCreationContext ctx{
            descriptor->metadata(),
            element.params
        };
        
        schema.add_element(element.id, 
            [descriptor, ctx] { return descriptor->create(ctx); }
        );
    }
    
    // ... rest of schema building
}
```

## Migration Path

### Phase 1: Foundation (Non-breaking)
- Add element metadata framework (element_metadata.hpp)
- Create element registry (element_registry.hpp)
- Add metadata to existing elements (optional at first)
- Existing factory pattern continues to work

### Phase 2: Plugin Infrastructure
- Implement plugin interface (element_plugin.hpp)
- Build s7core, s7io, s7net as shared libraries
- Create built-in elements plugin
- Add plugin discovery/loading to runtime

### Phase 3: Runtime Enhancement
- Update RuntimeService to use registry
- Add parameter validation
- Support variadic elements

### Phase 4: Studio Integration
- Pass metadata to s7studio for visual element properties
- Use visual hints for diagram rendering

## Benefits

1. **Extensibility**: External teams can build plugins without modifying core
2. **Loose Coupling**: Plugins linked at runtime, not compile time
3. **Self-Documenting**: Element metadata provides documentation
4. **Discoverable**: Tools can query registry for available elements
5. **Robust**: Parameter validation prevents invalid element creation
6. **Visual-Friendly**: Metadata enables better diagram rendering
7. **Scalable**: No need to modify switch statements for new elements
8. **Flexible**: Supports both fixed and variadic input architectures

## Implementation Priority

1. **High**: element_metadata.hpp + element_registry.hpp (foundation)
2. **High**: Decorate existing elements with metadata
3. **Medium**: Plugin interface + build system changes
4. **Medium**: Variadic element support
5. **Low**: Visual hints refinement (can evolve over time)

## Questions for Discussion

1. Should plugin versioning/compatibility checking be more sophisticated?
2. Do we need hot-reloading of plugins or is load-at-startup sufficient?
3. Should parameters support JSON schema validation or simpler approach?
4. How should element categories be standardized/extensible?
5. For variadic elements, should we support named ports or indexed?
6. Should element instances be creatable with default parameters?
7. Do we need element state serialization/deserialization?


# Element Library Implementation Strategy

## Executive Summary

The S7 platform currently has a working signal processing framework (s7core), I/O subsystem (s7io), and networking (s7net) compiled as static libraries into s7runtime. To support extensibility and the requested features, we propose:

1. **Convert to shared libraries** for dynamic loading capability
2. **Implement element metadata system** for self-documenting elements
3. **Create element registry** for discoverable, pluggable architecture
4. **Add plugin interface** for external element libraries
5. **Support variadic inputs** for flexible element definitions
6. **Enhance visual representation** with metadata hints

## Current State Analysis

### Strengths
- Clean Element base class with type-safe ports
- Template-based elements minimize code duplication
- Factory pattern already in place
- JSON-based schema loading established
- Compiler validates connections at build time

### Limitations
- Hardcoded element switch statement in RuntimeService (281 lines of if-else)
- No element discoverability (must know element name to use)
- No parameter validation (raw boost::json objects)
- No metadata/documentation
- No support for external plugins
- Fixed input/output counts
- No visual representation hints
- Static linking prevents runtime plugin loading

## Detailed Implementation Plan

### Stage 1: Metadata Framework (Week 1-2)

**Objectives**: Lay foundation for element discovery and self-documentation

**Files to Create**:
- `s7core/include/s7/element_metadata.hpp` - Metadata structures
- `s7core/include/s7/element_registry.hpp` - Registry pattern
- `s7core/src/element_registry.cpp` - Registry implementation

**Key Changes**:
1. Define `ParameterSpec` struct with type, defaults, ranges
2. Define `ElementMetadata` struct with ports, parameters, categories, visual hints
3. Implement singleton `ElementRegistry` class
4. Add `ElementDescriptor` wrapper combining factory + metadata

**Implementation Details**:

```cpp
// element_metadata.hpp structure
class ElementMetadata {
    std::string type_name;
    std::string display_name;
    std::string description;
    std::string category;
    std::vector<ParameterSpec> parameters;
    std::vector<PortSpec> input_ports;
    std::vector<PortSpec> output_ports;
    VisualHints visual;
};

class ElementRegistry {
    static ElementRegistry& instance();
    void register_element(const ElementDescriptor& desc);
    const ElementDescriptor* find(const std::string& type) const;
    std::vector<std::string> list_elements() const;
    std::vector<std::string> list_by_category(const std::string& cat) const;
};
```

**Tasks**:
- [ ] Design ParameterSpec (type system, validation rules)
- [ ] Design ElementMetadata (structure, extensibility)
- [ ] Design VisualHints (shape, color, size, icons)
- [ ] Implement ElementRegistry singleton
- [ ] Add unit tests for registry operations

**Deliverables**:
- Header files defining metadata structures
- Registry implementation with basic operations
- Documentation of metadata schema
- Example element registration code

---

### Stage 2: Metadata Annotation (Week 2-3)

**Objectives**: Add metadata to all existing built-in elements

**Files to Modify**:
- `s7core/src/elements.cpp` - Add metadata functions
- `s7io/src/io_elements.cpp` - Add metadata for I/O elements
- `s7net/src/net_elements.cpp` - Add metadata for network elements

**Implementation Pattern**:

For each element, create a factory function:

```cpp
// In elements.cpp
ElementMetadata create_gain_metadata() {
    return ElementMetadata{
        .type_name = "Gain",
        .display_name = "Gain / Amplifier",
        .description = "Multiplies input signal by constant factor",
        .category = "basic-arithmetic",
        .version = "1.0",
        .tags = {"linear", "stateless"},
        .input_ports = {
            {"in", "Input signal", SignalType::DOUBLE, false}
        },
        .output_ports = {
            {"out", "Output signal", SignalType::DOUBLE, false}
        },
        .parameters = {
            {
                .name = "factor",
                .type = "real",
                .description = "Multiplication factor",
                .default_value = 1.0,
                .required = true
            }
        },
        .visual = {
            .shape = "box",
            .color = "#4CAF50",
            .max_width = 100,
            .max_height = 50,
            .icon_name = "multiply"
        }
    };
}
```

**Elements to Annotate**:
1. **Basic Math** (s7core)
   - Constant, Gain, Adder, Multiply, Delay
   - Integrator, FirFilter, IirFilter

2. **Type Conversion** (s7core)
   - ReImToComplex, ComplexToReIm, ComplexToMagnitudePhase
   - Int12ToDouble, DoubleToInt12

3. **I/O** (s7io)
   - RealGnuplotFileSink, RealGnuplotFileSource
   - RealCompactBinaryFileSink, RealCompactBinaryFileSource

4. **Network** (s7net)
   - NetSink, NetSource (if exists)

**Metadata Guidelines**:
- **Categories**: "basic", "filters", "io", "network", "conversion", "control"
- **Tags**: "stateless", "stateful", "real-only", "complex", "time-varying"
- **Colors**: Use consistent palette per category
- **Shapes**: "box" (default), "circle" (sources), "triangle" (sinks)

**Tasks**:
- [ ] Define category taxonomy
- [ ] Define color scheme per category
- [ ] Create metadata for each element
- [ ] Group metadata in logical modules
- [ ] Add metadata validation

**Deliverables**:
- Metadata for all ~15-20 built-in elements
- Category taxonomy document
- Visual representation guide

---

### Stage 3: Plugin Infrastructure (Week 3-4)

**Objectives**: Enable external library loading and plugin interface

**Files to Create**:
- `s7core/include/s7/element_plugin.hpp` - Plugin interface
- `s7core/src/element_plugin.cpp` - Plugin loading implementation
- `examples/plugins/CMakeLists.txt` - Plugin build template
- `examples/plugins/example_plugin.cpp` - Example plugin

**Plugin Architecture**:

```cpp
// C API for ABI stability
extern "C" {
    struct S7PluginInfo {
        uint32_t api_version;
        uint32_t element_count;
        const char* plugin_name;
    };
    
    S7PluginInfo s7_plugin_query();
    void s7_plugin_register(void* registry_ptr);
}

// C++ wrapper
class ElementPlugin {
public:
    explicit ElementPlugin(const std::filesystem::path& path);
    ~ElementPlugin();
    
    bool load();
    bool unload();
    std::string name() const;
    uint32_t api_version() const;
    
    static std::vector<std::filesystem::path> 
    discover(const std::filesystem::path& plugin_dir);
};
```

**Key Decisions**:
1. **C ABI for compatibility**: Allows plugins built with different toolchains
2. **Registry pointer passing**: Plugins call back to register elements
3. **Version checking**: Plugins check API version compatibility
4. **Search paths**: ~/.s7/plugins, /opt/s7/plugins, ./plugins, PLUGIN_PATH env var

**Build Changes**:
1. Create `plugins/` directory in examples
2. Add CMakeLists.txt for plugin building
3. Modify s7runtime CMakeLists.txt to discover plugins

**Tasks**:
- [ ] Implement dlopen/dlsym wrapper (ElementPlugin class)
- [ ] Design C plugin interface (extern "C")
- [ ] Implement plugin discovery
- [ ] Create example plugin
- [ ] Add plugin path configuration
- [ ] Error handling for missing/incompatible plugins

**Deliverables**:
- Plugin loading infrastructure
- C interface headers
- Example plugin (demonstrates metadata registration)
- Plugin development guide

---

### Stage 4: Build System Refactor (Week 4-5)

**Objectives**: Convert libraries to shared, implement plugin loading

**Files to Modify**:
- Root `CMakeLists.txt` - Add plugin directory
- `s7core/CMakeLists.txt` - Convert STATIC -> SHARED
- `s7io/CMakeLists.txt` - Convert STATIC -> SHARED
- `s7net/CMakeLists.txt` - Convert STATIC -> SHARED
- `s7runtime/CMakeLists.txt` - Link shared libs, add plugin discovery
- `s7runtime/src/main.cpp` - Add plugin loading code

**Shared Library Changes**:

```cmake
# Before (static):
add_library(s7core STATIC src/...)

# After (shared):
add_library(s7core SHARED src/...)
set_target_properties(s7core PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "include/s7/element.hpp;include/s7/element_metadata.hpp;..."
)
install(TARGETS s7core
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include/s7
)
```

**Runtime Plugin Loading**:

```cpp
// In s7runtime/src/main.cpp
void load_plugins(ElementRegistry& registry) {
    auto plugin_paths = ElementPlugin::discover("./plugins");
    for (const auto& path : plugin_paths) {
        ElementPlugin plugin(path);
        if (plugin.load()) {
            LOG("Loaded plugin: " << plugin.name());
        } else {
            LOG("Failed to load plugin: " << path);
        }
    }
}
```

**Tasks**:
- [ ] Update CMakeLists.txt for all libraries
- [ ] Add install targets for shared libraries
- [ ] Update s7runtime to load plugins at startup
- [ ] Add environment variable support for plugin paths
- [ ] Handle dynamic library linking (rpath, LD_LIBRARY_PATH)
- [ ] Add to startup logging/debug

**Deliverables**:
- Shared libraries instead of static
- Plugin discovery and loading at runtime
- CMake infrastructure for plugin development
- Installation packages with proper RPATH

---

### Stage 5: Runtime Enhancement (Week 5-6)

**Objectives**: Use registry in RuntimeService, parameter validation

**Files to Modify**:
- `s7runtime/src/runtime_service.cpp` - Use registry instead of switch
- `s7runtime/include/s7/runtime_service.hpp` - Add parameter validation

**Key Changes**:

```cpp
// Before: huge switch statement in build_schema()
if (element.type == "Constant") {
    auto value = get_f64(params, "value", 0.0);
    schema.add_element(...);
} else if (element.type == "Gain") {
    auto factor = get_f64(params, "factor", 1.0);
    schema.add_element(...);
}
// ... 30+ more cases

// After: registry-based
auto& registry = ElementRegistry::instance();
const auto* desc = registry.find(element.type);
if (!desc) throw std::invalid_argument(...);

// Validate parameters
for (const auto& param : desc->metadata().parameters) {
    if (param.required && !element.params.contains(param.name)) {
        throw std::invalid_argument("Missing required parameter: " + param.name);
    }
}

// Create element with parameters
ElementCreationContext ctx{desc->metadata(), element.params};
schema.add_element(element.id, [desc, ctx] { 
    return desc->create(ctx); 
});
```

**Parameter Validation**:
1. Check required parameters present
2. Validate parameter types (int vs real vs string)
3. Check numeric ranges (min/max)
4. Validate enum values
5. Type coercion where safe (int to real)

**Tasks**:
- [ ] Refactor build_schema() to use registry
- [ ] Implement parameter validation
- [ ] Add validation error messages
- [ ] Remove hardcoded switch statement
- [ ] Add parameter type checking

**Deliverables**:
- Clean runtime_service.cpp without switch statement
- Parameter validation framework
- Better error messages for invalid schemas

---

### Stage 6: Variadic Element Support (Week 6-7)

**Objectives**: Support elements with dynamic input/output counts

**Files to Create/Modify**:
- `s7core/include/s7/element.hpp` - Extend Element base class
- New element types: `VariadicAdder`, `SignalMixer`

**Implementation Strategy**:

1. **Extend Element class**:
```cpp
class Element {
    // ... existing ...
    
    // Variadic support
    virtual bool supports_variadic_inputs() const { return false; }
    virtual std::size_t min_inputs() const { return input_ports_.size(); }
    virtual std::size_t max_inputs() const { return input_ports_.size(); }
    virtual void add_input(std::string_view name, SignalType type);
    virtual bool can_remove_input(std::size_t idx) const { return false; }
    virtual void remove_input(std::size_t idx);
};
```

2. **VariadicAdder Example**:
```cpp
class VariadicAdder : public Element {
public:
    VariadicAdder() : Element("VariadicAdder") {}
    
    bool supports_variadic_inputs() const override { return true; }
    std::size_t min_inputs() const override { return 2; }
    std::size_t max_inputs() const override { 
        return std::numeric_limits<std::size_t>::max(); 
    }
    
    void add_input(std::string_view name, SignalType type) override;
    
    void step() override {
        RealValue sum = 0.0;
        for (std::size_t i = 0; i < input_ports_.size(); ++i) {
            sum += read_input<RealValue>(i);
        }
        output_value_ = sum;
    }
};
```

3. **Metadata for Variadic Elements**:
```cpp
ElementMetadata create_variadic_adder_metadata() {
    auto meta = create_adder_metadata();
    meta.type_name = "VariadicAdder";
    meta.input_ports.clear();  // No fixed ports
    meta.variadic_inputs = {
        .enabled = true,
        .min_count = 2,
        .max_count = 1000,
        .port_name_template = "in_{index}",
        .signal_type = SignalType::DOUBLE
    };
    return meta;
}
```

**Tasks**:
- [ ] Extend Element class with variadic methods
- [ ] Implement VariadicAdder element
- [ ] Update metadata to describe variadic constraints
- [ ] Update compiler to handle variable input counts
- [ ] Update schema validation

**Deliverables**:
- Extended Element interface
- Example variadic elements (VariadicAdder, Mixer)
- Metadata describing variadic constraints
- Compiler support for variable port counts

---

### Stage 7: Visual Representation Integration (Week 7-8)

**Objectives**: Pass element metadata to s7studio for enhanced diagrams

**Files to Modify**:
- `s7observe/include/s7/observe.hpp` - Extend observation API
- `s7runtime/src/runtime_service.cpp` - Export element metadata
- Integration points with s7studio

**New Observation API**:

```cpp
class ElementCatalogService {
public:
    // Query available elements
    std::vector<ElementMetadata> all_elements() const;
    ElementMetadata get_element_metadata(const std::string& type) const;
    
    // Export for studio
    boost::json::value to_json_catalog() const;
    
    static ElementCatalogService& instance();
};
```

**JSON Catalog Format**:

```json
{
  "version": "1.0",
  "elements": [
    {
      "type": "Gain",
      "display_name": "Gain / Amplifier",
      "category": "basic",
      "description": "...",
      "ports": {
        "inputs": [{"name": "in", "type": "DOUBLE"}],
        "outputs": [{"name": "out", "type": "DOUBLE"}]
      },
      "parameters": [
        {
          "name": "factor",
          "type": "real",
          "default": 1.0,
          "min": -1e6,
          "max": 1e6
        }
      ],
      "visual": {
        "shape": "box",
        "color": "#4CAF50",
        "icon": "multiply"
      }
    }
  ]
}
```

**Tasks**:
- [ ] Create ElementCatalogService
- [ ] Export metadata as JSON
- [ ] Define visual representation schema
- [ ] Create WebSocket endpoint for catalog queries
- [ ] Document s7studio integration points

**Deliverables**:
- Metadata export API
- JSON catalog format
- Integration documentation for s7studio

---

## Timeline & Milestones

| Week | Stage | Milestone |
|------|-------|-----------|
| 1-2 | Metadata | Registry working, metadata structs defined |
| 2-3 | Annotation | All elements annotated with metadata |
| 3-4 | Plugins | Plugin interface working, example plugin |
| 4-5 | Build | Shared libraries, runtime plugin loading |
| 5-6 | Runtime | No more switch statement, params validated |
| 6-7 | Variadic | Dynamic input support implemented |
| 7-8 | Visual | Studio integration prepared |
| 8-9 | Testing | Integration tests, documentation |

**Total: 8-9 weeks of focused development**

## Resource Requirements

### Skills Needed
- C++20 (metaprogramming, lambdas, type traits)
- CMake (shared libraries, plugin discovery)
- Dynamic linking (dlopen, symbol resolution)
- JSON (boost::json, schema design)
- System architecture (plugin patterns)

### Tools/Libraries
- Existing: Boost (JSON, system), CMake, C++20
- Optional: nlohmann/json (if moving from boost::json)
- Testing: Catch2 or Boost.Test for unit tests

## Risks & Mitigation

| Risk | Probability | Mitigation |
|------|-------------|-----------|
| ABI compatibility issues | Medium | Use C interface for plugins, version checking |
| Performance impact | Low | Profile before/after, lazy plugin loading |
| Plugin discovery fragility | Medium | Multiple search paths, config file support |
| Parameter validation complexity | Medium | Start simple, enhance gradually |
| Breaking existing schemas | Low | Maintain backward compatibility |

## Success Criteria

1. ✅ All built-in elements have metadata
2. ✅ Example external plugin loads and registers elements
3. ✅ RuntimeService uses registry instead of switch statement
4. ✅ Parameters validated against metadata
5. ✅ Visual hints available for s7studio
6. ✅ No performance regression in element execution
7. ✅ Documentation covers element development
8. ✅ Tests verify plugin loading/registration

## Next Steps

1. **Review & Discuss**: Validate architecture with team
2. **Create Stage 1 Tasks**: Break metadata stage into detailed GitHub issues
3. **Establish Code Review Process**: Guidelines for metadata completeness
4. **Set up Development Environment**: Plugin build templates
5. **Documentation**: Start element development guide

## Related Documents
- `ELEMENT_LIBRARY_ANALYSIS.md` - Detailed architecture analysis
- `S7_Architecture_Overview.md` - High-level system design
- (To be created) `ELEMENT_DEVELOPMENT_GUIDE.md` - Plugin developer guide
- (To be created) `ELEMENT_METADATA_SCHEMA.md` - JSON schema specification


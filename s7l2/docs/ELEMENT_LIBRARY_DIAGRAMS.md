# Element Library System - Architecture Diagrams

## Current Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    s7runtime (executable)                    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ RuntimeService::build_schema()                       │  │
│  │                                                      │  │
│  │ if (type == "Constant") { ... }                     │  │
│  │ else if (type == "Gain") { ... }                    │  │
│  │ else if (type == "Adder") { ... }                   │  │
│  │ else if (type == "Multiply") { ... }                │  │
│  │ else if (type == "Integrator") { ... }              │  │
│  │ else if (type == "AnalogOscillator") { ... }        │  │
│  │ else if (type == "FirFilter") { ... }               │  │
│  │ else if (type == "IirFilter") { ... }               │  │
│  │ ... 20+ more cases ...                              │  │
│  │ else throw std::invalid_argument(...)               │  │
│  │                                                      │  │
│  │ ❌ Hardcoded, no extensibility, error-prone         │  │
│  └──────────────────────────────────────────────────────┘  │
│                           │                                 │
│              ┌────────────┼────────────┐                    │
│              │            │            │                    │
│              ▼            ▼            ▼                    │
│            ┌──┐        ┌──┐        ┌──┐                    │
│            │Constant   │Gain       │Adder                  │
│            └──┘        └──┘        └──┘                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
              ▼            ▼            ▼
          ┌────────┐   ┌────────┐   ┌────────┐
          │ s7core │   │ s7io   │   │ s7net  │
          │ STATIC │   │ STATIC │   │ STATIC │
          └────────┘   └────────┘   └────────┘
          (no plugins)
```

## Proposed Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    s7runtime (executable)                        │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ RuntimeService::build_schema()                            │  │
│  │                                                           │  │
│  │ auto& registry = ElementRegistry::instance();            │  │
│  │ for (const auto& elem : definition_.elements) {          │  │
│  │     const auto* desc = registry.find(elem.type);         │  │
│  │     if (!desc) throw std::invalid_argument(...);         │  │
│  │     schema.add_element(elem.id, [desc, elem] {          │  │
│  │         return desc->create(ctx);                        │  │
│  │     });                                                   │  │
│  │ }                                                         │  │
│  │                                                           │  │
│  │ ✅ Clean, extensible, registry-based, discoverable        │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │ PluginLoader::load_plugins()                              │  │
│  │  - Discovers plugins in ./plugins, ~/.s7/plugins         │  │
│  │  - Calls dlopen() for each plugin                        │  │
│  │  - Calls s7_register_elements() callback                 │  │
│  │  - Plugins register their elements with registry         │  │
│  └───────────────────────────────────────────────────────────┘  │
│                           │                                     │
└───────────────────────────┼─────────────────────────────────────┘
                            │
                ┌───────────┼───────────┐
                │           │           │
                ▼           ▼           ▼
            ┌────────┐  ┌────────┐  ┌────────┐
            │ s7core │  │ s7io   │  │ s7net  │
            │ SHARED │  │ SHARED │  │ SHARED │
            │        │  │        │  │        │
            │Registry│  │        │  │        │
            │ & Meta │  │        │  │        │
            └────────┘  └────────┘  └────────┘
                │           │           │
                └───────────┼───────────┘
                            │
                    ┌───────▼────────┐
                    │  Plugin Libs   │
                    │  (SHARED)      │
                    │                │
                    │ - CustomFilter │
                    │ - UserAddon    │
                    │ - ThirdParty   │
                    │                │
                    │ All register   │
                    │ with registry  │
                    └────────────────┘
```

## Element Lifecycle - Current vs Proposed

### Current: Hardcoded Registration

```
JSON Schema File
      │
      ▼
RuntimeService::load_from_file()
      │
      ├─ Parse JSON
      │
      ▼
build_schema()
      │
      ├─ for each element in JSON:
      │  ├─ Check element.type
      │  ├─ if == "Gain":
      │  │    ├─ Extract params (params["factor"])
      │  │    ├─ Create factory lambda
      │  │    └─ Add to schema
      │  │
      │  └─ if == "Constant":
      │       ├─ Extract params (params["value"])
      │       ├─ Create factory lambda
      │       └─ Add to schema
      │
      ▼
Schema object with factories

[Problem: 40+ lines of if-else, error-prone, no discovery]
```

### Proposed: Registry-Based Registration

```
Plugin Discovery        JSON Schema File
      │                      │
      ├─ Scan ./plugins       │
      ├─ Load .so files       │
      │  ├─ dlopen()          │
      │  ├─ dlsym()           │
      │  └─ Call s7_register  │
      │                       │
      ▼                       ▼
ElementRegistry ◄──────── RuntimeService::load_from_file()
      │                       │
      ├─ Constant             ├─ Parse JSON
      ├─ Gain                 │
      ├─ Adder                ▼
      ├─ CustomFilter         build_schema()
      ├─ UserAddon            │
      └─ ThirdPartyElement    ├─ for each element in JSON:
                              │  ├─ registry.find(type)
                              │  ├─ Validate parameters
                              │  ├─ Create via descriptor
                              │  └─ Add to schema
                              │
                              ▼
                        Schema object with factories

[Benefits: 10 lines of code, clean, discoverable, extensible]
```

## Element Metadata Hierarchy

```
ElementMetadata (complete description)
│
├─ Identity
│  ├─ type_name: "Gain"
│  ├─ display_name: "Gain / Amplifier"
│  ├─ description: "Multiplies input by constant factor"
│  └─ version: "1.0.0"
│
├─ Categorization
│  ├─ category: "basic"
│  └─ tags: ["stateless", "linear"]
│
├─ Interface Definition
│  ├─ input_ports:
│  │  └─ [Port(name="in", type=DOUBLE)]
│  │
│  ├─ output_ports:
│  │  └─ [Port(name="out", type=DOUBLE)]
│  │
│  └─ parameters:
│     ├─ [ParameterSpec(name="factor", type="real", default=1.0, min=-1e6, max=1e6)]
│     └─ [ParameterSpec(...)]
│
└─ Visual Representation
   ├─ shape: "box"
   ├─ color: "#4CAF50" (green)
   ├─ icon_name: "multiply"
   └─ size: 100x50
```

## Registry Pattern - Class Diagram

```
                 ┌──────────────────┐
                 │  ElementRegistry │
                 │    (Singleton)   │
                 ├──────────────────┤
                 │ - instance()     │
                 │ - register()     │
                 │ - find()         │
                 │ - list_all()     │
                 │ - list_by_cat()  │
                 └────────┬─────────┘
                          │ contains many
                          │
                 ┌────────▼────────────┐
                 │ ElementDescriptor   │
                 ├─────────────────────┤
                 │ - factory_          │
                 │ - metadata_         │
                 ├─────────────────────┤
                 │ + metadata()        │
                 │ + create()          │
                 │ + create(context)   │
                 └──────────┬──────────┘
                            │ references
                 ┌──────────▼──────────┐
                 │ ElementMetadata     │
                 ├─────────────────────┤
                 │ + type_name         │
                 │ + category          │
                 │ + parameters[]      │
                 │ + input_ports[]     │
                 │ + output_ports[]    │
                 │ + visual            │
                 └─────────────────────┘
```

## Plugin Architecture - Sequence Diagram

```
s7runtime                    Plugin.so              ElementRegistry
     │                            │                       │
     ├─ Load plugins               │                       │
     │   dlopen("Plugin.so")        │                       │
     │                             │                       │
     ├─ Get plugin info            │                       │
     │   dlsym("s7_plugin_query")   │                       │
     │                    ◄────────┤                       │
     │                    API version etc                  │
     │                             │                       │
     ├─ Register elements          │                       │
     │   dlsym("s7_register")       │                       │
     │──────────────────────────────┤                       │
     │                              ├─ Create descriptors   │
     │                              │  for each element    │
     │                              │                       │
     │                              ├─ register_element() ──┤
     │                              │                       ├─ Add to map
     │                              │                       │
     │◄─────────────────────────────┤                       │
     │                              │                       │
     ├─ Plugin loaded successfully  │                       │
     │  (elements now available)    │                       │
     │                              │                       │
     └─ Can use custom elements    │                       │
        in schemas                 │                       │
```

## Data Flow: Element Creation

```
JSON Schema Input
     │
     ▼
┌──────────────────────────────┐
│ RuntimeService::build_schema │
└─────────────┬────────────────┘
              │
              ├─ for each element:
              │
              ▼
         ┌────────────────────────────────┐
         │ Find in ElementRegistry        │
         │ const auto* desc = registry.find(type)
         └────────────┬───────────────────┘
                      │
                      ▼
         ┌────────────────────────────────┐
         │ Validate Parameters            │
         │ - Check required params        │
         │ - Validate types               │
         │ - Check ranges (min/max)       │
         │ - Check enums                  │
         └────────────┬───────────────────┘
                      │
                      ├─ If invalid: throw exception
                      │
                      ▼
         ┌────────────────────────────────┐
         │ Create Context                 │
         │ ElementCreationContext ctx{    │
         │   metadata: desc->metadata(),  │
         │   params: elem.params          │
         │ }                              │
         └────────────┬───────────────────┘
                      │
                      ▼
         ┌────────────────────────────────┐
         │ Call Factory Lambda            │
         │ std::unique_ptr<Element>       │
         │   elem = desc->create(ctx)     │
         │                                │
         │ Lambda extracts params:        │
         │   double factor = ctx.get_real │
         │   ("factor")                   │
         │   return make_unique<Gain>     │
         │   (factor)                     │
         └────────────┬───────────────────┘
                      │
                      ▼
         ┌────────────────────────────────┐
         │ Element Instance Created       │
         │ Ready to add to schema         │
         └────────────────────────────────┘
```

## Visual Representation Integration

```
ElementMetadata
    │
    ├─ type_name: "Gain"
    ├─ category: "basic"
    │
    └─ visual:
       ├─ shape: "box"
       ├─ color: "#4CAF50"
       ├─ icon_name: "multiply"
       └─ size: 100x50
                │
                ▼
        ┌──────────────────┐
        │ s7studio renders │
        │                  │
        │  shape ──────────┤─ box
        │  color ──────────┤─ green
        │  icon ───────────┤─ ×
        │  size ───────────┤─ 100×50
        │                  │
        │  ┌────────────┐  │
        │  │     ×      │  │
        │  │   Gain     │  │
        │  │            │  │
        │  └────────────┘  │
        │                  │
        └──────────────────┘
        
        With interactive metadata:
        - Hover: shows description
        - Right-click: parameter editor
        - Drag: connection points
```

## Stage-by-Stage Implementation

```
Current
└─ Week 1-2: Metadata Framework
   ├─ Headers: element_metadata.hpp
   ├─ Headers: element_registry.hpp
   └─ Struct definitions
      │
      ▼
   Week 2-3: Element Annotation
   ├─ Metadata for Constant
   ├─ Metadata for Gain
   ├─ ... (all ~20 elements)
   └─ Proof of concept
      │
      ▼
   Week 3-4: Plugin Interface
   ├─ Headers: element_plugin.hpp
   ├─ Plugin C interface
   ├─ Plugin loader
   └─ Example plugin
      │
      ▼
   Week 4-5: Build System
   ├─ Convert to SHARED libs
   ├─ Add plugin discovery
   ├─ CMake configuration
   └─ Example plugin build
      │
      ▼
   Week 5-6: Runtime Integration
   ├─ Remove switch statement
   ├─ Use registry in build_schema()
   ├─ Parameter validation
   └─ Plugin loading at startup
      │
      ▼
   Week 6-7: Variadic Support
   ├─ Extend Element base class
   ├─ VariadicAdder element
   ├─ Metadata for variadic
   └─ Compiler changes
      │
      ▼
   Week 7-8: Visual Integration
   ├─ ElementCatalogService
   ├─ JSON export
   ├─ WebSocket endpoint
   └─ Studio integration
      │
      ▼
   Week 8-9: Testing & Release
   ├─ Unit tests
   ├─ Integration tests
   ├─ Documentation
   └─ Community plugin example
      │
      ▼
   Full Element Library System ✅
```

## Component Dependencies

```
s7runtime (executable)
    │
    ├─── loads shared lib ───► s7core
    │                          ├─ Element base
    │                          ├─ ElementMetadata
    │                          └─ ElementRegistry
    │
    ├─── loads shared lib ───► s7io
    │                          ├─ IO elements
    │                          └─ Metadata
    │
    ├─── loads shared lib ───► s7net
    │                          ├─ Net elements
    │                          └─ Metadata
    │
    └─── dynamically loads ──► Plugins (at startup)
                              ├─ Custom elements
                              ├─ Metadata
                              └─ All s7core deps

Result: Fully extensible element system
```

## Registry Lookup Performance

```
Time Complexity: O(log n)

ElementRegistry::find("Gain")
    │
    ├─ Hash table lookup (unordered_map)
    │
    ├─ Average case: O(1)
    ├─ Worst case: O(n) [hash collision]
    │
    └─ In practice: ~100 elements
       └─ Lookup time: ~1 microsecond
       └─ Not performance critical
       └─ Called only during schema compilation

Per-element creation overhead:
    ├─ Registry lookup: ~1 μs
    ├─ Parameter validation: ~10 μs
    ├─ Element constructor: ~100 μs
    │
    ├─ Total: ~111 μs per element
    ├─ Schema with 20 elements: ~2.2 ms
    └─ One-time cost (at startup)

Runtime execution:
    ├─ Per-step overhead: 0 μs (registry not used)
    └─ No performance impact on simulation
```

## Summary

This architecture enables:
- ✅ **Extensibility**: Add elements without modifying core
- ✅ **Discoverability**: Registry allows programmatic discovery
- ✅ **Robustness**: Parameter validation prevents errors
- ✅ **Maintainability**: No giant switch statements
- ✅ **Scalability**: Add 100 elements with minimal changes
- ✅ **Visual Support**: Metadata drives diagram rendering
- ✅ **Future-Proof**: Foundation for documentation generation, serialization, etc.


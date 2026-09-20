# S7 Element Library & Distributed Architecture - Technical Reference

## Quick Navigation

### Core Architecture
- **DISTRIBUTED_ADDRESSING.md** - UUID-based element and node addressing for distributed computation
- **ELEMENT_LIBRARY_TECH.md** - Element library architecture, registry, plugins, parameter validation
- **SCHEMA_LIBRARY_WORKFLOW.md** - Schema-based element libraries, studio workflow, and runtime loading

### Developer Guides
- **ELEMENT_DEV_GUIDE.md** - Create custom elements, quick start, templates, troubleshooting

### Source Code References
- **s7core/include/s7/element.hpp** - Element base class
- **s7core/include/s7/elements.hpp** - Built-in templated elements
- **s7core/include/s7/schema_library.hpp** - Schema library loader and registration API
- **s7core/src/elements.cpp** - Element implementations
- **s7runtime/src/runtime_service.cpp** - Schema loading and compilation

---

## Distributed Element Addressing

### Problem
In a distributed system, elements run across multiple computational nodes. Each element needs:
- Unique identification (UUID)
- Network addressability
- Cross-node connection support

### Solution
Three-tier addressing:
1. **Node UUID**: Identifies computational node (s7runtime instance)
2. **Element UUID**: Unique within and across entire system
3. **Element ID**: Human-readable local name

### Key Files
- `DISTRIBUTED_ADDRESSING.md` - Complete specification
  - UUID generation (v4 random, v5 deterministic)
  - Network topology definition
  - RemoteElementProxy for cross-node connections
  - Catalog discovery via UUID

### Usage Example
```json
{
  "node_id": "compute-01",
  "node_uuid": "550e8400-e29b-41d4-a716-446655440000",
  "elements": [
    {
      "id": "oscillator",
      "uuid": "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
    }
  ]
}
```

Full address: `550e8400-e29b-41d4-a716-446655440000/6ba7b810-9dad-11d1-80b4-00c04fd430c8`

---

## Element Library Architecture

### Problem
Current system:
- 40+ line hardcoded switch statement for element registration
- No element discovery mechanism
- No parameter validation
- No plugin support

### Solution
Three-tier architecture:

**Tier 1: Metadata Layer**
- `ElementMetadata`: Describes element (name, ports, parameters, visual hints)
- `ParameterSpec`: Parameter definition with types and ranges
- `VisualHints`: Shape, color, icon for diagram rendering

**Tier 2: Registry Layer**
- `ElementRegistry`: Singleton registry for all available elements
- O(1) lookup by type, filtering by category
- Automatic parameter validation

**Tier 3: Plugin Layer**
- `ElementPlugin`: Dynamic library loading (dlopen/dlsym)
- C ABI interface for cross-toolchain compatibility
- Plugin discovery in standard search paths

### Benefits
- ✅ Add elements without modifying core
- ✅ Full element discovery
- ✅ Automatic parameter validation
- ✅ Support for external plugins
- ✅ Variadic input support
- ✅ Visual representation hints
- ✅ 100% backward compatible

---

## Implementation Stages

### Phase 1: Metadata Framework (Weeks 1-2)
**Create:**
- `s7core/include/s7/element_metadata.hpp` - Metadata structures
- `s7core/include/s7/element_registry.hpp` - Registry pattern
- `s7core/src/element_registry.cpp` - Implementation

**Deliverable:** Registry working, metadata defined

### Phase 2: Element Annotation (Weeks 2-3)
**Annotate:** All ~20 built-in elements with metadata

**Deliverable:** All elements have metadata, proof-of-concept working

### Phase 3: Plugin Infrastructure (Weeks 3-4)
**Create:**
- `s7core/include/s7/element_plugin.hpp` - Plugin interface
- `s7core/src/element_plugin.cpp` - Plugin loader
- Example plugin

**Deliverable:** Plugins can be discovered and loaded

### Phase 4: Build System (Weeks 4-5)
**Modify:**
- Convert s7core, s7io, s7net to SHARED libraries
- Add plugin search paths
- CMake configuration

**Deliverable:** Shared libraries, plugins loadable at runtime

### Phase 5: Runtime Integration (Weeks 5-6)
**Refactor:**
- RuntimeService uses registry instead of switch statement
- Parameter validation in build_schema()
- Plugin loading at startup

**Deliverable:** No hardcoded switch statement, clean registry-based instantiation

### Phase 6: Variadic Support (Weeks 6-7)
**Implement:**
- Extend Element base class for variadic inputs
- VariadicAdder element example
- Compiler support for variable ports

**Deliverable:** Elements can accept variable number of inputs

### Phase 7: Distributed Addressing (Weeks 7-8)
**Integrate:**
- UUID support in ExecutableGraph
- Remote element proxies
- Network topology definition
- Catalog discovery via UUID

**Deliverable:** Distributed computation with network-addressable elements

---

## Creating Custom Elements

### Quick Start
1. Implement element class (derive from `s7::Element`)
2. Create metadata (describe parameters, ports, visual hints)
3. Write factory lambda (instantiate with parameters)
4. Register with ElementRegistry

### Reference
See `ELEMENT_DEV_GUIDE.md` for:
- Complete working examples
- All parameter types
- Stateful and variadic elements
- Complex signals
- CMakeLists.txt templates
- Troubleshooting

### Minimal Example
```cpp
// Element
class Gain : public s7::Element {
    // input_value * factor = output_value
};

// Metadata
ElementMetadata metadata{
    .type_name = "Gain",
    .parameters = {{"factor", "real", ..., 1.0, ...}}
};

// Registration
registry->register_element(ElementDescriptor("Gain", factory, metadata));
```

---

## Parameter Specification

### Types
- `"real"`: Double precision floating point
- `"int"`: 64-bit signed integer
- `"string"`: UTF-8 string
- `"bool"`: Boolean
- `"enum"`: Enumerated choice with allowed values
- `"array"`: JSON array of values

### Validation
- Type checking
- Range validation (min/max for numeric)
- Required vs optional
- Automatic type coercion (int to real)
- Enum value validation

### Example
```cpp
ParameterSpec{
    .name = "frequency_hz",
    .type = "real",
    .description = "Frequency in Hz",
    .default_value = 1000.0,
    .min_value = 0.1,
    .max_value = 100000.0,
    .required = true
}
```

---

## Schema JSON Format

```json
{
  "name": "schema_name",
  "node_id": "compute-01",
  "node_uuid": "...",
  
  "elements": [
    {
      "id": "gen",
      "type": "AnalogOscillator",
      "uuid": "...",
      "params": {"frequency_hz": 1000}
    }
  ],
  
  "connections": [
    {
      "from_element": "gen",
      "from_port": 0,
      "to_element": "filter",
      "to_port": 0
    }
  ],
  
  "network_topology": [
    {
      "node_id": "compute-01",
      "node_uuid": "...",
      "port": 8001
    }
  ]
}
```

---

## Registry API

### Singleton Access
```cpp
auto& registry = s7::ElementRegistry::instance();
```

### Find Element
```cpp
const auto* desc = registry.find("Gain");
const auto& meta = desc->metadata();
```

### List Elements
```cpp
auto all = registry.element_types();
auto filters = registry.element_types_in_category("filters");
```

### Create Instance
```cpp
s7::ElementCreationContext ctx{meta, params_json};
auto elem = desc->create(ctx);
```

---

## Element Categories

| Category | Use | Color |
|----------|-----|-------|
| `basic` | Math, constants | #4CAF50 |
| `filters` | DSP filters | #2196F3 |
| `io` | File I/O | #FF9800 |
| `network` | Network comms | #9C27B0 |
| `control` | Logic | #F44336 |
| `conversion` | Type convert | #00BCD4 |
| `custom` | User defined | #795548 |

---

## Visual Representation

### Shapes
- `"box"`: General processing
- `"circle"`: Sources
- `"triangle"`: Sinks
- `"diamond"`: Logic
- `"trapezoid"`: I/O

### Visual Hints Metadata
```cpp
struct VisualHints {
    std::string shape;
    std::string color;          // #RRGGBB
    int max_width = 100;
    int max_height = 50;
    std::string icon_name;
};
```

---

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Element lookup by UUID | O(1) | Hash table |
| Category filter | O(n) | n = elements in category |
| Registry lookup | ~1 μs | One-time at compile |
| Plugin load | ~10 ms | One-time at startup |
| Remote element access | ~1 ms | Network latency |

---

## Backward Compatibility

- ✅ All existing schemas work unchanged
- ✅ Element type names unchanged
- ✅ Parameter names and defaults unchanged
- ✅ Automatic UUID generation if not specified
- ✅ Non-breaking implementation

---

## File Locations

### Architecture Documents
- `DISTRIBUTED_ADDRESSING.md` - Distributed system design
- `ELEMENT_LIBRARY_TECH.md` - Registry/plugin architecture
- `ELEMENT_DEV_GUIDE.md` - Element development

### Source Code (s7l2 project)
- `s7core/include/s7/element.hpp` - Element base class
- `s7core/include/s7/elements.hpp` - Built-in elements
- `s7core/src/elements.cpp` - Implementations
- `s7runtime/src/runtime_service.cpp` - Schema loading
- `s7runtime/include/s7/runtime_service.hpp` - Runtime interface

---

## Next Steps

### Implementation
1. Create metadata framework (Phase 1)
2. Annotate built-in elements (Phase 2)
3. Implement plugin infrastructure (Phase 3)
4. Convert to shared libraries (Phase 4)
5. Integrate with runtime (Phase 5)
6. Add distributed addressing (Phase 7)

### Development
1. Read `ELEMENT_LIBRARY_TECH.md` for architecture
2. Follow `ELEMENT_DEV_GUIDE.md` for element creation
3. Reference `DISTRIBUTED_ADDRESSING.md` for UUID usage

---

## Resources

- **S7 Vision**: `/docs/S7_VISION.md`
- **Runtime Architecture**: `/docs/S7_Runtime_Architecture.md`
- **Formal Model**: `/docs/S7_Formal_Model.md`
- **Terminology**: `/docs/S7_Terminology.md`



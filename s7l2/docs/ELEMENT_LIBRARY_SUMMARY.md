# Element Library System - Summary & Discussion Points

## Overview

This document summarizes the analysis and proposals for enhancing the S7 element library system to support dynamic loading, self-documentation, and flexible architectures.

## Key Documents Created

1. **ELEMENT_LIBRARY_ANALYSIS.md** - Comprehensive architecture analysis
   - Current system review
   - Desired features breakdown
   - Proposed architecture with code examples
   - Benefits and implementation priority

2. **ELEMENT_LIBRARY_IMPLEMENTATION.md** - Detailed implementation roadmap
   - 7 stages of development with timelines
   - Stage-by-stage tasks and deliverables
   - Resource requirements
   - Risk assessment
   - Success criteria

3. **ELEMENT_DEVELOPMENT_GUIDE.md** - Practical developer reference
   - Quick start guides
   - Code templates and examples
   - Parameter types and categories
   - Testing and troubleshooting
   - CMake configuration examples

## Current State Summary

### Architecture
```
┌─────────────────────────────────────────────┐
│         s7runtime (executable)              │
│  - Loads JSON schemas                       │
│  - Hardcoded element switch statement       │
│  - No plugin support                        │
│  - No parameter validation                  │
└──────────────┬──────────────────────────────┘
               │
      ┌────────┼────────┐
      │        │        │
      ▼        ▼        ▼
   s7core   s7io    s7net
  (STATIC) (STATIC) (STATIC)
  - Element base class
  - Templated elements
  - Schema/Compiler
  - 15-20 built-in elements
```

### Limitations
- **Hardcoded Registration**: Switch statement in runtime_service.cpp (40+ lines)
- **No Discoverability**: Must know element name to use
- **No Validation**: Raw parameter objects, no type checking
- **No Documentation**: Elements have no metadata/help
- **No Extensibility**: Cannot load external plugins
- **Fixed Topology**: Elements have fixed input/output counts
- **No Visual Support**: No hints for diagram rendering
- **Static Linking**: Cannot load shared libraries at runtime

## Proposed Solution Architecture

### Three-Tier Element System

```
┌─────────────────────────────────────────────────────────────┐
│                    ELEMENT METADATA LAYER                   │
│  - ElementMetadata (name, description, category, tags)     │
│  - ParameterSpec (type, default, range, validation)        │
│  - VisualHints (shape, color, icon for s7studio)           │
│  - Port descriptors with optional/variadic support         │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    ELEMENT REGISTRY LAYER                   │
│  - Centralized registry of all available elements           │
│  - Metadata lookup by element type                          │
│  - Element discovery and filtering                          │
│  - Parameter validation                                    │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                  ELEMENT PLUGIN LAYER                       │
│  - Shared library loading (dlopen)                          │
│  - C ABI-compatible plugin interface                        │
│  - Plugin discovery and version checking                    │
│  - Runtime registration of external elements               │
└─────────────────────────────────────────────────────────────┘
```

### New Architecture

```
┌─────────────────────────────────────────────────────────────┐
│         s7runtime (executable)                              │
│  - Loads JSON schemas                                       │
│  - Uses ElementRegistry for instantiation                   │
│  - Validates parameters                                    │
│  - Loads plugins at startup                                │
└──────────────┬──────────────────────────────────────────────┘
               │
      ┌────────┼────────┬──────────────┐
      │        │        │              │
      ▼        ▼        ▼              ▼
   s7core   s7io    s7net      s7core (shared)
 (SHARED) (SHARED) (SHARED)
 - Element base class
 - Metadata framework
 - Registry (singleton)
 - Plugin interface
 - 15-20 built-in elements

                    │
                    ▼
          ┌─────────────────────┐
          │  Plugin Libraries   │
          │  (SHARED)           │
          │ - External elements │
          │ - Metadata          │
          └─────────────────────┘
```

## Key Enhancements

### 1. Element Metadata
Every element describes itself:
- Name, description, version, category, tags
- Input/output port specifications
- Parameter requirements with types and ranges
- Visual representation hints (shape, color, icon)
- Example usage, documentation links

### 2. Pluggable Architecture
- Build s7core, s7io, s7net as shared libraries
- External teams write plugins with same interface
- Plugins discovered and loaded at runtime
- C ABI for compatibility across toolchains

### 3. Parameter Validation
- Type checking (real, int, string, enum, bool)
- Range validation (min/max for numerics)
- Required vs optional
- Automatic type coercion where safe

### 4. Variable Input Support
- Some elements can accept variable number of inputs
- Metadata describes constraints (min, max)
- Useful for: mixers, variadic adders, data aggregators

### 5. Visual Integration
- Metadata includes hints for s7studio diagrams
- Colors, shapes, icons per element
- Categories for visual grouping
- Port layout suggestions

## Implementation Roadmap

### Phase 1: Foundation (Weeks 1-3)
✅ Element metadata framework
✅ Element registry pattern
✅ Annotate existing elements with metadata

### Phase 2: Plugins (Weeks 3-5)
✅ Plugin interface (C ABI)
✅ Build shared libraries
✅ Runtime plugin discovery and loading

### Phase 3: Runtime (Weeks 5-6)
✅ Remove hardcoded switch statement
✅ Use registry for element instantiation
✅ Parameter validation against metadata

### Phase 4: Advanced (Weeks 6-8)
✅ Variadic input support
✅ Visual representation integration
✅ Studio catalog service

## Starting Implementation

### Immediate Next Steps (This Week)

1. **Create header files** (no implementation changes needed yet):
   - `s7core/include/s7/element_metadata.hpp`
   - `s7core/include/s7/element_registry.hpp`
   - `s7core/include/s7/element_plugin.hpp`

2. **Design decisions needed**:
   - Parameter schema (type system details)
   - Category taxonomy
   - Visual representation color scheme
   - Plugin search path strategy

3. **Proof of concept**:
   - Implement metadata for 1-2 existing elements
   - Create simple registry example
   - Test metadata serialization to JSON

### This ensures:
- ✅ No disruption to existing code (backwards compatible)
- ✅ Can iterate on design before full implementation
- ✅ Team can review interfaces early
- ✅ Smoother transition to full plugin architecture

## Discussion Questions

### Architecture
1. Should we use JSON Schema for parameter validation or simpler approach?
2. Do we need element versioning beyond version string?
3. Should metadata be in code or external files (YAML/JSON)?
4. How should we handle element migrations/deprecations?

### Parameters
1. Should array parameters support nested objects?
2. How granular should enum validation be?
3. Do we need parameter dependencies (e.g., "param B only valid if param A = X")?
4. Should there be runtime parameter reconfiguration (after creation)?

### Variadic Elements
1. Should variadic ports have names or just indices?
2. Can variadic elements be mixed with fixed ports?
3. Should there be limits on growth (memory, performance)?

### Plugins
1. Single API version or support multiple versions?
2. Should plugins be vendored or externally discovered?
3. Hot-reload during runtime or load-at-startup only?
4. Plugin sandboxing/security considerations?

### Visual Representation
1. Should shapes/colors be standardized or extensible?
2. Do we need layout algorithms for port positioning?
3. Should diagram comments be stored in metadata?
4. Icon/image asset strategy for s7studio?

### Performance
1. Is metadata lookup performance critical? (Likely not)
2. Should lazy-loading of plugin metadata be considered?
3. Any performance concerns with registry singleton?

### Testing
1. How should we test plugins (unit vs integration)?
2. Should there be a plugin validation tool?
3. Test coverage requirements for metadata completeness?

## Backwards Compatibility

Current schemas will continue to work:
- Existing JSON format unchanged
- Element type names unchanged
- Parameter names unchanged
- All existing elements work with new registry

Migration is **non-breaking** - new and old can coexist during transition.

## Benefits Summary

| Benefit | Impact |
|---------|--------|
| **Extensibility** | External teams can build plugins without core modifications |
| **Discoverability** | Tools can query registry for available elements |
| **Self-Documenting** | Metadata provides help, examples, constraints |
| **Robust** | Parameter validation prevents invalid configurations |
| **Maintainable** | No giant switch statement to modify |
| **Scalable** | Add 100 new elements = 0 changes to core |
| **Visual-Friendly** | Metadata enables better diagram rendering |
| **Future-Proof** | Foundation for state serialization, documentation generation, etc. |

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|-----------|
| ABI compatibility | Medium | C interface, version checking |
| Shared lib dependencies | Low | Proper RPATH, install strategy |
| Performance impact | Low | Registry is O(log n) lookup, cached |
| Learning curve | Medium | Comprehensive documentation + examples |
| Breaking changes | Low | Careful backwards compatibility planning |

## Success Measures

✅ All built-in elements have full metadata
✅ Example external plugin builds and loads
✅ Parameter validation working correctly
✅ No performance regression vs current
✅ s7studio can query element catalog via metadata
✅ Developer documentation complete
✅ First community plugin successfully developed
✅ Zero regression in existing schema compatibility

## Timeline

- **Week 1-2**: Metadata framework + design review
- **Week 2-3**: Annotate elements + proof of concept
- **Week 3-4**: Plugin interface + example
- **Week 4-5**: Build system changes + shared libraries
- **Week 5-6**: Runtime integration + parameter validation
- **Week 6-7**: Variadic support + tests
- **Week 7-8**: Visual integration + documentation
- **Week 8-9**: Bug fixes, integration testing, release

**Total: 8-9 weeks to full implementation**

## Next Meeting Agenda

1. Review proposed architecture (30 min)
2. Discuss design questions (45 min)
3. Agree on Phase 1 tasks (30 min)
4. Set up development environment (15 min)

**Goal**: Start Phase 1 implementation next week

## Appendix: Quick Reference

### Element Metadata Structure
```
ElementMetadata
├── type_name: string
├── display_name: string
├── description: string
├── category: string (basic, filters, io, network, control, conversion)
├── version: string
├── tags: string[]
├── input_ports: PortSpec[]
├── output_ports: PortSpec[]
├── parameters: ParameterSpec[]
└── visual: VisualHints
    ├── shape: string (box, circle, triangle, diamond)
    ├── color: string (#RRGGBB)
    ├── icon_name: string
    └── size: int x int
```

### Registry Operations
```cpp
// Find element
const auto* desc = registry.find("ElementName");

// List all elements
auto all = registry.element_types();

// Filter by category
auto filters = registry.element_types_in_category("filters");

// Get metadata
auto meta = desc->metadata();

// Create element
auto elem = desc->create(ctx);
```

### Parameter Types
- `real`: Double-precision floating point
- `int`: 64-bit signed integer
- `string`: UTF-8 string
- `bool`: Boolean
- `enum`: Enumerated choice
- `array`: JSON array

## Contact & Questions

For questions about this proposal, refer to the detailed analysis documents or discuss in team meetings.

**Related Documentation**:
- ELEMENT_LIBRARY_ANALYSIS.md - Full architecture
- ELEMENT_LIBRARY_IMPLEMENTATION.md - Implementation details
- ELEMENT_DEVELOPMENT_GUIDE.md - Developer reference


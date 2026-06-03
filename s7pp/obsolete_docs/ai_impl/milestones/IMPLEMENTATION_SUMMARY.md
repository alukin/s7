# Implementation Summary: s7stdlib Static Linking + Plugin Export

**Status:** Historical  
**Purpose:** Milestone summary of plugin architecture implementation  
**Last Updated:** May 10, 2026

---

## Objective

Enable **s7stdlib** to be simultaneously:
1. **Statically linked** — always available on every computational node (must-have core library)
2. **Exportable as a loadable plugin** — satisfying the `i_s7lib.hpp` ABI boundary used by the element catalog system

This unified approach allows:
- Zero runtime overhead (static linking)
- Unified element discovery (plugin manifest API)
- Future extensibility (additional plugin libraries)

---

## Changes Made

### 1. Created Built-in Plugin Implementation

**File:** `s7core/src/s7_builtin_plugin.cpp` (180 lines)

Implements the C ABI plugin interface from `i_s7lib.hpp`:

```cpp
extern "C" {
  const s7_plugin_manifest_t* s7_plugin_get_manifest(void);
  s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx);
  void s7_plugin_shutdown(void);
}
```

Features:
- Static manifest array containing all 13 built-in element descriptors
- Element type IDs follow the dot-format standard (e.g., `s7.core.math.constant`)
- Plugin metadata: vendor, version, API version, platform target
- No-op registration (elements already registered statically)

### 2. Created Plugin Header

**File:** `s7core/include/s7_builtin_plugin.hpp` (40 lines)

Declares the plugin entry points so they can be called from the runtime:

```cpp
extern const s7_plugin_manifest_t* s7_plugin_get_manifest(void);
extern s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx);
extern void s7_plugin_shutdown(void);
```

### 3. Updated CMakeLists.txt

**File:** `s7core/CMakeLists.txt`

Added `s7_builtin_plugin.cpp` to the s7core static library build:

```cmake
add_library(s7core STATIC
    ...
    src/s7_builtin_plugin.cpp
)
```

### 4. Fixed Include Path

**File:** `s7core/include/s7stdlib.hpp`

Removed non-existent include (the user clarified that `i_s7lib.hpp` is the plugin interface header, not `i_s7stdlib.hpp`):

```cpp
// REMOVED: #include "i_s7stdlib.hpp"
#include "s7.hpp"  // Kept
```

### 5. Updated Documentation

**File:** `ai_impl_docs/S7_BUILTIN_PLUGIN.md` (220 lines)

Comprehensive guide covering:
- Architecture overview and design rationale
- File roles and responsibilities
- Phases of static initialization, catalog publication, and plugin registration
- Element type ID naming scheme
- Comparison with external plugins
- Future extension possibilities
- ABI stability notes

**File:** `README.md`

Added link to the new documentation.

---

## Architecture Summary

### Build-Time: Static Library

```
s7core/
  include/
    s7stdlib.hpp         ← Public C++ element classes
    i_s7lib.hpp          ← Plugin ABI boundary
    s7_builtin_plugin.hpp ← Entry point declarations
  src/
    s7stdlib.cpp         ← Implementations
    s7registry.cpp       ← JSON factory registration
    s7_builtin_plugin.cpp ← Plugin ABI implementation (NEW)
    
CMakeLists.txt → libs7core.a (static, always linked)
```

### Runtime: Dual Access

Element access paths:

1. **Direct C++ (Application Code)**
   ```cpp
   #include "s7stdlib.hpp"
   auto sum = std::make_shared<s7::Sum<Real>>(weights);
   ```
   → Direct instantiation, zero overhead

2. **Via Plugin ABI (Element Catalog System)**
   ```cpp
   auto manifest = s7_plugin_get_manifest();  // Returns manifest array
   // GUI palette populated with metadata from manifest
   ```
   → Unified discovery, identical to external plugins

3. **Via JSON Registry (Netlist Builder)**
   ```cpp
   auto schema = SchemaBuilder::build(json_netlist);
   // "type": "Sum" → ElementRegistry → Sum factory
   ```
   → Factory-based instantiation for JSON-serialized graphs

---

## Built-in Elements (13 Total)

All elements are declared in the static manifest with stable type IDs:

| Type ID | Display Name | Category |
|---------|--------------|----------|
| `s7.core.math.constant` | Constant | Sources |
| `s7.core.dsp.impulse` | Impulse Source | Sources |
| `s7.core.math.integrator` | Integrator | Control/Dynamics |
| `s7.core.math.sum` | Sum | Math |
| `s7.core.type.to_complex` | To Complex | Type Conversion |
| `s7.core.type.to_reals` | To Reals | Type Conversion |
| `s7.core.analog.vca` | VCA | Analog/Control |
| `s7.core.dsp.fir_filter` | FIR Filter | Filters |
| `s7.core.dsp.iir_filter` | IIR Filter | Filters |
| `s7.core.osc.digital` | Digital Oscillator | Oscillators |
| `s7.core.osc.harmonic` | Harmonic Oscillator | Oscillators |
| `s7.core.analog.vco` | VCO | Oscillators |
| `s7.core.analysis.energy_difference` | Energy Difference | Analysis |

---

## Testing

✅ **All tests pass:**

```bash
CMake configure: ✓
Build s7core:    ✓ (libs7core.a)
Build s7main:    ✓ (executable)
Build s7tests:   ✓ (executable)
s7main/s7main:   ✓ (all examples pass)
  • Programmatic feedback loop
  • Algebraic loop rejection
  • Type converters (Real ↔ Complex)
  • Oscillator/VCA demo
  • Filter demo
  • Oscillator match test (100 periods, 200 samples/period)
  • JSON netlist instantiation ✓
```

No compilation errors or runtime failures.

---

## Integration Points

The built-in plugin is ready for integration with:

1. **Control-Plane Server** (`S7_CONTROL_PLANE.md`)
   - Call `s7_plugin_get_manifest()` to populate `runtime.catalog.subscribe` responses
   - Return element metadata to GUI for palette rendering

2. **Element Catalog System** (Future implementation)
   - Maintain capability state per node (e.g., `loaded`, `disabled`, `incompatible`)
   - Handle element version matching and fallback strategies

3. **Plugin Loader** (Future implementation)
   - Treat built-in plugin as always-loaded (no dlopen needed)
   - Same registration flow as external plugins

---

## Future Extensions

### Additional Static Libraries

Add specialized element libraries side-by-side with s7stdlib:

```cmake
# In s7core/CMakeLists.txt
add_library(s7filters STATIC
    src/s7filters.cpp
    src/s7filters_plugin.cpp  # Same pattern
)
```

### Conditional Loading Configuration

Nodes can declare which libraries they provide:

```yaml
node_01:
  capabilities:
    - library: "s7.core.stdlib"
      version: [0, 1]
      status: "enabled"
    - library: "s7.filters.dsp"
      version: [0, 2]
      status: "enabled"
```

### Multi-Platform Plugin Manifest

Make `build_target` dynamic based on CMake platform detection:

```cpp
#if defined(__x86_64__) && defined(__linux__)
  .build_target = "Linux-x86_64",
#elif defined(__arm__) && defined(__linux__)
  .build_target = "Linux-armv7l",
#endif
```

---

## Files Summary

| File | Lines | Purpose |
|------|-------|---------|
| `s7core/src/s7_builtin_plugin.cpp` | 180 | Plugin ABI implementation (NEW) |
| `s7core/include/s7_builtin_plugin.hpp` | 40 | Plugin entry point declarations (NEW) |
| `ai_impl_docs/S7_BUILTIN_PLUGIN.md` | 220 | Architecture documentation (NEW) |
| `s7core/CMakeLists.txt` | 93 | Updated to include plugin file |
| `s7core/include/s7stdlib.hpp` | 387 | Removed non-existent include |
| `README.md` | 30 | Updated documentation index |

---

## Design Rationale

### Why Static Linking?

1. **Performance:** No dlopen/dlsym overhead, direct inlining possible
2. **Certainty:** Core elements guaranteed available on all nodes
3. **Stability:** No version mismatch issues within a release

### Why Also Export as Plugin?

1. **Catalog Unification:** GUI uses same element discovery for all sources
2. **Future Flexibility:** Easy to add plugin-only or optional libraries
3. **System Clarity:** Clear boundary between built-in and external extensions

### Why C ABI Boundary?

1. **Binary Stability:** Works across C++ compiler/version changes
2. **Language Neutral:** Enables future non-C++ plugins (Rust, Go, etc.)
3. **Minimal API Surface:** Easy to evolve the contract over time

---

## Next Steps

Ready for:
1. **Control-plane server** to use `s7_plugin_get_manifest()` for element discovery
2. **Plugin loader** to handle external .so files with same ABI
3. **Baked engine** implementation (elements transition to IR form)
4. **Multi-node deployment** with heterogeneous element capabilities


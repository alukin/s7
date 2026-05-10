# S7 Built-in Plugin Architecture

**Date:** May 10, 2026  
**Status:** Implemented v0  
**Related:** [`docs/S7_PLUGIN_API.md`](S7_PLUGIN_API.md), [`s7core/include/s7_builtin_plugin.hpp`](../s7core/include/s7_builtin_plugin.hpp)

---

## Overview

The S7 runtime uses a **hybrid static + plugin model** for element libraries:

1. **s7stdlib is statically linked** — guaranteed to always be available on every computational node
2. **s7stdlib is also exported as a pseudo-plugin** — allowing the runtime's element catalog system to treat built-in elements identically to dynamically-loaded plugin elements

This design provides:
- **Zero runtime overhead** for built-in elements (static linking, no dlopen/dlsym)
- **Unified element catalog** — GUI discovers and manages built-in and plugin elements using the same interface
- **Future extensibility** — easy path to create additional static or dynamic element libraries

---

## Architecture

### Files

| File | Purpose |
|------|---------|
| `s7core/include/s7stdlib.hpp` | Public C++ interface for all built-in element classes (Constant, Integrator, Sum, FIRFilter, IIRFilter, VCO, etc.) |
| `s7core/src/s7stdlib.cpp` | Implementation of stateful elements (ImpulseSource, etc.) |
| `s7core/src/s7registry.cpp` | Registers elements with the `ElementRegistry` used by the JSON netlist builder and factory system |
| `s7core/src/s7_builtin_plugin.cpp` | **New:** Implements the C ABI plugin interface (`i_s7lib.hpp`) for s7stdlib |
| `s7core/include/s7_builtin_plugin.hpp` | **New:** C ABI declarations for the built-in plugin entry points |
| `s7core/include/i_s7lib.hpp` | Plugin ABI boundary — used by both built-in and external plugins |

### Building the Library

The CMakeLists.txt in `s7core/` builds a single **static library `libs7core.a`** containing:
- Core runtime (`s7.cpp`, `s7netlist.cpp`)
- Standard library elements (`s7stdlib.cpp`)
- Registry/factory system (`s7registry.cpp`)
- **Built-in plugin ABI layer** (`s7_builtin_plugin.cpp`)

All computational nodes link this library statically — it's always available.

---

## How It Works

### Phase 1: Static Initialization

When the runtime starts:

```cpp
// In s7main/src/main.cpp or runtime initialization
s7::registerBuiltins();  // Register all s7stdlib elements with ElementRegistry
```

This populates the `ElementRegistry` with element factories for all built-in types:
- `Constant`, `Integrator`, `Sum`, `ToComplex`, `ToReals`, `Vca`, `FIRFilter`, `IIRFilter`, `DigitalOscillator`, `HarmonicOscillator`, `Vco`, `EnergyDifference`

**Result:** Elements are directly instantiable via JSON netlists.

### Phase 2: Catalog Publication (Control Plane)

When the GUI connects to the runtime's control-plane server:

```rust
// Pseudo-code for control-plane server
fn on_catalog_get_request() {
    // Call the built-in plugin ABI
    let manifest = s7_plugin_get_manifest();
    
    // Return element list to GUI
    return PublishElementCatalog {
        elements: manifest.elements,
    };
}
```

The runtime calls:
```cpp
const s7_plugin_manifest_t* s7_plugin_get_manifest(void);
```

This returns a static manifest containing all built-in element metadata, including:
- `type_id`: Dot-separated stable identifier (e.g., `s7.core.math.constant`)
- `display_name`: Human-readable name
- `category`: GUI palette category
- `summary`: Description

**Result:** GUI palette is populated with built-in elements.

### Phase 3: Registration (Plugin Init)

If the control-plane system needs to formally "register" the built-in plugin:

```cpp
s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx);
```

For the built-in plugin, this is a no-op (elements already registered statically), but it satisfies the plugin ABI contract.

---

## Element Type ID Format

All s7stdlib elements use the **lower-case dot-separated format** from `S7_PLUGIN_API.md`:

```
s7.core.<domain>.<family>.<element>[.<variant>]
```

Examples:
- `s7.core.math.constant` — Constant value source
- `s7.core.math.integrator` — Integrator/accumulator
- `s7.core.math.sum` — Weighted sum
- `s7.core.dsp.fir_filter` — FIR filter
- `s7.core.analog.vco` — Voltage-controlled oscillator
- `s7.core.osc.digital` — Digital oscillator
- `s7.core.analysis.energy_difference` — Energy analysis

---

## Comparison: Built-in vs External Plugin

### Built-in Plugin (s7stdlib)

```cpp
// Always available, statically linked
#include "s7stdlib.hpp"
auto my_sum = std::make_shared<s7::Sum<Real>>(weights);

// Also available via plugin interface
const s7_plugin_manifest_t* manifest = s7_plugin_get_manifest();
```

**Advantages:**
- Zero runtime overhead (no dlopen/dlsym)
- Guaranteed availability on all nodes
- Direct C++ use without IndirectFactory instantiation

**Normal use case:**
- Core DSP/math elements that must be on every node
- High-performance, frequently-used elements

### External Plugin (Future)

```cpp
// Loaded at runtime from .so file
plugin_handle = dlopen("libmy_filter_pack.so", RTLD_LAZY);
auto manifest = (s7_plugin_get_manifest_fn)dlsym(plugin_handle, "s7_plugin_get_manifest")();

// Optional registration
auto register_fn = (s7_plugin_register_fn)dlsym(...);
register_fn(&ctx);
```

**Advantages:**
- Optional/conditional loading based on node capabilities
- Third-party element libraries
- Isolated ABI boundary (C only)

**Normal use case:**
- Vendor-specific elements (e.g., `acme.sdr.source.rtlsdr`)
- Domain-specific element collections
- Optional features

---

## Plugin Manifest Structure

The built-in plugin exposes a static manifest array in `s7_builtin_plugin.cpp`:

```cpp
static const s7_element_manifest_entry_t k_s7stdlib_elements[] = {
    S7_ELEMENT(s7.core.math.constant,
               "Constant",
               "Sources",
               "Emits a fixed scalar value"),
    // ... 12 more elements
};

static const s7_plugin_manifest_t k_s7stdlib_manifest = {
    .plugin_id = "s7.core.stdlib",
    .plugin_name = "S7 Core Standard Library",
    .plugin_version = {0, 1},
    .plugin_api_version = {0, 1},
    .elements = k_s7stdlib_elements,
    .element_count = 13,
    // ...
};
```

---

## Future Extensions

### Multi-library Support

If additional static libraries are needed (e.g., specialized filter pack):

```cpp
// In CMakeLists.txt
add_library(s7filters STATIC
    src/s7filters.cpp
    src/s7filters_plugin.cpp  // Exposes plugin ABI
)

// In main initialization
s7::registerBuiltins();           // from s7stdlib
s7::registerFilterLibraryBuiltins();  // from s7filters
```

Each library can export its own static manifest.

### Conditional Loading

Nodes can be configured to load only specific built-in libraries:

```yaml
# Deployment config for node_01
enabled_libraries:
  - s7.core.stdlib    # Always
  - s7.filters.dsp    # Conditional on this node's resources
```

---

## ABI Stability Notes

The plugin ABI (C boundary) is marked **experimental/internal** (`i_s7lib.hpp` v0.1):

- C linkage guarantees binary compatibility across C++ revision boundaries
- Struct layouts are fixed size, no variation
- Type IDs are strings (future-proof)
- Registry API is callback-based (extensible)

When public ABI stabilizes (v1.0+), external vendorscan build plugins against the published SDK.

---

## Build Verification

After implementing this architecture:

```bash
cd s7pp && mkdir cmake-build-debug && cd cmake-build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .

# Run tests
./s7main/s7main
# Expected: all DSP/control examples pass, JSON netlist instantiation works
```

All s7stdlib elements are:
- Directly compilable and linkable (C++ static)
- Discoverable via plugin manifest (C ABI)
- Instantiable from JSON netlists (registry system)


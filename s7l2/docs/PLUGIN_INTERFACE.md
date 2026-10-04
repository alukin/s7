# Plugin Interface

Element plugins are shared libraries that register elements into the process-wide `s7::ElementRegistry` at startup, without changes to core code. The element model and metadata are described in `ELEMENT_DEV_GUIDE.md`.

Element sources in a running process:

1. built-ins (`s7::register_builtin_elements`, `s7::register_net_elements`);
2. shared-library plugins (this document);
3. schema-backed libraries loaded from JSON (`SCHEMA_LIBRARY_WORKFLOW.md`).

## C ABI

Header: `s7core/include/s7/element_plugin.hpp`. A plugin exports these `extern "C"` symbols:

| Symbol | Required | Purpose |
|---|---|---|
| `const S7PluginInfo* s7_plugin_query()` | yes, unless a manifest is provided | Name, version, author, element count, `api_version` |
| `const char* s7_plugin_manifest_json()` | optional | UTF-8 JSON manifest; preferred over `s7_plugin_query` when present |
| `void s7_register_elements(void* registry)` | yes | Registers elements; `registry` is an `s7::ElementRegistry*` |

```cpp
struct S7PluginInfo {
    uint32_t api_version;      // must equal S7_PLUGIN_API_VERSION (currently 1)
    uint32_t element_count;
    const char* plugin_name;
    const char* plugin_version;
    const char* author;
};
```

Manifest example (`s7noise`):

```json
{
  "api_version": 1,
  "plugin_name": "s7noise",
  "plugin_version": "1.0.0",
  "author": "S7 Project",
  "description": "Noise source elements for the runtime.",
  "element_count": 2,
  "elements": ["WhiteNoiseGenerator", "PinkNoiseGenerator"]
}
```

`element_count` must match the `elements` array length when both are present.

Although the entry points are C functions, the registry pointer and `ElementDescriptor` are C++ types: a plugin must be built with the same compiler, standard library, and S7 headers as the host. `s7core` is currently a static library, so each plugin links its own copy of `s7core` code.

## Minimal plugin

```cpp
#include "s7/element_plugin.hpp"
#include "s7/element_registry.hpp"

static const S7PluginInfo kInfo{S7_PLUGIN_API_VERSION, 1, "example", "1.0.0", "Author"};

extern "C" const S7PluginInfo* s7_plugin_query() { return &kInfo; }

extern "C" void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    registry->register_element(s7::ElementDescriptor(
        "LinearTransform",
        [](const s7::ElementCreationContext& ctx) { /* create element */ },
        create_linear_transform_metadata()));
}
```

In-tree plugins are added with `add_subdirectory` and link `s7core`; see `s7noise/CMakeLists.txt`. Plugins install to `lib/s7/plugins`.

## Discovery and loading

Loader: `s7runtime/src/plugin_loader.cpp` (also compiled into `s7studio`).

Search order:

1. directories passed with `s7runtime --plugin-dir <dir>` (repeatable);
2. `~/.s7/plugins/`;
3. `/opt/s7/plugins/`;
4. `./plugins/` (relative to the working directory);
5. `$S7_PLUGIN_PATH` (a single directory).

File names: `libs7*.so*` (Linux), `libs7*.dylib` (macOS), `s7*.dll` (Windows). Symlinks are skipped.

Studio additionally tries the build-tree `s7noise` library next to its executable (`../s7noise/`), so a fresh build shows the noise elements without installation. `s7runtime` does not; use `--plugin-dir build/s7noise`.

Loading steps: `dlopen(RTLD_LAZY | RTLD_LOCAL)` → read manifest or query info → check `api_version` → call `s7_register_elements` → keep the library loaded until process exit (registered factories point into plugin code). A plugin that fails any step is reported on stderr and skipped; the process continues.

## Trust model

Plugins run with full process privileges; there is no sandboxing or signature check. Load only trusted plugins.

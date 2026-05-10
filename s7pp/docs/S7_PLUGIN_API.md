# S7 Plugin API Specification (Draft v0)

This document defines the initial plugin boundary for S7 runtime nodes.

Status: draft, experimental/internal-first.

## 1. Goals

The plugin API should let runtime nodes load additional element libraries from shared libraries while exposing enough metadata for:

- runtime-side planning and validation,
- GUI-side palette/catalog discovery,
- deployment-time capability checks.

## 2. Scope of v0

Included in v0:

- shared-library loading,
- plugin manifest query,
- element descriptor export,
- runtime registration of element factories,
- plugin compatibility/version checks,
- plugin diagnostics for load failures.

Out of scope for v0:

- stable public ABI guarantees across long release ranges,
- plugin-provided GUI binaries/widgets,
- plugin-defined transport protocol extensions,
- hot-reload state migration guarantees.

## 3. Design constraints

- boundary must be C ABI friendly,
- plugin internals may use C++,
- host/runtime identity and trust are handled by control-plane security,
- runtime remains authoritative for final validation,
- plugin load errors must be structured and visible via catalog diagnostics.

## 4. Versioning model

Use three independent versions:

- `plugin_api_version`: ABI contract version for runtime <-> plugin,
- `plugin_manifest_version`: descriptor schema version,
- `plugin_version`: semantic version of the plugin itself.

Compatibility rule for v0:

- runtime and plugin `plugin_api_version` major must match exactly,
- minor mismatch may be accepted only if runtime explicitly supports it,
- otherwise runtime rejects plugin as `incompatible`.

## 5. Shared library contract

## 5.1 Platform note

This document uses Linux-oriented terms (`.so`, `dlopen`, `dlsym`) as current baseline.

## 5.2 Required exported symbols

Each plugin library should export:

- `s7_plugin_get_manifest`
- `s7_plugin_register`

Optional:

- `s7_plugin_shutdown`

Names are intentionally stable in draft v0 for easier loader implementation.

## 5.3 Lifecycle

1. runtime loads shared library,
2. runtime resolves required symbols,
3. runtime calls `s7_plugin_get_manifest`,
4. runtime validates compatibility,
5. runtime calls `s7_plugin_register`,
6. runtime stores registration results in local catalog.

On unload or shutdown, runtime may call optional shutdown hook.

## 6. C ABI surface (conceptual)

The exact header can be finalized later, but the shape should remain close to this model.

```c
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t major;
    uint32_t minor;
} s7_version_t;

typedef struct {
    const char* key;
    const char* value;
} s7_kv_t;

typedef struct {
    const char* type_id;
    const char* display_name;
    const char* category;
    const char* summary;
    const char* descriptor_json;
} s7_element_manifest_entry_t;

typedef struct {
    const char* plugin_id;
    const char* plugin_name;
    s7_version_t plugin_version;
    s7_version_t plugin_api_version;
    s7_version_t manifest_version;
    const char* vendor;
    const char* build_target;
    const s7_kv_t* tags;
    uint32_t tag_count;
    const s7_element_manifest_entry_t* elements;
    uint32_t element_count;
} s7_plugin_manifest_t;

typedef struct s7_registry_api_t s7_registry_api_t;

typedef struct {
    s7_version_t runtime_plugin_api_version;
    const s7_registry_api_t* registry;
} s7_plugin_register_context_t;

typedef int32_t s7_status_t;

typedef const s7_plugin_manifest_t* (*s7_plugin_get_manifest_fn)(void);
typedef s7_status_t (*s7_plugin_register_fn)(const s7_plugin_register_context_t* ctx);
typedef void (*s7_plugin_shutdown_fn)(void);

#ifdef __cplusplus
}
#endif
```

## 7. Runtime registry API (conceptual)

Runtime-provided registry interface should include operations such as:

- `register_element_factory(type_id, factory_fn, descriptor_json)`,
- `register_alias(alias_type_id, target_type_id)` (optional),
- `register_plugin_warning(code, message)`.

The runtime should reject duplicate `type_id` registrations unless explicitly configured to allow override.

## 8. Element descriptor contract

Each element exported by plugin must provide descriptor metadata sufficient for runtime planning and GUI authoring.

Required descriptor fields:

- `type_id` (lower-case dot-separated, globally stable),
- `display_name`,
- `category_path`,
- `summary`,
- `plugin_id`,
- `version`,
- port descriptors,
- parameter schema,
- `is_stateful`,
- `has_direct_feedthrough`,
- supported execution domains,
- supported transport kinds,
- hardware requirements,
- placement hints.

## 8.1 Type ID naming

Type IDs must follow lower-case dot-separated format.

Recommended pattern:

- `<vendor>.<domain>.<family>.<element>[.<variant>]`

Examples:

- `s7.core.math.sum`
- `s7.dsp.filter.iir`
- `s7.dsp.fft.r2c`
- `acme.sdr.source.rtlsdr`

Type IDs are persistent identities and should not embed semantic version strings.

## 8.2 Parameter schema format

For v0, descriptor can carry parameter schema in JSON form with explicit schema version.

Runtime should treat unknown fields as forward-compatible extensions when safe.

## 9. Factory contract

Factory registration should create runtime element instances from runtime-provided construction context.

Construction context should include at least:

- element instance id,
- parameter payload,
- optional target profile hint,
- optional logger/diagnostics sink.

Factory must return either:

- valid element instance handle,
- or structured error code + message.

## 10. Plugin states in runtime catalog

Each plugin/element availability state should map to catalog statuses:

- `loaded`
- `disabled`
- `incompatible`
- `failed`
- `unsupported`

The control plane should expose these states to GUI as part of node capability discovery.

## 11. Error handling and diagnostics

## 11.1 Loader error classes

Suggested loader classes:

- `symbol_missing`
- `manifest_invalid`
- `api_version_mismatch`
- `registration_failed`
- `factory_duplicate_type`
- `dependency_unavailable`

## 11.2 Structured diagnostics payload

Diagnostics should include:

- plugin path,
- plugin id (if known),
- code,
- message,
- severity,
- timestamp,
- optional remediation hint.

## 12. Security considerations

- plugin loading is local node policy and should be controllable,
- unsigned/untrusted plugin policy is implementation-defined in v0,
- control plane may expose plugin status but should avoid leaking sensitive local paths unless authorized.

## 13. Worker-backed plugin representation

A plugin may export element descriptors whose execution is out-of-process worker-backed.

Descriptor should declare this explicitly via placement/hardware hints.

Runtime still presents those types in catalog; deploy validation decides availability per node.

## 14. Minimal loader pseudocode

```cpp
auto handle = dlopen(path, RTLD_NOW);
auto manifest_fn = (s7_plugin_get_manifest_fn)dlsym(handle, "s7_plugin_get_manifest");
auto register_fn = (s7_plugin_register_fn)dlsym(handle, "s7_plugin_register");
if (!manifest_fn || !register_fn) { mark_failed(path, "symbol_missing"); }

const auto* manifest = manifest_fn();
if (!manifest) { mark_failed(path, "manifest_invalid"); }
if (!is_compatible(manifest->plugin_api_version, runtime_api_version)) {
    mark_incompatible(path, manifest, "api_version_mismatch");
}

s7_plugin_register_context_t ctx = make_ctx(runtime_api_version, &registry_api);
auto rc = register_fn(&ctx);
if (rc != 0) { mark_failed(path, manifest, "registration_failed"); }
else { mark_loaded(path, manifest); }
```

## 15. Test checklist for plugin loader

- missing required symbol,
- malformed manifest,
- API version mismatch,
- duplicate type registration,
- registration returns error,
- successful registration appears in catalog,
- catalog change event emitted on load/rescan.

## 16. Suggested implementation order

1. define internal C header for v0 contract,
2. build runtime loader with diagnostics,
3. add registry API and one demo plugin,
4. expose plugin/element catalog via control plane,
5. add rescan command and change subscription,
6. add worker-backed descriptor flags.

## 17. Open items

- long-term ABI stabilization plan,
- binary compatibility policy across compilers/toolchains,
- signature/attestation requirements for plugin binaries,
- plugin unload safety guarantees,
- descriptor schema registry process.


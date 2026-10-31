# Element Development Guide

Simulation follows the [analog-computer model](SIMULATION_MODEL.md): dynamics
are explicit element networks. RK4 and other independent solvers belong in
test references, not a new runtime simulation engine.

How to write an S7 element, describe it with metadata, and register it. For packaging elements as a loadable shared library see `PLUGIN_INTERFACE.md`; for schema-backed elements see `SCHEMA_LIBRARY_WORKFLOW.md`.

## Element contract

Source: `s7core/include/s7/element.hpp`.

- An element **owns its output storage** as member variables. Ports are lightweight `Port{index, name, type, value_ptr}` records pointing at that storage. There is no separate output buffer in the base class.
- Ports are registered in the constructor with `register_input(index, name, type, nullptr)` and `register_output(index, name, type, &member)`. Port types are fixed per element instance; conversion between signal types is done by explicit converter elements, never implicitly (a type mismatch is a validation error).
- `Port::name` is a `std::string` owned by the port; `register_input/register_output` copy the name, so any string (including a local one) may be passed.
- `step()` reads inputs with `read_input<T>(index)` and assigns outputs directly. `read_input` performs no checks; bindings are validated once when the compiler calls `bind_inputs()`.
- Value storage: every `SignalType` is stored as its native C++ type, `signal_storage_t<Type>` (`s7/signal_types.hpp`): `double` for F64, `std::complex<double>` for C64, `std::int16_t` for I16 and I12 (restricted to the 12-bit range), `std::float128_t` for F128, `s7::ComplexInt<std::int16_t>` for CI16, and so on. The enumerator value of each `SignalType` is its wire type id. `signal_type_info()` gives kind, component width, storage size and name; `visit_signal_type()` turns a runtime type into a template argument; `IntegerRange<Type>` gives integer bounds; `s7/signal_arithmetic.hpp` provides integer and complex-integer arithmetic with an explicit `Overflow` rule. Read and write values with the port's exact storage type: `read_input<signal_storage_t<SignalType::I16>>(0)`. Type changes go through the `Convert` element (`s7/convert.hpp`).
- The `Element` constructor takes `has_state` (the element keeps internal state or performs side effects; such an element cannot be replayed as an implicit derivative operation) and `Feedthrough` (default `Feedthrough::Direct`). Elements with direct feedthrough, stateful or not, run in topological order and their outputs are visible downstream in the same step: `Constant → FirFilter([1]) → Gain` yields the constant at step 1.
- `Feedthrough::None` marks the element as causal memory (`Delay`, `PreviousValue`, `EulerIntegrator`, implicit `TrapezoidIntegrator`, `GaussLegendreIntegrator`, `SymplecticIntegrator`). Its `step()` writes the output for the *next* step from the current inputs. The runtime steps causal-memory elements after the others, keeps their published outputs unchanged until the step ends, and publishes the new outputs at the start of the next step. Cycles in a graph are valid only if they contain at least one causal-memory element; a stateful element with direct feedthrough (`Accumulator`, filters, nested `Schema`) does not break a cycle. Implicit and coupled methods use their dedicated numerical stages instead of an ordinary per-element `step()` call.

## Type system (decided and implemented 2026-10-05)

**Integrators.** `Accumulator` is a running sum without a time step (all arithmetic types). For integration over model time use `EulerIntegrator` (forward Euler / rectangle rule, y[n+1] = y[n] + dt·x[n]; causal memory, so it can close a feedback loop such as an ODE system without a `Delay`) or `TrapezoidIntegrator` (default `mode="implicit"`: coupled trapezoidal ODE solve, second order for the documented autonomous/held-input formulation, causal memory with no artificial feedback Delay). Its `mode="sampled"` performs quadrature of supplied input samples with direct feedthrough and still needs causal memory elsewhere in a loop. Both integrators take `dt` and `initial_value` and support F16–F128 and C16–C128. See [the trapezoidal numerical contract](TRAPEZOID_INTEGRATION.md) for iteration limits, stateless derivative requirements, held inputs and failure behavior. For a second-order system x'' = a(x), `SymplecticIntegrator` (semi-implicit Euler / Euler-Cromer: v[n+1] = v[n] + dt·a[n], x[n+1] = x[n] + dt·v[n+1]) bounds a conservative oscillator's energy instead of letting it drift the way applying `EulerIntegrator` independently to position and velocity does (`examples/2int_osc.s7diagram`, `docs/OPEN_PROBLEMS.md`). It takes one `acceleration` input and two outputs (`position`, `velocity`), plus `dt`/`initial_position`/`initial_velocity`, and supports the same floating types as the other integrators. The energy bound holds when acceleration depends only on position (and time); an acceleration that also depends on velocity (e.g. damping) loses the symplectic property, though the scheme is still a valid first-order method. Not an exact conservation law either way — see `OPEN_PROBLEMS.md` for the open comparison against other integrators on nonlinear systems.

`EulerIntegrator` also accepts optional `substeps` (default 1; 2–32 require
F64) for simultaneous forward-Euler substeps of a compiled coupled region.
`GaussLegendreIntegrator` is F64-only and solves two temporary fourth-order
implicit stages for all states in its region. Both keep the same outer `dt`,
first-state publication, and causal-memory contract; a per-element `step()`
cannot solve their coupled stages. The compiler validates the eligible pure
derivative cone and rejects cross-method feedback. See the
[coupled integration contract](COUPLED_INTEGRATION.md) for held inputs,
solver bounds, diagnostics, examples and performance evidence.
Registry-created Euler, trapezoid and Gauss elements adopt their resolved
execution-domain rate; a stored diagram `dt` is legacy metadata. C++ callers
may construct an integrator with an explicit `dt` or `std::nullopt` to follow
the rate.

**Division.** `Divider` computes `numerator / denominator` for real floating-point types. A zero denominator throws `std::domain_error` during execution so an invalid quotient cannot silently enter the graph.

**Math and limits.** `MathFunction` selects `sqrt`, `exp`, natural `log`, `pow`, `abs`, or `reciprocal` through its `function` parameter; `pow` uses the `exponent` parameter (default 2). `Power` computes `base` raised to a connected `exponent` input. These operate on real floating-point signals; sqrt of a negative value, log of a non-positive value, and reciprocal by zero are execution errors rather than NaN/infinite outputs. `Saturation` clamps real numeric signals to inclusive `lower_limit`/`upper_limit` values (defaults 0 and 1). `MinMax` takes 2–64 real numeric inputs and selects `min` or `max` with its `operation` parameter and `input_count`; a NaN input is ignored while another input is valid, propagating only if every input is NaN. `Sign` returns −1, 0, or 1; for unsigned inputs it returns 0 or 1. `Comparator` takes `lhs`/`rhs` real numeric inputs and an `operation` parameter (`lt`, `le`, `gt`, `ge`, `eq`, `ne`), outputting 1 or 0 in the ports' type; there is no boolean `SignalType`, so it follows `Sign`'s convention. Floating-point comparison follows IEEE 754 (NaN compares false except under `ne`).

**Writing a generic element.** Write the element as a template over `SignalType` (see `GainT` in `s7/elements.hpp`; `SignalOps<Type>` gives `add`/`multiply`/`one`/`zero` with the `Overflow` rule). In its JSON resource, declare a type variable and port rules (`type_variables` contains an entry for `T` with `allowed` types and `pin_parameter: "type"`; port `type_rule` is `T`, `component(T)` or `complex(T)`), and in the factory dispatch on the resolved type: `visit_signal_type(ctx.type(), [&]<SignalType T>() { ... })`. Convert parameters with `signal_value_from_json<T>()`: integers must be exact and in range; a floating-point value takes the nearest value of its type, and is rejected if that value is infinite (`1e40` for F32, `65520` for F16) or, for a nonzero value, zero (`1e-9` for F16). JSON integers convert exactly (F128 holds them up to 2^113); other JSON numbers have `double` precision, also for F128. Loaders create elements with `make_element_spec(id, descriptor, params)`, which turns the metadata into a `GenericElement` for the resolver.

This section is the contract; built-in elements and the registry already follow it.

- **Types.** Every port has exactly one type from the wire-format catalog (`S7_Network_Wire_Protocol_v1.md` section 8): I8–I128, U8–U128 (with I12/U12), F16, F32, F64, F128, complex floats C16–C128, and complex integers CI8, CU8, CI12, CI16, CI32, CI64, CI128 (CI12 since 2026-10-31: a node advertises the types it transports, see the wire protocol section 8). Each is stored as its native C++ type (`s7::float16_t`, `s7::float128_t`, `s7::int128_t`; complex integers as a pair of the integer type). Do math on signal values with `s7::math::sin`, `s7::math::ldexp`, `s7::math::pi_v`, ... rather than `std::`: with Clang, libstdc++ has no `<cmath>` overloads for F16/F128 (`s7/float_types.hpp`). Nothing converts implicitly.
- **Generic elements, static polymorphism.** An element is written once as a template (`GainT<T>`) and instantiated for each type it supports; the registry holds one entry (`Gain`) and selects the instance through a compile-time dispatch table. There is no per-sample virtual call or type switch.
- **Type rules instead of types.** An element declares rules such as "all ports have one type T" (`Gain`, `Adder`, `Multiply`, `Delay`), "output is the real type matching the complex input" (`ComplexToMagnitude`), or a fixed type (device sources, `NetSource` from its stream descriptor). It also declares the set of types it supports.
- **Type inference.** Before compilation, a unification pass gives every port a type: connections make types equal, fixed types and pinned `type` parameters seed them, and inference runs in both directions (a `Constant` feeding an I16 `Adder` becomes I16). A conflict, or a type outside an element's supported set, is a validation error naming the element. A connected part of the graph with no determining type defaults to F64. Studio shows the resolved type on each wire. Any element may pin its type with an explicit `type` parameter.
- **Explicit conversion.** (Implemented: `s7/convert.hpp`. `mode` is required; `rounding` defaults to `half_even` and `overflow` to `saturate`. Normalized codes: signed c/2^(b−1) ∈ [−1, 1), unsigned c/2^b ∈ [0, 1), CU8 offset binary (c−128)/128; integer→integer rescales by bit width; signed↔unsigned normalized is refused. CU8 converts in `normalized` mode only: an offset-binary code has no plain numeric value, so `value` mode to or from CU8 is refused. Both refusals are validation errors.) A type change is one `Convert` element: `from`, `to`, `mode` (`value`: keep the numeric value; `normalized`: fixed-point scaling, integer codes ↔ [-1, 1)), `rounding` (`half_even`, `toward_zero`) and `overflow` (`saturate`, `fault`), all stated explicitly. Widening conversions that are exact still go through `Convert`.
- **Integer overflow.** Integer arithmetic elements have an `overflow` parameter: `saturate` (default) or `wrap` (two's complement), so hardware behavior can be simulated exactly.
- **Accumulator type.** (Implemented on `Accumulator`, `FirFilter`, `Adder`, `Multiply`; a second type variable `A` with `defaults_to: "T"` in the metadata. Exact widening per `widens_exactly()`: signed→wider signed, unsigned→wider unsigned or strictly wider signed, integer→float with enough mantissa (I16→F32, I64→F128), float→wider float, complex component-wise.) Accumulating elements (FIR, Integrator, correlators, and `Adder`/`Multiply` when combining inputs) take an optional `accumulator_type`. By default it is the port type. When set, inputs are widened exactly inside the element, arithmetic runs in the accumulator type with that element's `overflow` rule, and the output port has the accumulator type (for example `FIR(accumulator_type: CI32)` on CI8 input outputs CI32). Narrowing back is always an explicit `Convert`.
- **Complex unsigned.** CU8 (RTL-SDR offset binary) is transport and normalized `Convert` only; it has no arithmetic. (c−128)/128 is a convention: the RTL2832U's measured zero is nearer 127.4 (gr-osmosdr), so remove that DC explicitly in the graph when it matters.
- **SDR elements publish samples at the converter's native width** ([ADR-023](../../doc/S7_Architectural_Decisions.md#adr-023)): RTL-SDR as CU8, HackRF as CI8, 12-bit devices (ADALM-Pluto/AD9361, LimeSDR, USRP sc12, Airspy) as CI12, 16-bit devices as CI16. Never publish 12-bit samples as CI16: low-aligned data (Pluto receive, libiio `le:S12/16>>0`) would read 1/16 of full scale in normalized mode, and the alignment would be invisible in the diagram. Pluto receive words are already CI12 values; for transmit (`le:S16/16>>0`, top 12 bits used), a sink takes CI12 and shifts left by 4, which is what normalized `Convert` CI12 → CI16 does.

## Minimal element

```cpp
#include "s7/element.hpp"

class LinearTransform final : public s7::Element {
public:
    LinearTransform(double scale, double offset)
        : s7::Element("LinearTransform"), scale_(scale), offset_(offset)
    {
        register_input(0, s7::port_names::in, s7::SignalType::F64, nullptr);
        register_output(0, s7::port_names::out, s7::SignalType::F64, &output_);
    }

    void step() override { output_ = read_input<double>(0) * scale_ + offset_; }

private:
    double scale_;
    double offset_;
    double output_{};
};
```

Generic elements that work on both real and complex values are written as templates (`ConstantT<T>`, `GainT<T>`, `AdderT<T>`, `MultiplyT<T>`, `DelayT<T>` in `elements.hpp`) with aliases such as `Gain` / `ComplexGain`.

Variable input count is expressed through parameters: `Adder` takes `weights` and `Multiply` takes `factors`, and the number of input ports equals the array length. Both require 2–64 inputs; omitting the array defaults to two unit weights/factors, while explicitly supplying an empty, single-entry or oversized array is an error. The same bounds apply to direct C++ construction. `ElementMetadata::variadic_inputs` documents this for tools; set its `count_parameter` to that array parameter, so tools and type inference know the ports without creating the element. A factory runs once per compilation (validation reuses the instances), so side effects in constructors happen once.

## Metadata

The in-memory API is `s7core/include/s7/element_metadata.hpp`. Author complete
metadata in a module-owned JSON resource, rather than C++ documentation strings.
Built-ins use `s7core/resources/element_metadata.json`; I/O, networking, noise,
spectral and Studio observers each own their resource. Schema-backed elements
already carry their descriptions and definitions in `.s7library` JSON.

The optional document-level `library` string identifies the owning module in
Studio Properties; an omitted value means provenance is unavailable. Schema
library registration supplies its library name automatically. Categories remain
browsing groups and are not library identities. Loaded plugin information also
identifies plugin ownership.

A complete fixed-type entry looks like this (one entry inside a versioned document):

```json
{
  "document_kind": "s7.element_metadata",
  "format_version": 1,
  "library": "my_plugin",
  "elements": [{
    "type_name": "LinearTransform",
    "display_name": "Linear Transform",
    "description": "Multiplies the current input by scale and adds offset. No state or delay.",
    "category": "basic", "version": "1.0.0", "tags": [],
    "stateless": true,
    "input_ports": [{"name":"in", "description":"Input signal", "type":"F64", "optional":false, "type_rule":""}],
    "output_ports": [{"name":"out", "description":"Transformed signal", "type":"F64", "optional":false, "type_rule":""}],
    "parameters": [
      {"name":"scale", "type":"real", "description":"Dimensionless multiplier", "default":1.0, "required":false, "enum_values":[]},
      {"name":"offset", "type":"real", "description":"Offset in output units", "default":0.0, "required":false, "enum_values":[]}
    ],
    "type_variables": [],
    "visual": {"shape":"box", "color":"#4CAF50", "max_width":100, "max_height":50, "icon_name":""},
    "variadic_inputs": {"enabled":false, "min_count":0, "max_count":0, "port_name_template":"", "signal_type":"F64", "type_rule":"", "count_parameter":""}
  }]
}
```

Parameter fields are `name`, `type`, `description`, `default`, `required`,
`enum_values`, with optional numeric `min`/`max`. A null default means no declared
default; explain any type-dependent fallback in the description. Required keys
must be supplied, even when `default` gives a suggested value. Document units,
array/object structure, conditional constraints, timing and small examples in
plain text. Supported type labels are `real`, `int`, `string`, `bool`, `enum`,
`type`, `array`, `object`, `value`. Factories still enforce typed signal values
and conditional/structural constraints; `object` documentation does not add a
new generic nested-object runtime validator.

Generic entries declare `type_variables`: each has `name`, `allowed` signal-type
names, `pin_parameter`, and `defaults_to` (empty when inferred independently).
Port `type_rule` can be empty, a variable name, `component(T)` or `complex(T)`.
The complete built-in resource contains examples of generic, variadic and
accumulator-type entries.

Embed the resource through the SDK's CMake helper:

```cmake
find_package(s7core REQUIRED)
# After add_library(my_plugin ...):
s7_embed_metadata(my_plugin resources/element_metadata.json my_metadata)
```

This generates `my_metadata.hpp` with `s7::resources::my_metadata`. The resource
is a configure dependency: editing JSON regenerates the header and rebuilds its
consumer. Installed applications/plugins require no source-tree path or separate
runtime metadata file. `ElementMetadataLibrary` checks the version, fields,
parameter/type/port declarations, defaults and duplicate type/parameter names;
missing requested entries fail with an explicit error. It never loads external
content. The existing C++ metadata API remains available to third-party plugins.

Parameters are checked against these declarations whenever an element is created through `make_element_spec` (`checked_params()`), and the factory receives them with optional non-null defaults filled in. Problems are validation errors naming the element and every bad parameter:

- an undeclared parameter name (so declare every parameter your factory reads);
- a missing `required` parameter;
- a value of the wrong kind: `int` (integral number), `real` (finite number; a literal beyond `double`'s range such as `1e400` is rejected), `bool`, `string`, `enum` (one of `enum_values`), `type` (a signal type name), `array`, or `value` (number, string or array; converted to the element's type by the factory with `signal_value_from_json<T>()`);
- a number outside `min` / `max`.

`checked_params()` does not generically validate nested object structure; the consuming factory owns those checks. A factory can rely on optional parameters being present when they have a non-null default. Unknown type labels are rejected by the JSON metadata loader.

Set optional `stateless: true` only when the factory's `Element::has_state()` is
false for every supported parameter configuration. Studio labels those elements
**stateless**. An omitted field leaves the element unmarked. Studio uses muted
family colors for known categories, with pale fills and dark matching text; for
other categories it softens `visual.color` when valid. Shape metadata remains a
hint; schema-backed diagram nodes have a double frame.

## Registration

```cpp
#include "s7/element_registry.hpp"
#include "s7/metadata_json.hpp"
#include "my_metadata.hpp"

static const s7::ElementMetadataLibrary metadata(s7::resources::my_metadata);
registry.register_element(s7::ElementDescriptor(
    "LinearTransform",
    [](const s7::ElementCreationContext& ctx) {
        const double scale = ctx.get_real("scale");
        const double offset = ctx.get_real("offset");
        return std::make_unique<LinearTransform>(scale, offset);
    },
    metadata.at("LinearTransform")));
```

`ElementCreationContext` offers `get_real`, `get_int`, `get_string`, and `has_param`; the `get_*` functions throw if the parameter is missing or has the wrong JSON type. `register_element` throws if the type name is already registered.

Built-ins are registered by `s7::register_builtin_elements()` (`s7core/src/builtin_element_registry.cpp`) and network elements by `s7::register_net_elements()` (`s7net`). Both are idempotent.

## Using elements in a schema

Runtime JSON schema:

```json
{
  "name": "my_schema",
  "elements": [
    { "id": "src", "type": "Constant", "params": { "value": 2.0 } },
    { "id": "lt",  "type": "LinearTransform", "params": { "scale": 2.5, "offset": -1.0 } }
  ],
  "connections": [
    { "from_element": "src", "from_port": 0, "to_element": "lt", "to_port": 0 }
  ]
}
```

In C++ code: `schema.add_element("lt", [] { return std::make_unique<LinearTransform>(2.5, -1.0); });`. The factory may be called several times (validation and compilation each instantiate elements), so constructors must not have external side effects.

## Testing

Each module keeps its smoke tests in `<module>/tests/` and registers them with `add_test` in its `CMakeLists.txt`. Tests use `assert`, so build tests without `NDEBUG`. Elements can be tested standalone by binding inputs directly:

```cpp
const double in = 5.0;
const std::array<s7::InputBinding, 1> bindings{s7::bind_input<s7::SignalType::F64>(in)};
element.bind_inputs(bindings);
element.step();
assert(s7::bound_value<s7::SignalType::F64>(element.outputs()[0]) == 11.5);
```

## Troubleshooting

- **Unknown element type**: the type name is case-sensitive; check that the plugin was found (the runtime prints plugin search paths and loaded plugins at startup).
- **Type conflict**: insert a `Convert` element (or `ReImToComplex` / `ComplexToReIm` between real and complex types).
- **Input port N is unconnected**: every input must have exactly one driver.
- **Cycle detected without causal memory**: put a causal-memory element (`Delay`, `PreviousValue`, `EulerIntegrator`, implicit `TrapezoidIntegrator`, `GaussLegendreIntegrator` or `SymplecticIntegrator`) in the loop; `Accumulator`, sampled `TrapezoidIntegrator` or a direct-feedthrough filter alone does not break it.

# Element Development Guide

How to write an S7 element, describe it with metadata, and register it. For packaging elements as a loadable shared library see `PLUGIN_INTERFACE.md`; for schema-backed elements see `SCHEMA_LIBRARY_WORKFLOW.md`.

## Element contract

Source: `s7core/include/s7/element.hpp`.

- An element **owns its output storage** as member variables. Ports are lightweight `Port{index, name, type, value_ptr}` records pointing at that storage. There is no separate output buffer in the base class.
- Ports are registered in the constructor with `register_input(index, name, type, nullptr)` and `register_output(index, name, type, &member)`. Port types are fixed per element instance; conversion between signal types is done by explicit converter elements, never implicitly (a type mismatch is a validation error).
- `Port::name` is a `std::string` owned by the port; `register_input/register_output` copy the name, so any string (including a local one) may be passed.
- `step()` reads inputs with `read_input<T>(index)` and assigns outputs directly. `read_input` performs no checks; bindings are validated once when the compiler calls `bind_inputs()`.
- Value storage: every `SignalType` is stored as its native C++ type, `signal_storage_t<Type>` (`s7/signal_types.hpp`): `double` for F64, `std::complex<double>` for C64, `std::int16_t` for I16 and I12 (restricted to the 12-bit range), `std::float128_t` for F128, `s7::ComplexInt<std::int16_t>` for CI16, and so on. The enumerator value of each `SignalType` is its wire type id. `signal_type_info()` gives kind, component width, storage size and name; `visit_signal_type()` turns a runtime type into a template argument; `IntegerRange<Type>` gives integer bounds; `s7/signal_arithmetic.hpp` provides integer and complex-integer arithmetic with an explicit `Overflow` rule. Read and write values with the port's exact storage type: `read_input<signal_storage_t<SignalType::I16>>(0)`. `DoubleToInt12` / `Int12ToDouble` use Q11 fixed point (code = x × 2048) until the general `Convert` element replaces them.
- The `Element` constructor takes `has_state` (informational: the element keeps internal state) and `Feedthrough` (default `Feedthrough::Direct`). Elements with direct feedthrough, stateful or not, run in topological order and their outputs are visible downstream in the same step: `Constant → FirFilter([1]) → Gain` yields the constant at step 1.
- `Feedthrough::None` marks the element as causal memory (`Delay`, `PreviousValue`). Its `step()` writes the output for the *next* step from the current inputs. The runtime steps causal-memory elements after the others, keeps their published outputs unchanged until the step ends, and publishes the new outputs at the start of the next step. Cycles in a graph are valid only if they contain at least one causal-memory element; a stateful element with direct feedthrough (`Integrator`, filters, nested `Schema`) does not break a cycle.

## Type system (decided 2026-10-05; implementation in progress)

**Writing a generic element.** Write the element as a template over `SignalType` (see `GainT` in `s7/elements.hpp`; `SignalOps<Type>` gives `add`/`multiply`/`one`/`zero` with the `Overflow` rule). In its registration, declare a type variable and port rules in the metadata (`type_variables = {{"T", allowed, "type"}}`, `port.type_rule = "T"`; rules may also be `component(T)` or `complex(T)`), and in the factory dispatch on the resolved type: `visit_signal_type(ctx.type(), [&]<SignalType T>() { ... })`. Convert parameters with `signal_value_from_json<T>()`. Loaders create elements with `make_element_spec(id, descriptor, params)`, which turns the metadata into a `GenericElement` for the resolver.

This section is the target contract; the code is being moved to it (`CODE_REVIEW_REQUIRED_FIXES.md`, signal types item).

- **Types.** Every port has exactly one type from the wire-format catalog (`S7_Network_Wire_Protocol_v1.md` section 8): I8–I128, U8–U128 (with I12/U12), F16, F32, F64, F128, complex floats C16–C128, and complex integers CI8, CU8, CI16, CI32, CI64, CI128. Each is stored as its native C++ type (C++26 on GCC: `std::float16_t`, `std::float128_t`, `__int128`; complex integers as a pair of the integer type). Nothing converts implicitly.
- **Generic elements, static polymorphism.** An element is written once as a template (`GainT<T>`) and instantiated for each type it supports; the registry holds one entry (`Gain`) and selects the instance through a compile-time dispatch table. There is no per-sample virtual call or type switch.
- **Type rules instead of types.** An element declares rules such as "all ports have one type T" (`Gain`, `Adder`, `Multiply`, `Delay`), "output is the real type matching the complex input" (`ComplexToMagnitude`), or a fixed type (device sources, `NetSource` from its stream descriptor). It also declares the set of types it supports.
- **Type inference.** Before compilation, a unification pass gives every port a type: connections make types equal, fixed types and pinned `type` parameters seed them, and inference runs in both directions (a `Constant` feeding an I16 `Adder` becomes I16). A conflict, or a type outside an element's supported set, is a validation error naming the element. A connected part of the graph with no determining type defaults to F64. Studio shows the resolved type on each wire. Any element may pin its type with an explicit `type` parameter.
- **Explicit conversion.** A type change is one `Convert` element: `from`, `to`, `mode` (`value`: keep the numeric value; `normalized`: fixed-point scaling, integer codes ↔ [-1, 1)), `rounding` (`half_even`, `toward_zero`) and `overflow` (`saturate`, `fault`), all stated explicitly. Widening conversions that are exact still go through `Convert`.
- **Integer overflow.** Integer arithmetic elements have an `overflow` parameter: `saturate` (default) or `wrap` (two's complement), so hardware behavior can be simulated exactly.
- **Accumulator type.** Accumulating elements (FIR, Integrator, correlators, and `Adder`/`Multiply` when combining inputs) take an optional `accumulator_type`. By default it is the port type. When set, inputs are widened exactly inside the element, arithmetic runs in the accumulator type with that element's `overflow` rule, and the output port has the accumulator type (for example `FIR(accumulator_type: CI32)` on CI8 input outputs CI32). Narrowing back is always an explicit `Convert`.
- **Complex unsigned.** CU8 (RTL-SDR offset binary) is transport and `Convert` only; it has no arithmetic.

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

Variable input count is expressed through parameters: `Adder` takes `weights` and `Multiply` takes `factors`, and the number of input ports equals the array length. `ElementMetadata::variadic_inputs` documents this for tools.

## Metadata

Source: `s7core/include/s7/element_metadata.hpp`.

```cpp
s7::ElementMetadata meta;
meta.type_name = "LinearTransform";
meta.display_name = "Linear Transform";
meta.description = "y = x * scale + offset";
meta.category = "basic";
meta.version = "1.0.0";
meta.input_ports.push_back({"in", "Input signal", s7::SignalType::F64, false});
meta.output_ports.push_back({"out", "Output signal", s7::SignalType::F64, false});
// ParameterSpec fields: name, type, description, default_value, min, max, enum_values, required
meta.parameters.push_back({"scale", "real", "Multiplication factor", 1.0, std::nullopt, std::nullopt, {}, false});
meta.parameters.push_back({"offset", "real", "Additive offset", 0.0, std::nullopt, std::nullopt, {}, false});
meta.visual = {"box", "#4CAF50", 100, 50, ""};  // shape, color, width, height, icon
```

Parameter `type` values: `real`, `int`, `string`, `bool`, `enum` (with `enum_values`), `array`.

The registry does **not** validate parameters or apply `default_value`; Studio only uses defaults to pre-fill its parameter dialog. Factories must handle missing parameters themselves (check `has_param` before `get_*`).

Conventional categories and colors used by Studio: `basic` #4CAF50, `filters` #2196F3, `io` #FF9800, `network` #9C27B0 / #5C6BC0, `conversion` #00BCD4, `simulation` #FFEB3B, `spectral` #9C27B0, `sources` #42A5F5, `custom` #795548. Shapes: `box` (processing), `circle` (sources).

## Registration

```cpp
#include "s7/element_registry.hpp"

registry.register_element(s7::ElementDescriptor(
    "LinearTransform",
    [](const s7::ElementCreationContext& ctx) {
        const double scale = ctx.has_param("scale") ? ctx.get_real("scale") : 1.0;
        const double offset = ctx.has_param("offset") ? ctx.get_real("offset") : 0.0;
        return std::make_unique<LinearTransform>(scale, offset);
    },
    meta));
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
- **Port type mismatch**: connect through an explicit converter (`ReImToComplex`, `ComplexToReIm`, `Int12ToDouble`, …).
- **Input port N is unconnected**: every input must have exactly one driver.
- **Cycle detected without causal memory**: put a causal-memory element (`Delay` or `PreviousValue`) in the loop; an `Integrator` or filter alone does not break it.

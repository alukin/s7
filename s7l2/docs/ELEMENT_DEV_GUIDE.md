# Element Development Guide

How to write an S7 element, describe it with metadata, and register it. For packaging elements as a loadable shared library see `PLUGIN_INTERFACE.md`; for schema-backed elements see `SCHEMA_LIBRARY_WORKFLOW.md`.

## Element contract

Source: `s7core/include/s7/element.hpp`.

- An element **owns its output storage** as member variables. Ports are lightweight `Port{index, name, type, value_ptr}` records pointing at that storage. There is no separate output buffer in the base class.
- Ports are registered in the constructor with `register_input(index, name, type, nullptr)` and `register_output(index, name, type, &member)`. Port types are fixed per element type; conversion between signal types is done by explicit converter elements, never implicitly (a type mismatch is a validation error).
- `Port::name` is a `std::string_view`. The string it refers to must outlive the element: use string literals, `port_names::*` constants, or strings owned by the element. Do not pass a local `std::string`.
- `step()` reads inputs with `read_input<T>(index)` and assigns outputs directly. `read_input` performs no checks; bindings are validated once when the compiler calls `bind_inputs()`.
- Value storage: `SignalType::COMPLEX` uses `std::complex<double>`; every other `SignalType` (including the integer types) is currently stored as `double`.
- `has_state = true` marks the element as causal memory. The runtime runs stateless elements first in topological order, then steps all stateful elements against a snapshot of stateful outputs and publishes their new outputs together (snapshot-then-commit). A stateful element's output therefore becomes visible to downstream elements one step later. Cycles in a graph are valid only if they contain at least one stateful element.

## Minimal element

```cpp
#include "s7/element.hpp"

class LinearTransform final : public s7::Element {
public:
    LinearTransform(double scale, double offset)
        : s7::Element("LinearTransform"), scale_(scale), offset_(offset)
    {
        register_input(0, s7::port_names::in, s7::SignalType::DOUBLE, nullptr);
        register_output(0, s7::port_names::out, s7::SignalType::DOUBLE, &output_);
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
meta.input_ports.push_back({"in", "Input signal", s7::SignalType::DOUBLE, false});
meta.output_ports.push_back({"out", "Output signal", s7::SignalType::DOUBLE, false});
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
const std::array<s7::InputBinding, 1> bindings{s7::bind_input<s7::SignalType::DOUBLE>(in)};
element.bind_inputs(bindings);
element.step();
assert(s7::bound_value<s7::SignalType::DOUBLE>(element.outputs()[0]) == 11.5);
```

## Troubleshooting

- **Unknown element type**: the type name is case-sensitive; check that the plugin was found (the runtime prints plugin search paths and loaded plugins at startup).
- **Port type mismatch**: connect through an explicit converter (`ReImToComplex`, `ComplexToReIm`, `Int12ToDouble`, …).
- **Input port N is unconnected**: every input must have exactly one driver.
- **Cycle detected without causal memory**: put a stateful element (e.g. `Delay`) in the loop.

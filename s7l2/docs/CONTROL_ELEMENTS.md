# Local control elements and reusable schemas

These blocks follow the [analog-computer simulation model](SIMULATION_MODEL.md).
They have explicit local transitions; none introduces a global solver, adaptive
step size or scheduler event. Headers are `s7/control_elements.hpp` and
`s7/elements.hpp`; the F64 aliases match registry type names below.

All control/data ports on a primitive share its inferred `type`. Numeric zero
means false and nonzero means true; there are no implicit type conversions.
`Switch` supports real integer and floating types. The other registered
primitives support F16, F32, F64 and F128. Nonfinite control/measurement values
are errors. Unused branches of a switch are not evaluated by the switch, but
both upstream branches still execute as normal graph elements.

## Switch

Inputs 0/1/2: `off`, `on`, `control`; output 0: `out`. Zero control selects
`off`, otherwise `on` (including negative controls). It is stateless and has
direct feedthrough. It does not break a feedback cycle.

## HysteresisRelay

Input: `in`; output: `out`. Parameters:

| Parameter | Default | Meaning |
|---|---|---|
| `lower_threshold` | -1 | Turn off at or below this input |
| `upper_threshold` | 1 | Turn on at or above this input |
| `low_value` | 0 | Off output |
| `high_value` | 1 | On output |
| `initial_on` | false | Initial state when input starts inside the band |

The lower threshold must be strictly below the upper. Inside the band the
relay retains its state. Threshold equality triggers the corresponding
transition. It is stateful **with direct feedthrough**: retaining state does
not make it causal memory. A thermal feedback loop closes through its plant's
integrator, not through the relay alone.

## ControlledIntegrator

Inputs 0–3: `derivative`, `enable`, `reset`, `reset_value`. Output 0 is the
integrated state. Every input must be wired, including unused controls; use
numeric constants 1 for enable and 0 for reset/reset_value in a plain integrator.

Parameters are `dt=0.001`, `initial_value=0`, optional `lower_limit` and
`upper_limit`, and `reset_mode="rising"` or `"level"`. Omitted limits are the
lowest/highest finite values in the chosen type. Initial state must be within
ordered limits; `dt` must be finite, positive and representable by S7's rate model.

For published state y[n]:

```
if reset triggers: next = clamp(reset_value[n], lower_limit, upper_limit)
else if enable[n] != 0: next = clamp(y[n] + dt*derivative[n], lower_limit, upper_limit)
else: next = y[n]
```

Reset overrides hold. Rising reset triggers on a zero-to-nonzero transition;
the previous reset state is initially false, so an initially asserted reset
triggers on the first step. Level reset triggers on every asserted sample.
Reset-edge history is updated even while disabled. Derivative is ignored when
held or reset; reset_value is ignored when reset does not trigger.

This is **causal memory**. The initial value is published first, and all
updates, including resets, appear on the following step. A reset is not a
same-step mutation visible to other elements. Limits bound the stored state
itself, so an outward derivative at a limit does not build hidden windup.
Overflow/nonfinite state calculations are errors rather than an implicit
change of numerical method.

## PhaseWrap

Input/output: `in`/`out`. Parameters `period` and `lower` default to native-type
2*pi and -pi. Output lies in `[lower, lower+period)`; the upper boundary maps
to the lower boundary. Use `period=1, lower=0` for cycles. Period must be
positive and the endpoints must form a finite, representable interval.
This is stateless with direct feedthrough.

## FrequencyMeter

Input 0: `in`; outputs 0/1: `frequency_hz`, `valid`. Parameter
`sample_rate_hz=1000` pins the model rate. A rising crossing is detected when
the previous sample is negative and the current sample is nonnegative. Its
position is linearly interpolated between the two samples. Frequency is
sample rate divided by the interval between successive rising crossings.

Frequency and valid start at zero. The second crossing publishes the first
estimate and valid=1. The last estimate and validity are held until another
crossing; valid means an estimate has been obtained, not that a signal is
still present. There is no timeout, PLL or noise suppression in this block.
Use filtering/hysteresis when required (a relay should output -1/+1, not 0/1)
and choose a rate that resolves crossings.
Interpolation is local measurement arithmetic, not event-driven execution.
The element has state and direct feedthrough.

## Separate schema-elements resource

The editable source of the bundled components is
[`s7core/resources/schema_elements.s7library`](../s7core/resources/schema_elements.s7library).
It is a normal `s7.element_library` document, not special C++ implementations
of controllers or plants. CMake embeds the resource in `s7core`, so it is
available headlessly and in installed applications without a source-tree path.
The original file is also installed under `share/s7/` in the SDK.
`register_builtin_elements()` registers the components after their primitives.

All bundled templates currently expose F64 ports and default to 1 kHz
(`dt=0.001`). Each instance is an independent `Schema` containing its own internal
element instances and their states. Registry templates remain unchanged when an instance
is edited. Generic arithmetic primitives remain available for other signal types.

| Schema element | Inputs | Outputs | Internal graph |
|---|---|---|---|
| `PIController` | error, kp, ki | control, unlimited | Parallel P/I, output saturation [-1,1], back-calculation anti-windup |
| `PIDController` | reference, measurement, kp, ki, kd, tau | control, unlimited | Parallel P/I, filtered derivative on measurement, saturation/anti-windup |
| `FirstOrderLag` | input, tau | output | `tau*y' = input-y`, initial y=0 |
| `RMSDetector` | input, tau | rms | `tau*q' = input^2-q`, output sqrt(q), initial q=0 |
| `PhaseDifference` | phase_a, phase_b | difference | Adder followed by PhaseWrap, radians in [-pi,pi) |

Controller equations with normalized control u:

```
PI:  raw = kp*error + integral
PID: error = reference - measurement
     filter_rate = (measurement - filter_state)/tau
     raw = kp*error + integral - kd*filter_rate
u = clamp(raw, -1, 1)
integral' = ki*error + 10*(u-raw)
PID filter_state' = filter_rate
```

All state updates use ordinary forward-rectangle causal integrators. Gains are
signal inputs: connect constants for fixed tuning, or other signals for explicit
gain scheduling. Initial integral/filter states are zero. Use positive tau;
`tau >= dt` is a useful starting constraint for the lag/RMS filter, and numerical
validation is still required for a chosen closed loop. Controller limits and
the 10/s tracking coefficient are editable internal block parameters. The RMS
detector measures an exponentially filtered mean square, not a finite-window RMS.

Schemas retain the existing direct-feedthrough contract; an internal integrator
is not automatically an outer feedback boundary. Use these controllers with
explicit causal plant states. Purely internal memory does not make every
schema safe to close an otherwise direct outer cycle.

## Studio: a category and editable tabs

The picker has a separate **Schema elements** category for both bundled and
user-loaded schema libraries, regardless of the library's original category.

Double-click a schema-based node to open its embedded graph in a new tab.
Double-click nested schemas to navigate deeper. Reopening the same instance
selects its existing tab; different instances have independent tabs and copies.
Primitive nodes still open their parameter dialog. The element picker, wiring,
page/zoom tools and undo/redo operate on the active tab.

Edits propagate as undoable embedded-schema changes into the parent, through
any nesting levels, to the root diagram. They never overwrite the bundled
resource or a library file. Saving/reloading the root preserves nested graph
edits, layouts, descriptions and exposed input/output bindings. The exposed
bindings remain the schema interface; external input wires are supplied by the
parent and are not drawn as internal source elements.

Closing a parent tab closes descendant tabs; closing the root document clears
all schema tabs. Root-level undo/replacement is reflected when a child tab is
activated again. Deleted parent instances cannot receive further tab edits.
**Save and Run apply to the root diagram** with the edited embedded schemas;
observation viewers are opened from the root. **Save Schema Library** from a
child tab exports that schema, including its exposed interface, under a new
library type name.

Run configuration updates embedded schemas recursively. Nested explicit rates
participate in parent rate validation, and inconsistent internal declared rates
are rejected when a nested graph is compiled. For headless preparation, use
`s7::configure_schema_discretization(json, dt)` before
`SchemaLibraryLoader::build_schema(json)`. It materializes registered schema
resources, updates rate/dt fields recursively, and leaves registry definitions
unchanged. It does not change update equations or execution order.

## Verification

```bash
cmake --build build --target s7core_control_elements_smoke s7core_schema_elements_smoke s7studio_schema_elements_test
ctest --test-dir build -R '^(s7core_control_elements_smoke|s7core_schema_elements_smoke|s7studio_schema_elements)$' --output-on-failure
```

Primitive tests cover all four floating types, integer switching, thresholds,
hold/reset/limit transitions, phase boundaries, frequency acquisition and causal
publication/feedback. Resource tests compare graphs with independent discrete
recurrences at 1 and 10 kHz and check saturation, instance independence, nested
rate conflicts and cycle rules. Studio checks the category, double-click tab
navigation, nested edits, undo/redo, registry/instance isolation, save/reload and
execution of the edited graph.

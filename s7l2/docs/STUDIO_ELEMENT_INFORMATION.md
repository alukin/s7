# Studio element information and parameter coverage

ST-007 audit, 2026-10-25. Complete metadata is authored in module-owned JSON
resources (`resources/element_metadata.json`, or Studio
`resources/observer_metadata.json`), embedded by CMake and decoded by
ElementMetadataLibrary. Factories reference entries by type identifier;
validation and documentation use the same declarations. See
[ELEMENT_DEV_GUIDE.md](ELEMENT_DEV_GUIDE.md#metadata) for the format and SDK helper.
Information is rendered from ElementMetadata,
ParameterSpec and ViewerRegistry, at the same source as Properties. Supplied
descriptions remain plain text, including markup-like strings. The library's
short summary remains available; right-click or the context-menu key opens full
information. Edit → Element Information… uses the active canvas selection,
including nested schemas and observers. Schema/observer double-click keeps its
existing behavior.

Properties splits documentation and parameter editing vertically. Both areas
scroll independently; the splitter is adjustable and dialog buttons stay outside
it. Information is resizable, read-only and selectable, with no external content
loading. Missing documentation is labelled explicitly.

## Runtime parameter inventory

The following keys were checked against factories in
`s7core/src/builtin_element_registry.cpp` and declarations in
`s7core/resources/element_metadata.json`, signal parsing in `signal_values.hpp`,
element constructors/steps in `elements.hpp`, `sources.hpp` and
`control_elements.hpp`, and the owning module registrations below. Every generic
built-in has the optional `type` pin in addition to the listed keys, except
Convert (which has independent `from` and `to` pins). Information lists the
allowed types and inference/default relationships from TypeVariableSpec.

`A` below means the optional `accumulator_type` pin; it defaults to input type
and must represent every input value exactly. Arithmetic overflow settings only
affect integer operations. Bounds/choices/defaults come directly from metadata;
descriptions explain further constructor and conditional constraints. Required
keys must be supplied even when metadata provides a suggested value.

| Type | Additional accepted keys | Semantic/example review |
|---|---|---|
| Constant | value | Fixed source; exact integers and complex pairs |
| Gain | factor, overflow | Current input times factor |
| Divider | none | numerator / denominator signal inputs |
| MathFunction | function, exponent | exponent only used for pow |
| Power | none | base and exponent are signals |
| Saturation | lower_limit, upper_limit | Ordered inclusive limits |
| MinMax | operation, input_count | 2..64 inputs, min/max |
| Sign | none | Numeric sign |
| Comparator | operation | lhs/rhs, six comparisons |
| Adder | weights, overflow, A | 2..64 typed weights; [1,-1] subtraction |
| Multiply | factors, overflow, A | 2..64 typed per-input factors |
| Delay, PreviousValue | initial_value | Previous-step output, causal memory |
| Accumulator | initial_value, overflow, A | Same-step running sum; no dt |
| EulerIntegrator | dt, initial_value | Forward rectangle; staged next state |
| TrapezoidIntegrator | dt, initial_value, mode | Implicit versus sampled timing and feedback restrictions |
| SymplecticIntegrator | dt, initial_position, initial_velocity | Velocity first; position-only conservative bound has timestep limits |
| Trig | function | Radian angle functions, real floating types |
| ChirpGenerator | start_hz, end_hz, sweep_samples, sample_rate_hz, amplitude, offset, phase, sweep, end_behavior | Positive logarithmic endpoints; phase in radians |
| PRBSGenerator | order, seed, bit_samples, sample_rate_hz, low, high | Orders 7/15; seed fits selected order |
| TriangleGenerator | sample_rate_hz, amplitude, offset, frequency_hz, phase, rise_fraction | Phase in cycles; rise endpoints produce sawtooths |
| PulseGenerator | sample_rate_hz, low, high, frequency_hz, duty, phase | Phase in cycles; duty in [0,1] |
| ImpulseGenerator | sample_rate_hz, start_sample, area | One-sample height = area * sample rate |
| StepGenerator | sample_rate_hz, start_sample, initial_value, final_value | Zero-based transition sample |
| ControlledOscillator | sample_rate_hz, amplitude, offset, phase | Frequency is an input signal; phase in radians |
| Switch | none | off/on/control are signals |
| HysteresisRelay | lower_threshold, upper_threshold, low_value, high_value, initial_on | Ordered thresholds, retained state between them |
| ControlledIntegrator | dt, initial_value, lower_limit, upper_limit, reset_mode | Reset priority, enable hold, bounded state, next-step publication |
| PhaseWrap | period, lower | Omitted: typed 2*pi and -pi; half-open interval |
| FrequencyMeter | sample_rate_hz | Rising crossings; invalid until two crossings |
| PWM | period_samples, sample_rate_hz | Duty input latched at carrier boundary |
| AnalogOscillator | amplitude, frequency_hz, sample_rate_hz, phase | Sine source; phase in radians |
| FirFilter | taps, overflow, A | Nonempty typed taps; current sample first; zero history |
| IirFilter | b0, b1, a1 | Subtract a1 feedback; zero previous input/output |
| ReImToComplex, ComplexToReIm, ComplexToMagnitudePhase | none | Component type relationships; phase in radians |
| Convert | from, to, mode, rounding, overflow | mode required; explicit conversion, no implicit wire conversion |

Schema library entries PhaseDifference, FirstOrderLag, RMSDetector, PIController
and PIDController have **no wrapper parameter keys**. The source is
`s7core/resources/schema_elements.s7library` and `schema_library.cpp`.
Descriptions identify exposed signals, units, initial states and timing;
information also lists internal element settings and schema dt. Controller gains
and lag/filter time constants are signal inputs. Internal parameters belong to
the nested elements and are documented there. Existing linear, pendulum and
signal-measurement examples were used to check these conventions; no numerical
research or new method is part of this increment.

| Owner/type | Accepted keys | Semantic/example review |
|---|---|---|
| s7io RecordedSource | type, file_path, sample_rate_hz, format, end_behavior | Zero rate uses file header; positive rate checks it; no resampling |
| s7io RecordingSink | type, file_path, sample_rate_hz, format, description | Node-local file; F64/C64; positive rate; 1024-byte description |
| s7net NetSink, NetSource | type, stream_id, max_elements_per_packet, max_batch_latency_ms, send_log_max_packets, max_block_ms, max_wait_ms, producer_node, consumer_nodes, max_latency_ms, sample_rate, timestamps | Stream UUID, millisecond waits, UUID arrays, positive integer rational rate, distinct timestamp-role strings; direction-specific settings labelled |
| s7noise WhiteNoiseGenerator, PinkNoiseGenerator | amplitude, seed | Expected standard deviation; seed 0 random; no peak or portable bitwise guarantee |
| s7spectral WindowedFFT | fft_size, hop_size, sample_rate_hz, window_type | Power of two, hop <= FFT size; initial/held results, single-precision FFT and amplitude limitations |

Plugin review used `s7noise/src/plugin_noise.cpp`, `noise_elements.cpp`,
`s7spectral/src/plugin_spectral.cpp`, `fft_element.cpp`, and existing plugin SDK
smoke examples. Spectral stays an out-of-tree SDK plugin. Third-party text is
preserved rather than replaced by this inventory.

## Observer parameter inventory

ViewerRegistry loads observer documentation from
`s7studio/resources/observer_metadata.json`. Actual consumers are
`diagram/element_ports.cpp`, `MainWindow::currentRunProbes()`,
`MainWindow::applyTriggerPreset()` and `observation/live_trigger.cpp`.

| Type | Accepted presentation keys | Semantic/example review |
|---|---|---|
| Scope | channel_count, channel_names, channel_units | Dynamic inputs; names and unit-label string arrays in input order |
| LiveOscilloscope | channel_count, channel_names, channel_units, trigger | At most eight connected channels; bounded acquisition; trigger preset applies before first run sample |
| PhasePortrait, PhaseTrajectory3D | channel_units | Fixed x/y[/z] roles; same-step axis values; names are not configurable |

`trigger` is an optional object. Documented nested keys: mode, channel, edge,
level, hysteresis, pretrigger_percent, holdoff_steps, auto_timeout_steps. The
default object is generated by the existing serializer, and prose gives accepted
types, ranges, raw measurement units, mode interactions and invalid-preset
handling. Other live-viewer controls are not diagram JSON keys. Trigger edits
apply immediately; ordinary binding edits apply to the next run. Existing
`2int_osc_live.s7diagram` and signal-measurement examples supply observer cases.

## Verification

GUI workflow cases exercise metadata coverage and semantic distinctions,
right-click targeting a row different from selection, keyboard context access,
categories/blank space, canvas information for ordinary/schema/observer items,
and long markup-like plugin text in both dialogs. Small-dialog checks verify
scrolling, copying and unchanged document contents on close/cancel. Existing
parameter-editing and schema/viewer regressions cover the preserved gestures.
Execution results and platform limitations are recorded in RELEASE_0.1.1.md.

Completed verification: 82/82 native Release CTest entries (42 GUI workflow
cases, no skips), five fresh headless smoke tests, Windows Studio cross-build
and two Wine smoke tests. Information and long-text workflows additionally pass
at 150%/200% with a 1440×900 logical Xvfb workspace. The complete run also covers
the selected-nested-editor shutdown fix exposed by information access. These
results retain the native Windows/physical-display/package qualification limits
in the release plan.

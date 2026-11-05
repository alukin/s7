# S7 0.1.2 preview

S7 0.1.2 is a maintenance release of the 0.1 preview line. It fixes defects
found in Studio's plots, observation windows and diagram editor since 0.1.1,
checks every bundled example in Studio, and includes the runtime, signal-type
and cross-language work merged into `main` since 0.1.1. Some of that work was
originally planned for 0.2.0. Studio still runs a schema with its bundled causal
graph runtime; no separate service is needed for local runs.

## Studio fixes

- **Plot axes.** Scope, Phase Portrait, Spectrum and the other ordinary plots
  label their axes in steps of 1, 2, 2.5 or 5 times a power of ten, with the
  same number of decimals on every label of an axis. Previously Qt Graphs chose
  steps such as 0.3 for a ±1 signal, and rounding residue at zero could print
  as `2.2045e-16` over the axis title.
- **Live Oscilloscope Auto Y** chooses a round scale (1, 2, 2.5 or 5 times a
  power of ten per division) with the divisions on round values, instead of
  stretching the data extremes over eight divisions. Units/div readouts and
  channel axis labels show values such as `0.5` rather than `0.27085`.
- **XY Trajectory and Constellation / XY dots** are drawn as small dots in the
  series color. Qt Graphs' default 16-pixel bordered squares merged into a
  thick black band that hid the trajectory. At most 16,384 dots are drawn, as a
  uniform subset that keeps the newest pair, and the status says so; hovering
  still inspects every raw pair. Newest-pair, peak-lag and inspected-pair
  markers are round 9-pixel markers.
- **Phase Portrait** marks the newest pair, as the 3D trajectory does. A
  trajectory resting at an equilibrium was previously a blank plot.
- **Triggered Live Oscilloscope time axis** spans exactly the pre-trigger
  percentage of the window, so its labels are round (−5 … 15 s at 25 % and
  2 s/div rather than −4.99634 … 15.0037 s), and the trigger line sits exactly
  under the pre-trigger handle.
- **Live Oscilloscope time base** starts at the smallest 1–2–5 scale that
  shows a configured run shorter than 20 s whole (0.2 s/div for the 2-second
  AM/FM examples) instead of squeezing it into the right edge of a 2 s/div
  screen. Longer and continuous runs keep 2 s/div. **Horizontal → Fit run**
  shows the whole run at any length, and a time base the user sets is kept for
  later runs.
- **Live Oscilloscope Spectrum** starts with fitted peaks, like the Scope's
  Spectrum tab; **Full frequency band** remains one click away.
- The translucent value box beside the pointer is back in Scope and Live
  Oscilloscope. Plot zoom uses Ctrl++, Ctrl+=, Ctrl+Shift++ and Ctrl+-, and
  Ctrl+] / Ctrl+[ zoom the time axis only.
- The main window no longer blinks when a run starts.
- Diagram editor: elements can be resized on the grid, with undo; text-only
  bodies, schema-backed elements and stateless elements are distinguished.
  Moving or resizing elements no longer freezes Studio, leaves wires behind
  their ports, or loses wires on undo. Sizes survive save/reopen and port-count
  edits; elements can shrink to two grid steps on each side when their ports
  fit. Wire drags reject new loops and tight U-turns, and route repair after
  an edit is limited to affected wires.
- Probes can be renamed from a probed wire's context menu; empty and duplicate
  probe names are refused with a message.
- Toolbars draw flat in every Qt style, and icon-only toolbar buttons have
  accessible names without an ellipsis.
- Large retained Scope comparisons load a bounded page per UI turn instead
  of projecting the whole recording in one call. Explicit bulk export remains
  synchronous.
- Studio windows share one process-wide plugin loader, so opening another
  window no longer incorrectly reports an already loaded plugin, such as
  `s7noise`, as unavailable.

## Examples

Every bundled diagram is now opened, run with the duration its article
documents at the diagram's own rate, and inspected in every observer window
and analysis tab by an automated Studio test (`bundledExamplesRunAndRender`).
The test fails on a run that does not start or complete, a message box, or a
plot whose axes hide or squeeze its data. Three Live Oscilloscope
demonstrations (`2int_osc_live2`, `symplectic_osc_live`, `demo_diagram2`) and
the spectrum reference family are now described in the
[examples guide](STUDIO_EXAMPLES.md).

Four editable Gauss–Legendre diagrams demonstrate the production integration
method: `2int_osc_gauss`, `linear/rc_charge_gauss`,
`linear/rc_discharge_gauss` and `linear/rlc_step_gauss`.

The new [screenshot gallery](../../screenshots/README.md) shows five actual
Studio captures from the Lorenz attractor, PID-controlled buck converter and
coherent AM detector, including recorded Scope and Live Oscilloscope Time and
Spectrum views. Each capture has a description and reproduction settings;
both repository READMEs link to the page.

## Runtime changes included

The following changes are part of 0.1.2 and affect validation, execution or
the C++ API:

- **Rate domains** ([ADR-022](../../doc/S7_Architectural_Decisions.md)).
  Integrators, generators, oscillators, PWM and the frequency meter follow the
  resolved domain rate; a stored `dt` or `sample_rate_hz` in a document is a
  legacy field. Source/event durations are seconds, and a duration that is not
  a whole number of samples at the adopted rate is a validation error. Older
  sample-count fields are converted using their saved rate. Studio no longer
  rewrites element parameters when the run rate changes and hides legacy
  fields from the ordinary property editor; Advanced JSON retains them.
- **Exact model time and pacing.** Deployed graphs and streams derive their
  rate from the diagram's `dt` as an exact rational, including rates such as
  44.1 kHz. Real-time deadlines are rounded from absolute step time, avoiding
  drift from a repeatedly rounded nanosecond period. Model time is checked
  against all resolved rates, including inferred and seeded rates.
- **Filter design rates and resampling.** FIR and IIR filters can record
  `design_rate_hz` and refuse another rate rather than silently change their
  response. Studio records missing filter design rates when opening a diagram
  and when adding a filter. The **Resampler**, available in the Conversion
  palette, explicitly joins rate domains: order 0 holds the latest value;
  orders 1 and 2 use delayed causal interpolation. Downsampling includes an
  anti-aliasing low-pass filter with documented latency. Equal rates pass
  through. S7 never inserts a resampler implicitly, and feedback across rate
  domains is rejected.
- **Nested schemas** are flattened for validation and compilation, including
  schemas with their own rate
  ([ADR-024](../../doc/S7_Architectural_Decisions.md)). Feedback can close
  through internal causal memory without an extra outer delay; direct-path
  cycles remain errors. All signal types can cross compiled schema ports,
  internal errors and duplicate IDs are reported by path, and probes and
  exposed-output addresses keep the nested instance's identity. Documents
  remain nested, and flattening stays within each node's compilation. A
  standalone `Schema` stepped outside a compiled graph still has its own
  runtime, requires bound inputs and supports only F64/C64 exposed ports.
- **Coupled numerical integration** supports opt-in simultaneous Euler
  substeps (`substeps` from 1 to 32) and two-stage, fourth-order
  `GaussLegendreIntegrator` for eligible F64 integrator networks. The compiler
  separates independent regions and Gauss strongly connected blocks, and
  evaluates only the derivative operations needed by each trial. Gauss solves
  use scaled residuals, pivoting and damped Newton iteration; nonfinite or
  nonconvergent trials fail explicitly and publish no rejected endpoint.
  Diagnostics report evaluation counts, residuals and numerical scratch
  estimates. Inputs are held across an outer step, trial stages are not
  observable, and a Gauss block is limited to 16 state components. The
  existing ordinary Euler, trapezoid and Euler–Cromer methods retain their
  contracts. See [methods, supported graphs and limits](COUPLED_INTEGRATION.md).
- **Fault publication** now restores published outputs and preserves the
  previously accepted staged causal-memory values when any execution phase
  fails ([ADR-021](../../doc/S7_Architectural_Decisions.md)). This includes
  elements inside flattened schemas. It does **not** undo private element
  state, such as a filter's history, or external side effects. In multi-rate
  execution the guarantee applies per domain step; domains that already
  completed remain advanced after a later domain faults.
- **Parameter and validation fixes.** Floating-point signal parameters that
  would overflow to infinity or underflow from nonzero to zero are rejected;
  subnormals and ordinary rounding remain valid, and JSON integer parameters
  avoid an intermediate double conversion. `real` parameters must be finite.
  F16/F32 normalized conversion scales and range-checks in a wider type before
  narrowing; integer bounds and NaN checks avoid undefined casts. `PhaseWrap`
  respects arbitrary lower bounds, and F16 oscillators retain their rate in
  double precision. Recursive schema-library definitions are rejected,
  excessive variadic port counts are bounded before type inference allocates,
  and graph walks no longer recurse down long chains.
- **Serial execution and FIR history.** The runtime dispatches through
  compiled element pointers rather than repeating element-ID lookups. Nested
  compilation removes per-instance execution/copying overhead, and scalar FIR
  history uses a ring buffer while preserving product and summation order.
  The element catalog refreshes when the registry changes, including later
  plugin/library registration and removal.
- **Complex frame convolution.** `s7::FirConvolution` and
  `s7::FftConvolution` process fixed-size C64 frames, either independently or
  as a continuous stream. They preallocate work storage; FFT convolution uses
  overlap-save. These are reusable C++ processors, not registered graph
  elements or new frame-valued ports. See [the API and checks](CONVOLUTION.md).

## Signal types and transport

- **CI12** adds complex signed 12-bit I/Q samples (wire ID `0x0207`), packed
  as three bytes per pair. Arithmetic and conversion use the actual 12-bit
  range. The [element developer guide](ELEMENT_DEV_GUIDE.md) documents native
  sample widths for SDR elements; this does not add a hardware driver.
- **Conversion rules.** CU8 offset-binary samples use normalized conversion
  with `(code - 128) / 128`; value-mode conversions to or from another type
  are refused. Normalized signed/unsigned integer conversions are also
  refused at element creation. A GCC optimization workaround preserves the
  sign when narrowing normalized signed integers.
- **Deployment capabilities.** `NODE_INFO.data_protocol.signal_types`
  advertises transported types, and deployment refuses a stream when an end
  does not support its type. A node without the list has the original v1
  catalog, which excludes CI12. Shared wire fixtures include CI12.
- **Connection fixes.** Control and data sockets use TCP_NODELAY. TLS servers
  accept session resumption from verified peers instead of failing resumed
  client-certificate sessions; control authorization remains certificate-based.

## Python and Java implementations

These are separate source implementations in the repository, with their own
documented F64 element subsets and limits; they are not full equivalents of
the C++ runtime.

- **[s7py](../../s7py/README.md)** adds `StepGenerator`, PWM, and stateless
  math/control elements, including phase wrapping and hysteresis. It now
  carries F64 streams between nodes through generated `NetSource`/`NetSink`
  boundaries and `/data` connections over mTLS. Its wire codec handles every
  v1 signal type, including CI12; delivery adds batching, backpressure,
  cumulative acknowledgements, reconnect/resume/replay, timestamp profiles,
  latency statistics, persistent sequence-range reservations, stream status
  and `RECOVER_STREAM`. Stepping runs on a worker so a blocked stream does
  not stop control requests. Separate data listeners and `trusted_plaintext`
  data are unsupported.
- **[s7java](../../s7java/README.md)** is a new Java 21+ data node using only
  the standard library. It implements the v1 wire codec, lossless stream
  protocol, mTLS `/data` connections and a small F64 causal graph runtime.
  It takes its node's part from a diagram file and writes stream status to a
  file; it has no control protocol or remote deployment endpoint. Peer identity
  comes from the certificate's node SAN, and unsupported diagrams, plaintext
  data endpoints and type declarations are refused. Sequence reservations
  survive restart, failed reservations are retried before sending, and wire
  positions use unsigned comparisons. The listener defaults to loopback and
  supports `--bind`; SEND_TIME requires an explicit synchronized-clock setting.
  Its state-directory lock excludes other Java nodes, but on Linux does not
  exclude the C++/Python `flock`: use distinct state directories.

Both implementations add checks against the shared wire fixtures and tests
of stream ordering, reconnect/replay and faults. Java additionally adds mTLS
loopback tests for certificate identity, unauthorized peers, connection resets,
underrun and send-log overflow. These tests are separate from the native C++
CTest suite; the implementation READMEs give their commands.

## Developer and research changes

Studio's document state, cancellable network work, verified discovery,
connection/removal planning, routing and pointer interactions now have focused
components and regression tests. Viewer capabilities use separate interfaces
for live acquisition and recorded comparisons. The C++ control implementation
and header were renamed from `ControlPlaneMvp` / `control_plane_mvp.hpp` to
`ControlPlane` / `control_plane.hpp`; callers must use the new names.

The repository adds reproducible numerical-feedback, pole/zero and coupled
integration research, with reports and compact evidence linked from the
[documentation index](INDEX.md). The performance investigation records serial
execution and convolution measurements, including corrected parallel benchmark
results. A within-node parallel scheduler remains
[deferred research](RELEASE_0.3.0_RUNTIME_PARALLELISM.md), not a release feature.
Whole-diagram cycle validation across network streams and the proposed test
grouping/selection changes remain planned work for 0.2.0. In particular, a loop
crossing nodes is still not checked as a whole for causal memory and can block
until its receive timeout; see [open problems](OPEN_PROBLEMS.md).

## Qualification and limits

The earlier pre-release record, introduced in preparation commit `61133d9`,
reports verification on Ubuntu 26.04 with a GCC 16 development snapshot and
Qt 6.10.2: the native Release suite passed 101/101
tests, including the Qt GUI workflow, plot and 3D suites under Xvfb with
software OpenGL (llvmpipe) and the four example shards, which open, run and
inspect all 73 bundled diagrams. This is an earlier development baseline,
not a fresh full-suite result for the final release revision.

The Wine test-isolation change (`024350f`) separately reports 93/93 Windows
cross-build tests passing. Studio tests now use Qt's null RHI with no desktop
display, and longer Wine timeouts; interactive `run_on_wine` keeps its display.
Those tests check behavior, not viewer rendering. Gallery captures at
`f285c23` exercised the three selected examples and live carrier FFT check on
Fedora 45 / Qt 6.11.2 under Xvfb with software OpenGL.

Final 0.1.2 binaries still need separate build and installed-package
qualification, with their exact revision and platform results recorded here
before publication. The records above do not establish that qualification.

Windows remains experimental. Physical GPU, high-DPI, accessibility, other
Linux distributions, larger graphs and multi-hour soaks remain outside this
release qualification, as in 0.1.1.

This is pre-MVP software: APIs, file formats, and protocols can change.
Observation storage is temporary rather than crash-durable. Save or export
results explicitly. Display previews can skip updates under load; authoritative
capture is separate. Live observer bindings are fixed for a run.

The [0.1.1 notes](RELEASE_0.1.1_NOTES.md), the
[numerical validation matrix](NUMERICAL_VALIDATION_MATRIX.md) and the
[observer latency profile](OBSERVER_UI_LATENCY_PROFILING.md) describe the
earlier tested envelopes.

## Change review scope

Reviewed all **88 commits** from tag `v0.1.1` through `f285c23`, including the
final combined source changes, tests, examples and documentation. The notes
describe the resulting behavior; intermediate changes later superseded by
another commit are not listed as separate features.
[Full comparison](https://github.com/alukin/s7/compare/v0.1.1...f285c23).

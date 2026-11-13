# Surface viewers and control-analysis implementation plan

Status: implementation delivered, 2026-11-13. The phased design below is
retained for traceability; the implementation and qualification record is at
the end. Deferred extensions remain outside this implementation.

## Intended result

Extend Studio with real and complex surface viewers, linked control-frequency
response plots, and time-frequency analysis. Keep both mathematical teaching
examples and practical control-system examples. Use the same well-known
surfaces as user demonstrations and independent numerical/rendering checks.

Reusable computation belongs in the corresponding headless libraries and
element plugins. Studio supplies read-only acquisition adapters, asynchronous
analysis scheduling, inspection and rendering. Adding a viewer must never
inject excitation, step a graph, reset a plant or change causal execution.

Deliver in the order below. Each milestone includes its examples and tests;
the gallery is part of the feature, not a final documentation task.

## Existing implementation and constraints

- `s7studio/src/viewers/` already provides viewer registration, source binding,
  Qt Graphs plots and Qt Quick 3D trajectory geometry. Extend those mechanisms.
- `s7studio/src/analysis/` provides FFT/spectrum processing and a shared bounded
  worker pool. Preserve its cancellation, stale-result rejection and activity
  rules rather than introducing a thread pool per viewer.
- `ObservationSource` journals scalar values with run/channel/step identity.
  `LocalSchemaRunner` currently rejects complex-valued probes before stepping.
  Initially expose complex results through explicit `ComplexToReIm` adapters;
  do not silently discard imaginary components or generalize all stored samples
  as a prerequisite for the surface renderer.
- `ImpulseGenerator` already exists in `s7core/include/s7/sources.hpp` and the
  registry. It emits one sample of height `area / dt`, follows its model-rate
  domain and supports a grid-aligned `start_time`. Reuse it.
- `s7spectral` is an out-of-tree installed-SDK plugin with `WindowedFFT` peak
  outputs. Preserve that build and plugin-loading boundary.
- Network frames currently carry named scalar channels per graph evaluation.
  They are not array-valued graph ports. `SpectralFrame` in
  `s7core/include/s7/spectral_types.hpp` is not a registered signal type and
  contains wall-clock timing; do not treat it as an available graph-port API.
  See also the spectral-port gap in `OPEN_PROBLEMS.md`.
- Plants/controllers remain editable schemas and reusable definitions in
  `s7core/resources/schema_elements.s7library`. No special C++ RC/RLC/controller
  implementations and no new global simulation solver are needed.

## 1. Data contracts, library ownership and numerical conventions

Agree and document these interfaces before wiring production windows.

### Ownership

| Component | Planned responsibility |
|---|---|
| `s7core` | Small general-purpose coordinate source if required; existing arithmetic, complex adapters and impulse source |
| `s7spectral` | Headless spectral/response processors, spectral graph elements, metadata and installed-SDK tests |
| Proposed `s7wavelet` | Headless CWT processor, causal wavelet-coefficient element, metadata and installed-SDK tests |
| `s7studio_observation` / `s7studio_results` | Read-only source projection, result provenance and serialization adapters |
| `s7studio_analysis` | Worker jobs invoking library processors, input validation and bounded scheduling |
| `s7studio_viewers` | Surface geometry, colors, camera, linked plots, controls and inspection |
| `examples/` | Editable diagrams, schema-library compositions, reference descriptions and reproducible generation |

Expose reusable processor APIs from the spectral/wavelet packages, separately
from their loadable registration modules. Studio may link those processors
without loading a plugin or stepping an analysis element. Graph elements wrap
the same processors where their causal contract permits it. Do not copy the
transform implementations into viewer classes.

Preserve `s7spectral` as an external SDK consumer: no main-build
`add_subdirectory(s7spectral)`. Provide installed CMake package targets for
processors and extend dependency/build/package scripts to build them against
the installed SDK before enabling the corresponding Studio features. Keep
spectral/wavelet features explicitly optional when those packages are absent;
show the availability reason in Studio and example documentation. Apply the
same out-of-tree pattern to `s7wavelet`. Package plugin modules, processor
libraries and dependency notices together when those features are selected.

### Immutable result types

Introduce focused result types rather than one universal plotting container:

- **Surface grid:** explicit X/Y coordinate arrays, dimensions, row/column
  indexing, real heights or complex values, validity mask and domain metadata.
- **Frequency response:** frequency coordinates, complex output/input response,
  units, excitation convention, capture interval and measurement method.
- **Time-frequency map:** model-time coordinates, frequency/scale coordinates,
  complex coefficients or a specified real quantity, validity/edge mask and
  transform settings.

Retain run/source/channel identities and effective model rate. An analytical
reference has explicit model/formula identity instead of a fabricated run ID.
Parameter sweeps preserve every constituent run and the parameter value.
Renderer projections may reduce resolution; inspection/export must identify
whether they expose source values or an aggregate display cell.

Use explicit linear quantities and referenced dB conversions. Frequency may
display Hz or rad/s with an explicit conversion. A log axis transforms display
coordinates, never stored data. Missing cells remain missing. No arrival-order
meshing, implicit resampling, fabricated samples or silent phase unwrapping.

**Completion:** result validation rejects mismatched dimensions, invalid axes,
duplicate cell identities, unsupported sizes and allocation overflow. Document
rate, timestamp, normalization, lifetime and completeness semantics in tests.

## 2. Real surface viewer and mathematical gallery

Implement a reusable `SurfacePlotView` using Qt Quick 3D indexed triangle
geometry and a C++ bridge, following `TrajectoryPlotView` ownership/teardown.
Use original numeric coordinates for inspection and bounded transformed
coordinates for GPU geometry. Share a focused color-mapping component with a
2D heatmap presentation. Avoid refactoring existing trajectory code unless a
small shared camera/axis component has a concrete use in both viewers.

First supported domain: a regular rectangular height field `h = f(x,y)`.
Register a real-surface observer through `ViewerRegistry`, observer metadata,
diagram ports and the Observers dock preview factory.

Provide surface/wireframe/heatmap presentations, orbit/zoom, reset and
orthographic views, axis titles/units, color legend, fixed/automatic ranges,
linear/log height where defined, numeric/equal-aspect controls and cursor
inspection. Add heatmap contour overlays in this milestone. Lighting must not
make a quantitative color legend misleading; provide an unlit color mode.

Start with 129 x 129 samples in bundled examples. Admit at most 513 x 513
cells per initial in-memory grid, with checked byte budgets and explicit
rejection. Larger source datasets may later use tiled processing; never allocate
an unbounded mesh. Drawing caps, reductions and clipping are visible settings.

### Schema sampling

Add a deterministic `GridScan` coordinate source in the core sources category
if the existing source primitives cannot express the scan cleanly. Its narrowly
defined job is emitting X/Y coordinates for a configured row-major rectangular
scan, one cell per logical evaluation, with inclusive bounds and configured
counts. It does not evaluate the mathematical function. Specify its first cell,
reset-by-new-instance and completion/hold behaviour in source smoke tests.

Persist the observer's scan descriptor, including first captured logical step,
dimensions and order. Assemble cells by that explicit identity and same-step
coordinate/value bindings; validate observed X/Y against the declared grid.
After the configured scan interval, held source outputs do not create new cells.
Missing steps produce holes and an incomplete result. A partial scan stays
inspectable; opening the viewer late reconstructs it from the retained source.

Static examples contain a coordinate source followed by ordinary arithmetic and
function elements. Reusable named surfaces are schema-library compositions,
not C++ elements named after each surface. Scan step/time is acquisition order,
not an additional physical coordinate. Stateful parameter experiments require
separate runs; scanning inputs through an evolving plant is not independent
evaluation of a static surface.

### Initial reference surfaces

| Surface and suggested domain | Numerical and visual checks |
|---|---|
| Plane `h = x + 2*y`, `[-2,2]^2` | Orientation, axis swaps, interpolation and normals |
| Paraboloid `h = x*x + y*y`, `[-2,2]^2` | Origin minimum, radial symmetry and scale |
| Hyperbolic paraboloid `h = x*x - y*y`, `[-2,2]^2` | Signed heights, saddle, zero diagonals and diverging colors |
| Gaussian `h = exp(-(x*x+y*y))`, `[-3,3]^2` | Unit peak and smooth tails |
| Separable wave `h = sin(x)*cos(y)`, `[-pi,pi]^2` | Periodic peaks, valleys and zero contours |
| Radial sinc `h = sin(r)/r`, `r = sqrt(x*x+y*y)`, `[-10,10]^2` | Rings, negative lobes and explicit value `h(0,0)=1` |

Supply a finite origin-safe graph for sinc, or use a grid avoiding the origin
until an explicit safe evaluation composition is available. Do not assume a
downstream switch prevents an upstream division by zero from executing.

Use independently evaluated formulas for reference fixtures, not the diagram
generator or the production mesh builder. Include non-square grids and
asymmetric domains so swapped axes cannot pass by symmetry.

**Completion:** every diagram runs headlessly, fills the expected cells and
matches reference values; Studio displays the same data. Rendered checks cover
nonblank output, holes, color mapping, labels, camera changes and contours.

## 3. Complex surface viewer

Reuse the surface renderer for `w = f(z)`, with `z = x + i*y`:

- Domain coordinates: `Re(z)`, `Im(z)`.
- Default height: `abs(w)`; selectable `Re(w)`, `Im(w)` and explicit log magnitude.
- Default color: `arg(w)` using a cyclic palette and a phase wheel/legend.
- Top-down domain-coloring presentation, with optional magnitude contours.
- Inspect both complex components, magnitude and phase at the original cell.

Initially accept same-step real/imaginary components through explicit graph
adapters. At zero, phase is undefined and receives a distinct marker. Phase
interpolation must respect the cyclic seam; never interpolate angle linearly
across `-pi`/`pi`. Missing/invalid vertices remove incident triangles. Clipped
large values are visibly marked; magnitude/phase inspection retains originals.

Ship `z`, `z*z`, `1/z`, and `(z-1)/(z+1)` with expected zeros, poles and phase
winding. Use half-cell-offset domains where necessary to avoid evaluating exact
poles in the runtime graph; synthetic masked fixtures separately test holes.
Do not turn a runtime numerical error into a supposedly complete surface.
Branch-cut examples such as `log(z)` follow after branch conventions and
available complex-function primitives have been explicitly specified.

**Completion:** tests distinguish zero from invalid phase, exercise the cyclic
color seam, pole masks and log-height limits, and verify component pairing
through stop/reset/reopen and source replay.

## 4. Impulse-response and control-frequency analysis

Add a response processor in `s7spectral` and invoke it from Studio workers on
immutable input/output captures. Begin with finite, uniformly sampled, gap-free
SISO records and zero initial state (or explicitly recorded equilibrium offsets).
The user selects the actual input and output; the viewer does not infer a
transfer function from arbitrary schema topology.

For a pulse at captured step `n0` with actual height `A`, form
`h[k] = output[n0+k] / A` after the documented baseline subtraction. Compute
the unnormalized DFT of `h` to obtain sampled discrete frequency response.
Include direct feedthrough at `k=0`, preserve causal delays and retain enough
post-pulse data to expose truncation. Never apply the ordinary spectrum view's
one-sided amplitude doubling or coherent-tone normalization to this result.
No default Hann taper on an impulse response; any deliberate taper is recorded
and changes the estimate. Zero-padding changes frequency sampling, not resolution.

Reuse `ImpulseGenerator`: area one gives `A = 1/dt`. A unit-sample experiment
sets area to the effective `dt`; implement this through explicit experiment
configuration, not by silently changing the existing source's meaning. Freeze
the convention and actual rate/height in result metadata. Reject an absent,
zero, multiple or nonfinite excitation when using the single-pulse estimator.
Source sample counters, published runtime steps and model timestamps must be
tested together; never shift curves to make continuous references appear exact.

Register a control-response presentation with linked Bode, Nyquist, Nichols,
and a 3D curve `(frequency, Re(H), Im(H))`, colored by frequency. Cursor
selection identifies the same frequency across all views. Show gain units and
explicit dB reference; do not assume every response is dimensionless. Phase
wrapping/unwrapping is a visible choice and undefined phase creates gaps.

Reference/measurement identity is visible. Continuous `G(s)` references use
`s=j*omega`; discrete `G(z)` references use `z=exp(j*omega*dt)`. Compare the
executed graph against its independent discrete recurrence first, then study
convergence to the continuous model. Do not label a finite impulse experiment
on a nonlinear or time-varying model as a universal LTI transfer function.
Unstable/nondecaying records retain their time response but do not receive a
claim of a converged steady-state frequency response. Automatic stability
verdicts and margins require a later explicit loop/assumption contract.

Examples: gain, one/multiple explicit Delays, RC low-pass, RLC capacitor voltage
and mass-spring-damper displacement. Compose reusable plants from the existing
schema library. Compare damping values in separately identified fresh runs to
build a frequency x damping x gain surface. Provide impulse and step comparisons.
Add analytical complex-plane transfer landscapes and highlight the imaginary
axis/unit-circle path linked to the corresponding frequency-response point.

**Completion:** exact gain/delay tests catch normalization and phase errors;
RC/RLC checks compare independent recurrences and timestep refinement. Test
capture gaps, unfinished tails, invalid excitation, baseline settings, export
and run replacement. Swept-sine experiments are a follow-up estimator using
the same response result/presentations and explicit settling intervals.

## 5. Spectrogram and continuous wavelet analysis

Deliver a spectrogram first to exercise time-frequency data and shared
heatmap/surface presentations. Extend the spectral processor package with
bounded STFT windows/hops, model-step coordinates and the already documented
amplitude/power/PSD conventions. Then add the CWT processor in `s7wavelet`.

Start CWT with one documented complex Morlet convention, logarithmic scales,
an explicit scale-to-frequency mapping and finite kernel support. Lock down
its DC correction, normalization, bandwidth/central-frequency parameters,
sampling factors and support cutoff before implementation. Return complex
coefficients; display magnitude by default and optionally squared magnitude or
phase. Squared coefficients are not automatically a calibrated PSD.

Use explicit boundary extension (zero padding initially), report the affected
region with a cone-of-influence/support mask and retain the boundary convention
on export. Link time/frequency cursors to the time trace and coefficient slices.
Offer heatmap and optional 3D height presentations of the same map. Phase is
unavailable at zero/insignificant coefficients according to a displayed threshold.

Begin with frozen/recorded bounded intervals; extend to rolling analysis only
after budgets and edge behaviour pass. A centered transform requires future
samples relative to its center: live results therefore have explicit latency
or a marked unresolved newest region. Do not present an acausal transform as
an instantaneous controller signal.

Use the shared worker pool with one active and one replaceable pending request
per panel, cancellation between tiles/scales, checked input/output/scratch byte
budgets and stale-run/configuration rejection. Provisional initial bounds:
65,536 input samples, 128 CWT scales and 256 MiB per admitted job including
scratch. Also enforce a shared pool-wide memory budget; concurrent jobs cannot
each reserve the entire application allowance. Oversized work is rejected or
explicitly reduced, never silently downsampled. Measure and adjust these bounds
before advertising interactive performance.

Examples: stationary tone, separated tone bursts, overlapping tones, chirp,
one-sample impulse, resonator ring-down, chirp through RLC and PLL acquisition.
Explain the different fixed-window STFT and scale-dependent CWT resolution.
Wavelet maps describe signal behaviour, not automatically a transfer function.

**Completion:** compare optimized CWT against an independent direct-convolution
reference on small fixtures, plus tone frequency/amplitude behaviour, impulse
kernel shape, time translation and chirp ridges away from affected edges. Test
STFT normalization, zero/DC input, gaps, nonfinite data, budgets, cancellation,
freeze/reanalysis and live-edge validity. Export transform settings and masks.

## 6. Reusable graph elements in the corresponding libraries

Deliver the processor packages alongside their viewer milestones. Add useful
scalar-port wrappers without inventing a frame signal type or thousands of
per-bin output ports:

| Library | Planned element | Contract |
|---|---|---|
| `s7spectral` | Existing `WindowedFFT` | Retain dominant-frequency/amplitude use; audit actual sample-rate handling and normalization before reusing its conventions |
| `s7spectral` | Proposed `FourierCoefficient` | Real scalar input; C64 coefficient at a configured frequency over a bounded window, plus an update/valid indication; explicit window/hop and normalization |
| `s7wavelet` | Proposed `WaveletCoefficient` | Real scalar input; one configured-scale C64 coefficient from a finite causal FIR realization, plus validity; documented delay and initialization support |

Output C64 is observed using explicit component adapters until typed complex
capture is added. A held coefficient is not a newly computed result every
runtime step: validity/update and the represented input interval must be part
of the element contract. Source/model rate comes from existing rate-domain
adoption, not an independent contradictory parameter.

The causal wavelet element and the centered offline CWT are distinct modes
with explicit alignment. Reuse kernel generation/numerics and prove equality
after the documented delay on fully supported intervals. A feedback use must
respect the actual direct-feedthrough/causal-memory classification; private
filter history alone does not make an element a cycle-breaking memory element.

Full-spectrum and full-scalogram graph elements depend on an explicit future
array/frame-valued port design. Record that as an architectural decision before
changing `SignalType`, compiler/runtime contracts, plugin ABI or wire encoding.
The initial viewer and processor work does not depend on that expansion.

For each added element: provide idempotent plugin registration, element
metadata, rate/parameter validation, bounded instance-owned storage, reset/new
instance semantics, tests, headless JSON and Studio diagram examples. Runtime
signal math follows `s7::math` and project floating-type conventions. Keep
library dependencies Qt-free. Do not add an analysis element to a graph merely
because a viewer is open.

**Completion:** build/load both plugin packages against an installed SDK with
no Studio; verify multiple independent instances, type errors, supported rate
adoption, coefficient timing, units and finite resource limits. Exercise the
same registered element in headless and Studio runs. Windows tests use CMake
target invocation/emulation conventions.

## Example delivery and verification

Proposed directories: `examples/surfaces/`, `examples/complex_functions/`,
`examples/frequency_response/` and `examples/time_frequency/`. Each contains
editable diagrams, a README with formulas/units/domains, recommended model
rate and duration, expected features, numeric reference points and known
limits. Include reproducible generation scripts where useful, but keep the
independent reference implementation separate.

Update `STUDIO_EXAMPLES.md`, `STUDIO_OBSERVATION_VIEWERS.md`, the user guide,
library READMEs and `docs/INDEX.md`; include example help and diagrams in native
and Windows packages. Extend save/open/export to preserve explicit dimensions,
complex components, coordinates, masks, source provenance and analysis settings.
Do not force multidimensional results into `.s7scope` or expose temporary spill
files as interchange formats. Add a versioned analysis-result format and
document JSON metadata plus flat CSV cell/bin exports.

Verification is layered:

1. Library numerical tests: independent formulas, recurrences, DFTs and direct
   convolutions, with stated tolerances and reasons.
2. Module tests: grid/result validation, masks, geometry/index bounds, normals,
   colors, worker cancellation, memory admission and serialization rollback.
3. Whole-system examples: execute real diagrams headlessly; check cells,
   impulse alignment and coefficients; compare Studio capture with the same
   runtime results. No tests write under the user's home directory.
4. Studio lifecycle tests: open before/after capture, hide/close/reopen,
   clear presentation without deleting data, reset, change diagrams, replay,
   incomplete capture and stale worker completion.
5. Local rendered GUI tests: actual Qt Quick 3D pixels, axes/color legend,
   contours, holes, camera, linked cursors and heatmap/3D consistency. Offscreen
   geometry/QML success is not a rendering pass. Follow local-only GUI policy
   in `STUDIO_GUI_TESTING.md`; keep GUI artifacts outside Git.
6. Headless build/SDK tests and Windows cross-build/Wine checks, followed by
   native desktop rendering qualification. Preserve a Qt-free core build.

Use existing `build-tests`/CTest targets and add focused cases under each
module's `tests/`. Extend `s7spectral_sdk_smoke` and add the corresponding
wavelet SDK smoke test. Broad regression runs follow focused checks; record
actual commands/results rather than implying all platforms are qualified.

## Suggested reviewable increments

| Increment | Demonstrable result |
|---|---|
| A | Grid/result contract, coordinate scan and real surface viewer with plane/bowl/saddle |
| B | Full real gallery, heatmap/contours, complex surfaces and component adapters |
| C | Spectral processor package, impulse examples and linked Bode/Nyquist/Nichols/3D response |
| D | Parameter-response surfaces and linked analytical transfer landscapes |
| E | STFT spectrogram, shared time-frequency inspection/export and scalar spectral element |
| F | Wavelet processor/plugin, scalogram, causal coefficient element and control examples |

Implement each increment as small module-focused changes with its own tests
and examples. Do not assign a release version or performance guarantee until
scope and measured results justify it. Deferred extensions: unstructured meshes,
multisheet Riemann surfaces, automatic model linearization, MIMO identification,
wavelet coherence/reconstruction, automatic loop-stability verdicts and new
frame-valued graph ports.

## Technical references

- Existing project contracts: [observation viewers](STUDIO_OBSERVATION_VIEWERS.md),
  [FFT spectrum](STUDIO_SPECTRUM.md), [signal sources](SIGNAL_SOURCES.md),
  [simulation semantics](SIMULATION_MODEL.md), [linear examples](../examples/linear/README.md),
  [spectral plugin](../s7spectral/README.md) and [GUI testing](STUDIO_GUI_TESTING.md).
- Julius O. Smith, [Example Applications of the DFT](https://dsprelated.com/freebooks/mdft/Example_Applications_DFT.html):
  impulse-response/frequency-response relationship and independent DFT references.
- MathWorks, [Time-Frequency Analysis and Continuous Wavelet Transform](https://www.mathworks.com/help/wavelet/ug/time-frequency-analysis-and-continuous-wavelet-transform.html):
  STFT/CWT resolution comparison and edge-effect interpretation. This is a
  conceptual reference, not a dependency or a promise of MATLAB compatibility.

## Implementation and qualification record (2026-11-13)

All six implementation increments are delivered:

| Increment | Implemented result |
|---|---|
| A/B | Explicit indexed GridScan; real/complex surface observers; shared 3D/wireframe/heatmap; contours, fixed ranges, equal numeric aspect and logarithmic height; cyclic phase colors; mathematical gallery; bounded dock previews |
| C | Gain/delay/RC/RLC/mechanical impulse diagrams; asynchronous finite-record response analysis; linked Bode/Nyquist/Nichols and frequency-colored 3D curve; units, referenced dB, result files and CSV |
| D | Nine fresh schema-run damping sweep with independent discrete-transfer checks and importable surface; explicit analytical second-order landscape with imaginary-axis path and linked frequency marker |
| E | STFT spectrogram, original-input freeze/reanalysis, follow mode, numeric map/trace/slice inspection, versioned result files and full matrix CSV; FourierCoefficient graph element |
| F | Morlet CWT scalogram with full-support/edge mask and cyclic phase display; cancellable shared-pool processing and heavy-job admission; wavelet plugin and causal coefficient element; transient/chirp/resonance/PLL examples |

The scan descriptor is emitted as explicit index/columns/rows/active signals.
Portable results use a versioned, bounded, little-endian format with JSON
metadata, axes, complex data, validity and optional frozen original input.
Imports stage and validate before replacing a view. Surface save reconstructs
the authoritative captured prefix even after its presentation has been cleared.

Reusable processors are header-only in their corresponding Qt-free packages.
Studio compiles those same processors directly; its analysis windows therefore
do not require optional graph plugins to be installed. The out-of-tree plugins
export installed CMake analysis targets, and `tools/build_analysis_plugins.sh`
builds them against the installed SDK. Native/Windows packaging includes them
when available. No array/frame signal type, vendor patch, global ODE solver or
change to runtime causal semantics was introduced.

Validation includes independent surface formulas, DFT and discrete-transfer
references, direct long-double CWT convolution and chirp ridges, source/view
parity, save/open/CSV and malformed-import rollback, SDK module/JSON graph
execution, actual Linux Qt/OpenGL rendering, headless builds and Windows/Wine
checks. GUI workflows run serially for their timing-sensitive responsiveness
checks; example-rendering groups remain parallel. A lifecycle test now accepts
both Stopping and Stopped retirement notifications, consistent with its
already documented paused-worker race, while still requiring notification
before window destruction.

Final qualification results:

| Check | Result |
|---|---|
| Release build and complete native CTest suite | 112/112 passed, including all GUI example groups and workflow tests; 310 seconds |
| Fresh Qt-free build | GridScan and damping-sweep tests: 2/2 passed |
| Windows cross-build with Clang 22 and Qt | Studio, analysis viewers, headless sweep and optional plugins built successfully |
| Windows executables under Wine | Surface, response, time-frequency, GridScan and damping-sweep tests: 5/5 passed |
| Installed SDK integration | Both native plugin processor suites and canonical JSON graphs passed; both Windows plugin JSON graphs executed under Wine |
| Targeted Linux OpenGL rendering with optional plugins | Five example cases plus setup/cleanup: 7 passed, 0 failed, 0 skipped; separate optional-element run: 4 passed |
| Final optional-plugin builds | Both spectral and wavelet packages installed successfully for Linux and Windows |

The final native suite log is `/tmp/s7-completed-plan-tests.log`; Windows
viewer checks are in `/tmp/s7-wine-delivery.log`. Render captures used for
manual visual inspection are in `/tmp/s7-analysis-review`. These local
qualification artifacts are intentionally outside the repository.

See [the testing guide](STUDIO_ANALYSIS_TESTING.md) for launch commands and all
example families. Native Windows GPU rendering remains a manual qualification;
Wine/offscreen checks do not claim it. Unstructured meshes, multisheet Riemann
surfaces, automatic linearization/stability verdicts, MIMO identification,
wavelet coherence/reconstruction and frame-valued graph ports remain the
explicitly deferred extensions listed above.

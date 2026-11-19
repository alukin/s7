# Education core — implementation manifest

Audit date: **2026-11-19**. Baseline: `96fe7b438ceeb9a206d404b4ae2a05943c89b21d`.
This is an implementation inventory, not release qualification. The
[release work plan](../docs/RELEASE_0.2.0_WORKPLAN.md) owns G1–G4 acceptance.

The maintainer revised the delivery approach on 2026-11-19: **design new
diagrams for the education collection**. Existing examples are engineering
references only. New canonical graphs belong in `examples/education/`;
student guides belong in `education/labs/`. Do not copy a graph once per
course. Use existing elements, schema resources, production loaders and
test tools; no new runtime semantics are required by this manifest.

## Audit findings

The baseline contains **98 example diagrams**. Inspected JSON payloads include
the 13 linear, 10 control, five frequency-response, three spectrum, eight
time-frequency and seven signal-measurement examples. These have different
readiness and observer paths; filenames alone do not establish lab coverage.
SP06 added the first new education diagram. FN01–FN03 add three more,
bringing that batch's inventory to 102. FN04–FN06 and CT01–CT03 add six
more original graphs, for 108 diagrams. SP01/SP03 add two more original graphs, bringing the
source inventory to 110 diagrams. SP05/SP07 add two more, for
112 diagrams. SP08/SP09 add two more original graphs, for
114 diagrams. SP10 adds one, then SP11 adds one, for **116 diagrams**,
including 18 education labs. CT04/CT05 add two, for **118 diagrams**
and 20 education labs. CT06/CT07 add two, for **120 diagrams** and
22 education labs. CT08 adds one, for **121 diagrams** and **23 education labs**.
Development checks are recorded in EVIDENCE.md;
final release qualification remains open.

- `examples/linear/generate_diagrams.py` generates explicit RC/RLC/mechanical
  graphs. Saved linear models use forward Euler, Euler–Cromer or Gauss–Legendre;
  a file named `symplectic` does not establish a numerical advantage for RC.
- `tools/generate_control_examples.py` and `tools/generate_measurement_examples.py`
  generate control and measurement graphs. PI/PID and plants use embedded or
  registered schema elements; their definitions remain in
  `s7core/resources/schema_elements.s7library`, not special C++ controllers.
- `s7tests/linear_examples_smoke.cpp` checks continuous closed forms and
  timestep refinement; `control_examples_smoke.cpp` checks independent
  controller/plant recurrences, saturation and disturbance behavior;
  `measurement_examples_smoke.cpp` checks AM/filter recurrences and source
  formulas. These are reference implementations to inspect, not educational
  completion evidence for new saved graphs.
- `s7tests/diagram_harness.hpp` is a useful JSON/probe utility, but its
  `build_schema` is a test-side element loader. New SP06 execution uses
  `SchemaLibraryLoader::build_schema`, the production schema loader, followed
  by normal validation, compilation and `ObservationService` sampling.
- `s7studio/tests/spectrum_test.cpp` has direct-DFT checks;
  `time_frequency_test.cpp` checks coefficient normalization and viewer
  behavior. These feed synthetic observations to the analysis path; they
  do not independently qualify a new saved SP08/SP09/SP11 lab.
- `s7core/tests/convolution_smoke.cpp` tests frame-processing convolution.
  That API is not a graph-port FIR lab. SP06 instead exposes two delays,
  three gains and an adder in its new teaching graph.
- `s7studio/tests/gui/workflows_test.cpp` exercises production open/run/view
  paths. Its bundled-example matrix requires a documented duration for each
  new diagram. A dedicated SP06 workflow additionally edits a tap, saves,
  reopens and checks both runs against the written recurrences.
- Existing package scripts copy `examples/`, but do not copy the education
  directory. Curriculum/guide packaging and package-access checks remain open.
  A source run is not evidence for an installed education bundle.

## Source review

On 2026-11-19, the public [6.3000 Fall 2026 calendar](https://sigproc.mit.edu/fall26/),
[6.3100 Fall 2026 calendar](https://introcontrol.mit.edu/fall26/) and
[historical 6.003 reading list](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/)
were rechecked. Their published topic headings support the broad curriculum
alignment. Both current calendars remain tentative. Root-locus specifics,
window-function details and precise textbook passages need individual review
when writing those labs; a broad calendar heading is not an exact exercise
attribution. No textbook problem is claimed as reproduced here.

SP06 is an original illustration of the 6.3000 unit-sample/convolution and
frequency-response/filtering topics. Its equations are independently derived;
no assignment, figure or solution key is copied. FN01–FN03 illustrate
MIT 6.003 difference equations, block diagrams and modularity; their new
contracts and source ledger are recorded in their guides. FN04–FN06 and CT01–CT03 add scalar transfer/pole-map and physical-mode
contracts, with independent formulas and public MIT topic links. Other labs remain
`proposed` until their individual source/model contracts are frozen.

## The 24 core labs

All paths in the reference column are under `examples/` and are **references,
not delivered lab diagrams**. Every row needs a new graph and student guide.
FN01–FN06, CT01–CT08, SP01, SP03 and SP05–SP11 now have new saved graphs and guides. Other entries
describe planned work, not existing educational files.

| ID | Engineering references inspected | New educational graph and reference work | Development status |
| --- | --- | --- | --- |
| FN01 | `frequency_response/delay.s7diagram` | [New unit-delay graph](../examples/education/fn01_unit_delay.s7diagram), [guide](labs/FN01-unit-delay/README.md); closed-form samples, unstable gain and missing-memory rejection | diagram-ready; development checks recorded separately |
| FN02 | `linear/rc_charge_euler.s7diagram`, `rc_discharge_euler.s7diagram` | [New natural/forced graph](../examples/education/fn02_natural_forced.s7diagram), [guide](labs/FN02-natural-forced/README.md); initial-state/input finite sums and wrong-area case | diagram-ready; development checks recorded separately |
| FN03 | Same RC references | [New decomposition graph](../examples/education/fn03_initial_conditions.s7diagram), [guide](labs/FN03-initial-conditions/README.md); input/state separation, missing state and added delay | diagram-ready; development checks recorded separately |
| FN04 | `frequency_response/gain.s7diagram`, `delay.s7diagram` | [New graph](../examples/education/fn04_impulse_transfer.s7diagram), [guide](labs/FN04-impulse-transfer/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| FN05 | `linear/rc_charge_euler.s7diagram`, `2int_osc.s7diagram`; integrator docs/tests | [New graph](../examples/education/fn05_integration_rules.s7diagram), [guide](labs/FN05-integration-rules/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| FN06 | Same integrator references | [New graph](../examples/education/fn06_pole_maps.s7diagram), [guide](labs/FN06-pole-maps/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| SP01 | `spectrum/three_bin_tones.s7diagram` | [New graph](../examples/education/sp01_fourier_synthesis.s7diagram), [guide](labs/SP01-fourier-synthesis/README.md); independent sample/DFT checks, declared limits and viewer scaling | diagram-ready; development checks recorded separately |
| SP03 | `spectrum/off_bin_tone.s7diagram`; source tests | [New graph](../examples/education/sp03_aliasing.s7diagram), [guide](labs/SP03-aliasing/README.md); independent sample/DFT checks, declared limits and viewer scaling | diagram-ready; development checks recorded separately |
| SP05 | `frequency_response/rc.s7diagram`; convolution tests | [New graph](../examples/education/sp05_convolution.s7diagram), [guide](labs/SP05-convolution/README.md); independent finite-sum/mode references and declared failure cases | diagram-ready; development checks recorded separately |
| SP06 | Delay/source primitives; convolution tests | [New three-tap FIR](../examples/education/sp06_three_tap_fir.s7diagram); [guide](labs/SP06-three-tap-fir/README.md), direct convolution and complex H | diagram-ready; development checks recorded separately |
| SP07 | `frequency_response/rc.s7diagram`, `rlc.s7diagram` | [New graph](../examples/education/sp07_filter_transfer.s7diagram), [guide](labs/SP07-filter-transfer/README.md); independent finite-sum/mode references and declared failure cases | diagram-ready; development checks recorded separately |
| SP08 | `spectrum/off_bin_tone.s7diagram`, `two_tone_sum.s7diagram` | [New graph](../examples/education/sp08_dft_leakage.s7diagram), [guide](labs/SP08-dft-leakage/README.md); independent complex DFT/window sums and actual viewer normalization | diagram-ready; development checks recorded separately |
| SP09 | Spectrum references and `spectrum_test.cpp` | [New graph](../examples/education/sp09_window_normalization.s7diagram), [guide](labs/SP09-window-normalization/README.md); independent complex DFT/window sums and actual viewer normalization | diagram-ready; development checks recorded separately |
| SP10 | `s7core/tests/convolution_smoke.cpp` | [New scalar graph](../examples/education/sp10_circular_convolution.s7diagram), [guide](labs/SP10-circular-convolution/README.md); direct/DFT sums plus recorded-input FIR/FFT and stream-tail checks | diagram-ready; development checks recorded separately |
| SP11 | `time_frequency/chirp.s7diagram`, `tone_bursts.s7diagram` | [New burst/chirp graph](../examples/education/sp11_short_time_fourier.s7diagram), [guide](labs/SP11-short-time-fourier/README.md); independent complex STFT, window/hop coordinates, applied map/slice and import checks | diagram-ready; development checks recorded separately |
| SP14 | `signal_measurement/am_coherent_detection.s7diagram` | New AM/detector graph; sideband formula, discrete filter reference and transient failure case | proposed |
| CT01 | `linear/rc_charge_euler.s7diagram`, `control/first_order_open_loop.s7diagram` | [New graph](../examples/education/ct01_first_order.s7diagram), [guide](labs/CT01-first-order/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| CT02 | `linear/rlc_step_euler.s7diagram`, `mass_spring_damper_euler.s7diagram` | [New graph](../examples/education/ct02_damped_modes.s7diagram), [guide](labs/CT02-damped-modes/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| CT03 | Linear/integrator references | [New graph](../examples/education/ct03_sample_time.s7diagram), [guide](labs/CT03-sample-time/README.md); independent formulas and declared limits/edits | diagram-ready; development checks recorded separately |
| CT04 | `control/first_order_{p,pi,pid}.s7diagram` | [New controller comparison](../examples/education/ct04_controller_comparison.s7diagram), [guide](labs/CT04-controller-comparison/README.md); independent P/PI/PID states, load rejection, zero-Kd and coarse-filter cases | diagram-ready; development checks recorded separately |
| CT05 | `control/first_order_pi.s7diagram`, `first_order_pi_no_aw.s7diagram` | [New windup comparison](../examples/education/ct05_actuator_windup.s7diagram), [guide](labs/CT05-actuator-windup/README.md); bounded command versus demand/state, recovery IAE and nested instance edit | diagram-ready; development checks recorded separately |
| CT06 | Frequency-response/control primitives | [New pole-motion graph](../examples/education/ct06_closed_loop_poles.s7diagram), [guide](labs/CT06-closed-loop-poles/README.md); analytic mode powers, gain sweep, marginal/growing cases | diagram-ready; development checks recorded separately |
| CT07 | Frequency-response observers and tests | [New margin graph](../examples/education/ct07_stability_margins.s7diagram), [guide](labs/CT07-stability-margins/README.md); exact rational response, analytic crossovers and Bode/Nyquist points | diagram-ready; development checks recorded separately |
| CT08 | Control primitives and schema-backed controller definitions | [New compensator graph](../examples/education/ct08_lead_lag.s7diagram), [guide](labs/CT08-lead-lag/README.md); reusable lead/lag, independent states/finite convolution/rational response, cancellation and wrong-sign checks | diagram-ready; development checks recorded separately |

FN05/FN06 require particular care: supplied-sample backward integration,
explicit algebraically reduced recurrences and coupled numerical stages are
different contracts. Do not add a general implicit solver to fill a teaching
row. SP10 resolves its recording workflow using scalar word builders and explicit
FIRs, then bridges recorded samples to the separate frame APIs in the test.
Availability of frame convolution alone is not counted as graph support.
CT08 now checks the compensator, nested plants and actual open-/closed-loop paths.

## Delivery order

1. **SP06 first:** a complete small new graph, independent oracle, negative
   case and Studio workflow; it establishes the local candidate for G2.
2. **FN01–FN03:** new discrete-state foundations with exact startup semantics.
3. **FN04–FN06 with CT01–CT03:** distinct new teaching graphs for transfer,
   numerical-method and plant lessons; shared reference utilities only where
   they do not erase mathematical distinctions.
4. **SP01/SP03, then SP05/SP07:** synthesis, sampling and LTI prerequisites.
5. **SP08–SP11:** saved-graph capture → independently checked analysis;
   SP10's scalar recording/frame-API workflow is resolved below the lab table.
6. **CT04–CT08 and SP14:** small controller/measurement batches with their
   own negative cases. Coordinate G2 only after local SP06 acceptance.

Each batch needs focused tests, student instructions, Studio evidence and
eventual installed-package checks. The remaining core lab (SP14), source-specific
reviews, package navigation and final-candidate qualification are open;
no core ID was removed or replaced.

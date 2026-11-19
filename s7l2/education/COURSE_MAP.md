# MIT-aligned S7 curriculum map — 48 proposed laboratory exercises

**Planning proposal, 2026-11-18. None of the rows below is a claim that
the associated exercise has already passed the lab acceptance standard.**
A corresponding example or viewer may exist, but its precise learning
objectives, source attribution and independent oracle still require audit.

This map organizes the same underlying S7 example library into four
progressions. The MIT course references are **topic alignments**, not
official lab numbers, problem solutions or MIT endorsement. See
[primary sources and reading rules](SOURCES.md),
[Fall 2026 course-week alignment](LECTURE_ALIGNMENT.md) and
[the verification contract](LAB_STANDARD.md).

## Map conventions

**2026-11-19 maintainer revision:** build new educational diagrams for
every delivered lab. The R/A/N inventory below describes reference material
and estimated conceptual overlap only; it no longer selects existing files
for delivery. Canonical education diagrams live in `examples/education/`;
the [core manifest](CORE_MANIFEST.md) tracks actual implementation and gaps.

- **R — reuse/audit:** one or more suitable existing example diagrams;
  verify and augment rather than duplicate.
- **A — adapt/extend:** existing behavior is a starting point but needs
  a focused new diagram variant, test or experiment mode.
- **N — new:** no qualifying educational S7 diagram is assumed.
- **D — dependency/research:** work may require additional capabilities;
  not a 0.2.0 release gate without separate approval.
- **Core:** planned first 0.2.0 educational qualification set (24 lab IDs:
  6 FN + 10 SP + 8 CT). **Core is a planning target**, not a numerical
  test pass or a guarantee that prerequisites exist. Any change must
  be approved and recorded in the release work plan.

The **48 items are distinct learning exercises, not 48 separate diagrams**.
One canonical graph may serve multiple IDs with different student questions,
methods or references. R/A classifications indicate inventory estimates,
not validated readiness.

<a id="foundations-fn-10-labs"></a>

## Foundations (FN) — 10 labs

Primary alignment: William McC. Siebert, *Circuits, Signals, and Systems*,
especially Chapter 7–8 material reviewed in
[INTEGRATOR_REFERENCES](../docs/INTEGRATOR_REFERENCES.md);
Mahajan–Freeman's *Discrete-Time Signals and Systems: An Operator Approach*
(Chapters 1, 3, 6), and historical
[MIT 6.003](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/).

| ID | Learning experiment | Existing S7 starting point | Mode / first independent check | 0.2.0 |
| --- | --- | --- | --- | --- |
| FN01 | **Block algebra and unit delay.** Construct an explicit gain/adder/delay realization of a first-order difference equation | [frequency response/delay](../examples/frequency_response/README.md) | A — recurrence including n=0 and initialized delay | Core |
| FN02 | **Natural and forced response.** Compare zero input, impulse and step in a linear recurrence | [linear response](../examples/linear/README.md) | A — geometric closed form with specified initial state | Core |
| FN03 | **Initial conditions and causality.** Contrast zero-input and zero-state terms and delay placement | [RC charge/discharge](../examples/linear/README.md) | R — analytical RC response plus discrete recurrence | Core |
| FN04 | **Impulse ↔ transfer function.** Measure a graph's response and predict its discrete transfer function | [frequency response](../examples/frequency_response/README.md) | R — direct impulse recurrence and complex H(z) | Core |
| FN05 | **Integration rules.** Compare forward/backward Euler and trapezoid on permitted systems | [integrator notes](../docs/INTEGRATORS.md), [linear diagrams](../examples/linear/README.md) | A — independently solved recurrences and exact response | Core |
| FN06 | **Pole mappings.** Investigate s-to-z stability under each integration rule | [integrator references](../docs/INTEGRATOR_REFERENCES.md) | A — algebraic pole formulas and boundary cases | Core |
| FN07 | **Physical system analogy.** Map RLC charge/current to spring displacement/velocity | [linear response](../examples/linear/README.md) | R — matched normalized equations, explicitly distinct units | Later |
| FN08 | **Conservation and numerical drift.** Harmonic oscillator energy versus method and timestep | [oscillator](../examples/STARTER_EXAMPLES.md), [pendulum](../examples/pendulum/README.md) | R — analytic sinusoid and energy invariant | Later |
| FN09 | **Feedback and causal memory.** Distinguish valid delayed loops and invalid algebraic cycles | [control](../examples/control/README.md) | A — exact step-by-step recurrence + rejection tests | Later |
| FN10 | **Continuous versus discrete models.** Vary sample period and compare physical pole/phase with chosen discrete realization | [linear response](../examples/linear/README.md) | A — continuous analytic solution and exact recurrence | Later |

**Pedagogical boundary:** initial values and publication phase are part
of the mathematical model. A forward-Euler path with an implicit extra
sample delay is a different discrete system, even if its plot looks similar.
Use actual formulas; do not equate "leapfrog" with a different symplectic
scheme solely by name.

<a id="signal-processing-sp-16-labs"></a>

## Signal processing (SP) — 16 labs

Primary alignment: [MIT 6.3000 Signal Processing, Fall 2026](https://sigproc.mit.edu/fall26).
Topics follow the broad course progression: sinusoids/Fourier, sampling,
convolution, frequency response, DFT, time-frequency and applications.

| ID | Learning experiment | Existing S7 starting point | Mode / first independent check | 0.2.0 |
| --- | --- | --- | --- | --- |
| SP01 | **Fourier synthesis.** Construct a truncated square-wave series; inspect harmonics, overshoot and Parseval | [spectrum](../examples/spectrum/README.md) | N — analytical Fourier coefficients and partial sums | Core |
| SP02 | **Sinusoids, beats and phase.** Separate amplitude/phase shifts from nearby-frequency beating | [signal measurement](../examples/signal_measurement/README.md) | A — trigonometric sum/product identity | Later |
| SP03 | **Sampling and aliasing.** Display distinct continuous tones yielding the same sampled sequence | [signal sources](../docs/SIGNAL_SOURCES.md), [spectrum](../examples/spectrum/README.md) | N — exact equality of samples at aliased frequencies | Core |
| SP04 | **Quantization and reconstruction.** Measure resolution/noise and sample-and-hold artifacts without hiding rates | [resampling contracts](../../doc/S7_Sample_Rate_Model.md) | N — exact quantization levels/error bounds; model scope explicit | Later |
| SP05 | **Convolution from impulses.** Build an LTI response by superposition of shifted responses | [response examples](../examples/frequency_response/README.md) | A — independently summed discrete convolution | Core |
| SP06 | **Three-tap FIR.** Construct moving average from unit delays, gains and summer; split source/filter over hosts optionally | [convolution](../docs/CONVOLUTION.md), [spectrum](../examples/spectrum/README.md) | N — h=[1/3,1/3,1/3], impulse and exact H(e^jw) | Core |
| SP07 | **Filter transfer functions.** Compare a first-order low-pass and second-order resonance in time/frequency | [frequency response](../examples/frequency_response/README.md) | R — exact discrete pole/zero formula and continuous limit | Core |
| SP08 | **DFT frequency resolution and leakage.** Contrast coherent and non-bin tones | [spectrum](../examples/spectrum/README.md) | R — exact DFT amplitude/window-bin formula | Core |
| SP09 | **Window functions.** Investigate rectangular/Hann leakage, coherent gain and amplitude correction | [spectrum](../examples/spectrum/README.md) | A — independent windowed DFT and normalization | Core |
| SP10 | **Circular versus linear convolution.** Demonstrate aliasing in cyclic convolution and correct zero-padding | [convolution](../docs/CONVOLUTION.md) | N — direct summation modulo N versus linear convolution | Core |
| SP11 | **Short-time Fourier transform.** Identify transient bursts and chirp ridges, vary window/hop | [time–frequency](../examples/time_frequency/README.md) | R — independent short-record STFT and time/frequency coordinates | Core |
| SP12 | **Wavelet versus STFT.** Compare localized events and edge/support behavior | [time–frequency](../examples/time_frequency/README.md) | A — independent finite-support coefficient on selected sample points | Later |
| SP13 | **Chirp and resonant filtering.** Measure frequency-dependent response to a swept tone | [time–frequency](../examples/time_frequency/README.md) | R — expected resonant frequency, transient/steady-state bounds | Later |
| SP14 | **AM modulation and coherent detection.** Predict sidebands and recovered message, including detector settling | [AM/FM](../examples/signal_measurement/README.md) | R — modulation algebra and filter recurrence | Core |
| SP15 | **Noise, correlation and PSD.** Contrast deterministic sequences with seeded random signals | [correlation](../examples/signal_measurement/README.md) | A — seeded statistics and independent PSD estimates | Later |
| SP16 | **2D images: filtering and transform.** Low-pass, edges and 2D DFT/DCT on complete frames | [whole-frame research](../docs/VIDEO_IO_FRAME_DESIGN.md) | D — exact small matrix convolution; no dependency on live camera | Later, capability review |

SP06 is the **preferred candidate** for the practical 0.2.0 distributed
engineering demonstration. Its **local** version must be validated
independently first. The two-host exercise should then establish
sample sequence/rate behavior and a supported recorded-output path; no
new live remote-observation API or network feedback loop is implied.

<a id="modeling-and-control-ct-14-labs"></a>

## Modeling and control (CT) — 14 labs

Primary alignment: [MIT 6.3100, Fall 2026](https://introcontrol.mit.edu/fall26/),
[course information](https://introcontrol.mit.edu/fall26/information).
Course emphasis spans physical models, sampled-time dynamics, control,
frequency-domain tools, state-space, LQR, state estimation and identification.

| ID | Learning experiment | Existing S7 starting point | Mode / first independent check | 0.2.0 |
| --- | --- | --- | --- | --- |
| CT01 | **First-order plant models.** RC, motor-speed or thermal response and time constant | [linear](../examples/linear/README.md), [control](../examples/control/README.md) | R — analytic exponential with explicit units | Core |
| CT02 | **Second-order modes and damping.** RLC versus mass–spring–damper | [linear](../examples/linear/README.md) | R — eigenvalues, damping ratio, closed-form response | Core |
| CT03 | **Sample time and discrete poles.** Evaluate stable/unstable discretizations of the same physical plant | [integrators](../docs/INTEGRATORS.md) | A — exact discrete recurrence and pole locations | Core |
| CT04 | **P/PI/PID and disturbance rejection.** Compare tracking, settling, steady error and load steps | [control](../examples/control/README.md) | R — frozen controller and plant recurrences | Core |
| CT05 | **Actuator limits, windup and anti-windup.** Compare recovery with and without correction | [control](../examples/control/README.md) | R — step-by-step bounded actuator/state checks | Core |
| CT06 | **Root locus through gain.** Trace sampled closed-loop poles as scalar feedback gain changes | [frequency response](../examples/frequency_response/README.md) | A — characteristic polynomial roots independent of S7 | Core |
| CT07 | **Bode, Nyquist and margins.** Identify crossover and phase/gain reserve on a declared discrete transfer function | [frequency response](../examples/frequency_response/README.md) | A — direct complex transfer evaluation; units/dB/unwrap | Core |
| CT08 | **Lead/lag compensation.** Compare transient and frequency response after controller network changes | [control](../examples/control/README.md) | N — exact chosen discrete transfer and reference response | Core |
| CT09 | **Identification from data.** Estimate parameters from synthetic step/PRBS measurements | [signal sources](../docs/SIGNAL_SOURCES.md), [control](../examples/control/README.md) | N — independently fitted parameter/error and identifiability note | Later |
| CT10 | **State-space and modes.** Build a two-state plant with explicit A, B, C matrices | [linear](../examples/linear/README.md) | A — independent matrix recurrence and eigenpairs | Later |
| CT11 | **Controllability and pole placement.** Compare controllable and uncontrollable plants | [control](../examples/control/README.md) | N — rank and independently calculated closed-loop eigenvalues | Later |
| CT12 | **Linear-quadratic regulator.** Compare gains for two Q/R weighting choices | [control](../examples/control/README.md) | D — independent discrete Riccati solution; declare model scope | Later, capability review |
| CT13 | **State observer.** Estimate unmeasured states with explicitly modeled sensor noise | [control](../examples/control/README.md) | N — observer error dynamics and independent recurrence | Later |
| CT14 | **Robustness and mismatch.** Vary plant parameters, controller gain and sample interval | [control](../examples/control/README.md) | A — measured margins and independently chosen stability bounds | Later |

Control labs must distinguish the **continuous physical plant** from a
specified **discrete approximation** and from a *sampled controller*.
S7's solver choice does not silently change the physical system or supply
an exact zero-order-hold equivalent.

<a id="systems-and-inference-in-8-labs"></a>

## Systems and inference (IN) — 8 labs

Primary alignment: contemporary
[MIT 6.3010 catalog](https://student.mit.edu/catalog/m6c.html),
with publicly available [6.011 (2018) syllabus](https://ocw.mit.edu/courses/6-011-signals-systems-and-inference-spring-2018/pages/syllabus/)
as historical context. This is an **advanced extension** requiring
separate capability/readiness reviews; no IN lab is automatically a
0.2.0 blocker.

| ID | Learning experiment | Existing S7 starting point | Mode / first independent check | 0.2.0 |
| --- | --- | --- | --- | --- |
| IN01 | **Random processes and reproducibility.** Ensemble means, variance and seeded trials | [noise source](../examples/STARTER_EXAMPLES.md) | A — theoretical moments and multi-seed confidence intervals | Later |
| IN02 | **Autocorrelation and delay.** Recover known lag using correlated/delayed signals | [IQ correlation](../examples/signal_measurement/README.md) | R — direct finite-sample correlation with exact lag convention | Later |
| IN03 | **Power spectral density.** Interpret periodogram window/bandwidth and integrate PSD | [spectrum](../examples/spectrum/README.md), [STFT](../examples/time_frequency/README.md) | A — independently computed PSD and energy normalization | Later |
| IN04 | **Random inputs through LTI systems.** Relate input/output covariance and PSD | [frequency response](../examples/frequency_response/README.md) | N — known FIR relation and Monte Carlo envelope | Later |
| IN05 | **Matched filtering/detection.** Find a known pulse in seeded noise, measure ROC | [convolution](../docs/CONVOLUTION.md) | N — independent correlation and threshold false-alarm rates | Later |
| IN06 | **Wiener filtering.** Estimate a clean signal from observed noisy data | [spectral library](../docs/CONVOLUTION.md) | D — independently solved Wiener–Hopf normal equations | Later, capability review |
| IN07 | **Kalman filter.** Track scalar or two-state plant with process/measurement noise | [control](../examples/control/README.md) | D — independent Riccati covariance and state-estimate recurrence | Later, capability review |
| IN08 | **Distributed sensing and TDOA.** Use synthetic synchronized sensor waveforms for time-delay estimates | [IQ correlation](../examples/signal_measurement/README.md) | D — known geometric delays; timing provenance and clock-offset stress | Later, research-only |

The last item extends S7's actual distributed-engineering interests; it
is **not** attributed as a named MIT 6.3010 exercise.

## Suggested prerequisite paths

```text
Beginner: FN01 → FN02/FN03 → FN04
                 ├→ SP03 → SP05 → SP06 → SP07/SP08 → SP09/SP10 → SP11/SP14
                 └→ FN05 → FN06 → CT01 → CT02 → CT03 → CT04 → CT05
                                                        └→ CT06 → CT07 → CT08

Advanced extensions: CT10 → CT11 → CT12/CT13 → CT14
                     SP15 → IN01 → IN02/IN03 → IN04 → IN05/IN06/IN07
                     IN02 + distributed timing contracts → IN08
                     whole-frame contract → SP16
```

## Counting, delivery and qualification

- **Planned:** 10 FN + 16 SP + 14 CT + 8 IN = **48 lab IDs**.
- **Initial 0.2.0 educational target:** **24 Core** (6 FN, 10 SP, 8 CT).
  Expand beyond that when independently verified and pedagogically useful.
  Do not substitute a screenshot, untested variant, or an existing directory
  name for qualified core coverage. A maintainer may revise the target,
  with explicit justification, before tagging.
- Create new educational diagrams; do not copy an old graph or create
  duplicate copies for different course routes. Existing examples are references.
- New core work should be prioritized by the
  [implementation plan](IMPLEMENTATION_PLAN.md). Each lab advances its status
  only through [LAB_STANDARD](LAB_STANDARD.md) evidence.
- Do not automatically implement optional IN/SP16 capability dependencies
  merely to fill a count. Such features need an independent architecture
  decision and qualification envelope.

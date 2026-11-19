# New education diagrams

These graphs are created specifically for S7 teaching laboratories. Earlier
examples elsewhere in `examples/` are implementation references. Open a diagram
through **File → Example Diagrams… → education** and set its stated run profile.

| Lab | New diagram | Run | Guide |
| --- | --- | --- | --- |
| FN01: one sample of memory | [fn01_unit_delay.s7diagram](fn01_unit_delay.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN01-unit-delay/README.md) |
| FN02: natural and forced response | [fn02_natural_forced.s7diagram](fn02_natural_forced.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN02-natural-forced/README.md) |
| FN03: initial conditions | [fn03_initial_conditions.s7diagram](fn03_initial_conditions.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN03-initial-conditions/README.md) |
| FN04: from one pulse to a transfer function | [fn04_impulse_transfer.s7diagram](fn04_impulse_transfer.s7diagram) | 10 s at 20 Hz | [Student lab](../../education/labs/FN04-impulse-transfer/README.md) |
| FN05: three rules for the same decay | [fn05_integration_rules.s7diagram](fn05_integration_rules.s7diagram) | 6 s at 2 Hz | [Student lab](../../education/labs/FN05-integration-rules/README.md) |
| FN06: stable does not mean accurate | [fn06_pole_maps.s7diagram](fn06_pole_maps.s7diagram) | 30 s at 0.333333 Hz | [Student lab](../../education/labs/FN06-pole-maps/README.md) |
| CT01: a capacitor takes time to charge | [ct01_first_order.s7diagram](ct01_first_order.s7diagram) | 5 s at 10 Hz | [Student lab](../../education/labs/CT01-first-order/README.md) |
| CT02: how damping changes a spring | [ct02_damped_modes.s7diagram](ct02_damped_modes.s7diagram) | 8 s at 100 Hz | [Student lab](../../education/labs/CT02-damped-modes/README.md) |
| CT03: can a slow sample rate invent growth? | [ct03_sample_time.s7diagram](ct03_sample_time.s7diagram) | 10 s at 2 Hz | [Student lab](../../education/labs/CT03-sample-time/README.md) |
| CT04: controller comparison | [ct04_controller_comparison.s7diagram](ct04_controller_comparison.s7diagram) | 16 s at 100 Hz | [Student lab](../../education/labs/CT04-controller-comparison/README.md) |
| CT05: actuator windup | [ct05_actuator_windup.s7diagram](ct05_actuator_windup.s7diagram) | 12 s at 100 Hz | [Student lab](../../education/labs/CT05-actuator-windup/README.md) |
| CT06: closed-loop poles | [ct06_closed_loop_poles.s7diagram](ct06_closed_loop_poles.s7diagram) | 32 s at 10 Hz | [Student lab](../../education/labs/CT06-closed-loop-poles/README.md) |
| CT07: stability margins | [ct07_stability_margins.s7diagram](ct07_stability_margins.s7diagram) | 64 s at 10 Hz | [Student lab](../../education/labs/CT07-stability-margins/README.md) |
| CT08: lead/lag compensation | [ct08_lead_lag.s7diagram](ct08_lead_lag.s7diagram) | 64 s at 10 Hz | [Student lab](../../education/labs/CT08-lead-lag/README.md) |
| SP01: build a square wave from smooth tones | [sp01_fourier_synthesis.s7diagram](sp01_fourier_synthesis.s7diagram) | 2 s at 128 Hz | [Student lab](../../education/labs/SP01-fourier-synthesis/README.md) |
| SP03: three tones, one sampled sequence | [sp03_aliasing.s7diagram](sp03_aliasing.s7diagram) | 2 s at 64 Hz | [Student lab](../../education/labs/SP03-aliasing/README.md) |
| SP05: add shifted impulse responses | [sp05_convolution.s7diagram](sp05_convolution.s7diagram) | 2 s at 20 Hz | [Student lab](../../education/labs/SP05-convolution/README.md) |
| SP06: three-tap FIR | [sp06_three_tap_fir.s7diagram](sp06_three_tap_fir.s7diagram) | 4 s at 300 Hz | [Student lab](../../education/labs/SP06-three-tap-fir/README.md) |
| SP07: which filter resonates? | [sp07_filter_transfer.s7diagram](sp07_filter_transfer.s7diagram) | 64 s at 64 Hz | [Student lab](../../education/labs/SP07-filter-transfer/README.md) |
| SP08: where did the tone amplitude go? | [sp08_dft_leakage.s7diagram](sp08_dft_leakage.s7diagram) | 2 s at 128 Hz | [Student lab](../../education/labs/SP08-dft-leakage/README.md) |
| SP09: change the window, preserve the units | [sp09_window_normalization.s7diagram](sp09_window_normalization.s7diagram) | 2 s at 128 Hz | [Student lab](../../education/labs/SP09-window-normalization/README.md) |
| SP10: where does the tail go? | [sp10_circular_convolution.s7diagram](sp10_circular_convolution.s7diagram) | 2 s at 8 Hz | [Student lab](../../education/labs/SP10-circular-convolution/README.md) |
| SP11: when did that frequency happen? | [sp11_short_time_fourier.s7diagram](sp11_short_time_fourier.s7diagram) | 4 s at 128 Hz | [Student lab](../../education/labs/SP11-short-time-fourier/README.md) |

## First time with S7? Start here

1. **FN01:** count four samples and follow the one-sample memory around a loop.
2. **FN02:** compare a remembered value, one input sample and a sustained input.
3. **FN03:** separate the contribution of initial state from that of the input.
4. **FN04:** connect a pulse response with its complex transfer function.
5. **FN05/FN06:** compare numerical rules, then separate stability from accuracy.
6. **CT01–CT03:** explore charging, damped motion and the effect of sample rate.
7. **SP01/SP03:** build harmonic sums, then explain indistinguishable samples.
8. **SP05/SP06:** add shifted impulse responses, then build a small FIR filter.
9. **SP07:** compare filter transfer functions with their physical limits.
10. **SP08/SP09:** explain DFT leakage and correctly normalize periodic windows.
11. **SP10:** predict wrap-around, pad enough zeros and account for FIR startup.
12. **SP11:** locate a burst/chirp, vary window and hop, and explain edge overlap.
13. **CT04/CT05:** compare controller/load errors, then inspect hidden integral windup.
14. **CT06/CT07:** connect sampled pole motion with measured open-loop stability margins.
15. **CT08:** compare matched DC gain, transient settling and control effort.

Each file saves the run configuration in the table. FN01–FN03 use
**2 s / 10 Hz**. Click Run and keep the saved settings; the short-run notice is intentional. Double-click
**Compare the named signals** to open Scope. Each yellow description frame
links to an offline lab guide and MIT online readings. Predict the first
samples before running, then follow the guide's one-parameter experiment.

For [SP10](../../education/labs/SP10-circular-convolution/README.md), run
**2 s at 8 Hz**. Read the second eight-sample block at Scope times
**1.125…2 s**, after the periodic paths have warmed up. Compare the linear,
four-sample circular and eight-sample padded outputs. One shared coefficient
changes all three FIRs. This also verifies the separate production FIR/FFT
frame processors using the graph's recorded word and independent references.
Build `s7tests_education_convolution` and `s7core_convolution_smoke`; regenerate
the new graph with `python3 -B examples/education/generate_convolution_lab.py`.

For [SP11](../../education/labs/SP11-short-time-fourier/README.md), run
**4 s at 128 Hz**. Open a map, choose **FFT size 64**, then **Hop 32 samples**,
**Magnitude**, and **Display up to 64 Hz**. Compare 15 columns at 2 Hz spacing
with N=128/H=64 and N=64/H=16. Read the guide before interpreting a window
center as an event time. Build `s7tests_education_stft`; regenerate with
`python3 -B examples/education/generate_stft_lab.py`.

For [CT04](../../education/labs/CT04-controller-comparison/README.md) and
[CT05](../../education/labs/CT05-actuator-windup/README.md), keep the saved
100 Hz profiles. CT04 separates tracking, applied control and error; CT05
separates output, actual command, unlimited demand and integral state.
CT05’s primary experiment edits only `aw/i_rate.weights` in its nested tab,
then returns to the root before running. Build `s7tests_education_control`;
regenerate both graphs with `python3 -B examples/education/generate_control_labs.py`.

Headless check: build `s7tests_education_foundations`, then run
`ctest --test-dir build-release -R '^s7tests_education_foundations$' -V`.
Reproduce the three new graphs with
`python3 -B examples/education/generate_foundations.py` from `s7l2/`.

## Transfer, numerical methods and physical plants

Continue from FN03 to [FN04](../../education/labs/FN04-impulse-transfer/README.md),
then FN05/FN06 and CT01–CT03. Each new graph has a prediction, a primary edit,
an independent numerical reference and a linked offline guide with MIT readings.
FN06 separates growing and stable modes into different Scopes; CT02 separates
position and velocity by units. FN04's short transient is easiest to see at
0–1 s; its longer capture supports frequency-response analysis.

FN05/FN06 use **fixed, algebraically derived Delay/Gain recurrences**. Redesign
the gains when changing h. CT01–CT03 use actual EulerIntegrators, which follow
the selected run rate automatically. All stored initial states appear at Scope
time h; compare the continuous formulas at state time `t_Scope−h`.

Build `s7tests_education_linear`, then run
`ctest --test-dir build-release -R '^s7tests_education_linear$' -V`.
Regenerate this batch with
`python3 -B examples/education/generate_linear_labs.py` from `s7l2/`.

## Harmonics and sampling

[SP01](../../education/labs/SP01-fourier-synthesis/README.md) compares one-,
three- and five-term Fourier sums with a square target. Predict the retained
harmonics, investigate ringing and remove the ninth harmonic.
[SP03](../../education/labs/SP03-aliasing/README.md) uses 10, 74 and 54 Hz
functions that produce the same samples at 64 Hz. Edit one frequency, then
increase the run rate to 256 Hz to distinguish the tones.

For both Scopes, set the analysis interval explicitly to **0–2 s**, click
**Analyze**, then choose **Spectrum → Rectangular → Peak amplitude → Linear**.
Use **All inputs**, no mean removal and the full interval (SP01 N=256;
SP03 N=128, or N=512 at 256 Hz). The full guides specify the timing, bin spacing,
power and PSD conventions. The plotted segments do not reconstruct a
continuous waveform. Equal sampled spectra cannot identify the original alias.

Build `s7tests_education_signals`, then run
`ctest --test-dir build-release -R '^s7tests_education_signals$' -V`.
Regenerate both original graphs with
`python3 -B examples/education/generate_signals.py` from `s7l2/`.

## Continue to SP06

SP06 builds a moving average from two zero-initialized unit delays, three
gains of 1/3 and an adder: `y[n]=(x[n]+x[n-1]+x[n-2])/3`, `x[n<0]=0`.
The source combines 0.25 V DC, a 1 V peak 10 Hz sine and a 0.5 V peak
100 Hz sine. Open the Scope to compare input and filtered output. After
startup, DC is unchanged, the 10 Hz gain is about 0.98543 with −12° phase,
and the 100 Hz tone cancels. Step k observes source index n=k−1 at time k/300 s.
Select `tap1`, press F4, set its factor to 0 and rerun: the output mean
becomes 1/6 V and the 100 Hz tone survives with amplitude 1/6 V peak.

Headless check: build `s7tests_education_sp06`, then run
`ctest --test-dir build-release -R '^s7tests_education_sp06$' -V`.
Reproduce the saved diagram with `python3 -B examples/education/generate_sp06.py`
from `s7l2/`. Qualification and current limitations are recorded in the
[education evidence](../../education/EVIDENCE.md) and
[24-lab implementation manifest](../../education/CORE_MANIFEST.md).

The exercise is original S7 work aligned with convolution and filtering
topics in [MIT 6.3000 Fall 2026](https://sigproc.mit.edu/fall26/), not an official
MIT lab or endorsement. This is development work, not a qualified course bundle.

## Convolution and filter transfer functions

[SP05](../../education/labs/SP05-convolution/README.md) compares filtering a
pair of pulses with summing their separately shifted responses. Two Scopes
expose the contributions, actual output and residual. Predict the overlap,
edit one pulse weight, then deliberately change remembered input history.

[SP07](../../education/labs/SP07-filter-transfer/README.md) compares a low-pass
and a resonant filter built from explicit EulerIntegrators. Its **64 s / 64 Hz**
profile gives a sufficiently decayed tail for measured frequency response.
Inspect the output Scope at **0–10 s**, then open each frequency-response
observer. The guide distinguishes actual discrete H(z), continuous G(s),
log-frequency coordinates, dB reference and wrapped/unwrapped phase.

Build `s7tests_education_lti`, then run
`ctest --test-dir build-release -R '^s7tests_education_lti$' -V`.
Regenerate these original graphs with
`python3 -B examples/education/generate_lti.py` from `s7l2/`.

## DFT leakage and periodic windows

[SP08](../../education/labs/SP08-dft-leakage/README.md) compares a coherent
16 Hz tone with a 16.5 Hz tone, using the same 128 Hz recording over first,
full and late intervals. Predict why the tallest 1 s FFT bin is below 1 V,
then recover a coherent 1 V peak from the actual 2 s record. The guide also
explains inclusive interval boundaries, odd-length transforms and finite means.

[SP09](../../education/labs/SP09-window-normalization/README.md) compares
Rectangular, Periodic Hann and Periodic Hamming with a 2 V calibration and
a strong/weak mixture. Keep **0–1 s**, **N=128**, **All inputs**, **Remove mean
off** for the initial comparison. The guide distinguishes coherent gain,
window energy, ENBW, peak amplitude, power/bin and PSD. Legends follow the
displayed quantity; the input selector keeps the supplied signal unit.

Build `s7tests_education_spectral`, then run
`ctest --test-dir build-release -R '^s7tests_education_spectral$' -V`.
Regenerate both original graphs with
`python3 -B examples/education/generate_spectral_labs.py` from `s7l2/`.

CT06/CT07 use an original two-lag sampled loop. Read the distinction between
ideal unlimited commands, actual discrete poles and the positive-frequency
Nyquist branch before interpreting stability. Build `s7tests_education_loop`;
regenerate with `python3 -B examples/education/generate_loop_labs.py`.

CT08 uses the reusable schema-based `LeadLagController` and nested first-order
plants. The independent pulse path measures the lead open loop. Its primary
zero edit becomes P16, not the P4 baseline. Build `s7tests_education_compensator`
and `s7core_schema_elements_smoke`; regenerate with
`python3 -B examples/education/generate_compensator_lab.py`.

# Example diagrams

Studio can open these `.s7diagram` files through **File → Example Diagrams…**.
Set **Run Configuration** to the experiment duration and rate in the linked guide,
start the local run, then double-click the named observer. The saved model is
an explicit causal graph; viewers and analysis tools observe its outputs.

The [starter and standalone guide](STARTER_EXAMPLES.md) explains the introductory
diagrams. The [Studio examples guide](../docs/STUDIO_EXAMPLES.md) gives a shorter
guided path through the collection. Reference status and numerical limits are
given by each family guide and the [numerical validation matrix](../docs/NUMERICAL_VALIDATION_MATRIX.md).

For a course-oriented route through these canonical diagrams, see the
[MIT-aligned education curriculum](../education/README.md),
[48-exercise topic map](../education/COURSE_MAP.md) and
[lab qualification standard](../education/LAB_STANDARD.md).
These are **planned original S7 teaching exercises**, not a claim
that 48 MIT examples are implemented, verified or officially endorsed.

## Education laboratories

New diagrams designed specifically for teaching; see the
[education diagram guide](../education/index.md) and per-lab evidence.

| Lab | Diagram |
| --- | --- |
| FN01 — One sample of memory | [Unit-delay feedback](../education/labs/FN01-unit-delay/fn01_unit_delay.s7diagram) |
| FN02 — Natural and forced response | [Three input/state experiments](../education/labs/FN02-natural-forced/fn02_natural_forced.s7diagram) |
| FN03 — Initial conditions | [Separate input and stored past](../education/labs/FN03-initial-conditions/fn03_initial_conditions.s7diagram) |
| FN04 — From one pulse to a transfer function | [New teaching graph](../education/labs/FN04-impulse-transfer/fn04_impulse_transfer.s7diagram) |
| FN05 — Three rules for the same decay | [New teaching graph](../education/labs/FN05-integration-rules/fn05_integration_rules.s7diagram) |
| FN06 — Stable does not mean accurate | [New teaching graph](../education/labs/FN06-pole-maps/fn06_pole_maps.s7diagram) |
| CT01 — A capacitor takes time to charge | [New teaching graph](../education/labs/CT01-first-order/ct01_first_order.s7diagram) |
| CT02 — How damping changes a spring | [New teaching graph](../education/labs/CT02-damped-modes/ct02_damped_modes.s7diagram) |
| CT03 — Can a slow sample rate invent growth? | [New teaching graph](../education/labs/CT03-sample-time/ct03_sample_time.s7diagram) |
| CT04 — Does integral action remove the load error? | [New teaching graph](../education/labs/CT04-controller-comparison/ct04_controller_comparison.s7diagram) |
| CT05 — Why does a clipped actuator keep pushing? | [New teaching graph](../education/labs/CT05-actuator-windup/ct05_actuator_windup.s7diagram) |
| CT06 — Closed-loop poles | [New teaching graph](../education/labs/CT06-closed-loop-poles/ct06_closed_loop_poles.s7diagram) |
| CT07 — Stability margins | [New teaching graph](../education/labs/CT07-stability-margins/ct07_stability_margins.s7diagram) |
| CT08 — Lead/lag and control effort | [New teaching graph](../education/labs/CT08-lead-lag/ct08_lead_lag.s7diagram) |
| SP01 — Build a square wave from smooth tones | [New teaching graph](../education/labs/SP01-fourier-synthesis/sp01_fourier_synthesis.s7diagram) |
| SP03 — Three tones, one sampled sequence | [New teaching graph](../education/labs/SP03-aliasing/sp03_aliasing.s7diagram) |
| SP05 — Add shifted impulse responses | [New teaching graph](../education/labs/SP05-convolution/sp05_convolution.s7diagram) |
| SP07 — Which filter resonates? | [New teaching graph](../education/labs/SP07-filter-transfer/sp07_filter_transfer.s7diagram) |
| SP08 — Where did the tone amplitude go? | [New teaching graph](../education/labs/SP08-dft-leakage/sp08_dft_leakage.s7diagram) |
| SP09 — Change the window, preserve the units | [New teaching graph](../education/labs/SP09-window-normalization/sp09_window_normalization.s7diagram) |
| SP10 — Where does the tail go? | [New teaching graph](../education/labs/SP10-circular-convolution/sp10_circular_convolution.s7diagram) |
| SP11 — When did that frequency happen? | [New teaching graph](../education/labs/SP11-short-time-fourier/sp11_short_time_fourier.s7diagram) |
| SP14 — AM and coherent detection | [New teaching graph](../education/labs/SP14-coherent-am/sp14_coherent_am.s7diagram) |
| SP06 — Three-tap FIR | [Three-tap moving average](../education/labs/SP06-three-tap-fir/sp06_three_tap_fir.s7diagram) |

## Array viewers

- [Video Player test patterns](video/video_patterns.s7diagram) and [run guide](video/video_patterns.md)
- [Waterfall from VectorPSD](time_frequency/waterfall_psd.s7diagram) and [run guide](time_frequency/waterfall_psd.md)

## Starter and standalone experiments

| Diagram | Guide |
|---|---|
| [Two-integrator oscillator](2int_osc.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [Gauss–Legendre oscillator](2int_osc_gauss.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [2D phase portrait](2int_osc_phase_portrait.s7diagram) | [Experiment notes](2int_osc_phase_portrait.md) |
| [3D helix](3int_helix_phase_trajectory.s7diagram) | [Experiment notes](3int_helix_phase_trajectory.md) |
| [Editor demonstration](demo1.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [Triggered noise demonstration](demo_diagram2.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [Full double pendulum](double_pendulum_full.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [Small-angle double pendulum](double_pendulum_small_angle.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |
| [PWM/PID/RLC converter](pwm_pid_rlc.s7diagram) | [Experiment notes](pwm_pid_rlc.md) |
| [Symplectic live oscillator](symplectic_osc_live.s7diagram) | [Starter guide](STARTER_EXAMPLES.md) |

## Linear response (13)

RC, RLC and mechanical response; Euler, Euler–Cromer and selected Gauss variants. [Experiment guide](linear/README.md). Typical run: 10 s at 1 kHz; resonance 30 s.

| Diagram |
|---|
| [mass spring damper euler](linear/mass_spring_damper_euler.s7diagram) |
| [mass spring damper symplectic](linear/mass_spring_damper_symplectic.s7diagram) |
| [rc charge euler](linear/rc_charge_euler.s7diagram) |
| [rc charge gauss](linear/rc_charge_gauss.s7diagram) |
| [rc charge symplectic](linear/rc_charge_symplectic.s7diagram) |
| [rc discharge euler](linear/rc_discharge_euler.s7diagram) |
| [rc discharge gauss](linear/rc_discharge_gauss.s7diagram) |
| [rc discharge symplectic](linear/rc_discharge_symplectic.s7diagram) |
| [rlc resonance euler](linear/rlc_resonance_euler.s7diagram) |
| [rlc resonance symplectic](linear/rlc_resonance_symplectic.s7diagram) |
| [rlc step euler](linear/rlc_step_euler.s7diagram) |
| [rlc step gauss](linear/rlc_step_gauss.s7diagram) |
| [rlc step symplectic](linear/rlc_step_symplectic.s7diagram) |

## Nonlinear single pendulum (3)

One nonlinear pendulum with three integration methods. [Experiment guide](pendulum/README.md). Typical run: 100 s at 1 or 10 kHz.

| Diagram |
|---|
| [nonlinear pendulum euler](pendulum/nonlinear_pendulum_euler.s7diagram) |
| [nonlinear pendulum symplectic](pendulum/nonlinear_pendulum_symplectic.s7diagram) |
| [nonlinear pendulum trapezoid](pendulum/nonlinear_pendulum_trapezoid.s7diagram) |

## Small-angle double pendulum (9)

Slow, fast and mixed initial modes with three integration methods. [Experiment guide](double_pendulum/README.md). Typical run: 20 s at 1 kHz.

| Diagram |
|---|
| [double pendulum fast euler](double_pendulum/double_pendulum_fast_euler.s7diagram) |
| [double pendulum fast symplectic](double_pendulum/double_pendulum_fast_symplectic.s7diagram) |
| [double pendulum fast trapezoid](double_pendulum/double_pendulum_fast_trapezoid.s7diagram) |
| [double pendulum mixed euler](double_pendulum/double_pendulum_mixed_euler.s7diagram) |
| [double pendulum mixed symplectic](double_pendulum/double_pendulum_mixed_symplectic.s7diagram) |
| [double pendulum mixed trapezoid](double_pendulum/double_pendulum_mixed_trapezoid.s7diagram) |
| [double pendulum slow euler](double_pendulum/double_pendulum_slow_euler.s7diagram) |
| [double pendulum slow symplectic](double_pendulum/double_pendulum_slow_symplectic.s7diagram) |
| [double pendulum slow trapezoid](double_pendulum/double_pendulum_slow_trapezoid.s7diagram) |

## Feedback control (10)

First-order and DC-motor plants with open loop, P, PI and filtered PID controllers. [Experiment guide](control/README.md). Typical run: 12 s at 1 kHz.

| Diagram |
|---|
| [dc motor open loop](control/dc_motor_open_loop.s7diagram) |
| [dc motor p](control/dc_motor_p.s7diagram) |
| [dc motor pi](control/dc_motor_pi.s7diagram) |
| [dc motor pi no aw](control/dc_motor_pi_no_aw.s7diagram) |
| [dc motor pid](control/dc_motor_pid.s7diagram) |
| [first order open loop](control/first_order_open_loop.s7diagram) |
| [first order p](control/first_order_p.s7diagram) |
| [first order pi](control/first_order_pi.s7diagram) |
| [first order pi no aw](control/first_order_pi_no_aw.s7diagram) |
| [first order pid](control/first_order_pid.s7diagram) |

## Buck converter (2)

Fixed-duty and PID-regulated load-step comparisons. [Experiment guide](buck/README.md). Typical run: 1 s at 100 kHz.

| Diagram |
|---|
| [buck open loop load step](buck/buck_open_loop_load_step.s7diagram) |
| [buck pid load step](buck/buck_pid_load_step.s7diagram) |

## Relay thermostat (2)

Normal and narrow hysteresis bands under a cooler-ambient disturbance. [Experiment guide](relay/README.md). Typical run: 1200 s at 10 Hz.

| Diagram |
|---|
| [thermostat](relay/thermostat.s7diagram) |
| [thermostat narrow band](relay/thermostat_narrow_band.s7diagram) |

## Van der Pol oscillator (3)

Nonlinear limit cycles at two damping strengths. [Experiment guide](van_der_pol/README.md). Typical run: 40 s at 1 kHz; mu=3 for 60 s.

| Diagram |
|---|
| [van der pol mu1 euler](van_der_pol/van_der_pol_mu1_euler.s7diagram) |
| [van der pol mu1 symplectic](van_der_pol/van_der_pol_mu1_symplectic.s7diagram) |
| [van der pol mu3 symplectic](van_der_pol/van_der_pol_mu3_symplectic.s7diagram) |

## Lorenz system (2)

Stable and chaotic parameter sets. [Experiment guide](lorenz/README.md). Typical run: 40 s at 4 kHz; stable case 10 s.

| Diagram |
|---|
| [lorenz attractor](lorenz/lorenz_attractor.s7diagram) |
| [lorenz stable](lorenz/lorenz_stable.s7diagram) |

## Predator and prey (4)

Population cycles, equilibrium, nearby state and faster growth. [Experiment guide](lotka_volterra/README.md). Typical run: 40 s at 1 kHz; faster growth at 4 kHz.

| Diagram |
|---|
| [predator prey](lotka_volterra/predator_prey.s7diagram) |
| [predator prey equilibrium](lotka_volterra/predator_prey_equilibrium.s7diagram) |
| [predator prey faster growth](lotka_volterra/predator_prey_faster_growth.s7diagram) |
| [predator prey near equilibrium](lotka_volterra/predator_prey_near_equilibrium.s7diagram) |

## Phase-locked loop (3)

Frequency step, phase step and out-of-range acquisition. [Experiment guide](pll/README.md). Typical run: 6 s at 1 kHz.

| Diagram |
|---|
| [pll frequency step](pll/pll_frequency_step.s7diagram) |
| [pll out of range](pll/pll_out_of_range.s7diagram) |
| [pll phase step](pll/pll_phase_step.s7diagram) |

## Signal measurement (7)

Coherent AM, FM, RMS steps and bounded live I/Q analysis. [Experiment guide](signal_measurement/README.md). Typical run: 2 s at 10 kHz; I/Q 8 s at 1024 Hz.

| Diagram |
|---|
| [am coherent detection](signal_measurement/am_coherent_detection.s7diagram) |
| [fm frequency detection](signal_measurement/fm_frequency_detection.s7diagram) |
| [iq correlation live](signal_measurement/iq_correlation_live.s7diagram) |
| [rms dc step](signal_measurement/rms_dc_step.s7diagram) |
| [rms sine step](signal_measurement/rms_sine_step.s7diagram) |

## Spectrum (3)

Known tone and off-bin FFT references in recorded and live views. [Experiment guide](spectrum/README.md). Typical run: 4 s at 1024 Hz.

| Diagram |
|---|
| [off bin tone](spectrum/off_bin_tone.s7diagram) |
| [three bin tones](spectrum/three_bin_tones.s7diagram) |
| [two tone sum](spectrum/two_tone_sum.s7diagram) |

## Real surfaces (6)

Plane, bowl, saddle, Gaussian, wave and sinc galleries. [Experiment guide](surfaces/README.md). Typical run: 5 s at 1 kHz.

| Diagram |
|---|
| [bowl](surfaces/bowl.s7diagram) |
| [gaussian](surfaces/gaussian.s7diagram) |
| [plane](surfaces/plane.s7diagram) |
| [saddle](surfaces/saddle.s7diagram) |
| [sinc](surfaces/sinc.s7diagram) |
| [wave](surfaces/wave.s7diagram) |

## Complex surfaces (4)

Identity, square, inverse and rational maps with phase color. [Experiment guide](complex_functions/README.md). Typical run: 5 s at 1 kHz.

| Diagram |
|---|
| [identity](complex_functions/identity.s7diagram) |
| [inverse](complex_functions/inverse.s7diagram) |
| [rational](complex_functions/rational.s7diagram) |
| [square](complex_functions/square.s7diagram) |

## Frequency response (5)

Impulse experiments for gain, delay, RC, RLC and mechanical response. [Experiment guide](frequency_response/README.md). Typical run: 2 s; use each diagram’s saved rate.

| Diagram |
|---|
| [delay](frequency_response/delay.s7diagram) |
| [gain](frequency_response/gain.s7diagram) |
| [mass spring damper](frequency_response/mass_spring_damper.s7diagram) |
| [rc](frequency_response/rc.s7diagram) |
| [rlc](frequency_response/rlc.s7diagram) |

## Time–frequency (8)

STFT and Morlet CWT examples for tones, transients and a PLL. [Experiment guide](time_frequency/README.md). Typical run: 8 s at 1024 Hz; PLL 6 s at 1 kHz.

| Diagram |
|---|
| [chirp](time_frequency/chirp.s7diagram) |
| [impulse](time_frequency/impulse.s7diagram) |
| [overlapping tones](time_frequency/overlapping_tones.s7diagram) |
| [pll acquisition](time_frequency/pll_acquisition.s7diagram) |
| [resonant chirp](time_frequency/resonant_chirp.s7diagram) |
| [ring down](time_frequency/ring_down.s7diagram) |
| [tone](time_frequency/tone.s7diagram) |
| [tone bursts](time_frequency/tone_bursts.s7diagram) |

## In-graph analysis (2)

Fourier and wavelet coefficients computed by optional plugins. [Experiment guide](analysis_elements/README.md). Typical run: 2 s at 1024 Hz.

| Diagram |
|---|
| [fourier coefficient](analysis_elements/fourier_coefficient.s7diagram) |
| [wavelet coefficient](analysis_elements/wavelet_coefficient.s7diagram) |

**Total: 123 diagrams: 99 general examples and 24 teaching labs.**

## Headless JSON examples

The [runtime example guide](../s7runtime/examples/README.md) covers all twelve
JSON schemas and libraries, including two-node network pairs, gnuplot output,
nested schemas, vector wires and optional in-graph analysis plugins.

## Audio

- [Live microphone spectrum](audio/microphone_spectrum.s7diagram) and [run guide](audio/microphone_spectrum.md)
- [Live SDR spectrum](sdr/live_spectrum.s7diagram) and [run guide](sdr/LIVE_SPECTRUM.md)

## Software-defined radio

See the [SDR examples](sdr/README.md).

- [SoapySDR stereo FM receiver diagram](sdr/fm_receiver.s7diagram) and
  [tuning and playback guide](sdr/FM_RECEIVER.md).

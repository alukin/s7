# AM/FM generation, detection and RMS measurement

Run the recording diagrams for **2 seconds at 10 kHz**, dt = 0.0001 s.
The live variants below also support continuous inspection:

- [Live coherent AM detection](am_coherent_detection_live.s7diagram): seven bounded
  channels across `message_scope`, `carrier_scope` and `detector_scope`. Use 0.02 s
  to inspect carrier cycles, or 0.5–1 s for the envelope/message.
- [Live FM frequency detection](fm_frequency_detection_live.s7diagram): seven
  bounded channels across `message_scope`, `frequency_scope`, `waveform_scope` and
  `validity_scope`. Use 0.02–0.05 s for the waveform and 0.5 s for frequency tracking.

The live variants keep the same functional graphs, replace recording observers with
Live Oscilloscope, and remove standalone recorded probes. Units are supplied by
observer channel metadata. Run Configuration should report **7 live, 0 recording**
channels. Select **Run continuously until stopped**, then double-click an observer.
Hover shows raw sample values; Freeze holds only the display. Acquisition settings
can change during the run. See [prototype help](../../docs/LIVE_OSCILLOSCOPE_PROTOTYPE.md).
The original four diagrams remain the full-recording numerical-reference examples.

For XY and correlation, open [IQ circle and causal delay](iq_correlation_live.s7diagram)
and run **8 seconds at 1024 Hz** (3 live channels, no recording). In `iq_scope`,
select Constellation / XY with I and Q to see a unit circle. For Correlation,
select A=I, B=I_delayed_6, N=4096 and maximum |lag|=16: the peak is +6 samples,
or 0.005859375 s. Six explicit Delay elements produce the delay. See
[analysis help](../../docs/STUDIO_PAIR_ANALYSIS.md) for normalization, persistence,
density, freeze and inactive-view behavior. This example has a separate rate
from the AM/FM recording examples above.

Use **Pin window / A–B** or click the plot to freeze a displayed input. In Time,
A=0 and B=4095 select 4096 retained positions; **Measure A–B** gives I/Q RMS near
`1/sqrt(2)` V, independently of display gain/offset. Try cursors in all four tabs,
then **Export frozen window…** as metadata-rich window JSON, raw CSV or displayed
data CSV. See [inspection/export help](../../docs/STUDIO_WINDOW_INSPECTION.md).
For [stable Time synchronization](../../docs/STUDIO_TRIGGERING.md), open `iq_scope`
before running, retain 128 samples, and choose Normal, source I, Rising, level
0.25, hysteresis 0.02 and 25% pre-trigger. At 1024 Hz the 16 Hz waveform stays
aligned to its sampled edge at T=0. Single holds one capture; Rearm waits for a
new one while raw acquisition continues. Stopping during post-trigger collection
preserves a labeled partial frame.

| Recording diagram | Exercise |
|---|---|
| [Coherent AM detection](am_coherent_detection.s7diagram) | Carrier/envelope and recovered message |
| [FM frequency detection](fm_frequency_detection.s7diagram) | Instantaneous frequency versus crossing-period estimate |
| [Sine RMS amplitude step](rms_sine_step.s7diagram) | 50 Hz sine amplitude 2 -> 1 V at 1 s |
| [Signed DC RMS step](rms_dc_step.s7diagram) | -2 -> +1 V at 1 s |

**Experimental teaching diagrams.** Headless checks cover the envelopes below.
Manual Studio open/run/observe/nested edit/rerun remains unqualified. All graphs
use existing primitives and editable bundled schema components.

## AM: synchronous coherent detector

```
message m(t) = sin(2*pi*2*t)
carrier c(t) = sin(2*pi*100*t)
envelope = 1 + 0.6*m(t)
signal = envelope*c(t)                    V
mixer = 2*signal*c(t)
filtered' = (mixer-filtered)/0.02
recovered = (filtered-1)/0.6
```

The receiver deliberately uses the **same coherent carrier** as the transmitter;
carrier recovery is not assumed to occur automatically. The filtered state
starts at zero. A bundled `FirstOrderLag` performs the low-pass operation with
tau = 0.02 s. There are 100 model samples per carrier cycle.

Open `message_scope` for original/recovered dimensionless messages, `carrier_scope`
for the AM voltage, envelope and carrier, and `detector_scope` for mixed and
filtered voltages. Expect startup (initial recovered value -1/0.6), then a delayed,
slightly attenuated message with residual twice-carrier ripple. These are
filtering effects, not perfect envelope reconstruction.

Change modulation index, carrier frequency or filter tau consistently. The index
0.6 keeps the envelope positive. Overmodulation crosses zero and invalidates a
simple envelope-detector interpretation; coherent detection has a different
contract. This exercise uses synchronous mixing, not diode/absolute-value
envelope detection. To study incoherent reception, add a separate local carrier
and explicitly choose its frequency/phase errors.

## FM: VCO plus crossing-period detection

```
message = sin(2*pi*2*t)
f_command = 100 + 20*message              Hz
phase[n+1] = phase[n] + 2*pi*f_command[n]*dt
signal[n] = sin(phase[n])                 V, peak amplitude 1
raw_message = (measured_frequency-100)/20
smoothed' = (valid_message-smoothed)/0.02
```

ControlledOscillator generates the FM signal, initially at phase zero.
FrequencyMeter measures rising-crossing intervals with linear interpolation.
A Switch supplies zero to the detector filter until the second crossing has
made the estimate valid; otherwise an uninitialized zero-Hz reading would
look like a false message of -5. The smoothing state starts at zero.

Open `message_scope` for the message, raw valid estimate and smoothed estimate;
`frequency_scope` for commanded/measured Hz; `waveform_scope` for FM voltage;
and `validity_scope` for initialization status. The carrier frequency spans
80–120 Hz. The meter reports an interval-average frequency held between
crossings, **not instantaneous frequency**. Its lag plus low-pass smoothing
causes message phase delay and transient error. Validity stays set after
acquisition; it does not certify continuing crossings or provide a timeout.

Try smaller frequency deviation, a slower/faster message, or different filter
tau. Keep instantaneous frequency positive and well resolved by the model rate.
Rapid FM, multiple crossings per step, noise and signal loss are outside the
checked envelope. No discriminator or PLL is hidden inside FrequencyMeter.

## RMS: filtered mean square, then square root

Both RMS diagrams use an editable bundled `RMSDetector`:

```
q' = (signal*signal-q)/0.05       V^2/s
rms = sqrt(q)                   V
q(0) = 0
```

This is an **exponentially weighted RMS**, not a rectangular-window RMS or an
instantaneous magnitude. For a sine the settled target is |A|/sqrt(2); for DC
it is |A|. `rms_scope` compares measured RMS with that steady target, while
`source_scope` shows signed voltage/amplitude. Targets jump immediately at 1 s;
the measured result must settle through the filter. The DC case shows that
negative voltage still has positive RMS. The sine result retains small
twice-frequency ripple, since tau is finite.

Double-click `rms` to inspect its square/filter/square-root graph. Change tau
using `rms_tau`; keep tau comfortably above dt. Smaller tau follows changes
faster but increases ripple. Change sine frequency and check that the filter
still averages enough cycles. These diagrams omit quantization and instrument
bandwidth/calibration errors.

## Reference checks, refinement and timing

From the repository root:

```bash
python3 tools/generate_measurement_examples.py
cmake -S . -B build
cmake --build build --target s7tests_measurement_examples_smoke --parallel 2
ctest --test-dir build -R '^s7tests_measurement_examples_smoke$' --output-on-failure
```

The test loads all four native graphs unchanged, then repeats at 20 kHz.
It checks input equations, filter recurrences, frequency-meter crossing/validity
semantics and timestamps. No independent solver is added to runtime execution.

- AM low-pass output is checked against a closed-form response to the DC,
  message and mixer sideband terms. Maximum voltage error must be below 0.02 V;
  recovered-message RMS error after 0.3 s must be below 0.25.
- FM waveform is checked against independently integrated discrete phase and
  the continuous formula `phase=200*pi*t+10*(1-cos(4*pi*t))`. Maximum continuous
  waveform error must be below 0.02 V. Independent interpolated crossings must
  match the meter within 1e-6 Hz. After 0.3 s, measured versus instantaneous
  frequency error must stay below 4 Hz and smoothed-message RMS error below 0.3.
- RMS output is checked against independent discrete mean-square recurrences
  and closed-form filtered-square responses, including the step. Maximum
  analytical error must be below 0.03 V; settled error after 1.5 s below 0.02 V.

AM filter, FM continuous-waveform and RMS analytical errors must decrease by
a factor of 1.7–2.3 on timestep halving. Refinement does not remove intended
filter lag or convert a crossing-period average into instantaneous frequency.

For a Studio 20 kHz repeat, use dt = 0.00005 throughout embedded schemas and
source rates. **Double RMS amplitude-step start_sample from 10000 to 20000**
to keep the event at 1 s. Run Configuration updates rates, not event sample
counts. AM/FM have no event counts to rescale.

At runtime step k, causal filter states represent `(k-1)*dt` and the observation
label is `k*dt`. Sine/VCO outputs use the phase for that same source sample,
then advance phase. First filter publication is its zero initial state.
References use physical sample time `(k-1)*dt`; a two-second run's last such
sample is `2-dt`. Different signal/filter stages have documented causal
publication, so a same-step source change affects a filter's next published state.

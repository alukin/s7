# SP07 — Which filter resonates?

**50 minutes · Intermediate · Prerequisites: [FN04](../FN04-impulse-transfer/README.md),
[FN05/FN06](../FN05-integration-rules/README.md), [CT02](../CT02-damped-modes/README.md).**
Current Linux development build; no hardware or plugin required.
Status: `diagram-ready`; [development evidence](../../EVIDENCE.md) records
checks separately from release qualification.

**Your question:** does the measured response match the actual discrete filter?
Predict each filter's startup, compare their measured complex responses,
change damping and distinguish numerical instability from physical resonance.

## Two physical models, explicit local states

Open **Help → Education: start here → sp07_filter_transfer.s7diagram**.
The new graph builds two models from Gain/Adder/EulerIntegrator networks:

```text
Low-pass:   dx/dt = 2(u−x),       x(0)=0 V
Resonant:   dq/dt = v,
            dv/dt = 16(u−q)−b v, q(0)=0 V, v(0)=0 V/s
b=1.6 per second; natural frequency ω0=4 rad/s; damping ratio ζ=b/(2ω0)=0.2
```

The input and filter outputs are F64 voltages. Internal rate/acceleration
signals are V/s and V/s². Three explicit integrators provide causal memory;
both resonant states update from the current pair, so this is forward Euler,
not sequential Euler–Cromer or an implicit solver. There is no special plant
or global ODE solver hidden behind the drawing.

Click **Run** and keep **64 s at 64 Hz**. This means **4096 model samples**,
not a requirement to wait 64 wall-clock seconds. The long record allows the
resonant tail to decay before frequency analysis. The source emits one
**1 V sample at n=8**, with start_time=0.125 s and area=1/64 V·s.
It is a unit discrete sample, not a unit-area continuous impulse.

## Predict the first nonzero outputs

| n | Scope time (s) | Input (V) | Low-pass (V) | Resonant (V) |
| --- | --- | --- | --- | --- |
| 8 | 0.140625 | 1 | 0 | 0 |
| 9 | 0.15625 | 0 | 0.03125 | 0 |
| 10 | 0.171875 | 0 | 0.0302734375 | 0.00390625 |

Open **Input pulse** and **Filter outputs: inspect 0–10 s**. Use **0–10 s**
for the output plot, and zoom further near the pulse to inspect startup.
Separate Scopes prevent the 1 V input from hiding the smaller responses.
Both output channels use volts and share a scale. For a dense trace, Scope
may draw a pixel envelope retaining extrema and endpoints; the full recording
still contains all 4096 samples. Use recorded values for numerical checks. The low-pass decays
without ringing; the resonant channel oscillates and decays.

At step k, Scope time is k/F_s and the stored state is x[k−1] or q[k−1].
Subtract h=1/64 s when comparing a displayed state with physical state time.
The low-pass responds one calculation after the input; the resonant voltage
responds two calculations later because the input first changes v, then q.
These stages belong in the discrete transfer function.

## Derive the actual discrete transfer

Let h=1/F_s and z=exp(j2πfh). From the drawn forward-Euler updates:

```text
x[n+1] = (1−2h)x[n] + 2h u[n]
q[n+1] = q[n] + h v[n]
v[n+1] = −16h q[n] + (1−b h)v[n] + 16h u[n]

H_low(z) = 2h / [z−(1−2h)]
H_res(z) = 16h² / [(z−1)(z−1+b h)+16h²]
```

Writing these in z⁻¹ exposes one and two leading sample delays respectively.
Both DC gains are **1**. At the saved rate, the resonant gain peaks at about
**2.99043 near 0.625 Hz**, not exactly the continuous model's peak.
Frequency f is Hz; ω=2πf is rad/s. Do not enter ω0=4 as a frequency in Hz.

Double-click **Low-pass frequency response** and **Resonant frequency response**.
Completed captures analyze automatically; **Analyze captured impulse** can
request analysis explicitly. Blue curves are measured discrete responses.
Keep **Gain dB reference = 1**: magnitude is `20 log10 |H|`, dimensionless V/V.
The Bode x coordinate is **log10(f / 1 Hz)**: coordinate 0 means **1 Hz**,
coordinate −1 means 0.1 Hz. DC is available in the inspection control and
result, but cannot be plotted on a logarithmic frequency axis. The padded
transform has 4096 points, spacing **1/64 Hz**, Nyquist **32 Hz**.

The default phase is unwrapped: the low-pass tends to **−180°** at Nyquist;
the resonant filter tends to **−360°**. Turning off **Unwrap phase** changes
representation, not H. The continuous analytical-reference checkbox is a
separate model: do not mistake its orange curve for an exact discrete oracle.

For an optional physical comparison in the **resonant** viewer, enable
**Analytical second-order reference**, then set its **ω0=4 rad/s**,
**Damping ratio=0.2**, **DC gain=1**. Enabling the reference unlocks these fields. The orange curve is
G_res(s), while blue is the measured discrete H_res(z). After the damping
edit below, use **Damping ratio=0.4** for that comparison. The default
reference fields describe a different model; enter these values explicitly.
This second-order reference is unsuitable for the first-order low-pass.
The GUI test checks both orange and blue curves independently.

## Change damping, not the legend's meaning

Select **Damping coefficient: edit factor** (`damping`), press **F4**, change
**factor 1.6 → 3.2**, save a copy, reopen and rerun. This changes ζ from
0.2 to 0.4, not to 3.2. Predict less ringing and a lower resonant peak:
about **1.44494 near 0.546875 Hz**. The low-pass response and both DC gains
stay unchanged. Role-based viewer names remain accurate after the edit.
Use F4 to inspect the actual coefficient; teaching labels are not live formulas.

## Continuous limit and a deliberate failure

The physical transfer functions are
`G_low(s)=2/(s+2)` and `G_res(s)=16/(s²+b s+16)`.
They are limits of the discrete formulas as h→0, not equal curves at every
sample rate. Restore b=1.6, change Run frequency **64 → 128 Hz**, retain 64 s.
The source area remains 1/64 V·s, so its one-sample height becomes **2 V**
and its sample index becomes **16**. Dividing output by the recorded pulse
height still gives the correct transfer; input normalization matters.
The headless test verifies this higher-rate graph and its transfer, with
roughly halved complex error at 0.5 Hz against each continuous reference.
At 8192 paired samples, automatic completed-capture analysis remains supported.

As a failure experiment, set the rate to **8 Hz** (h=0.125 s), retaining
b=1.6 and 64 s. The physical model is still stable, but forward Euler's
resonant pole modulus is `sqrt(1−b h+16h²)=sqrt(1.05)>1`: the numerical
response grows. The low-pass pole is 0.75 and remains stable. This failure
is verified headlessly; its frequency plot must not be interpreted as a
steady stable-system transfer. For the resonant model, Euler decay requires
**0<h<b/16**, so the saved h=1/64 is inside that envelope.

A short record can also bias a stable filter's frequency estimate when its
tail has not decayed. More zero padding interpolates the finite record; it
does not restore a missing tail. The headless test truncates the stable
saved trace to 2 s and confirms that its resonant DC estimate differs from
1 by more than 0.05, despite correct execution. Preserve the saved 64 s record for the
stated frequency comparisons. Linear plot segments do not reconstruct a
continuous signal, and these software sources do not model an ADC.

## Independent runtime and viewer checks

The [headless test](../../../s7tests/education_lti_smoke.cpp) production-loads
this [saved graph](sp07_filter_transfer.s7diagram),
checks every input/state against independent geometric/complex-mode powers,
a fresh repeat, damping edit, rate refinement and unstable contrast.
Separate direct complex sums Y/X at selected frequencies are compared with
the rational H above, independently of the viewer FFT.

```bash
cmake --build build-release \
  --target s7tests_education_lti --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_lti$' --output-on-failure -V
```

F64 sample budget: `1e−10 × max(1, |reference|)` in each channel's units;
time budget 1e−12 s with integer-nanosecond rounding. Complex transfer budget:
1e−9 V/V. GUI checks separately verify recorded/plotted samples, names,
units, time axes, every measured frequency bin, Bode dB scaling and wrapped/
unwrapped phase against independent formulas, including save/reopen/rerun.
Source-code links require the checkout; the guide is bundled offline.
These are bounded Linux development checks; installed packages, native
Windows and final release qualification remain open.

## Reading and next steps

- [MIT 6.003 lecture 9 — Frequency response](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec09/).
- [MIT 6.003 lecture 11 — Continuous-time frequency response and Bode plots](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec11/).

Public 2011 resource pages checked 2026-11-19. This original S7 model uses
independently derived equations; it is topic-aligned and does not reproduce
a named MIT problem or imply endorsement. No PDF or solution key is bundled.
Diagrams/generator: CC0-1.0; guide: CC-BY-4.0.

Previous: [SP06](../SP06-three-tap-fir/README.md).
Continue to [SP08 — DFT leakage](../SP08-dft-leakage/README.md) and
[SP09 — window normalization](../SP09-window-normalization/README.md).
[Return to the learning route](../../index.md).

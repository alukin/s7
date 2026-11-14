# Impulse frequency-response laboratory

Run each diagram for **2 model seconds at 1 kHz**. Open `response`; these small
completed records are analyzed automatically. The blue **Analyze captured impulse**
button remains available for explicit analysis. The dock preview shows measured
gain from the same impulse capture. The input
is a one-sample pulse with area 1 (height 1000), delayed by 0.05 model seconds.
Analysis divides the output by the actual pulse height and retains plant delay.
Open the separate excitation and output scopes first: they show the causal
time-domain experiment without a tall input pulse hiding the plant response.
The RLC and mass–spring examples use **1024 Hz**, as marked on their sheets.

- `gain.s7diagram`: magnitude 2.5 at every frequency and zero phase.
- `delay.s7diagram`: unit magnitude and phase -3*2*pi*f/1000 radians (wrapped).
- `rc.s7diagram`: FirstOrderLag with tau=0.1 seconds. The executed Euler graph
  has H(z)=0.01*z^-1/(1-0.99*z^-1). Compare its low-frequency response with
  the continuous reference 1/(1+0.1*s); the two differ near the sample band edge.

The window links Bode, Nyquist, Nichols and a 3D frequency/Re/Im trajectory.
The Bode horizontal coordinate is explicitly log10(f / 1 Hz); DC is omitted
there but remains available in Nyquist and inspection. The gain dB reference
is editable and explicit. A frequency selector marks the same bin in each
view. CSV preserves complex response, excitation normalization and run provenance.
Phase is unwrapped by default; turn off **Unwrap phase** to compare principal
phase and its branch cuts. The delay example reaches −540° at 500 Hz.

Assumptions: LTI system, zero initial state, gap-free uniform sampling, and a
capture long enough for the response to decay. The viewer labels the result a
finite-record estimate; it does not infer stability or promise a complete tail.
The current capture limit is 65,536 paired samples. Invalid experiments report
an error rather than silently windowing or coercing data.

For an independent parameter experiment, run:

```sh
./build-release/s7runtime/s7analysis_lab /tmp/damping_surface.csv
```

This executes nine fresh `SecondOrderLowPass` schema instances at damping
ratios 0.1 through 1.7, omega0=2π·5 rad/s, 1024 Hz, 8192 samples each. Every
frequency response is checked against an independent discrete transfer formula.
Open a Real Surface observer and choose Open result to load the generated CSV.
X is frequency, Y is damping ratio, height is output/input gain. The metadata
preserves all nine experiment identities and configurations.

In the response window, enable **Analytical second-order reference** and set
omega0/damping explicitly. Orange curves show the continuous reference; they
are not inferred from the captured graph. The Analytical landscape tab shows
G(s)=omega0²/(s²+2*zeta*omega0*s+omega0²), with the imaginary-axis path drawn
on the surface. The shared frequency selector marks the nearest landscape cell
on that path. A frequency outside the sampled landscape hides its marker.
The landscape uses log(1+|G|) height so poles do not flatten the rest of the
surface. The reference's editable DC gain scales both landscape and curves.
Saved response results preserve
complex values, pulse normalization, units and provenance.

Additional physical second-order examples run **2 seconds at 1024 Hz**:

- `rlc.s7diagram`: capacitor-voltage response with L=1 H,
  C=1/(2π·5)² F, R=2·0.3·(2π·5) Ω. DC voltage gain is one. Enable the
  analytical second-order reference at omega0=2π·5, damping=0.3 to compare
  the sampled graph against its continuous reference.
- `mass_spring_damper.s7diagram`: force-to-displacement response with mass
  1 kg, stiffness (2π·5)² N/m and damping 2·0.3·(2π·5) N·s/m. Its DC gain is
  1/(2π·5)² m/N. The explicit force-scale Gain accounts for this physical
  calibration before the reusable normalized second-order graph.

Both should show a decaying oscillatory impulse response and a resonant
frequency-response peak. Their shapes agree after scaling by stiffness;
physical input/output units remain distinct. The continuous normalized
reference defaults to unit DC gain; set its DC gain to 0.001013212 when
comparing this displacement example.

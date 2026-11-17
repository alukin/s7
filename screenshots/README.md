# S7 Studio in action

Six screenshots from the current S7 source: three bundled examples covering
nonlinear dynamics, feedback control and signal processing, plus the integrated
mathematical help. These are actual Studio windows and runs using its bundled
causal graph runtime; no separate runtime service is needed. The 0.1.2 release
archives are an earlier preview; these captures include the newer block shapes.

To reproduce them, choose **File → Example Diagrams…**, open the named diagram,
and set the duration and rate in **Run Configuration**. Double-click an observer
in the diagram to open its window. **Scope** displays recorded signals;
**Live Oscilloscope** provides bounded live acquisition with Time, Spectrum,
Constellation / XY, and Correlation views. A finished run remains available
for inspection.

## 1. Lorenz attractor: a trajectory in three dimensions

![Lorenz attractor in Studio's 3D phase trajectory viewer, with labeled x, y and z axes and two butterfly-shaped lobes.](01-lorenz-attractor.png)

The **3D phase trajectory** viewer plots the three coupled states of the Lorenz
system. The trajectory winds around two lobes, and the red marker identifies
the newest point. Drag to rotate, scroll to zoom, or use XY, XZ and YZ for
projections. This capture uses a zoomed perspective view after the run and
recorded-history loading have both completed.

**Try it:** open `lorenz/lorenz_attractor.s7diagram`, run **40 seconds at
4 kHz**, then open `base_trajectory`. The diagram also contains a second
network with a slightly different initial state for exploring sensitivity to
initial conditions. This is an experimental teaching model; its long chaotic
trajectory is not a claim of pointwise numerical accuracy.

[Model, equations and numerical checks](../s7l2/examples/lorenz/README.md).

## 2. Buck converter: the editable control diagram

![PID-controlled buck converter diagram in Studio, showing the connected power-stage, feedback-controller and observation elements.](02-buck-diagram.png)

The diagram describes an ideal 24 V synchronous buck converter regulated
toward **12 V**. PWM, explicit current and voltage integrators, and an editable
PID schema form the feedback loop. Gain triangles, signed summing circles and
integrator method codes identify the basic operations. Observation elements expose voltage,
current, switching and load signals. The model is assembled from connected
elements; double-click the PID instance to inspect its internal graph.

**Try it:** open `buck/buck_pid_load_step.s7diagram` and run **1 second at
100 kHz**. The load resistance changes from **8 Ω to 4 Ω at 0.5 s**.

## 3. Buck converter: the recorded Scope response

![Recorded Scope showing the buck converter's output voltage and 12 V reference, including startup and recovery after the load change at 0.5 seconds.](03-buck-scope.png)

The **Scope** window shows the recorded output and reference across the run.
Startup brings the output toward 12 V; the heavier load at 0.5 s causes a dip,
followed by recovery. Zoom into the load transition to inspect regulation, or
into a few switching periods to inspect ripple. Scope retains the recorded
data for later inspection and export.

**Open:** `voltage_scope` in the same buck converter diagram. The example is
an experimental ideal converter model with documented numerical checks, not
a hardware design.

[Converter model, controller and experiments](../s7l2/examples/buck/README.md).

## 4. AM detection: original and recovered messages in Live Scope

![Live Oscilloscope Time view comparing the original 2 Hz message with the recovered message from a coherent AM detector.](04-am-live-time.png)

The **Live Oscilloscope** Time view compares the original **2 Hz** message
with the recovered message. A coherent detector mixes the AM signal with the
same **100 Hz** carrier used by the transmitter, then applies a low-pass
filter. Startup, a small delay and residual carrier ripple in the recovered
trace show the filter's behavior.

**Try it:** open `signal_measurement/am_coherent_detection_live.s7diagram`,
open `message_scope`, and run **2 seconds at 10,000 Hz**, its bundled rate. For this capture,
**Analysis retention → Raw samples / channel** was set to **32,768 before
starting**. The example uses seven bounded live channels and no full-run
recording channels. The Time view also supports continuous runs and freezing
the display for inspection.

## 5. AM detection: measuring the carrier spectrum

![Live Oscilloscope Spectrum view of the 100 Hz carrier using a 4096-point periodic Hann window.](05-am-live-spectrum.png)

The same example's `carrier_scope` provides frequency analysis in the
**Spectrum** tab. Selecting the pure `carrier` channel shows the **100 Hz**
carrier with **peak amplitude approximately 1**. The **4,096-point periodic Hann window** at
10,000 Hz gives **2.44140625 Hz bin spacing**; its nearest bin to the carrier
is at **100.09765625 Hz**. The carrier is between bins, so the windowed peak
need not equal its ideal amplitude exactly. The plot shows the useful low-frequency range.
This is the unmodulated carrier channel, whose amplitude is dimensionless.

**Open:** `carrier_scope` → **Spectrum**, then choose
`carrier_scope/carrier [1]`. The `am_V` and `envelope_V` channels are also
available in this window for inspecting modulation in the Time view.

[AM detector and related examples](../s7l2/examples/signal_measurement/README.md)
· [Spectrum controls](../s7l2/docs/STUDIO_SPECTRUM.md)
· [All bundled examples](../s7l2/docs/STUDIO_EXAMPLES.md).

## 6. Mathematical help: block notation and equations

![Studio Help presents the gain, signed sum, product, delay, accumulator and integrator symbols with their MIT and Siebert references.](06-block-symbol-help.png)

Studio's offline help explains the displayed notation, actual recurrences and
initial conditions. The block guide distinguishes S7's current-input accumulator
from Siebert's earlier-input convention and labels each integration method.
The integrator guide covers pole mappings, explicit feedback realizations and
frequency warping, with MIT/Siebert references. Press **F1**, then choose
**Basic block symbols** or **Integrators, delays, and s/z mappings**.

[Integrator guide](../s7l2/docs/INTEGRATORS.md)
· [Block notation and equations](../s7l2/docs/DIAGRAM_SYMBOLS.md).

## Capture provenance and reproduction

Captured on 2026-11-17 from runtime/UI revision `ec6117b` on Fedora 45 with
Qt 6.11.2 and Mesa llvmpipe, using a private virtual display. Capture-only
workflow changes in this update wait for recorded phase history, select the
carrier channel, enlarge windows and set retention/camera framing. The PNGs
come directly from Studio's rendered windows. These examples illustrate
workflows; their linked articles describe the numerical checks and limits.
The capture workflow checks the recorded source for capture loss or storage
failure. Visible notices about skipped preview updates concern screen refresh;
they do not themselves indicate lost recorded samples.

To regenerate the source captures from `s7l2/`:

```sh
cmake --build build-release --target s7studio_gui_workflows_test s7studio_gui_capture --parallel 2
S7_README_SCREENSHOTS=1 S7_GUI_EXAMPLES=0/1 \
S7_USABILITY_CAPTURE_DIR=/tmp/s7-readme-captures \
python3 tools/run_gui_test.py --artifacts /tmp/s7-readme-artifacts \
  --capture build-release/s7studio/s7studio_gui_capture --graphics \
  --screen-size 2200x1400 --timeout 240 \
  -- build-release/s7studio/s7studio_gui_workflows_test \
  bundledExamplesRunAndRender:lorenz/lorenz_attractor.s7diagram \
  bundledExamplesRunAndRender:buck/buck_pid_load_step.s7diagram \
  bundledExamplesRunAndRender:signal_measurement/am_coherent_detection_live.s7diagram \
  helpAndAbout
```

Choose the named viewer captures from that directory; the diagram image is
`buck_buck_pid_load_step-studio.png`, and the help image is
`basic-symbols.png`. Inspect every PNG before replacing the published
files. The test report records run failures, skipped graphics and plot-range
problems; a screenshot alone is not numerical validation.

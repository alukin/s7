# Two-integrator oscillator with Phase Portrait

Open [2int_osc_phase_portrait.s7diagram](2int_osc_phase_portrait.s7diagram)
in Studio. It extends `2int_osc.s7diagram` with a diagram-only **PhasePortrait**
observer, keeping the original computational elements, connections and Scope.

- X input: `eulerintegrator_1`, output 0; initial value 1.
- Y input: `eulerintegrator_2`, output 0; initial value 0.
- Equations: `dx/dt=-y`, `dy/dt=x`.

Set **Run > Run Configuration** to 10 seconds and 1, 10 or 100 kHz. Run locally,
then double-click `phase_portrait_1`. It shows the integrator states against
each other, starting at `(1,0)` and rotating counterclockwise. The existing
Scope continues to show the second integrator against model time.

The continuous solution is `(cos(t),sin(t))`. Forward Euler produces slight
outward drift: after `n` integration updates the radius is
`exp(n*log1p(dt*dt)/2)` and the angle is `n*atan(dt)`. This is a numerical-method
limit, not a plotting error. A causal-memory observation at step `k` publishes
the state after `k-1` updates.

Diagram Phase Portrait observers collect from the first delivered sample even
while hidden. Opening after the run, closing/reopening, or opening before the
run preserves the same trace. Clear/reset clears the state pairs. The viewer
retains at most two million pairs; this is not a persistent full-run recorder.
Mailbox losses remain reported and cannot be recovered by opening a viewer.

## Verified coverage

```bash
cmake --build build --target s7studio
ctest --test-dir build -R '^s7studio_phase_portrait$' --output-on-failure
```

The offscreen integration regression loads the saved diagram and resolves its
observer connections, runs the real bundled worker and MainWindow sample
routing, and examines every plotted `QCPCurve` point. Ten model seconds at
1/10/100 kHz produce 10,000/100,000/1,000,000 paired points respectively. Each
pair must equal the same-step headless integrator outputs exactly and agree
with the independent closed-form Euler recurrence within `1e-10`. It checks
axis mapping, plot ranges, opening before/after execution, closing/reopening,
clear and matching-step pairing in either arrival order. High-rate cases check
late-open rendering; this does not qualify live rendering under arbitrary load.

Optional viewer screenshot from the test:

```bash
QT_QPA_PLATFORM=offscreen S7_PHASE_PORTRAIT_PNG=/tmp/oscillator-phase.png \
  ./build/s7studio/s7studio --phase-portrait-check
```

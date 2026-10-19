# Three-integrator helix with 3D Phase Trajectory

Open [3int_helix_phase_trajectory.s7diagram](3int_helix_phase_trajectory.s7diagram)
in Studio, run for **10 model seconds at 1 or 10 kHz**, and double-click
`phase_trajectory_1`. Opening before the run also works.

This extends the two-integrator oscillator with a third integrator:

- X: first integrator, `dx/dt = -y`, initially 1.
- Y: second integrator, `dy/dt = x`, initially 0.
- Z: third integrator, `dz/dt = 0.2`, initially 0.

The trajectory is a rising helix, drawn as one continuous curve in a rotatable
Qt Quick 3D scene (drag to rotate, scroll to zoom; a red marker shows the newest
point). The **XY** button looks down the Z axis at an almost circular orbit;
**XZ** and **YZ** show the oscillating states against increasing height; **3D**
returns to the default view. The existing Scope shows Y against model time.
Auto Scale fits each axis to the data.

The continuous solution is `(cos(t), sin(t), 0.2*t)`. Forward Euler causes
slow outward radial drift. At runtime step `k`, causal-memory outputs publish
the state after `n=k-1` updates: radius `exp(n*log1p(dt*dt)/2)`, angle
`n*atan(dt)`, and height `0.2*n*dt`.

The viewer collects same-step triples before opening. Closing/reopening keeps
the trace; Clear or starting a new run clears it. It retains at most two million
triples. Mailbox losses are reported; retained history is not persistent storage.

## Verification

```bash
cmake --build build --target s7studio
ctest --test-dir build -R '^s7studio_phase_trajectory_3d$' --output-on-failure
```

The test loads the diagram, checks the observer wiring, runs the bundled worker
through MainWindow routing, and compares every recorded triple with same-step
headless states exactly and the discrete analytical solution within `1e-10`.
It checks that the trajectory geometry is one line strip with a vertex for each
triple at its scaled position inside the frame, the axis labels, opening
before/after the run, closing/reopening, clearing and out-of-order coordinates,
for ten seconds at 1/10 kHz. Offscreen, Qt Quick 3D draws nothing, so the
rendered picture is checked by hand (see the manual test plan in
[STUDIO_OBSERVATION_VIEWERS.md](../docs/STUDIO_OBSERVATION_VIEWERS.md)). This
does not establish an arbitrary live-rendering throughput limit.

Optional screenshot (on a desktop session; offscreen leaves the 3D view empty):

```bash
S7_PHASE_TRAJECTORY_3D_PNG=/tmp/helix-3d.png \
  ./build/s7studio/s7studio --phase-trajectory-3d-check
```

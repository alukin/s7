# Starter and standalone diagrams

Open a diagram with **File → Example Diagrams…** in Studio. Use **Run
Configuration** to set the duration and the rate shown below; the diagram's
saved `dt` is the default rate. Double-click the named observer after starting
the run. Observers read the causal graph and do not feed values back into it.

| Diagram | Run and observer | What to inspect |
|---|---|---|
| [Two-integrator oscillator](2int_osc.s7diagram) | 10 s at 1 kHz; `scope_1`, `live_1` | The two states form an oscillator. Forward Euler causes slowly growing amplitude; inspect the recorded trace in `scope_1`, or `v` in the bounded live window of `live_1` (exact sample values; see the [live oscilloscope guide](../docs/LIVE_OSCILLOSCOPE_PROTOTYPE.md)). Compare with the Gauss or symplectic variant. |
| [Gauss oscillator](2int_osc_gauss.s7diagram) | 10 s at 1 kHz; `scope_1` | The same connected state equations use coupled fourth-order Gauss–Legendre integration. Compare amplitude drift with forward Euler. The temporary solver stages are internal to the integrator, not extra graph states. |
| [Symplectic live oscillator](symplectic_osc_live.s7diagram) | 10 s at 1 kHz; `live_1` | Euler–Cromer keeps the position/velocity orbit bounded over a longer interval; compare with the forward-Euler live oscillator. |
| [Editor demonstration](demo1.s7diagram) | 10 s at 1 kHz; `scope_1` | A 2 Hz oscillator, seeded white noise, summation and an explicit Euler integrator show basic element and wire editing. Requires `s7noise`. |
| [Triggered noise demonstration](demo_diagram2.s7diagram) | 10 s at 1 kHz; `scope_1` | Open the Live Oscilloscope, adjust the rising-edge trigger level and hysteresis, and compare the noisy sum with the separate noise channel. Requires `s7noise`. |
| [Small-angle double pendulum](double_pendulum_small_angle.s7diagram) | 20 s at 1 kHz; `scope_angles` | Two coupled linearized angle/velocity pairs use explicit Euler integrators. Inspect both angle channels and compare normal-mode behavior with the [qualified family](double_pendulum/README.md). This standalone layout is an experimental demonstration. |
| [Full double pendulum](double_pendulum_full.s7diagram) | 20 s at 1 kHz; `scope_states` | The nonlinear acceleration terms are built from trigonometric, product, sum and gain elements in nested subsystems (angle functions, denominator, θ1 and θ2 acceleration); double-click one to open it. Follow both angles and velocities; the sensitive trajectory is illustrative, not a long-time accuracy reference. |

The [2D phase portrait](2int_osc_phase_portrait.md), [3D helix](3int_helix_phase_trajectory.md)
and [PWM/PID/RLC converter](pwm_pid_rlc.md) have their own experiment notes.
The [examples index](README.md) links every bundled diagram and its family guide.

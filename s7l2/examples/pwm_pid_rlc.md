# PWM voltage PID regulator with a changing RLC load

Open [pwm_pid_rlc.s7diagram](pwm_pid_rlc.s7diagram) in Studio.
Set **Run > Run Configuration** to **1 second at 100 kHz** (10 µs
model step). Double-click the four scopes to inspect voltage, switching,
currents and load resistance. Zoom into a few milliseconds to see PWM ripple.
Keep this sample rate: the oscillator and PWM declare 100 kHz, and the
controller's discrete derivative coefficients assume that rate.

The power stage is an ideal synchronous buck: a 24 V switch drives a
10 mH inductor with 0.2 Ω winding resistance, followed by a 1000 µF
capacitor and a changing resistor in parallel. The resistance varies as
`R(t) = 8 + 4 sin(4πt)` Ω, covering 4–12 Ω twice per second.
Both inductor current and capacitor voltage start at zero.

```
12 V reference -> error -> PID -> duty limiter -> PWM -> 24 V switch -> L -> output
                    ^                                                    |
                    +---------------- voltage feedback ------------------+
                                                                         |
                                                           C || R(t) -> return
```

The diagram expands the electrical state equations into ordinary blocks:

- `diL/dt = (24*gate - vC - 0.2*iL) / 0.01`
- `dvC/dt = (iL - vC/R(t)) / 0.001`

Euler integrators provide causal memory in the feedback loops. No extra
unit delay is inserted into the electrical equations.

The parallel PID uses `Kp=0.04`, `Ki=8`, `Kd=0.0004`, with a 2 ms
first-order derivative filter. Derivative acts on measured voltage to
avoid reference-step kick. Its backward-Euler discrete transfer function is
`Kd*(1-z^-1)/(tau+dt-tau*z^-1)`. The integral state integrates
`Ki*error + 100*(limited_duty - unlimited_duty)` for back-calculation
anti-windup. Duty is limited to [0,1].

The core `PWM` element emits numeric 0/1 using trailing-edge modulation.
It samples duty at the beginning of each 100-sample carrier period and
holds it for that period, giving 1 kHz switching and 1% duty resolution.
Values outside [0,1] are clamped; nonfinite sampled duty raises an error.

This is a simulation of an ideal synchronous stage, allowing reverse
inductor current. It omits diode drops, switching losses, dead time,
current limiting and component saturation. The changing resistor models
load disturbance; L and C remain fixed. Output ripple is expected.

Validation:

```bash
cmake --build build --target s7core_pwm_smoke
ctest --test-dir build -R '^s7core_pwm_smoke$' --output-on-failure
```

The test loads the actual diagram's computational schema, validates and
compiles it, and runs one second of switching simulation. It checks
regulation, bounded startup/ripple and the full resistance range. PWM tests
cover all four real floating signal types, carrier-boundary latching,
clamping and invalid input.

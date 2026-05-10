# s7main examples

## I/O examples for Phase 2 preparation

Files:

- `io_file_real_sink.json`
- `io_mem_real_sink.json`
- `io_net_real_loopback_to_file.json`

These models are intended to exercise the currently registered `s7io` modules
through the same netlist path used by runtime control-plane commands.

- file model: `Constant -> RealFileSink`
- mem model: `Constant -> RealMemSink`
- net loopback model: `Constant -> RealNetSink` and `RealNetSource -> RealFileSink`

Run helper:

```bash
./s7main/examples/run_io_examples.sh
```

Optional explicit binary path:

```bash
./s7main/examples/run_io_examples.sh /home/al/projects/s7/s7pp/build_verify/s7main/s7main
```

Control dispatcher smoke test (stdio mode):

```bash
./s7main/examples/control_stdio_smoke.sh
```

## Classical oscillator (2 integrators)

File: `classical_oscillator_2integrators.json`

This netlist models a classical harmonic oscillator with two state variables:

- `x' = v`
- `v' = -x`

Discrete-time Euler form used by this schema:

- `x[n+1] = x[n] + dt * v[n]`
- `v[n+1] = v[n] - dt * x[n]`

Implementation mapping:

- `int_x` and `int_v` are the two `Integrator` blocks (state memory).
- `gain_v_to_xdot` is `Sum(weights=[dt])`.
- `gain_x_to_vdot` is `Sum(weights=[-dt])`.

Current parameters:

- `dt = 0.001`
- `x(0) = 1.0`
- `v(0) = 0.0`


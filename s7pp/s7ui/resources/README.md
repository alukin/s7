# s7ui Resources

This directory contains schema JSON examples aligned with scenarios covered by `s7tests`.

## Files

- `fir_impulse_response.json` - `ImpulseSource -> FIRFilter`
- `iir_step_response.json` - `Constant(step) -> IIRFilter`
- `oscillator_agreement.json` - `HarmonicOscillator` vs `DigitalOscillator` into `EnergyDifference`
- `classical_oscillator_2integrators_test.json` - two-integrator oscillator compared against `HarmonicOscillator`
- `netio_real_loopback.json` - `Constant -> RealNetSink` loopback with `RealNetSource`
- `thread_pool_run_agreement.json` - small graph used to compare sequential and pool-backed execution


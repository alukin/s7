# New education diagrams

These graphs are created specifically for S7 teaching laboratories. Earlier
examples elsewhere in `examples/` are implementation references. Open a diagram
through **File → Example Diagrams… → education** and set its stated run profile.

| Lab | New diagram | Run | Guide |
| --- | --- | --- | --- |
| FN01: one sample of memory | [fn01_unit_delay.s7diagram](fn01_unit_delay.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN01-unit-delay/README.md) |
| FN02: natural and forced response | [fn02_natural_forced.s7diagram](fn02_natural_forced.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN02-natural-forced/README.md) |
| FN03: initial conditions | [fn03_initial_conditions.s7diagram](fn03_initial_conditions.s7diagram) | 2 s at 10 Hz | [Student lab](../../education/labs/FN03-initial-conditions/README.md) |
| FN04: from one pulse to a transfer function | [fn04_impulse_transfer.s7diagram](fn04_impulse_transfer.s7diagram) | 10 s at 20 Hz | [Student lab](../../education/labs/FN04-impulse-transfer/README.md) |
| FN05: three rules for the same decay | [fn05_integration_rules.s7diagram](fn05_integration_rules.s7diagram) | 6 s at 2 Hz | [Student lab](../../education/labs/FN05-integration-rules/README.md) |
| FN06: stable does not mean accurate | [fn06_pole_maps.s7diagram](fn06_pole_maps.s7diagram) | 30 s at 0.333333 Hz | [Student lab](../../education/labs/FN06-pole-maps/README.md) |
| CT01: a capacitor takes time to charge | [ct01_first_order.s7diagram](ct01_first_order.s7diagram) | 5 s at 10 Hz | [Student lab](../../education/labs/CT01-first-order/README.md) |
| CT02: how damping changes a spring | [ct02_damped_modes.s7diagram](ct02_damped_modes.s7diagram) | 8 s at 100 Hz | [Student lab](../../education/labs/CT02-damped-modes/README.md) |
| CT03: can a slow sample rate invent growth? | [ct03_sample_time.s7diagram](ct03_sample_time.s7diagram) | 10 s at 2 Hz | [Student lab](../../education/labs/CT03-sample-time/README.md) |
| SP06: three-tap FIR | [sp06_three_tap_fir.s7diagram](sp06_three_tap_fir.s7diagram) | 4 s at 300 Hz | [Student lab](../../education/labs/SP06-three-tap-fir/README.md) |

## First time with S7? Start here

1. **FN01:** count four samples and follow the one-sample memory around a loop.
2. **FN02:** compare a remembered value, one input sample and a sustained input.
3. **FN03:** separate the contribution of initial state from that of the input.
4. **FN04:** connect a pulse response with its complex transfer function.
5. **FN05/FN06:** compare numerical rules, then separate stability from accuracy.
6. **CT01–CT03:** explore charging, damped motion and the effect of sample rate.
7. **SP06:** apply explicit delays to a small signal-processing filter.

Each file saves the run configuration in the table. FN01–FN03 use
**2 s / 10 Hz**. Click Run and keep the saved settings; the short-run notice is intentional. Double-click
**Compare the named signals** to open Scope. Each yellow description frame
links to an offline lab guide and MIT online readings. Predict the first
samples before running, then follow the guide's one-parameter experiment.

Headless check: build `s7tests_education_foundations`, then run
`ctest --test-dir build-release -R '^s7tests_education_foundations$' -V`.
Reproduce the three new graphs with
`python3 -B examples/education/generate_foundations.py` from `s7l2/`.

## Transfer, numerical methods and physical plants

Continue from FN03 to [FN04](../../education/labs/FN04-impulse-transfer/README.md),
then FN05/FN06 and CT01–CT03. Each new graph has a prediction, a primary edit,
an independent numerical reference and a linked offline guide with MIT readings.
FN06 separates growing and stable modes into different Scopes; CT02 separates
position and velocity by units. FN04's short transient is easiest to see at
0–1 s; its longer capture supports frequency-response analysis.

FN05/FN06 use **fixed, algebraically derived Delay/Gain recurrences**. Redesign
the gains when changing h. CT01–CT03 use actual EulerIntegrators, which follow
the selected run rate automatically. All stored initial states appear at Scope
time h; compare the continuous formulas at state time `t_Scope−h`.

Build `s7tests_education_linear`, then run
`ctest --test-dir build-release -R '^s7tests_education_linear$' -V`.
Regenerate this batch with
`python3 -B examples/education/generate_linear_labs.py` from `s7l2/`.

## Continue to SP06

SP06 builds a moving average from two zero-initialized unit delays, three
gains of 1/3 and an adder: `y[n]=(x[n]+x[n-1]+x[n-2])/3`, `x[n<0]=0`.
The source combines 0.25 V DC, a 1 V peak 10 Hz sine and a 0.5 V peak
100 Hz sine. Open the Scope to compare input and filtered output. After
startup, DC is unchanged, the 10 Hz gain is about 0.98543 with −12° phase,
and the 100 Hz tone cancels. Step k observes source index n=k−1 at time k/300 s.
Select `tap1`, press F4, set its factor to 0 and rerun: the output mean
becomes 1/6 V and the 100 Hz tone survives with amplitude 1/6 V peak.

Headless check: build `s7tests_education_sp06`, then run
`ctest --test-dir build-release -R '^s7tests_education_sp06$' -V`.
Reproduce the saved diagram with `python3 -B examples/education/generate_sp06.py`
from `s7l2/`. Qualification and current limitations are recorded in the
[education evidence](../../education/EVIDENCE.md) and
[24-lab implementation manifest](../../education/CORE_MANIFEST.md).

The exercise is original S7 work aligned with convolution and filtering
topics in [MIT 6.3000 Fall 2026](https://sigproc.mit.edu/fall26/), not an official
MIT lab or endorsement. This is development work, not a qualified course bundle.

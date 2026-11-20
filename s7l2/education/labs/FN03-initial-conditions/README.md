# FN03 — Where does the response come from?

**30 minutes · Beginner · Prerequisite: [FN02](../FN02-natural-forced/README.md).**
Use the current Linux S7/Studio development build; no hardware or plugin.
Status: `diagram-ready`, with [development evidence](../../EVIDENCE.md#fn01fn03-foundations)
and release qualification tracked separately.

**Your question:** if you know the input, do you know the whole output?
You will predict the first sample from the stored past, separate input and
initial-state effects, then deliberately break that separation by changing
one initial value.

## Three runs inside one diagram

Open **Help → Education: start here → fn03_initial_conditions.s7diagram**.
This new graph contains three independent copies of the same explicit
Delay/Gain/Adder rule, `y[n]=u[n]+0.5 y[n−1]`:

| Row / Scope channel | Input | Stored past y[−1] |
| --- | --- | --- |
| TOTAL / `total` | 1 V step from n=5 | 2 V |
| NATURAL / `natural` | 0 V | 2 V |
| FORCED / `forced` | Same step as total | 0 V |

The shared input does not create shared state: each row has its own Delay.
All signals are F64 voltages; 0.5 is dimensionless. The lower-right Adder
sums the natural and forced outputs. The final check subtracts that sum
from total. Scope's fourth channel is this **`residual`**.

## Predict, then inspect

For this linear rule, predict `total = natural + forced` at every sample.
If that is true, the residual must remain zero, including before the step.

| n | Scope time (s) | Total (V) | Natural (V) | Forced (V) | Residual (V) |
| --- | --- | --- | --- | --- | --- |
| 0 | 0.1 | 1 | 1 | 0 | 0 |
| 1 | 0.2 | 0.5 | 0.5 | 0 | 0 |
| 4 | 0.5 | 0.0625 | 0.0625 | 0 | 0 |
| 5 | 0.6 | 1.03125 | 0.03125 | 1 | 0 |
| 6 | 0.7 | 1.515625 | 0.015625 | 1.5 | 0 |

Calculate at least n=0 and n=5 yourself. Click **Run**, keep the saved
**2 seconds, 10 Hz**, and confirm the intentional 20-sample run. Double-click
**Compare the named signals**. The natural curve decays, the forced curve
starts at n=5, and total combines them. Residual lies at zero. Curves may
overlap; use the Scope channel controls to inspect them individually.

**Important:** Delay's **initial_value = 2** describes y[−1], the output
remembered before the first calculation. It is not y[0]. The first current
output is `0 + 0.5×2 = 1 V`. Scope labels sample n at `(n+1)/10 s`; all 20
samples are recorded from 0.1 through 2 s. Source start_time 0.5 s selects
n=5, which appears at Scope time 0.6 s. No extra sample or delay is implied.

## Break the decomposition

Select **Past output: starts at 2 V** in the **TOTAL** row
(`total_memory`), press **F4**, and change **initial_value** from **2 to 0**.
Leave `natural_memory` and `forced_memory` unchanged. Save your own copy,
reopen it and rerun. Predict the first residual before looking.

Total now equals forced, but the summed parts still include the original
natural response. The first residual becomes **−1 V**. At later samples it
is `−0.5^n V`, tending toward zero. The model runs correctly; it is the
comparison's initial-state assumption that is now wrong. Changing only the
input is not enough to reproduce a run: you must also preserve the initial state.
Parameter edits do not rewrite explanatory labels; F4 shows the actual value.

Restore total_memory to 2. As an extension, change **both** total_memory
and natural_memory to 4: predict the first total, natural and residual.
That extension has no automatic qualification claim here.

## Delay placement is part of the model

An additional unit Delay on the step path to TOTAL would move that row's
input response one sample later. It would no longer equal natural + forced
from the original paths. At n=5 the residual would be −1 V, even though the
stored past is still correct. The automated test constructs this declared
variation from a copy of the saved graph and verifies its changed sequence.
This is a different model, not a harmless way to organize the drawing.

Do not infer that any nonlinear system can be decomposed this way.
Superposition here follows from the specified **linear** recurrence.

## Independent verification

For initial memory b and a=0.5, independently expand the recurrence:

```text
y[n] = a^(n+1) b + sum from j=0 to n of a^(n−j) u[j]
natural[n] = a^(n+1) × 2 V
forced[n] = sum from j=5 to n of a^(n−j) V  (zero when n<5)
residual[n] = total[n] − natural[n] − forced[n]
```

The [headless test](../../../s7tests/education_foundations_smoke.cpp) loads
the [actual saved graph](fn03_initial_conditions.s7diagram)
through the production loader and checks all channels, the unobserved sum,
source values, startup and final samples against independent finite sums.
It checks fresh-run equality, the missing-state experiment and the extra
input-delay variation. The GUI workflow separately checks F4 editing,
recorded samples, guide links and save/reopen/rerun.

```bash
cmake --build build-release \
  --target s7tests_education_foundations --parallel 2
ctest --test-dir build-release \
  -R '^s7tests_education_foundations$' \
  --output-on-failure -V
```

The frozen local envelope is F64, 10 Hz, 20 samples and the named variations;
absolute tolerance is 1e−12 V. These are short discrete calculations, with
no continuous-model or real-time guarantee. If residual is unexpectedly
nonzero, check the three initial values, equal gains and input connections
before changing tolerances. Installed packages and native Windows are unqualified.

## Readings and next steps

Original S7 topic illustration, not a reproduced MIT exercise. These public
Mahajan–Freeman readings were checked 2026-11-19:

- [MIT 6.003: difference equations](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap1/).
- [MIT 6.003: difference equations and modularity](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap2/).

The yellow frame opens this offline guide or the original online readings.
No MIT handout is bundled. S7 diagrams/generators: CC0-1.0; this guide: CC-BY-4.0.

Previous: [FN02](../FN02-natural-forced/README.md).
Continue to [FN04 — impulse and transfer](../FN04-impulse-transfer/README.md).
For a signal-processing application using delays, try
[SP06 — Three-tap FIR](../SP06-three-tap-fir/README.md), or return to the
[learning route](../../index.md).

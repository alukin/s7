# Live constellation and correlation

The **Constellation / XY** and **Correlation** tabs share the Live Oscilloscope's
bounded, read-only acquisition. Time-tab gain and offset do not change their
inputs. No runtime analysis elements or disk journal are created. Settings are
currently local to the window and are not saved with diagrams.

## Try both views

Open `examples/signal_measurement/iq_correlation_live.s7diagram` and run **8 model
seconds at 1024 Hz**. Double-click `iq_scope`. Two causal sinusoidal sources give
16 Hz I and Q in quadrature; six explicit Delay elements give a delayed I channel.

Choose **Constellation / XY**, X=I, Y=Q, to see a unit circle. Switch between dots,
lines with dots, and density. Choose **Correlation**, A=I, B=I_delayed_6, N=4096
and maximum |lag|=16. The peak is at **+6 samples**, or **0.005859375 model seconds**.
A restricted lag range helps distinguish the desired delay from periodic repeats.

## Constellation / XY

X and Y are different real channels from the same coherent step. Arbitrary units
are allowed and shown on each axis. **Equal numeric aspect** means equal numeric
units per screen pixel; it does not convert physical units. The newest finite pair
is marked. This is a raw XY/IQ display, with no symbol timing, carrier recovery,
equalization or automatic modulation classification.

**Persistence** is a lifetime in model steps. Zero uses the current acquisition
window. Positive values retain newly observed pairs until their age reaches the
chosen step count, subject to a hard cap of **65536 finite pairs**. Repeated
snapshots contribute each step once. Evictions are counted; repainting does not
age samples. Missed intervals cannot be reconstructed after ring eviction.
Inactivity, analysis settings changes and new runs/generations restart persistence
when analysis resumes.

Nonfinite pairs are counted and omitted. Missing steps and invalid pairs break
lines. Up to 64 line segments are drawn; additional segments remain visible as
dots with a status message. All retained pairs remain available for raw inspection.
Within 12 screen pixels, hover chooses the nearest point, breaking exact ties
toward the newest step. It reports step, time and stored f64 values at 17-digit
precision; this cannot recover precision lost before acquisition.

Density uses a 64², 128² or 256² grid over the retained ranges. Each pair contributes
one count; colors use a logarithmic count scale. Hover explicitly reports an
aggregate bin's bounds, units and count, rather than claiming an individual sample.
Counts disappear when their raw pairs expire; there is no repaint-based decay.

## Correlation

Auto-correlation uses A for both inputs; cross-correlation selects A and B.
The latest N consecutive samples are used, with power-of-two N from 64 to 65536.
Insufficient retention reports **Filling**. Missing steps and nonfinite values
reject the interval rather than filling it with zeros.

The convention is `rAB[l] = sum(A[n] * B[n+l])` over overlapping indices:
**positive lag means B occurs later than A**. Linear correlation uses FFT padding
to at least `2*N-1`, preventing circular wraparound. Padding creates no observations.
Optional mean removal subtracts each full input window's mean first. There is no
implicit window taper.

| Normalization | Value | Units |
|---|---|---|
| Raw sum | `rAB[l]` | UA·UB |
| Biased | `rAB[l]/N` | UA·UB |
| Unbiased | `rAB[l]/(N-abs(l))` | UA·UB |
| Coefficient | `rAB[l]/sqrt(sum(A²)*sum(B²))` | dimensionless |

The coefficient denominator uses the **full N-sample windows**, after optional
mean removal, for every lag. It is not a per-overlap Pearson coefficient.
Zero-energy coefficient inputs and normalization overflow report unavailable.
Internal amplitude scaling avoids unnecessary overflow/underflow.

Maximum |lag| ranges from zero to N-1. The axis shows samples or model seconds.
The peak marker chooses the largest value, or magnitude with **Absolute peak**.
Numerical ties prefer the smallest absolute lag, then negative lag. Hover snaps
to integer lag, ties toward the earlier lag, and reports both lag units, computed
value, normalization units and overlap count. Plot reduction preserves extrema;
inspection reads the full computed lag array.

## Activity, freeze and bounds

Only the visible tab of a visible, non-minimized oscilloscope submits analysis.
Inactive tabs stop polling and cancel queued work. An executing FFT finishes its
bounded transform before observing cancellation; stale completions are discarded.
Desktop focus changes do not suspend visible oscilloscopes. Acquisition continues
independently of every window.

FFT, XY and correlation panels share **two worker threads**, with one active and
one replaceable pending request per panel. Replacements are counted; processing
every step/window is not promised. Results own their exact bounded raw frame and
show run and inclusive step interval. A preceding result is labeled updating while
a new request runs. Returning to a tab analyzes retained data and restarts FFT
averages/hold and XY persistence.

Hover holds the displayed result. **Freeze display** pins its raw frame; changed
analysis settings reanalyze that frame even after retention shrinks. **Resume
live / Fit** returns to current acquisition. Paired cursors, interval statistics
and saved-window export remain subsequent work.

## Verification

`s7studio_pair_analysis` compares FFT correlation to independent long-double direct
sums across signed lags and all normalizations, delayed/inverted impulses,
auto-correlation, zero energy, extreme scales, maximum N, invalid intervals and
worker cancellation/replacement. XY checks cover a circle, four clusters, density
count conservation, same-step values, gaps, invalid pairs, overlap deduplication,
model-step eviction, run/generation resets and repeated storage wraps.

The rendered GUI workflow runs the example and exercises actual QML raw hover,
equal aspect, density image loading, positive delay, frozen reanalysis, reset,
close/reopen and inactive-tab/minimized scheduling. Build `build-tests`, then run
`ctest --test-dir build-release --output-on-failure`. These checks do not establish
long-duration latency/resource targets or native Windows rendering.

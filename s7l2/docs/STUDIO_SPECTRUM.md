# Live FFT spectrum

The **Spectrum** tab of a Live Oscilloscope analyzes the retained raw samples.
It shares bounded acquisition with the Time tab and never creates a recording
journal or adds an FFT element to the causal graph. See also
[constellation and correlation](STUDIO_PAIR_ANALYSIS.md). Paired cursors, region
measurements and export are later work.

## Try it

Open `examples/signal_measurement/am_coherent_detection_live.s7diagram`, run
two model seconds at **12800 Hz**, and double-click `carrier_scope`. Choose
**Spectrum**, then select `carrier_scope/carrier [1]` as Source. With N=4096
and the default periodic Hann window, the carrier is at **100 Hz**, bin 32,
with peak amplitude 1. Choose the AM signal channel to see the carrier and
modulation sidebands; increasing acquisition retention permits longer FFTs.
The existing 10 kHz example rate also works, with a noncoherent 100 Hz tone.

FFT size is independent of acquisition retention. N can be a power of two from
64 to 65536; fewer than N retained positions reports **Filling**. No zero padding
or implicit interpolation/resampling is used to manufacture resolution. The
frequency spacing is the rational model sample rate divided by N. Zoom and
frequency-axis offset affect presentation, not the FFT or sample rate.

Inputs are one real channel or an explicitly selected I/Q pair, `z=I+iQ`.
I/Q requires matching nonempty supplied units or an explicit shared unit
declaration. The declaration does not convert values. Real spectra are
one-sided, including DC and Nyquist. I/Q spectra are shifted two-sided spectra;
a positive complex exponential appears at positive frequency. There is no
hidden mean removal, full-scale calibration, dBFS convention or dBm conversion.

## Windows and normalization

All windows are **periodic**, with phase `theta=2*pi*n/N`:

| Window | Coefficients |
|---|---|
| Rectangular | `1` |
| Hann | `.5 - .5*cos(theta)` |
| Hamming | `.54 - .46*cos(theta)` |
| Blackman | `.42 - .5*cos(theta) + .08*cos(2*theta)` |
| Blackman-Harris | `.35875 - .48829*cos(theta) + .14128*cos(2*theta) - .01168*cos(3*theta)` |

For `X[k]=sum(w[n]*x[n]*exp(-i*2*pi*k*n/N))`, `S1=sum(w)`, `S2=sum(w*w)`:

- Peak amplitude: `abs(X)/S1`, in supplied units U.
- Mean-square power per bin: `abs(X)^2/(N*S2)`, in U².
- PSD: `abs(X)^2/(Fs*S2)`, in U²/Hz.

Real one-sided bins are doubled except DC and Nyquist. I/Q bins are not doubled.
Summing power bins, or integrating PSD with bin spacing, gives the
window-weighted mean square; this is distinct from squaring coherent-tone
amplitude. Status reports coherent gain `S1/N` and ENBW `Fs*S2/S1^2`.
**Remove mean** subtracts the unweighted raw-window mean before windowing.
Nonfinite values or missing steps in the analyzed interval reject the transform
with a visible reason. Numerical overflow reports unavailable.

Linear results remain authoritative. dB uses an explicit positive reference in
the selected linear quantity's units: `20*log10(A/Aref)` or
`10*log10(P/Pref)`. Raw zero remains negative infinity. The chosen display offset
is then added and the labeled display floor is applied only to plotting. Bin
inspection retains the unfloored computed value and reference. Time-tab display
gain/offset never changes the FFT input.

## Averaging, freeze and overload

Averaging is available for power/PSD and operates in linear units before dB.
**Block mean** accumulates K spectra (1–128), showing the current contribution
count; after K, the next spectrum starts a new block. **Exponential power mean**
uses the first spectrum as its initial value, then
`Pavg=(1-alpha)*Pavg+alpha*P`, with explicit alpha. Peak hold tracks the displayed
linear quantity, including its averaging. Reset clears both averages and hold.
Status distinguishes the latest FFT window from the model-step coverage of the
average and peak hold.

The nominal hop is N/2 model steps, independent of repaint count. Repeated
refreshes of the same hop never add contributions. The latest complete
hop-aligned interval is used when still retained; minimum retention N can
require a newer interval. Missing hops are counted, and averages/hold restart
on a discontinuity. More retention does not force every hop to be processed.
Changes to run, acquisition generation, input, N, rate, window, mean removal or
normalization reset analysis state. Pure display changes preserve it.

The worker pool has two threads shared by all FFT, XY and correlation panels, with one active
and one replaceable pending request per panel. Replaced pending requests and
skipped model hops are separate counters. Jobs check cancellation during input
processing and after the bounded FFT. A result owns its exact input frame and
shows its run and inclusive step interval. While computing, a preceding result
is labeled **Updating**. Stale run/configuration completions are discarded.
Switching away, hiding/closing or minimizing cancels queued analysis and stops
polling; capture continues. Returning analyzes retained data and restarts
averages/hold. Desktop focus changes do not suspend a visible oscilloscope.

Hover snaps to a computed bin, reports its frequency/value/normalization, and
holds the displayed result until the pointer leaves. Peak and fixed-bin markers
report bins without interpolating peak frequency. **Freeze display** holds the
completed spectrum and its bounded raw input; changing N/window while frozen
reanalyzes that input even if the ring has wrapped or shrunk. **Resume live / Fit**
returns to current acquisition. Settings are not yet saved with the diagram.

## Backend and verification

Studio's private `s7studio_analysis` library uses double-precision FFTW 3;
the out-of-tree `s7spectral` plugin remains separate. Native Studio builds require
`fftw-devel` on Fedora or `libfftw3-dev` on Ubuntu. The dependency scripts retain
FFTW when Studio is enabled even with `--no-spectral`. The Windows sysroot uses
MSYS2's FFTW import library/DLL; headless runtime builds acquire no FFTW dependency.
Plans and aligned scratch are reused; planner/destructor calls are serialized,
and no global FFTW cleanup invalidates other engines. See the
[FFTW transform interface](https://www.fftw.org/fftw3_doc/Complex-One_002dDimensional-DFTs.html)
and [thread-safety contract](https://www.fftw.org/fftw3_doc/Thread-safety.html).
FFTW's GPL notice and exact source-package identity are included by the existing
Linux package inventory; Windows collection preserves the MSYS2 license tree.

`s7studio_spectrum` checks an independent long-double direct DFT, inverse scaling,
DC/Nyquist, coherent/off-bin/two-tone inputs, window gains/ENBW, Parseval power
and PSD for deterministic noise and I/Q, signed I/Q tones, explicit dB references,
gaps/nonfinite/overflow, averaging/reset/skipped hops, maximum N and stale jobs.
The rendered GUI workflow checks the causal AM example's 100 Hz carrier, actual
QML bin hover, filling, dB, frozen reanalysis and reset.

Run `ctest --test-dir build-release -R 's7studio_(spectrum|live_oscilloscope|gui_workflows)' --output-on-failure`.
These checks do not establish long-duration latency/resource targets or native
Windows rendering parity from the full [design](LIVE_OSCILLOSCOPE_DESIGN.md).

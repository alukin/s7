# Time–frequency laboratory

Run **8 model seconds at 1024 Hz**, open `map` and select STFT or Morlet CWT.
Small completed captures are analyzed automatically when the window opens (or
the run finishes). Use the blue **Analyze / freeze** button for larger records
or a new snapshot, or Follow for rolling results. The dock preview displays an
actual STFT magnitude map; opening its full window adds transform controls and
numeric inspection.
CWT defaults to 32 logarithmically spaced frequencies over 2–100 Hz. The PLL
example retains its original rate and **6-second** experiment; open `wavelet_map`.

| Diagram | Expected features |
|---|---|
| tone | Horizontal 32 Hz ridge; STFT peak-tone magnitude 1 at the coherent bin |
| tone_bursts | 32 Hz content localized to quarter-duty bursts every 2 seconds |
| overlapping_tones | Simultaneous 32 and 64 Hz ridges, amplitudes 1 and 0.6 |
| chirp | Rising frequency from 4 to 90 Hz over 8 seconds |
| impulse | Broadband event at 1.5 seconds; finite wavelet support spreads it in time |
| ring_down | Impulse-driven reusable second-order plant: oscillation near 16 Hz decays |
| resonant_chirp | A 4–90 Hz chirp crossing the 16 Hz plant resonance |
| pll_acquisition | Existing causal PLL frequency-step experiment with an added read-only time–frequency observer |

STFT uses periodic Hann, default FFT size 256 and 50% hop. Its magnitude uses
peak-tone normalization, including the distinct DC/Nyquist factors. CWT uses a
DC-corrected complex Morlet, omega0=6, finite support ±4 sigma and discrete L2
normalization. Its magnitudes are not calibrated tone amplitudes or PSD.
Squared magnitude remains squared coefficient magnitude, not PSD.
The FFT selector offers powers of two, and only the active method's settings
are enabled. **Display up to (Hz)** initially focuses the map and slice on
0–100 Hz (2–100 Hz for the default CWT). Raise it to inspect higher bins;
the numeric inspector and result files always retain the entire transform.
The chirp ridge is straight on a linear frequency axis and curved on a log axis.

Gray cells are boundary affected or invalid, not zero energy. CWT shows only
full-support coefficients in the map; numeric inspection/export retains the
edge coefficients and their mask. Phase is hidden for coefficients at or below
1e-12 magnitude. The frequency axis can be logarithmic for positive-frequency
maps. Frequency rows and time columns inspect the full result; the drawing
selects at most 513×513 source cells and explicitly reports that reduction.

The input trace and a frequency slice are linked to a numeric time selector.
The initial selector is near the center of the input energy, keeping a short
transient in view, with a strong valid frequency selected there. Switching
methods keeps the cursor's model time and frequency, using the nearest bins.
The bounded input envelope retains extrema and their
timestamps, including a one-sample impulse.
Saved `.s7analysis` results retain the original input interval, complex matrix,
coordinates, validity, model rate, transform settings and run provenance.
Changing transform settings while frozen reanalyzes that original interval.
Opening a result stages and validates it before replacing the current view.
CSV exports the full matrix with a metadata header and cell identities.

Follow processes the latest 65,536 retained samples and may skip intermediate
windows. Hidden/minimized views cancel work. A shared heavy-job reservation
bounds concurrent memory; transforms never execute on the runtime thread.
CWT requires future samples around each center, so the newest region remains
edge affected until its support exists. It is not an instantaneous control
signal. Use the causal `WaveletCoefficient` plugin for graph computation.

The second-order plant is an editable schema containing two explicit Euler
integrators. Its frequency/damping reflect this discretization; refine the
model rate when comparing to continuous-time formulas. The PLL graph is also
an ordinary causal model. No global ODE solver is used.

`generate_examples.py` reproduces the diagrams and reusable plant definition.

STFT also offers mean-square power per bin and PSD. For a periodic-Hann
window of N samples, S1=N/2 and S2=3N/8. If C is the stored peak-amplitude
coefficient and a is 1 at DC/Nyquist or 2 elsewhere, power is
|C|²·S1²/(a·N·S2), and PSD is |C|²·S1²/(a·Fs·S2). Summing power bins or
integrating PSD with bin spacing yields the window-weighted mean square.
These two options are disabled for CWT. Saved/exported complex coefficients
remain peak-amplitude STFT coefficients with transform metadata, independent
of the selected display quantity.

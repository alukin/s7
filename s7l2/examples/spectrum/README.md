# Spectrum and Live Oscilloscope references

Open any diagram in Studio and run it for **4 model seconds at 1024 Hz**. Each
contains a recorded `scope` and a bounded `live` oscilloscope fed by the same
causal signals. Open `scope`, select **Spectrum**, and press **Analyze**. The
default interval starts with the run and covers its first quarter (about one
second), giving **N = 1024** samples and **1 Hz** FFT bins. Leave **All inputs**
checked and use the default periodic Hann window and peak-amplitude quantity.

| Diagram | Expected one-sided spectrum |
|---|---|
| [Three bin-centered tones](three_bin_tones.s7diagram) | Separate traces: 32 Hz at 1 V peak, 96 Hz at 0.5 V peak, 160 Hz at 0.25 V peak. |
| [Two-tone sum with DC](two_tone_sum.s7diagram) | One trace: DC at 0.1 V, 32 Hz at 1 V peak, 96 Hz at 0.25 V peak. |
| [Off-bin tone](off_bin_tone.s7diagram) | One 32.5 Hz, 1 V peak sine: adjacent 32 and 33 Hz bins are each about 0.849 V with the periodic Hann window. Neither bin alone represents the sine's full amplitude. |

The recorded Spectrum starts with **Fit significant peaks**, making low-frequency
components readable. In **Display → Frequency range**, choose **Full frequency
band** to see 0–512 Hz, then switch back to compare. The plot and marker use
computed bins; changing the view range does not recalculate the FFT.

Open `live` and select **Time**. Set **Time / div** to **0.02 s/div** for a 0.2 s
view near the end of the run. In the three-tone diagram, the three traces should
reach approximately ±1, ±0.5, and ±0.25 V; the 32 Hz trace has about six cycles
across the 0.2 s span. The sum shows the composite waveform and its 0.1 V
offset. The off-bin tone still has a 1 V time-domain peak even though its FFT
energy is shared by neighboring bins.

The Live Oscilloscope's **Spectrum** tab starts with the full 4-second retained
window, **N = 4096** and **0.25 Hz** bins. Select **Fit significant peaks** in
**Display → Frequency range** to inspect the same signals closely. The 32.5 Hz
tone now lands exactly on bin 130 at 1 V peak: the longer window resolves a
frequency that split between adjacent 1 Hz bins in the recorded one-second
analysis. The three-tone example also lets you select each live source to check
its own 1, 0.5, or 0.25 V peak.

These diagrams use `AnalogOscillator`, `Adder`, and `Constant` elements. The
observers only read their outputs; they do not affect the model or its sample
rate. Run Configuration sets the effective rate, so keep it at 1024 Hz when
comparing with the frequencies and bins above.

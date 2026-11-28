# Live SDR spectrum

`live_spectrum.s7diagram` receives I/Q from a LibreSDR (ADALM-Pluto compatible)
at `ip:analog.local`, tuned to 88.9 MHz at 672 kS/s, and shows it in the
**Live spectrum** viewer as an SDR-style curve: a filled, lightly smoothed
spectrum with a decaying peak trace, two-sided around the tuned frequency.

1. Build Studio with SoapySDR support and install SoapyPlutoSDR (see the
   [hardware guide](../../docs/HARDWARE_IO.md)). The example only receives.
2. Open the diagram and choose **Run** (continuous; **Stop** ends it). The run
   is paced by the radio's sample clock.
3. Double-click **spectrum**. The FM station at 88.7 MHz sits 200 kHz left of
   the centre line; the centre itself may show the receiver's DC offset.

`ComplexToReIm` splits the C32 samples into I and Q, the viewer's two
channels. Its radio buttons switch between **Bars** and **Curve**; the
frequency axis of an I/Q spectrum is always linear. Levels are dB relative to
converter full scale (dBFS) after a 2048-point Hann-windowed FFT; set
`fft_size`, `floor_db` and `bars` on the observer, or in the window. To look
at another band, change `frequency_hz` in the receiver's options and
`centre_hz` on the viewer (which only labels the axis).

The radio is optional hardware. If it is not connected, the run stops after a
few seconds with "no device context found"; nothing else depends on it.
The example is never run by automated tests: it opens a real radio.

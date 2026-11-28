# Live microphone spectrum

`microphone_spectrum.s7diagram` captures the default PipeWire input (usually
the built-in or headset microphone) as stereo F32 at 48 kHz and shows it in a
Live Oscilloscope.

1. Build Studio with PipeWire support (`S7_PIPEWIRE`, see the
   [hardware guide](../../docs/HARDWARE_IO.md)) and open the diagram.
2. Choose **Run**. The saved run is continuous (`-1`); stop it with **Stop**.
   The run is paced by the sound card, so model time follows the device clock.
3. Double-click **spectrum** and open the **Spectrum** tab. Pick `left` or
   `right` as Source; use the FFT size, window and dB controls described in the
   [spectrum guide](../../docs/STUDIO_SPECTRUM.md). Music shows its bass and
   voice bands; a whistle or tuning fork shows a single sharp peak.

Values are in full-scale units (FS): ±1 is the converter limit. If the Time
trace flattens at ±1 the input is clipping and the spectrum gains spurious
harmonics; lower the input gain (for example `wpctl set-volume @DEFAULT_SOURCE@ 0.15`).
To use another input, set the AudioSource `device` to a PipeWire node name;
to analyze what this computer is playing, add `"options": "{\"capture_sink\":\"true\"}"`
to capture the default output's monitor instead.

The example is never run by automated tests: it opens a real audio device.

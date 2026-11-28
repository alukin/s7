# Listen to a local FM station with SoapySDR

Open the [FM stereo receiver](fm_receiver.s7diagram) in Studio. It is tuned to
**88.7 MHz**, Українське Радіо — Чернігівська хвиля,
on RX channel 0 of the LibreSDR at `ip:analog.local`. Earlier receiver tests
found a strong signal and stable 19 kHz stereo pilot at this frequency; a
second tested station, Армія FM at **100.6 MHz**, was weaker.

1. Connect an FM antenna to the receiver's RX input. Build with SoapySDR and
   PipeWire enabled and install the SoapySDR module for your radio.
   This setup uses [SoapyPlutoSDR](https://github.com/pothosware/SoapyPlutoSDR).
2. To use the second LAN receiver, change **SoapySDR RX** → `device` to
   `driver=plutosdr,uri=ip:analog1.local`. For a different radio, use
   `s7hw --devices soapy` and adapt the antenna, bandwidth and gain options to
   those the driver supports. Tune by changing the string-valued
   `options.frequency_hz` in Hz.
3. **Stereo playback** uses the default PipeWire output. Edit the sink's `device`
   to select a specific PipeWire node, or choose the `portaudio` backend and
   select a stereo device with `s7hw --devices portaudio`.

In Studio, press **F5** and accept **Run Configuration** with the **672,000 Hz**
model step rate. The diagram starts in **Run continuously until stopped** mode.
Press **Shift+F5** to stop playback. The radio paces the graph.

To listen from the command line, run these commands from the repository
directory. Press Ctrl+C to stop sooner.

```sh
build-release/s7hw --check examples/sdr/fm_receiver.s7diagram
build-release/s7hw --run examples/sdr/fm_receiver.s7diagram --frames 40320000
```

Use `s7hw` directly if installed. The check runs offline. The run configures
the receiver and sends live audio to the selected output. The frame count
is in RF samples. Hardware tuning changes can persist after the run.

The graph filters and downsamples tuned I/Q from 672 kS/s to a 336 kHz
complex IF. The FM demodulator normalizes the phase change to ±75 kHz.
The [stereo multiplex decoder](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.450-4-201910-I%21%21PDF-E.pdf)
recovers the 38 kHz difference channel from the 19 kHz pilot, blends to mono
when the pilot fades, and applies 50 µs de-emphasis. Matching anti-alias
resamplers feed the left and right channels to a 48 kHz audio sink.

Both LAN radios report 671,999 samples/s when asked for 672 kS/s. S7 accepts
the realized rate and reports it
([ADR-028](../../../doc/S7_Architectural_Decisions.md#adr-028)). The graph and
audio device use nominal rates. The 32,768-frame playback queue absorbs short-term
jitter. If it empties, the sink plays silence while the queue refills, then
resumes playback. A device disconnection or loss of SDR stream continuity
still ends the run with an error.

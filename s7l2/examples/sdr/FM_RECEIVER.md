# Listen to a local FM station with SoapySDR

Open the [FM receiver diagram](fm_receiver.s7diagram) in Studio. Save a working
copy after editing **SoapySDR RX**. Set its `device` to the selector from
`s7hw --devices soapy` and set `frequency_hz` in the string-valued `options`
object to a strong local station. Connect a receive antenna. This example
requires a driver and radio that can deliver one C32 channel at exactly
1.2 MS/s. The saved 100 MHz tuning is an example.

The RX source produces tuned complex I/Q. A symmetric 97-tap Hamming FIR
isolates the station around zero frequency. The FM discriminator measures the
signed phase change between adjacent I/Q samples. Gain converts radians per
sample to full-scale units at ±75 kHz deviation. The recording still contains
the mono baseband, pilot and stereo components at the SDR sample rate.

Use the device-paced hardware runner from a directory where you can write
`fm-baseband.s7b`. After capture, convert to 48 kHz mono audio with FFmpeg.
The resampler rejects the pilot and higher components. De-emphasis follows
resampling. Choose `50fm` or `75fm` to match your station.

```sh
s7hw --check fm_receiver.s7diagram
s7hw --run fm_receiver.s7diagram --frames 12000000
ffmpeg -f f64le -ar 1200000 -ac 1 -skip_initial_bytes 40 -i fm-baseband.s7b \
  -af 'aresample=48000:cutoff=0.63,highpass=f=30,aemphasis=type=50fm' \
  -c:a pcm_s16le fm-audio.wav
```

The 40-byte offset applies to the supplied compact-binary recording with an
empty description. Keep the recording format and description unchanged for
this command. Studio edits the graph. Capture runs through `s7hw`, which
currently supports one hardware boundary at one rate. Listen to the resulting
WAV with your audio player. This graph receives only and produces mono audio.

# SDR examples

- [Live stereo FM receiver](fm_receiver.s7diagram) tunes 88.7 MHz with SoapySDR
  and plays 48 kHz stereo audio. See the [tuning and playback guide](FM_RECEIVER.md).
- [Live SDR spectrum](live_spectrum.s7diagram) shows the I/Q spectrum around
  88.9 MHz in the Live spectrum viewer; see the [guide](LIVE_SPECTRUM.md).
- [Basic SoapySDR capture](sdr_receive.json) checks the receive path and
  reports frame counts. Replace the device selector before running it.
  C32 explicitly requests driver conversion.

Open diagrams in Studio and use `s7hw --check` to validate them without
activating a device. Capture requires a build with SoapySDR and a driver
that supports your radio.

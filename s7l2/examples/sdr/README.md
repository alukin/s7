# SDR examples

- [Mono FM receiver](fm_receiver.s7diagram) filters SoapySDR I/Q, demodulates
  broadcast FM, and records the result. Follow the [capture and audio
  guide](FM_RECEIVER.md) to make a 48 kHz mono WAV.
- [Basic SoapySDR capture](sdr_receive.json) checks the receive path and
  reports frame counts. Replace the device selector before running it.
  C32 explicitly requests driver conversion.

Open diagrams in Studio and use `s7hw --check` to validate them without
activating a device. Capture requires a build with SoapySDR and a driver
that supports your radio.

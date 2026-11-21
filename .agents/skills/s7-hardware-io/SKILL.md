---
name: s7-hardware-io
description: Design or verify S7 SDR, audio and video I/O with deterministic replay, clocks, backpressure and controlled hardware tests.
---

# S7 hardware I/O engineering

Use for SoapySDR/Pluto/LibreSDR, PipeWire/PortAudio, audio loops, GStreamer integration, buffers, real-time pacing and device faults.

1. Start with `doc/S7_Hardware_IO_Contract.md` and `s7l2/docs/HARDWARE_IO.md` for the supported backend and build options. Consult `s7l2/docs/HARDWARE_IO_LIVE_TESTS.md` for live reproduction and the relevant work plan for proposed capabilities.
2. Describe the signal chain and hardware profile: sample type/layout, channel count, negotiated rates, actual clock source and mapping, frames/timestamps, latency measurement endpoints, ownership, buffer bounds and expected failures. Treat realtime and numerical semantics independently.
3. For behavior changes, use the existing simulated, fake or replay path where it can reproduce the affected behavior. Add focused cases for the relevant faults, such as partial transfer, discontinuity or an xrun. Compare measured behavior to an independent reference where appropriate.
4. Gate physical tests: discover and verify the intended device; ask for authorization before initiating RF TX, potentially audible playback, changing system audio routes/defaults or claiming exclusive access to shared hardware. For RF use legal bands or safe attenuated coax paths with explicit gain/power control. Do not assume a specific SDR board's transceiver revision from firmware alone.
5. For an authorized live run, record the negotiated format, clock mapping and the measurements needed for the claim, including drops or xruns where relevant. For latency claims, name the endpoints and report a distribution. Use a bounded run and release the device afterward.
6. Cross-check physical capture with deterministic replay and suitable independent reference; report separately whether results are simulation-only, replay-verified or hardware-measured.
7. Use `s7-visual-validation` when Studio live views or scope displays are part of acceptance. A successful driver call is not proof of visible, complete output.

Report hardware/driver/kernel versions, permission/safety controls, exact commands, measured results and unverified devices/features. Never transmit or reconfigure physical devices implicitly.

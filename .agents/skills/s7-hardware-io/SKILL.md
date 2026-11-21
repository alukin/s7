---
name: s7-hardware-io
description: Design or verify S7 SDR, audio and video I/O with deterministic replay, clocks, backpressure and controlled hardware tests.
---

# S7 hardware I/O engineering

Use for SoapySDR/Pluto/LibreSDR, PipeWire/PortAudio, audio loops, GStreamer integration, buffers, real-time pacing and device faults.

1. Read `AGENTS.md`, `doc/S7_Hardware_IO_Contract.md`, `s7l2/docs/HARDWARE_IO.md`, `s7l2/docs/HARDWARE_IO_LIVE_TESTS.md` and the applicable release work plan. Distinguish existing SDK features from proposed 0.3.0 work.
2. Describe the signal chain and hardware profile: sample type/layout, channel count, negotiated rates, actual clock source and mapping, frames/timestamps, latency measurement endpoints, ownership, buffer bounds and expected failures. Treat realtime and numerical semantics independently.
3. Implement a deterministic fake/replay backend and independent golden/reference checks before making hardware claims. Test underflow/overflow, partial transfer, discontinuity, restart, cancellation, device error, backpressure and temporary unavailability.
4. Gate physical tests: discover and verify the intended device; ask for authorization before initiating RF TX, potentially audible playback, changing system audio routes/defaults or claiming exclusive access to shared hardware. For RF use legal bands or safe attenuated coax paths with explicit gain/power control. Do not assume a specific SDR board's transceiver revision from firmware alone.
5. For a live run record actual stream format, clock/counter mapping, drops/xruns, jitter and input-to-output latency distribution, not only average throughput. Use bounded-duration tests, temporary resources and cleanup. No inferred realtime guarantee from a single run or virtual loop.
6. Cross-check physical capture with deterministic replay and suitable independent reference; report separately whether results are simulation-only, replay-verified or hardware-measured.
7. Use `s7-visual-validation` when Studio live views or scope displays are part of acceptance. A successful driver call is not proof of visible, complete output.

Report hardware/driver/kernel versions, permission/safety controls, exact commands, measured results and unverified devices/features. Never transmit or reconfigure physical devices implicitly.

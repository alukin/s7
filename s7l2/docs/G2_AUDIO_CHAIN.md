# G2 audio chain — two-node equalizer

The 0.2.0 G2 two-host workflow is a one-way audio chain (maintainer decision,
2026-11-28; see the [work plan](RELEASE_0.2.0_WORKPLAN.md#step-2--qualify-the-practical-two-host-workflow-g2)):
host A generates a deterministic audio test signal, host B runs an equalizer
and records input and output, and the recordings are checked against
independent references. This document holds the design inputs and evidence as
they are produced. A live microphone → equalizer → speakers round trip is a
0.3.0 target.

## 1. Sustained sample rate (measured 2026-11-28)

Question: which audio sample rate can a deployed two-node chain keep in real
time, with one S7 step per audio sample and a scalar F64 stream between the
nodes?

### Method

`tools/measure_g2_audio_rate.py` starts two fresh local `s7runtime` nodes per
rate (repository test certificates, mTLS control plane), deploys with `s7ctl
check` and `s7ctl deploy`, and measures for a fixed window after a warm-up:

- **Node A:** three `AnalogOscillator`s (220 Hz, 1 kHz and 0.3 × the sample
  rate) summed by an `Adder`.
- **Node B:**
  - a 3-band equalizer: RBJ low shelf at 200 Hz +6 dB, peak at 1 kHz −6 dB
    with Q 1, and high shelf at 0.3 × the rate +4 dB. Each band is a
    direct-form-I biquad built from existing primitives (one 5-input
    `Adder` and four `Delay`s, so the feedback goes through causal memory);
  - `RecordingSink`s for the stream as received and for the equalizer output.
- **Pacing:** the nodes step on the deployment's UTC grid at `dt = 1/rate`.
  When a node falls behind, every step still runs; the lag is reported.

A rate counts as **sustained** only if all of these hold:
- Both nodes step within 0.5 % of nominal over the window.
- The grid lag stays below 100 ms at all times since activation and does not
  grow.
- Neither stream end reports a fault.
- Both recordings pass the oracle:
  - B's recorded input equals the tones computed independently in Python, to
    1e-7 (phase accumulation in binary64). A lost, repeated or reordered
    sample fails this check.
  - B's recorded output equals an independent Python biquad cascade of that
    input, to 1e-9.

A dropped sample and a 1e-6 change in one recorded value were both rejected by
the oracle. A run starved to a 5 % CPU quota was reported as not sustained.

Host: AMD Ryzen 9 7940HS (16 logical CPUs), Linux 7.0, GCC 16 trunk Release
build of `11fec52`, load average below 2.5. Stream settings were the
defaults: up to 1024 samples per packet and 10 ms maximum batch latency.

```sh
python3 tools/measure_g2_audio_rate.py --build build-release \
    --rates 8000 16000 24000 32000 44100 48000 96000 192000 --seconds 30 \
    --output g2-sweep.jsonl
# slower-host emulation: each node pinned to one CPU, 20 % CPU quota
python3 tools/measure_g2_audio_rate.py --build build-release \
    --rates 8000 16000 32000 48000 96000 --seconds 20 --cpus 4 6 --cpu-quota 20
```

### Results

Unrestricted, 30 s windows, data on the shared mTLS control connection:

| Rate (Hz) | Sustained | Step rate A / B (Hz) | Max lag A / B (ms) | Packet latency p50 / p99 (ms) | CPU A / B (% of a core) | Peak RSS B (MiB) | Samples checked |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 000 | yes | 8 000 / 8 000 | 2.1 / 12.0 | 0.1 / 0.3 | 15 / 2 | 21.0 | 264 809 |
| 16 000 | yes | 15 999 / 16 001 | 3.8 / 14.5 | 0.1 / 0.3 | 20 / 3 | 21.1 | 529 505 |
| 24 000 | yes | 24 002 / 23 995 | 2.1 / 12.1 | 0.1 / 0.3 | 18 / 3 | 20.9 | 794 279 |
| 32 000 | yes | 31 999 / 32 004 | 3.5 / 12.2 | 0.1 / 0.5 | 21 / 3 | 21.0 | 1 059 096 |
| 44 100 | yes | 44 100 / 44 109 | 0.9 / 11.5 | 0.1 / 0.5 | 21 / 4 | 21.0 | 1 459 446 |
| 48 000 | yes | 48 001 / 47 996 | 0.4 / 11.2 | 0.1 / 0.3 | 21 / 4 | 20.8 | 1 588 828 |
| 96 000 | yes | 96 003 / 95 994 | 1.4 / 11.2 | 0.1 / 0.5 | 22 / 7 | 20.8 | 3 176 330 |
| 192 000 | yes | 192 000 / 192 004 | 0.8 / 7.5 | 0.1 / 0.5 | 27 / 12 | 20.9 | 6 353 921 |

Each node pinned to one CPU with a 20 % CPU quota (`systemd-run --user
CPUQuota=20%`), 20 s windows:

| Rate (Hz) | Sustained | Step rate A / B (Hz) | Max lag A / B (ms) | CPU A / B (%) |
| ---: | :---: | ---: | ---: | ---: |
| 8 000 | yes | 8 000 / 7 997 | 2.8 / 13.9 | 11 / 2 |
| 16 000 | yes | 15 999 / 16 006 | 1.2 / 13.5 | 15 / 2 |
| 32 000 | yes | 32 005 / 31 991 | 1.5 / 12.4 | 15 / 3 |
| 48 000 | yes | 48 003 / 48 018 | 66.9 / 75.0 | 18 / 4 |
| 96 000 | yes, marginal | 96 207 / 95 789 | 81.6 / 93.1 | 20 / 4 |

At 48 kHz with a separate mTLS data listener (`--data-security mtls`), the run
was also sustained: max lag 1.1 / 11.9 ms, CPU 22 / 4 %.

In every run the equalizer output matched the reference cascade exactly (the
maximum difference was 0.0). The input matched the generated tones within
4e-10.

### What the numbers mean

- **End-to-end delay is the batching.**
  - At 48 kHz a packet carries about 486 samples (10 ms), and node B holds
    about 470 received samples.
  - Node B steps about 10 ms behind the grid, waiting for each batch.
  - The reported packet latency, 64 µs p50 on loopback, covers only the
    transport.
  - At 192 kHz the 1024-sample packet limit is reached first, which gives
    about 5.3 ms batches and a lower lag on B.
- **Node A's CPU is mostly pacing, not computation.**
  - The grid pacer sleeps until each step's due time, so a source node wakes
    for every sample until timer granularity makes it run steps in short
    bursts. That gives 15–27 % of a core at any rate.
  - Under a 5 % quota, node A still produced 48 kHz on average: the actual
    computation is well below 5 % of a core.
  - Node B blocks on the stream and wakes about once per packet.
  - Waking for a group of due steps instead of every step would cut the
    source's CPU. That is a possible runtime improvement, not needed for G2.
- **Throttling shows up as stalls, not lost samples.**
  - With a 20 % quota, CFS throttling makes the nodes fall up to 75 ms (48
    kHz) and 93 ms (96 kHz) behind before catching up.
  - Every sample was still delivered and checked.
  - For later live playback, the buffer must cover such stalls.

### Decision input

- **Recommendation for G2:** a 48 kHz sample rate, the standard audio rate
  that recordings play back at directly. It was sustained with a large margin
  on this host and still held under the 20 % single-CPU quota. 16 kHz is the
  fallback if the two physical hosts cannot hold 48 kHz.
- **Limits of this evidence:**
  - Both nodes ran on one host over loopback; the release qualification needs
    two physical hosts and a real network.
  - A CPU quota emulates a slower host only roughly: throttled time slices
    are not slower cores.
  - This development host is 3–10× faster than typical student machines.
- **Repeat the measurement on the G2 hosts with the same script** before
  freezing the rate.

## 2. Next steps

1. Fix the audio rate with the maintainer and add a chirp to the source.
2. Freeze the G2 diagram as a saved `.s7diagram` that opens in Studio, with
   per-band gain checks against the analytic equalizer response.
3. Run it on two physical hosts with resource metrics and archived results.
4. Play back the recorded input and output for listening.

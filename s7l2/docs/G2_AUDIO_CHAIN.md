# G2 audio chain — two-node equalizer

The 0.2.0 G2 two-host workflow is a one-way 48 kHz audio chain (maintainer decision,
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

Section 1 used hand-built biquads and the default 10 ms batches. The G2
chain now uses the bundled equalizer element and buffered streams
(sections 2 and 3).

## 2. Decisions (maintainer, 2026-11-28)

- **48 kHz** audio rate.
- **Buffered stream ends.** Latency does not matter for G2, but sound quality
  does: no gaps, no lost or repeated samples.
- **The equalizer is a schema-based element.** The FIR/IIR choice was left
  open. It is the bundled
  [`AudioEqualizer`](CONTROL_ELEMENTS.md#separate-schema-elements-resource):
  - 5 IIR bands (low shelf 100 Hz; peaks at 400 Hz, 1.6 kHz and 6 kHz; high
    shelf 12 kHz) with gain inputs in dB;
  - built only from z⁻¹ `Delay`s, `Adder`s and the gain multipliers;
  - generated by `tools/generate_audio_equalizer.py`;
  - its schema declares 48 kHz, so a graph at another rate is refused instead
    of silently shifting the bands.

## 3. Buffered streams

The G2 stream uses these settings. They are now the measurement script's
defaults:
- `max_elements_per_packet` 4800: 100 ms at 48 kHz, 38 400 bytes, well under
  the 256 KiB payload limit;
- `max_batch_latency_ms` 100;
- `max_wait_ms` 5000: how long node B waits for late data before faulting;
- the default producer send log of 4096 packets, about 6.8 minutes of audio.

Node B steps about one batch behind the grid. When node A stalls, B waits
and continues; no sample is dropped or repeated.

**Defect found and fixed.** Deployment did not give the consumer `NetSource`
the stream's `max_elements_per_packet`. It kept the default 1024 and rejected
every larger packet as `MALFORMED_PACKET`. As a result, no deployed stream
could use packets of more than 1024 elements. `extract_node_schema` now
passes the size to both ends, and `s7net_diagram_deployment_smoke` checks it.

### Results with the AudioEqualizer element (2026-11-28)

- **Chain:** node A: three tones (220 Hz, 1 kHz, 0.3 × rate) → buffered
  stream → node B: `AudioEqualizer` with gains +6, −3, −6, +3, +4 dB, and
  recordings of its input and output.
- **Oracle:** B's output is compared with an independent sample-level Python
  implementation of the documented bands.
- **Rates other than 48 kHz:** the script embeds the element's schema
  re-timed to the run rate, for capacity only.
- **Conditions:** 30 s windows, `11fec52` plus these changes, Release build,
  host load average 3–4 from other processes.

| Rate (Hz) | Profile | Sustained | Step rate A / B (Hz) | Max lag A / B (ms) | Packets | CPU A / B (%) | Peak RSS A / B (MiB) | Output error |
| ---: | --- | :---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 000 | unrestricted | yes | 15 999 / 16 021 | 1.7 / 101.7 | 330 | 17 / 2 | 20.9 / 21.8 | 2.6e-15 |
| 44 100 | unrestricted | yes | 44 101 / 44 122 | 0.5 / 101.5 | 330 | 17 / 3 | 20.6 / 21.6 | 3.3e-15 |
| **48 000** | unrestricted | **yes** | 48 001 / 47 996 | 3.2 / 100.7 | 333 | 18 / 3 | 20.5 / 21.7 | 3.7e-15 |
| 96 000 | unrestricted | yes | 95 995 / 95 992 | 1.6 / 51.8 | 663 | 19 / 5 | 20.8 / 22.0 | 5.6e-15 |
| **48 000** | 1 CPU each, 20 % quota | **yes** | 47 999 / 47 981 | 74.3 / 162.7 | 333 | 19 / 4 | 20.7 / 21.6 | 3.7e-15 |
| 96 000 | 1 CPU each, 20 % quota | yes | 95 744 / 95 691 | 80.8 / 96.3 | 667 | 20 / 6 | 20.8 / 21.8 | 5.6e-15 |

- **Lag:** node B's lag includes the batch it waits for: 100 ms, or 50 ms at
  96 kHz, where the 4800-sample packet fills first.
- **Throttled runs:** under the quota, the stalls stayed within one batch
  plus 63 ms, and every sample arrived.
- **Input check:** B's recorded input matched the generated tones within
  3.2e-10 in every run, so no sample was lost, repeated or reordered.

## 4. Qualification run (2026-11-28)

### What was run

- **Diagram:** `examples/audio/two_node_equalizer.s7diagram`, generated by
  `tools/generate_g2_audio_example.py`. See the
  [run guide](../examples/audio/two_node_equalizer.md).
- **Node A:** a deterministic 48 kHz programme: a logarithmic sweep from
  20 Hz to 20 kHz over 10 s, then five tones.
- **Node B:** `AudioEqualizer` with gains +6, 0, −9, +3 and −6 dB, and
  recordings of its input and output.
- **Runner:** `tools/run_g2_audio_demo.py`.
  - `local`: both nodes on one host.
  - `two-host`: node A on this host, node B on another host over ssh.
  - `node`, `deploy` and `check`: the same steps separately.
- **What the runner does:**
  - deploys with `s7ctl` using buffered streams;
  - polls control-plane timing and stream state every second;
  - samples each node's CPU and RSS on its own host;
  - checks the recordings and writes `report.md`, `report.json`,
    `response.svg` and 32-bit float WAVs.

**Checks, with tolerances and why:**

| Check | Reference | Tolerance |
| --- | --- | --- |
| Every received sample | The programme from the documented source algorithms (tones analytic; the sweep's phase recursion) | 1e-9: the oscillators accumulate phase in binary64; observed ≤ 3.2e-10 over 120 s |
| Every output sample | An independent sample-level implementation of the equalizer's documented bands applied to that input | 1e-9: the same arithmetic in another operation order; observed 1e-15 |
| The same graph in one process | `s7runtime` simulation mode on the same diagram | bit-identical (model-time equivalence) |
| Tone gains (10.5–14 s, whole periods) | Analytic \|H(e^jω)\| in the frequency domain | 0.01 dB; observed < 1e-5 dB |
| Sweep response, 28 third-octave points | Same analytic response; RMS ratio in a short window | 0.5 dB, for the finite window on a moving frequency; observed ≤ 0.045 dB |

**Negative checks:** the checker rejects
- a recording with one output sample changed by 1e-6, in the output and
  local-run checks;
- a recording with one dropped input sample, in the input check at exactly
  that sample (240 000), as well as the output and local-run checks.

The ctest `s7_g2_audio_equalizer_local` (label `integration`) runs the local
variant in about 20 s.

### Results

Full reports: [G2_AUDIO_RESULTS_2026-11-28.json](G2_AUDIO_RESULTS_2026-11-28.json).

- **Development host:** AMD Ryzen 9 7940HS, Ubuntu 26.04.1, GCC 16 trunk,
  Release build.
- **Second host:** the Oracle Linux 10.2 VirtualBox VM (14 vCPUs, 31 GiB) on
  the same machine, bridged to the LAN. Its build used `gcc-toolset-15`
  (GCC 15.2.1), Release, headless.
- **Credentials:** the two-host runs used fresh credentials from
  `tools/generate_test_runtime_certs.sh`. Node B's port was opened with a
  runtime-only `firewalld` rule, removed afterwards.

| Run | Samples checked | Distributed = local | Input / output error | Step rate A / B (Hz) | Lag A / B, max (ms) | Packet latency min / mean / max (ms) | CPU A / B (%) | Peak RSS A / B (MiB) | Queues: B buffer max, A send log max |
| --- | ---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| local, shared mTLS, 16 s | 772 801 | yes, 0 differ | 4.0e-11 / 1.0e-15 | 47 990 / 48 013 | 3.3 / 101.7 | — / — / 1.6 | 20 / 4 | 21.4 / 21.9 | 4798 samples, 0 packets |
| host → VM, shared mTLS, 16 s | 816 001 | yes, 0 differ | 4.0e-11 / 1.0e-15 | 48 001 / 48 059 | 0.9 / 104.8 | 4.12 / 4.29 / 4.59 | 19 / 4 | 21.4 / 19.8 | 4789 samples, 0 packets |
| host → VM, split mTLS data listener, 120 s | 5 803 197 | yes, 0 differ | 3.2e-10 / 1.0e-15 | 48 000 / 48 014 | 3.1 / 105.8 | 3.99 / 4.26 / 5.78 | 19 / 4 | 23.2 / 19.8 | 4796 samples, 34 packets |

All three runs passed every check, and no stream fault was reported. The tone
gains matched the analytic response to better than 1e-5 dB. The sweep curve
matched within 0.045 dB at 160 Hz, its largest deviation.

- **Reading the timing columns:**
  - Node B trails by one 100 ms batch, by design.
  - Cross-host packet latency includes the VirtualBox bridge and the offset
    between the two NTP-synchronized clocks, which `chronyc` reported at
    about 0.5 ms.
  - The stream reports p50/p99 only as power-of-two microsecond buckets, so
    the table gives the exact minimum, mean and maximum.
- **Cross-toolchain result:** node B on Oracle Linux, built with GCC 15.2,
  produced bit-identical output to the one-process run built with GCC 16 on
  Ubuntu.

**Studio:** the example was also run by Studio's **Run**, the local
one-process path, through a one-off run of the bundled-example GUI test. That
test normally skips distributed examples, to keep recordings out of the test
directory. Its output was bit-identical to the host → VM deployment: 0 of
720 000 samples differ.

### Defect found and fixed: Studio Run of any graph with a sink failed

- **Symptom:** a finished Studio **Run** of a diagram with a `RecordingSink`
  reported failure ("Output port out of range for element …") after
  recording everything.
- **Cause:** `runtime::collectSamples` logs every element's final output and
  assumed every element has one. Sinks (recordings, network and device
  outputs) have none.
- **Fix:** it now skips them.
- **Regression check:** `s7studio_run_lifecycle` (`runWithASinkCompletes`)
  uses an input-only test element. It fails without the fix.

### Defect found and fixed: imprecise JSON numbers

The first local run differed from the one-process run in the last bit of
29 % of the output samples. The cause:
- Boost.JSON's default parser is not correctly rounded.
  "0.11512925464970229" (the equalizer's ln 10 / 20) became the
  neighbouring double.
- A local file therefore loaded a different parameter value than a deployed
  graph, whose control plane already parsed precisely. The difference
  propagated through `exp()` into the mid-band gain.
- Library files, schema files, the bundled schema library, restored
  deployments and Studio documents, clipboard and parameter entry all used
  the imprecise parser.

All production parsing now uses `s7::parse_json` (`s7core/include/s7/json_parse.hpp`,
correctly rounded numbers). `s7core_schema_elements_smoke` checks that
an element library file keeps 0.11512925464970229 exactly; the check fails
with the old parser.

### Limits of this evidence

- **Not two physical hosts.** The VM shares the CPU, memory and NIC of the
  development host. Repeat `two-host` on two physical Linux hosts with the
  same commands, and archive the reports, before the final release
  qualification.
- **Studio observation:** Studio observes local runs only. A deployed graph is inspected
  through its recordings (`RecordedSource` into a Scope or Live Spectrum, or
  the WAV files). Live remote observation is not a 0.2.0 requirement.
- **Timing claims:** timing and resource figures are measurements of these
  runs, not guarantees. The development host is several times faster than
  typical student machines (see section 1 for the CPU-quota emulation).

## 5. Next steps

1. Run `two-host` on two physical hosts for the release candidate and add the
   report to the release evidence.
2. Local audio loop design (microphone → second local node → speakers), a
   0.3.0 track item.

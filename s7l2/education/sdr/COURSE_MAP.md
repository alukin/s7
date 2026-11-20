# SDR and Digital Communications — 24 MIT-aligned S7 labs

**Proposal as of 2026-11-20. 24 lab IDs planned; 0 implemented or
qualified by this document.** These are *original* S7 exercises illustrating
MIT subject topics. Refer to [SOURCES.md](SOURCES.md) for course editions,
source links and the distinction between topic alignment and exact MIT
lab reproduction. The [S7 teaching standard](../LAB_STANDARD.md) controls
what may be advertised as independently verified.

## Notation

- **SIM**: deterministic, offline headless S7 diagram + independent oracle.
- **IQ**: replay a well-defined complex-I/Q dataset through the same processing
  path, with capture metadata, artifact hash and gap treatment.
- **RX**: opt-in real SDR reception.
- **CABLE**: approved TX/RX through a **properly attenuated, terminated coax
  path**; never connect an arbitrary TX output directly to an RX input.
- **OTA**: optional, explicitly authorized on-air TX/RX with recorded regional
  compliance and measured spectral behavior.
- **CAP**: likely new S7 capability (only after inspecting actual source/API;
  label this **candidate gap**, not a proven missing feature).

Modes after SIM are *targets*, not currently guaranteed to run.

## A. Physical signal and receiver foundations (SDR01–SDR07)

| ID | Learning objective / original experiment | Primary MIT alignment | Target modes | Independent first oracle and negative case | Capability investigation |
| --- | --- | --- | --- | --- | --- |
| **SDR01** | Complex I/Q, analytic representation, positive/negative frequencies; rotate known phasor | MIT-450 ch. 4–5; MIT-02 ch. 13 | SIM, IQ, RX | Closed-form complex sinusoid/FFT bins; deliberately conjugate Q and show spectrum inversion | Complex-source/observer review |
| **SDR02** | Bandpass-to-baseband sampling, aliasing, quadrature sampling | MIT-02 ch. 13; MIT-450 ch. 4 | SIM, IQ | Direct sampled-sine identity; change sample rate across Nyquist boundary | Explicit rate metadata |
| **SDR03** | FFT, windowed PSD, dynamic range, waterfall and spectral leakage | MIT-02 ch. 13; MIT-450 ch. 7 | SIM, IQ, RX | Direct DFT, energy/PSD normalization; wrong window/ENBW test | Calibrated spectrum/waterfall path |
| **SDR04** | Real receiver controls, IQ DC spike, I/Q imbalance, noise floor, clipping | MIT-02 ch. 9–10; MIT-450 ch. 3, 7 | SIM, IQ, RX | Parameterized imbalance/image-rejection and quantization equations; deliberate clipping | Device probe, capture metadata, gain readback |
| **SDR05** | Heterodyne frequency translation and complex downconversion | MIT-02 ch. 14; MIT-450 ch. 6 | SIM, IQ, RX | Exact mixer frequency shift and complex carrier residual; sign error | NCO/mixer as visible causal elements |
| **SDR06** | FIR channel selection and decimation with anti-alias protection | MIT-02 ch. 11–12; MIT-965 Week 3 | SIM, IQ, RX | Direct convolution + polyphase reference; omit anti-alias filter | Explicit multirate processing or bounded alternate |
| **SDR07** | AGC, gain headroom, ADC quantization and effective SNR | MIT-450 ch. 3, 7; MIT-02 ch. 9 | SIM, IQ, RX | Closed-form AGC recurrence and quantizer thresholds; overload/recovery | Observable AGC and absolute relative-gain semantics |

## B. Modulation, detectors and BER (SDR08–SDR15)

| ID | Learning objective / original experiment | Primary MIT alignment | Target modes | Independent first oracle and negative case | Capability investigation |
| --- | --- | --- | --- | --- | --- |
| **SDR08** | AM envelope vs coherent demodulation, phase and filter settling | MIT-02 ch. 14; MIT-450 ch. 6 | SIM, IQ, CABLE, OTA optional | Analytic AM sidebands and envelope; phase-error test | Modulator/detector graph; TX test owner |
| **SDR09** | FM discriminator, instantaneous phase derivative and deviation | MIT-965 RF receiver topic; MIT-02 ch. 14 (broad) | SIM, IQ, CABLE | Exact FM phase increments; wrong unwrap/deviation test | Complex angle/difference and receiver filter |
| **SDR10** | OOK/ASK and preamble/threshold detection | MIT-02 ch. 4, 14 | SIM, IQ, CABLE | Reference symbol waveform and threshold errors vs SNR; drifting threshold | Symbol/framing elements; controlled TX |
| **SDR11** | Binary FSK and continuous-phase modulation | MIT-02 ch. 14 (general digital modulation); MIT-450 ch. 6 | SIM, IQ, CABLE | Analytic tone spacing/phase accumulation; nonorthogonal separation | State-owned phase modulator, correlator |
| **SDR12** | BPSK and PAM, waveform energy and coherent symbol decisions | MIT-450 ch. 5–6 | SIM, IQ, CABLE | Euclidean decision boundaries + AWGN expectation; sign/phase error | Constellation viewer |
| **SDR13** | QPSK and 16-QAM, Gray labeling and EVM | MIT-450 ch. 5–6, 8 | SIM, IQ, CABLE | Exact constellation geometry/energy and symbol mapper; rotation/normalization failure | Mapper/slicer and EVM observer |
| **SDR14** | Pulse shaping, root-raised-cosine matched pair and ISI | MIT-450 ch. 4, 6, 8 | SIM, IQ, CABLE | Analytic tap generation + direct convolution, Nyquist samples; mistimed sample | RRC taps, matched-filter path |
| **SDR15** | BER/SER versus Eb/N0, seeded AWGN and optional simple coding | MIT-450 ch. 7–8; MIT-02 ch. 5–8 | SIM, IQ, CABLE | BPSK/QPSK theory + confidence intervals across seeds; symbol/bit mismatch | BER counters, reproducible RNG, alignment |

## C. Synchronization, channel and OFDM (SDR16–SDR21)

| ID | Learning objective / original experiment | Primary MIT alignment | Target modes | Independent first oracle and negative case | Capability investigation |
| --- | --- | --- | --- | --- | --- |
| **SDR16** | Carrier frequency offset, phase recovery and phase ambiguity | MIT-973 L17–19; MIT-450 ch. 6 | SIM, IQ, CABLE | Inject known CFO/phase, compare estimated residual and cycle slips; capture-range failure | NCO, FLL/PLL/Costas loop state |
| **SDR17** | Symbol timing recovery, interpolation and clock-rate error | MIT-973 L17–19; MIT-450 ch. 4 | SIM, IQ, CABLE | Known fractional-symbol offsets and recovered event indices; timing-drift failure | Explicit resampling and timing-recovery contract |
| **SDR18** | Frame synchronization and correlation-based packet acquisition | MIT-02 ch. 4, 9; MIT-973 L17–19 | SIM, IQ, CABLE | Exact sliding correlation/preamble peak; false alarms/missed packet | Framing, packet metadata and gap treatment |
| **SDR19** | Multipath, channel estimation and linear equalization | MIT-973 L2, L20–21; MIT-450 ch. 9 | SIM, IQ, CABLE | Known FIR channel + LS estimate/normal equations; ill-conditioned channel | Adaptive/linear equalizer elements |
| **SDR20** | OFDM orthogonality, IFFT/FFT and cyclic prefix | MIT-973 L3–4, L8–10 | SIM, IQ, CABLE | Direct DFT small-N, orthogonality and circular-convolution reference; short CP | Whole-symbol buffering and explicit framing |
| **SDR21** | OFDM CFO/timing, pilots, channel estimation and EVM | MIT-973 L17–21 | SIM, IQ, CABLE | Known pilots/channel, CFO and symbol boundary; swapped pilot bins | Multi-frame synchronization, rate and status evidence |

## D. Practical radio and research experiments (SDR22–SDR24)

| ID | Learning objective / original experiment | Primary MIT alignment | Target modes | Independent first oracle and negative case | Capability investigation |
| --- | --- | --- | --- | --- | --- |
| **SDR22** | Reproduce a measured TX → channel → RX chain; compare simulation with calibrated coax loop and permitted OTA | MIT-965 practical RF experimentation (topic alignment; **S7-original procedure**) | SIM, IQ, CABLE, OTA optional | Known pilots/energy, expected attenuation, measured EVM/CFO; insufficient attenuation and clipping must fail safely | Bounded RF TX, duplex or coordinated separate runs, safe stop/drain |
| **SDR23** | Receive and decode ADS-B (1090 MHz), preamble, PPM bits, valid frame CRC/parity | [MIT-965 Week 8 ADS-B lab](https://rfsoc.mit.edu/6S965/F25/assignments/week08/rfsoc_03) | SIM, IQ, RX | Synthetic valid/invalid ADS-B frames, pulse timing and CRC/parity per applicable format; wrong rate/noise failure | Streaming burst detection, decoder test vectors, receiver continuity |
| **SDR24** | Compare independent SDR sample clocks, CFO, latency, coherence and cross-device correlation | MIT-973 L20–21; MIT-450 ch. 9 (**S7-original extension**) | SIM, IQ, CABLE, OTA only where lawful | Two generated streams with known offset/drift, cross-correlation and statistical uncertainty; unsynchronized-clock negative case | Multi-device capture, timestamp provenance, transport/clock mapping |

**24 total** (7 foundations + 8 modulation + 6 receiver systems + 3 applied).
These are separate educational objectives and acceptance records, not 24
claims that every hardware configuration is interchangeable.

## Suggested learning paths

- **Senior undergraduate**: SDR01–08, SDR10, SDR12–15, SDR23 with an
  RX-only or offline ADS-B option.
- **Engineer**: SDR01–07, SDR16–18, SDR22–23, emphasizing traceable RF
  measurement, capture continuity, parameter reporting and recovery.
- **PhD/advanced engineer**: SDR16–24, including OFDM and multi-device
  synchronization, with optional numerical and experimental extensions.

Progress from synthetic impairments to recorded captures to live devices
rather than starting with uncontrolled RF and explaining failures afterward.
The reference signal-processing graph should not change merely because a
source switches from SIM to IQ or hardware when it is meaningful to compare.

## Shared prerequisites and deliberate boundaries

- Apply the inherited [education LAB_STANDARD](../LAB_STANDARD.md) and
  [verification matrix](VERIFICATION_MATRIX.md) to *every* lab.
- Use the already implemented 24 DSP/control examples as **engineering
  references**, but author a new purpose-built saved diagram for each
  delivered SDR lab (or documented comparison pair).
- A lesson must **record** center frequency, tuned LO, effective sample
  rate, bandwidth, gain, antenna/connection, sample format, frames, drops,
  hardware/host timestamp provenance and fixture identity whenever relevant.
- Real transmitter labs require an authorized operator, explicit opt-in,
  physically safe coaxial fixtures or documented spectrum compliance. The
  2.4 GHz ISM/Wi-Fi allocation is **not blanket permission** for arbitrary
  SDR waveforms, powers or antennas.
- The current runner's one-boundary/one-rate limitation is a **tracked
  engineering dependency**, not grounds to abandon the 24-lab roadmap.
  Any change to type, rate, pacing or causal graph semantics requires an
  explicit architectural decision and regression tests, not an ad hoc
  shortcut in an individual example.
- Do not equate an RF observation or a valid-looking plot with pass/fail
  evidence. Successful graph math, transport delivery and physical RF
  correctness are distinct claims.

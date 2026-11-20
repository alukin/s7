# MIT-aligned audio and effects — twelve proposed S7 laboratories

**Planning map (2026-11-20): 12 original lab IDs, none implemented or
qualified by this document.** Primary source: [6.341 lecture notes](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/pages/lecture-notes/)
and detailed [source ledger](SOURCES.md). Topic alignment does not imply
MIT provided these specific laboratory exercises.

Every lab ultimately has: a purpose-built `.s7diagram` and student guide,
an independent mathematical/numerical reference, a deliberate failure
experiment, a verified Studio workflow and, when practical, an audible
FILE/LIVE run with exact hardware and latency evidence.

## Lab sequence

| ID | Original S7 experiment | MIT theoretical alignment | First independent oracle and deliberate failure | Profiles / engineering target |
| --- | --- | --- | --- | --- |
| **AU01** | **WAV source to speaker.** Read known mono/stereo PCM, inspect samples and spectrum, produce file and audible playback | 6.341 L5, L6, L15–17 (sampling/quantization/DFT) | Exact PCM read/write, channel order, sample count, dBFS, clipping/EOF and trailing-output check | SIM, FILE, standalone LIVE output; WAV adapter, bounded playback drain |
| **AU02** | **Live microphone/guitar monitoring.** Hardware input -> identity S7 causal graph -> headphone output | **S7-original systems lab**, 6.341 L4–5 for timing context | Expected sample mapping and impulse delay with known test injection; underrun, overflow, device removal | SIM, virtual duplex, physical LIVE; **first primary engineering milestone**, measured physical latency |
| **AU03** | **Tone control and equalizer.** FIR/IIR shelving and three-band response, tune interactively | 6.341 L2, L7–9 | Exact difference equation, poles/zeros and complex H(e^jw); unstable coefficient and zipper noise failure | SIM, FILE, LIVE; safe runtime coefficient updates |
| **AU04** | **Echo and feedback delays.** Delay taps, dry/wet mix, decay and feedback stability | 6.341 L2–4, L7–8 (group delay and IIR) | Analytical impulse train or explicit delay recurrence; wrong extra graph-step delay and unstable feedback | SIM, FILE, LIVE; exact circular delay buffer and bypass |
| **AU05** | **Reverberation.** Comb/all-pass networks, impulse response and decay control | 6.341 L3, L7–8 (all-pass/IIR topology) | Algebraic filter recurrences, known impulse decay and stability; incorrect initialization/feedback | SIM, FILE, LIVE; stateful multi-filter stress |
| **AU06** | **Guitar overdrive.** Hard/soft clipping, waveshaping, harmonics and optional antialiasing | 6.341 L6, L9–11 (quantization and multirate DSP; **S7-original effect**) | Exact waveshaper and seeded multi-tone harmonic content; aliased products and excessive amplitude | SIM, FILE, LIVE; Hi-Z path, bounded volume, oversampling if supported |
| **AU07** | **Noise gate, compressor, limiter.** Envelope follower, attack/release and dynamic range | 6.341 L7–9 (IIR filter structures; **S7-original dynamics experiment**) | Discrete envelope/gain recurrences and dB transfer curve; pumping, overshoot and unsafe limiter assumptions | SIM, FILE, LIVE; parameter handling and level meters |
| **AU08** | **Chorus/flanger.** LFO-modulated fractional delay, feedback and comb-notch movement | 6.341 L2–4 (phase/all-pass/fractional delay) | Interpolation taps and exact test-signal delay; discontinuous delay jumps, polarity error | SIM, FILE, LIVE; phase-continuous LFO, interpolated delay |
| **AU09** | **Tremolo and vibrato.** Amplitude vs time modulation, rate/depth comparison | 6.341 L4–5 (fractional delay/rate conversion; **S7-original effect**) | Sine-envelope sidebands and known time-varying interpolation; negative phase/zero-depth failures | SIM, FILE, LIVE; stable rate/phase ownership |
| **AU10** | **Pitch shifting/time stretching.** Two approaches; discuss artifacts rather than promise studio quality | 6.341 L4–5, L10–11, L21–22 | Reference resampling and one short-time method with controlled chirp; pitch/time coupling and transient smearing | SIM, FILE, optional LIVE; variable frame/rate and documented delay |
| **AU11** | **FFT convolution/spectral effects.** Block convolution and live vs offline tradeoffs | 6.341 L15–22 (DFT, FFT, STFT and filtering) | Independent O(N²) DFT/direct linear convolution on small vectors; wrap-around, wrong overlap/tail | SIM, FILE, optional LIVE; block FIFO/frame contracts and latency measurement |
| **AU12** | **Guitar pedalboard capstone.** Live input -> selectable effects chain -> audio output with comparisons | 6.341 L7–11, L15–21 as theoretical foundation; **S7-original systems capstone** | Identity baseline; each active element independently tested; bypass/dry-wet, cumulative delays, stress/dropouts | SIM, FILE, LIVE; live-safe parameter changes, latency/dropout dashboard |

## Levels and implementation order

- **First vertical slice: AU02 identity pass-through**. It may need
  core runtime development **before** its student diagram can execute
  through real audio. Implement AU01 deterministic WAV fixture support
  in parallel as the reproducible audio test substrate.
- **Core effects: AU03/AU04/AU06/AU07.** With a proven full-duplex base,
  these produce an audible, useful guitar signal-processing chain.
- **Advanced effects: AU05/AU08–AU11.** These deliberately stress causal
  state, feedback, fractional delay, block/frame processing and latency.
- **Capstone: AU12** combines the proven components and records both
  numerical fidelity and device-paced latency/dropout measurements.

## What is out of scope

No music theory, note/chord recognition, beat detection, composition or
performance instruction; the guitar is a **signal source** and an excellent
latency detector. No acoustic localization, distributed microphones, and
no 24-lab mandate. UI menus/knobs are desirable but no GUI component
may change audio/graph semantics or do work in an audio callback.

A user-provided WAV file, microphone or instrument signal is valid input
for a practical session, but automated tests must use synthetic/licensed
reproducible test fixtures and require no sound card.

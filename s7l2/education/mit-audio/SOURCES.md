# MIT and supplementary reference ledger — S7 mit-audio

**Reviewed: 2026-11-20.** The original S7 laboratory objectives below
illustrate relevant theory. They do **not** reproduce specific MIT
homework, project answers or lecture figures, and MIT does not endorse S7.

## Primary: MIT 6.341 (Alan V. Oppenheim, Fall 2005)

- [Official course](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/)
- [Lecture notes index](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/pages/lecture-notes/)
- [Syllabus and lecture calendar](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/pages/syllabus/)
- [Project index](https://ocw.mit.edu/courses/6-341-discrete-time-signal-processing-fall-2005/pages/projects/) — authentic audio-related projects and WAV files are indexed. **Do not bundle them** without rights/provenance review.

| Lectures (as titled on official index) | Relevant S7 topic |
| --- | --- |
| 2: Phase, Group Delay and Generalized Linear Phase; 3: Minimum-phase and All-pass Systems | AU03/AU05/AU08 transfer phase and all-pass effects |
| 4: DT Processing of CT Signals and CT Processing of DT Signals: Fractional Delay | AU04/AU08/AU09 changing delay |
| 5: Sampling Rate Conversion; 10–11: Multirate Systems and Polyphase Structures | AU01/AU10 sample conversion, timing, interpolation |
| 6: Quantization and Oversampled Noise Shaping | AU06 clipping/distortion and noise behavior (topical, not an exact guitar assignment) |
| 7: IIR, FIR Filter Structures; 8: Filter Design: IIR Filters; 9: Filter Design: FIR Filters | AU03–AU05 tone control, EQ, echo and reverb topology |
| 15: DFT; 16: Linear Filtering with the DFT; 17: Spectral Analysis with the DFT; 18: Periodogram; 19: FFT Algorithms | AU01/AU11/WAV analysis, block convolution and exact normalization |
| 20: Goertzel Algorithm and Chirp Transform; 21: Short-time Fourier Analysis; 22: Modulated Filter Bank | AU10–AU11 spectral/time-domain comparison |

Source caveat: **physical real-time guitar performance, audio callback
architecture, dry/wet crossfading, overdrive, compression, chorus/flanger,
realtime latency experiments and a pedalboard** are **new S7 educational
extensions** informed by the above methods; they are not assigned MIT
6.341 labs. For each delivered guide, record a specific lecture link/title
and the distinction between topic alignment and original extension.

## Supplementary engineering references (non-MIT)

- [PortAudio API documentation](https://www.portaudio.com/docs/): audio
  host stream and callback behavior. Device and host latency estimates
  are not physical loopback guarantees.
- [PipeWire documentation](https://docs.pipewire.org/): Linux native
  device graphs, stream negotiation, buffers and latency configuration.
- [S7 hardware contract](../../../doc/S7_Hardware_IO_Contract.md),
  [implemented sound I/O](../../docs/HARDWARE_IO.md),
  [live-test evidence](../../docs/HARDWARE_IO_LIVE_TESTS.md),
  [hardware probe instructions](../../tools/hardware/README.md).
- [S7 numerical and DSP teaching sources](../mit-ee/README.md):
  prerequisite filter, Fourier, convolution, frequency-domain and
  numerical verification exercises.

## Attribution and fixture policy

Each lab guide must carry (1) exact MIT edition/lecture topic and date
checked, (2) original S7 mathematical model and diagram, (3) independently
computed oracle, (4) sample/fixture provenance and license, (5) tests and
result evidence. Prefer self-generated impulses, chirps, and synthetic
or maintainer-recorded short audio. Do not include recorded commercial
music, textbook pages, lecture graphics or MIT WAV sample packs by
assumption; check reproduction rights first. Student-supplied guitar/mic
content is their own fixture and should not silently be uploaded, shared,
or installed as a system sample.

The objective is sound and correct S7 engineering, not reproducing the
MIT course's historic MATLAB/Athena environment.

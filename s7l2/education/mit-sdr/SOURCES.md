# MIT-aligned SDR teaching sources and attribution record

**Source review: 2026-11-20.** This file records the public sources used
to organize a *new, original* S7 curriculum. **Source-reviewed** below means
the cited course page explicitly covers the topic; it does not certify that
any S7 diagram reproduces a particular problem, nor does it make the lab
numerically correct. Check the linked resource again when implementing a
lab; sites, course numbers and downloadable artifacts can change.

## Primary: MIT

| Key | Source (edition) | Verified public content and appropriate topics | Limits |
| --- | --- | --- | --- |
| MIT-02 | [6.02 Introduction to EECS II: Digital Communication Systems, Fall 2012](https://ocw.mit.edu/courses/6-02-introduction-to-eecs-ii-digital-communication-systems-fall-2012/pages/readings/), *Bits, Signals, and Packets* | ch. 4 digital signaling; 5–8 error control; 9 noise; 10 channel models; 11 convolution; 12 frequency response; 13 Fourier analysis; 14 modulation/demodulation | Historical undergraduate course; chapter-level **topic mapping**, not copied problem text |
| MIT-450 | [6.450 Principles of Digital Communications I, Fall 2006 lecture notes](https://ocw.mit.edu/courses/6-450-principles-of-digital-communications-i-fall-2006/pages/lecture-notes/), Gallager and Zheng | ch. 3 quantization; 4 source/channel waveforms; 5 signal spaces; 6 channels/modulation/demodulation; 7 noise; 8 detection/coding; 9 wireless communication | Graduate theory; verify exact chapter subsection before quoting theorem or mapping a named example |
| MIT-973 | [6.973 Communication System Design, Spring 2006 lecture index](https://ocw.mit.edu/courses/6-973-communication-system-design-spring-2006/pages/lecture-notes/), Stojanovic | L2 practical digital communications; L3 multitone systems; L4 802.11a architecture; L8–L10 FFT; L17–19 synchronization; L20 wireless channels; L21 channel estimation; L11–16 coding/Viterbi | Some listed lectures have **no downloadable notes**. Topic alignment is supported by the index, not claimed textbook/PDF verification. FPGA/ASIC-specific content is not required |
| MIT-965 | [6.S965 Digital Systems Laboratory II, Fall 2025 assignments](https://rfsoc.mit.edu/6S965/F25/assignments) | Week 3 FIR deployment; later RFSoC reception; [Week 8 ADS-B reception/decoding](https://rfsoc.mit.edu/6S965/F25/assignments/week08/rfsoc_03) | RFSoC board is not Pluto/LibreSDR; adapt **learning objectives**, not FPGA-specific code, wiring or assignment instructions |
| MIT-6040 | [MIT Fall 2026 catalogue, 6.6040 Digital Systems Laboratory II](https://websis.mit.edu/catalog/m6b.html) | Catalog context for newer subject number | **Do not** attribute archived 6.S965 lab details to a 6.6040 course without checking its published materials |
| MIT-SP | [6.3000 Signal Processing, Fall 2026](https://sigproc.mit.edu/fall26) and S7's [existing teaching sources](../mit-ee/SOURCES.md) | prerequisite DSP, Fourier, filtering, DFT, STFT, convolution | Existing S7 materials are foundations, not new SDR labs |

Other publicly available MIT work (e.g. 6.011 signals/inference, Haystack
Digital RF) may be linked when a specific advanced lab needs it, but must not
be used to invent a purported MIT SDR assignment.

## Supplementary: practical SDR learning

| Key | Source | Use | Rights / caution |
| --- | --- | --- | --- |
| AD-BOOK | [Analog Devices — *Software-Defined Radio for Engineers*](https://www.analog.com/en/resources/technical-books/software-defined-radio-for-engineers.html), Collins, Getz, Pu, Wyglinski (2018) | Architecture, baseband signal processing, synchronization, OFDM, Pluto examples | Free PDF to read does **not** imply blanket permission to redistribute content |
| AD-LABS | [Analog Devices communications laboratory materials](https://wiki.analog.com/university/labs/comms) and [textbook/lab repository](https://github.com/sdrforengineers/LabGuides) | Practical labs, transmitter/receiver exercises, capture measurements | ADI wiki permits noncommercial academic use under attribution conditions; GitHub content carries its own per-repository licensing; do not copy examples without exact license review |
| AD-HW | [Pluto system documentation](https://developer.analog.com/docs/system-level/solutions/platforms/pluto/index.html), [hardware notes](https://developer.analog.com/docs/system-level/tools/pluto/hacking/hardware.html), [AD936x board overview](https://developer.analog.com/docs/system-level/solutions/reference-designs/fmcomms2/common/introduction_to_ad9361_ad9364.html) | ADALM-Pluto Rev. B nominal RF profile, signal chain and device caveats | Do not assume modified firmware ranges or undocumented RFIC/channel wiring |
| SOAPY | [SoapyPlutoSDR plugin](https://github.com/pothosware/SoapyPlutoSDR) and S7 [hardware guide](../../docs/HARDWARE_IO.md) | Discover actual driver capabilities, support USB/libiio setup; do not hardwire to Pluto API | Backend behavior depends on device, driver version and firmware |
| PYSDR | [Marc Lichtman, *PySDR: A Guide to SDR and DSP using Python*](https://pysdr.org/) | Accessible reading and independent numerical/Python illustration | Check current site license before reuse; link rather than bundle chapters or figures |
| HMC | [Jason Gallicchio, Harvey Mudd College, *Learn SDR*](https://gallicchio.github.io/learnSDR/) | Video/flowgraph lessons on complex/negative frequencies, QPSK, matched filtering, FLL, timing recovery, Costas loops; newer optional lessons include OFDM and noise | Videos were recorded in 2021 (GNU Radio 3.8); site reports GNU Radio 3.10 flowgraphs retested October 2026. GNU Radio flowgraphs are *inspiration*, not S7 deliverables |

## Examples of legitimate lab mappings

- **SDR01–SDR03**: MIT-450 ch. 4 and MIT-02 ch. 13; AD-BOOK and
  HMC complex-number/sampling/spectrum videos.
- **SDR04–SDR07**: MIT-02 ch. 9–12, MIT-450 ch. 3 and MIT-965 FIR;
  AD-HW/AD-LABS for physical radio interpretation.
- **SDR08–SDR11**: MIT-02 ch. 4/14 and MIT-450 ch. 6; AD-BOOK, HMC.
  For FM/FSK, **topic alignment is broader than an exact MIT exercise**;
  distinguish new S7 designs from named course problems.
- **SDR12–SDR15**: MIT-450 ch. 4–8, MIT-02 ch. 5–9;
  HMC QPSK/pulse-shaping lessons.
- **SDR16–SDR21**: MIT-973 L3–4, L17–21; MIT-450 ch. 9;
  AD-BOOK synchronization/OFDM and HMC FLL/Costas/timing/OFDM.
- **SDR22**: MIT-965 as a hardware experimentation precedent;
  Pluto-specific TX/RX procedure is **S7-original** and subject to
  hardware qualification.
- **SDR23**: MIT-965 Week 8 *ADS-B with the RFSoC*: this is the
  **closest specific MIT practical lab**. The S7 implementation should
  use its own diagrams and test data and cite the MIT assignment page.
- **SDR24**: MIT-973 wireless-channel topics and MIT-450 wireless theory;
  *multi-device clock/coherence measurements are an S7-original extension*,
  not an MIT assignment.

## Per-lab provenance ledger (mandatory during implementation)

Before marking a lab `source-reviewed`, record in its guide:

1. Exact MIT course code, term, **direct chapter/lecture/lab URL**, verified
   section/title and date accessed; distinguish topic alignment from a
   claim of specific example reproduction.
2. Independent S7 statement of the mathematical system, units, sampling
   and time-index conventions, initial conditions and expected negative case.
3. Non-MIT supporting links with edition/lesson identifiers where available.
4. Dataset and code provenance, exact license, attribution and permitted
   redistribution. Use generated synthetic fixtures by default.
5. Source-to-implementation and reference-test paths. Track separately:
   source verification, diagram acceptance, numerical verification, Studio
   verification and RF qualification.

Do not embed copyrighted textbooks, figures, MIT logos, video transcripts,
problem sheets or third-party code unless specific license grants and
requirements have been checked. Cite originals by URL. Describe all S7 labs
as **original labs illustrating topics taught in MIT courses**.

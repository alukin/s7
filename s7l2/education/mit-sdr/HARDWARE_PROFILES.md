# SDR hardware profiles, RF operation and measurement evidence

**Planning status: 2026-11-20. No device was opened or transmitted by
preparing this file.** Device capability fields below are *questions to
measure*, not manufacturer specifications or S7 conformance claims.

## Bench inventory and qualification targets

| Profile | Equipment | Intended role | Must measure / qualify |
| --- | --- | --- | --- |
| P1 | ADALM-PLUTO **Rev. B** | Reference student RX, optional controlled TX, cross-check with AD teaching examples | Actual RFIC/firmware, SoapyPlutoSDR/libiio, RX/TX channel count, tune range, gain, per-mode rates, clock and timestamps |
| L1 | LibreSDR #1 (Zynq-7020 / AD936x-family) | Independent RX/TX, comparison with P1 | Actual chip ID (reported model need not prove physical chip), Soapy device enumeration, connected RF channels, full-scale representation, available IIO interfaces |
| L2 | LibreSDR #2 (Zynq-7020 / AD936x-family) | Repeatability, clock-offset, future two-device experiments | Same as L1, plus measured clock drift and independent gain/phase stability |
| REF | S7 synthetic and recorded-IQ fixtures | Always-available non-transmitting reference | Sample generation/provenance, sample rate, normalization, signal power, time base and gap annotations |

No specific RX/TX connector mapping, multi-channel phase coherence, external
reference input, 2x2 transceiver behavior, firmware modification or hidden
device-mode capability is assumed. A driver enumerating a channel does not
prove that a physical RF connector works.

The public Soapy backend is implemented and documented in
[HARDWARE_IO.md](../../docs/HARDWARE_IO.md); [bounded prior real-radio
observations](../../docs/HARDWARE_IO_LIVE_TESTS.md) are a baseline but do
**not** qualify all three devices in all conditions. Verify each backend's
driver/version readback, not just the model printed on an enclosure.

## Fixture and operator safety

- **SIM / IQ**: transmitters remain closed. Never require live hardware to
  pass normal CI, install checks or default test commands.
- **RX**: opt-in; appropriate antenna/front-end, tunable receiver setup,
  documented gain; receive-only is not permission to monitor protected
  communications. Avoid recording personal data and use publicly receivable
  signals or self-generated tests.
- **CABLE TX/RX**: use a *calculated, characterized attenuator chain*,
  compatible 50-ohm terminations/cables and (where relevant) isolation
  and DC blocking. Choose attenuation from measured/source maximum power
  and receiver safe input limit; never guess a one-size-fits-all dB value.
  Verify connection and instrument-calibrated output level before enabling
  TX. Never connect RF TX output directly to RX; do not connect two TX
  ports. Start with TX disabled, low gain, explicit finite duration and
  verified stop/abort path. An unmatched/damaged cable is not a lab feature.
- **OTA**: no implicit transmitter permission. The fact that 2.4 GHz hosts
  Wi-Fi and ISM devices **does not** make every SDR waveform, emission
  power, duty cycle or antenna legal. Check **current** applicable Ukrainian
  and local regulations, permitted use category, radio equipment approval,
  EIRP, unwanted emissions, occupied bandwidth and channel-sharing rules
  for *the specific radio/device/test*. Record evidence and supervisor
  authorization. Other jurisdictions need independent checks. If not
  satisfied, execute the identical lab via SIM/IQ/CABLE instead.
- **Other users of the spectrum**: prefer shielded coax experiments; never
  occupy an active channel for a demo without coordination and compliance.
  Spectrum occupancy measurement precedes any optional OTA test.
- **Automatic tests**: separate CI-safe `--check` / fake-provider tests from
  explicit human-authorized RF tests. Hard interlock: no implicit RX/TX from
  opening a diagram or running the documentation index.

Regulatory starting point: [Ukraine's NCEC radio spectrum/register information](https://nkek.gov.ua/en/activity/fields-of-activity/radio-frequency-spectrum/register-of-radio-equipment-and-radiating-devices).
This is not a legal finding or blanket authorization. Review up-to-date
national and local requirements before any real transmission.

## Hardware evidence record (per independent run)

Use machine-readable JSON/YAML next to a small, legally redistributable
sample fixture, **not** an unverifiable screenshot.

- Lab ID, S7 commit and artifact revision, reference test version, run ID,
  host OS and toolchain, Soapy driver/version, libiio version, kernel,
  firmware, exact device identifier or pseudonymous local label.
- Device mode and direction; physical RF port and fixture topology,
  termination/attenuation/cable losses, estimated input level, antenna
  only if OTA; permitted allocation/regulatory evidence for OTA.
- Requested **and read-back** RF center frequency, sample rate, bandwidth,
  channel IDs, gain/AGC, antenna, RX/TX settings; exact units and full scale.
  Report rejected or rounded settings as observed, not as silently accepted.
- Complex sample format and conversion path, effective software frame
  count, transfer block length, hardware/software drop/overflow/underflow,
  continuity segments and recovery outcome.
- Hardware timestamp presence, clock identity or **unknown**, driver-local
  time epoch, host receive completion, model sample time, applicable rate
  and ppm uncertainty. Software-delivered sample counts are **not**
  hardware acquisition sample counters.
- Measurement formula, reference spectrum normalization, PSD units,
  signal/noise windows, gain interpretation, uncertainty and pass/fail
  tolerance. Don't invent BER or EVM precision.
- Files + hashes and license/provenance; operator and opt-in execution
  command; redaction of serial numbers and exact device identifiers from
  public artifacts where appropriate.

## RF test phases and acceptance

1. **Discovery (no RF transmit)**: enumerate each Soapy device,
   capabilities, rate/bandwidth/gain choices, bounded RX read. Verify
   sample shape/format, stop/cancel and safe release of the device.
2. **RX signal validation**: receive a known allowed external or
   generator-produced (shielded/attenuated) signal. Check frequency
   offset, spectral metrics, continuity and conversion independently.
3. **CABLE TX test**: operator signs off fixture and limits; output finite
   low-level tone/known QPSK training signal and verify receiver input
   and frequency response using a separate read. Stop on an error,
   disconnect or unexpected gain; no infinite TX loops.
4. **Coordinated RX/TX**: after explicit architectural review, qualify
   duplex on one device or interprocess coordinated two-device RX/TX.
   A new implementation must establish lifecycle, pacing, buffering,
   timestamps and error handling. The existing single-boundary runner
   is **not** automatically duplex.
5. **OTA optional**: only with confirmed legal/emission envelope and
   operator authorization; record spectrum and environmental
   conditions. Do not use OTA as the only way to validate a lab.
6. **Multi-device synchronization**: first synthetic known drift,
   then cable fan-out/reference, then independently-clocked radios;
   distinguish arrival-time jitter, sample-rate error, CFO, coherent
   phase and absolute timestamp calibration.

## Priority for capability changes

See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md): hardware capture
record/replay and checked Soapy metadata before simultaneous duplex,
explicit sample-rate domains before general multirate processing, stable
frame segmentation before OFDM, and timing provenance before cross-device
claims. Causality and data loss must never be hidden by convenience code.

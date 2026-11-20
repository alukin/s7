# Hardware I/O contract: sound and SDR

Status: **implementation direction accepted by the maintainer, 2026-11-16;
initial SDK/backend implementation with limited opt-in hardware evidence**.

The maintainer approved proceeding with implementation after this design review.
This document describes the target contract; it does not imply that every
capability has shipped. Section 13 records initial implementation choices and
limits; [the C++ hardware guide](../s7l2/docs/HARDWARE_IO.md) documents actual
APIs and execution. No new wire protocol or deployment behavior is introduced.

This document complements the [formal model](S7_Formal_Model.md),
[sample-rate model](S7_Sample_Rate_Model.md),
[sample-frame profile](S7_Sample_Stream_Profile.md), and
[audio/clock research](S7_Audio_and_Frame_Compression_Research.md).
Those existing contracts remain authoritative for current implementations.

## 1. Agreed scope and module boundaries

| Module/provider | Availability | Responsibility |
| --- | --- | --- |
| `s7audio` | Public | Sound source/sink elements and independent audio implementation |
| PortAudio provider | Public | Cross-platform audio capture/playback through PortAudio |
| Native PipeWire provider | Public, Linux | Direct PipeWire capture/playback and native device/stream integration |
| `s7sdr` | Public | Common SDR element/device contracts, capability descriptions and provider integration |
| SoapySDR provider | Public, optional dependency | Access to installed SoapySDR device modules |
| AD936x provider | Private, optional dependency | LibreSDR and Pluto support through the locally installed private AD93xx library |

Both audio providers are required design targets; PortAudio is not a replacement
for native PipeWire. Both SDR providers are required design targets; SoapySDR is
not a replacement for the private AD936x implementation. Provider support is
qualified separately, not inferred from sharing the same S7 interface.

Public sound code is implemented from public audio APIs and documentation.
Private source, binaries, repository access details and implementation fragments
are not copied into the public S7 tree or its release packages. The private
provider is separately built against the installed public S7 SDK and locally
installed private dependency. Public builds never fetch that dependency.

The initial public SDK is `s7hardware`; public element names are `AudioSource`,
`AudioSink`, `SdrSource`, and `SdrSink`. No new graph value type is introduced.

## 2. Invariants and units

1. Causality follows graph connections and causal memory. Device callbacks,
   worker completion and packet arrival never create graph dependencies.
2. A channel value is one declared real or complex value. I and Q are components
   of one complex value, not two independently scheduled graph channels.
3. A sample frame contains one value from every selected channel, in declared
   order. Physical simultaneity and RF phase coherence require separate evidence.
4. A device block contains a contiguous range of sample frames. Device quantum,
   application block size, transport packet and logical graph step are distinct.
5. The first element adapter consumes/produces one simultaneous sample frame per
   logical evaluation, following the current scalar channel-port model. Bulk
   execution requires its own equivalence/causality review.
6. Types, rates, channel order and changes of interpretation are explicit.
   Providers never insert graph resamplers or implicitly normalize I/Q values.
7. Bounded storage, partial progress, discontinuities and cancellation are part
   of the interface. Unknown evidence is represented as unknown.

Local hardware blocks are independent of network packet encoding and its
channel/payload limits. Network adapters must enforce the existing profile's
limits explicitly; they cannot change hardware semantics to fit a packet.

## 3. Devices, sessions and streams

The contract distinguishes three owners:

- **Provider:** discovery, supported operations and factories. It owns no hidden
  process-wide live device or shared stream state.
- **Device session:** one explicitly opened physical device or OS audio endpoint,
  its control state, resource ownership and any coordinated RX/TX directions.
- **Stream:** one direction, ordered channel selection, immutable negotiated
  descriptor, transfer state and segment identity.

Live state belongs to a run/session owner. Two elements may share a device only
through an explicit session reference; the same URI in two elements must not
silently create shared mutable state. Compatible full-duplex elements can share
one session while retaining independent direction owners and results.
The session owns external resources and control coordination; logical evaluation
history remains element-owned. Sharing a session does not transfer graph values
between elements or establish a causal feedback path.

Discovery reports provider ID, backend selector, display information, candidate
stable identity and identity provenance. Model names, channel counts and kernel
versions are not physical-device identities. Ambiguous USB/network aliases stay
separate until independently associated. OS enumeration indices are not promised
to remain stable between runs. Cross-provider aliases must not bypass ownership
checks or be merged solely because their labels resemble one another.

One application owner serializes each stream's transfers, releases and lifecycle
operations. A controller may request cancellation concurrently. A provider must
state whether RX and TX can run concurrently and which controls are shared.
Other processes can still change or claim hardware where the OS/driver permits;
an S7 session is not a claim of universal physical exclusivity.

## 4. Capability discovery and negotiated descriptors

Capabilities distinguish **supported**, **unsupported** and **unknown**. An API
method existing does not establish that a particular device implements it.

| Area | Required capability information |
| --- | --- |
| Direction/channels | Capture/RX and playback/TX availability; ordered channel IDs and valid combinations |
| Format | Application formats, known native storage/valid bits, packing/alignment, conversion behavior |
| Rate | Supported discrete rates/ranges and constraints shared between directions |
| Timing | Available position counters, time domains, clock relationships and timing confidence |
| Transfer | Buffer/quantum limits, copy/borrow support, interruptibility, partial transfer behavior |
| Fault evidence | Observable overrun/underrun/disconnection conditions and whether exact lost counts exist |
| Duplex | Shared resources, start restrictions, common timeline or independently qualified sampling clocks |
| Family-specific controls | Supported SDR frequency/gain/antenna/bandwidth controls or audio channel roles |

Opening/preparing a stream returns its actual negotiated descriptor before
logical execution starts. It includes the selected provider/device/session,
direction, ordered channel IDs, S7 value type and amplitude interpretation,
storage layout, exact nominal frame rate, clock identity/provenance, resource
limits, configuration revision and timing revision.

Requested and negotiated settings are distinct. A changed rate, type, channel
set or scale must be accepted explicitly and revalidated against the graph,
or preparation fails. The device never silently rewrites the document's model
rate. A descriptor cannot change underneath a running stream.

Application format, device-native format, server-side conversion and physical
converter resolution are separate facts. In particular:

- Audio may use an explicitly selected application PCM format even when a host
  API/server converts device storage. Report known conversion/resampling and
  mark unobservable parts unknown. An optional strict native-data request must
  fail if the required property cannot be established.
- Native 12-bit IQ is represented as S7 `CI12`, following ADR-023. A 16-bit
  container alone is not evidence of 16 significant bits. Storage unpacking and
  bit alignment must preserve the declared values; normalization is explicit.
- SoapySDR's `CS16` name/full-scale information must be translated with sufficient
  device evidence; it is not automatically a declaration of S7 `CI16`.
- Packed 24-bit PCM needs an explicit representation/scale decision before it is
  supported through `I32`; the first implementation need not support it.

SDR capability queries must separate driver-visible channels from the configured
RF connectors. Stock Pluto, modified Pluto and LibreSDR may use the same provider
with different qualified profiles. S7 never enables a firmware modification,
creates connectors, or asserts that an exposed scan channel is physically wired.

## 5. Data access and ownership

The provider contract supports three explicitly advertised access forms:

| Access form | Ownership and lifetime |
| --- | --- |
| Copy | Caller supplies bounded storage; result describes the complete-frame prefix copied/accepted |
| Borrow | One scoped provider buffer view; valid until explicit release/commit according to its direction |
| Owned block | Immutable S7-owned storage suitable for asynchronous queues; its lease keeps storage charged and alive |

A provider need not support borrow or owned-block access. Lack of borrow does
not force an extra graph-side copy when a caller-supplied destination suffices.
“Zero-copy” claims must identify the actual boundary and format being preserved.

Every view describes all selected channels, component offsets, frame stride,
accessible byte bounds and value/storage interpretation. All offsets and size
products are validated before access. Counts are simultaneous frames, not scalar
components or bytes. Partial consumption advances every selected channel equally.

RX views are read-only. A released RX view, committed TX view or returned OS
callback buffer cannot be retained for later asynchronous use. Copy into an
admitted S7 block or retain a genuinely owned lease before returning it.
PortAudio/PipeWire callback storage is not assumed to be a transferable lease.

Cancellation never frees a buffer while an application loan remains valid.
Lifecycle calls requiring outstanding loans to be returned report a busy/loan
condition rather than invalidating memory. Owned leases may outlive queue/session
closure when their backing allocation supports it, and remain budgeted until
the final lease is released.

## 6. Frame positions, time and continuity

Each stream start establishes a session/stream identity and a segment. A block
describes its segment, first frame, frame count, configuration/timing revisions
and position provenance. Software-delivered frame positions and native hardware
counters are distinct. Counter wrap/reset and grid mapping must be explicit.

Optional timing evidence identifies:

- the clock/time domain and origin interpretation;
- the frame/component to which a timestamp applies;
- whether it describes acquisition, callback delivery, backend submission or
  predicted presentation;
- whether the evidence is measured, backend-estimated, synthetic or unknown;
- any qualified uncertainty and counter/grid mapping.

Absent timestamps are absent, never zero. Host arrival time does not become
acquisition time. A backend-local clock does not become UTC or a peer's clock.
Shared callback timing does not by itself prove ADC/DAC oscillator lock, absolute
alignment or RF phase coherence. Partial reads must rebase positions and events.

Continuity evidence distinguishes at least known break, no observed break and
unknown/insufficient observation. The latter two are not a guarantee of lossless
capture. An exact loss count is optional; zero must not stand in for unknown.

Translate only metadata with a defined meaning into the existing frame transport.
The accepted wire profile is not extended by inventing hardware event IDs here.
Unsupported native-counter/quality metadata needs a separately reviewed wire
schema before network advertisement.

## 7. Transfers, progress and failures

A transfer result reports whole frames completed, status, direction, segment,
relevant continuity evidence and provider/native error detail. Proposed statuses:

| Status | Meaning |
| --- | --- |
| Success | Reported complete-frame prefix transferred at the stated boundary |
| WouldBlock / Timeout | No further progress before the polling/deadline condition; not automatically a discontinuity |
| Cancelled | An explicit stop/cancel request interrupted the operation |
| SourceOverflow / SinkUnderflow | Observed acquisition loss or missed output demand |
| ContinuityLost | A known break, including recovery that discarded/recreated an active device buffer |
| Disconnected / DeviceBusy | Endpoint disappeared or could not be acquired |
| Unsupported / InvalidConfiguration | Required capability or requested settings unavailable |
| BackendError | Other provider failure, retaining native detail |

Enum names are provisional; these are not wire numeric values. A result may
contain a valid nonzero prefix and then an error. That prefix is accounted for
exactly once; the suffix is not falsely reported as transferred. One channel
cannot advance farther than another within a selected-channel frame.

Waiting deadlines are explicit and cancellable. A backend timeout implemented
by discarding a DMA buffer is a continuity event, not an ordinary empty poll.
Backend capabilities must reveal interruptibility limits. This design makes no
unqualified hard-real-time or universal driver-shutdown latency promise.

Separate the following milestones:

1. producer supplied data;
2. S7 admitted it to a queue;
3. a graph consumed it;
4. a provider accepted it for output;
5. hardware presentation occurred or was estimated.

An ordinary successful sink write establishes provider acceptance only. Playback
confirmation is a separate optional capability. Network consumption ACKs retain
their existing meaning; they do not establish DAC presentation or graph commit.

The proposed first policy is **strict**: observed loss faults the affected
stream/run boundary. Drain any trustworthy preceding prefix in defined order,
then require explicit recovery into a new segment. Never silently skip/repeat
frames, insert graph samples, change devices or resume stateful DSP across loss.

Audio APIs may require an output buffer to be filled after an application fault.
A bounded safety fill or device stop may therefore be physically necessary. It
must be reported as backend emergency behavior, not as successful S7-produced
samples or exact continued playback. Best-effort audio concealment remains a
separate explicit policy, not the default measurement contract.

## 8. Lifecycle, configuration and real-time callbacks

Proposed lifecycle:

`Closed -> Open -> Prepared -> Running -> Stopping -> Stopped`

Runtime failures latch `Faulted`. Restart/reconfiguration requires an explicit
owner action, a fresh validated descriptor when needed, and a new segment;
reopening a disconnected device is not automatic continuity recovery.

- Schema loading, metadata inspection, type inference and offline validation
  perform no device I/O. Preparation/activation belongs to an explicit run owner.
- Preparation validates capabilities, resource budgets, negotiated settings and
  compatibility of shared RX/TX controls before normal transfers start.
- TX/playback activation requires explicit selection of that direction. Drivers'
  other open/configuration side effects must be documented; a discovery/open API
  is not automatically a promise of RF silence or transactional configuration.
- Initial configuration is frozen while running. A later live-control design
  must define safe frame boundaries, readback and revision changes. Failed
  hardware writes can leave partial configuration and must report that fact.
- Shutdown requests cancellation, wakes waits, quiesces and joins owners,
  returns loans, then releases provider resources. Repeated stop/close is safe.
  Drain versus discard is explicit and bounded; defaults need review.

PortAudio and PipeWire callbacks hand off bounded data/status through preallocated
storage. No graph evaluation, network waits, compression, blocking locks, device
discovery, logging I/O or unbounded allocation runs in a real-time callback.
Variable callback quantum sizes must be accepted within admitted bounds. A
larger/unexpected quantum or a full queue produces an explicit result/fault.

## 9. Clock ownership and graph integration

Three distinct rates/times are retained:

1. **Model/domain rate:** S7's exact declared rational rate.
2. **Negotiated stream rate:** the nominal application frame grid agreed with
   the provider and checked against the domain before its first sample.
3. **Physical progression:** the actual device/server clock, with drift and
   only the timing evidence the provider can establish.

The existing rate-authoritative rule stays intact. A device selected at 48 kHz
does not silently change a 44.1 kHz run. Exact equality of two nominal rates also
does not establish that their physical clocks are synchronized. The existing
fixed-ratio `Resampler` is not evidence of adaptive clock matching.

**Proposed first execution profile:** one explicitly selected pacing master per
connected hardware pipeline. Capture availability or playback capacity controls
when the runtime may advance, while each sample keeps the existing evaluation,
publication and causal-memory order. A block of N frames permits N logical frame
evaluations, never one substituted graph step. Pacing does not choose causality.

The initial `hardware::Run` owner implements this separately from existing
UTC/model-paced serving. The service/Studio do not yet activate hardware runs.
It must not be approximated by an element silently overriding the model rate,
adding a second wall-clock throttle, or running the graph in its device callback.

A selected master cannot keep independently clocked capture/playback devices
aligned indefinitely through buffering. Such a connection requires a qualified
shared-clock relationship or a separately explicit clock-matching/resampling
boundary. UTC/PTP agreement alone is insufficient. The initial profile should
reject unsupported independent-clock combinations rather than claim stable
duplex operation. Same-session duplex is admitted only with adequate timeline
evidence and an explicit start/prefill policy.

Hardware boundaries have irreversible external effects. Under the existing
failed-domain notification pattern, an affected source/sink latches a fault and
refuses an ordinary retry that would consume/emit different samples. Already
captured/transmitted samples cannot be rolled back. Reversible output snapshots
do not establish external-effect atomicity. Commit hooks, recovery across a
failed step and bulk optimizations need their own reviewed implementation.

## 10. Resources, build selection and private-provider boundary

Admission bounds capture/output queue frames and bytes, provider/application
buffers, outstanding loans, metadata and worker count. Account for application
storage separately from backend/OS/device queues when the latter are not fully
observable. Report queue occupancy/high-water marks and meaningful latency
estimates without equating them to hardware presentation guarantees.

Pools are instance-owned and allocated before steady-state transfers. Resource
exhaustion never causes hidden heap growth. Queue capacity and latency targets
are separate settings; a large SDR block must not set the audio buffering policy.

Proposed optional build settings use `AUTO / ON / OFF` per backend: `AUTO` builds
when its development dependency is present, `ON` fails clearly if missing, and
`OFF` avoids the dependency. Linux release builds that claim both audio backends
explicitly enable and test both. PortAudio availability does not make a native
PipeWire build/test unnecessary. Core S7 and simulation-only builds remain usable
without audio, SoapySDR or private AD936x dependencies.

Runtime selection records the actual backend and device. An explicit unavailable
selection fails. An optional `auto` mode may select once from a documented
priority order before preparation; it must not silently switch after activation
or silently substitute a null source/sink. Fake providers are explicit tests.

The private provider is installed/loaded through a supported local SDK extension
boundary. Its packaging and source stay outside public S7 distribution. Public
CI exercises its contract with fake traces and the public providers; private
qualification can exercise the same tests against the installed private provider.
Building a private provider must not require publishing its implementation.

## 11. Backend-specific obligations

| Backend | Specific checks in addition to the common contract |
| --- | --- |
| PortAudio | Capture/playback/duplex, selected host API and device, negotiated PCM format/rate, variable callback sizes, callback status flags and stream-local timing provenance |
| Native PipeWire | Native stream/device targeting, graph/device timing distinctions, observed server conversion/resampling, parameter changes/disconnection and callback buffer lifetime |
| SoapySDR | Driver-specific format/full-scale mapping, complete/partial reads and writes, timeout/loss flags, timestamp validity and optional direct-buffer/status support |
| Private AD936x | Explicit qualified LibreSDR/Pluto channel profiles, paired frame alignment, shared RF controls, RX/TX lifetime/cancellation and available timing/continuity evidence |

An API capability is not a driver guarantee. In particular, the inspected public
SoapyPlutoSDR source advertises one channel per direction; that implementation
cannot simply be assumed equivalent to the private dual-channel provider.

GNU Radio is a design reference for backend factories, batching, lifecycle and
sample-position metadata. Its runtime-dependent blocks are not the public S7
hardware interface. Source reuse is a separate component/licensing decision;
no GNU Radio or private implementation code is incorporated by this document.

## 12. Acceptance evidence before implementation claims

| Check | Required evidence |
| --- | --- |
| Common behavior | The same fake-device traces test all adapters: partial prefixes, variable quanta, invalid configuration, timeouts, loss, disconnection and restart |
| Ownership | Outstanding loans, callback lifetime, cancellation races, independent RX/TX lifecycle and resource release tested with sanitizers |
| Numeric interpretation | Exact channel order, native widths/alignment and declared scaling, including CI12 RX/TX boundary cases; explicit conversion reference tests |
| Timing | Unknown timestamp cases stay unknown; backend-local anchors, counter reset/wrap and partial-read rebasing tested |
| Clock drift | Independent-clock tests reject unsupported configurations; no claim that finite queues or a fixed-ratio resampler eliminate drift |
| Causality | Changing device/queue block sizes preserves scalar reference outputs, Delay latency, failure behavior and rate-domain ordering |
| Real devices | PortAudio and native PipeWire both exercised; Soapy and private AD936x tested separately for advertised device/channel combinations |
| Release isolation | Public build/install runs without the private dependency or repository access; external private provider can use the installed public SDK |

Performance evidence must identify device, OS/backend, type/channel/rate, quanta,
queue sizes, wall time, errors, CPU/memory and stop latency. Transport success is
not RF coherence, a guarantee of no physical sample loss, or a hard deadline.

## 13. Initial implementation choices and qualification

ADR-026 records the accepted hardware boundary direction. The first implementation
uses conservative restrictions that preserve the contract:

1. Exactly one hardware boundary and one matching rate domain per hardware run.
   Capture-master and playback-master paths use ordinary scalar steps. Duplex,
   multiple boundaries and independent-clock matching are rejected before opening
   devices; shared-session execution remains a future qualified extension.
2. Strict loss/fault behavior; runs are single-use. Audio queues prefill to their
   admitted capacity; SDR providers prefill one block. Completion/stop aborts
   pending output. Acceptance is not presentation; graceful drain is deferred.
3. Caller-owned copy transfers only, so no borrowed-buffer/owned-lease API is
   advertised. Audio initially supports F32/I16/I32 application PCM. Soapy C32
   explicitly requests conversion; native integer modes require format/full-scale
   evidence. Physical converter formats/server resampling can remain unknown.
4. Public `s7hardware` SDK, independent `s7audio`, and provider-selected `s7sdr`
   elements; separately loaded external provider factories retain module code
   through stream lifetimes. The private provider is built outside public S7.
5. Explicit backend/device selection, no backend `auto` or runtime fallback.
   Configuration is stopped-only. The existing element plugin ABI, graph causal
   semantics, network protocol and deployment semantics are unchanged.

Mock-backed tests cover scalar/Delay equivalence across block sizes, valid
prefixes, short writes, fault latching, callback SPSC handoff, emergency output,
cancellation, metadata/plugin loading, installed-SDK extension lifetime and the
actual Soapy adapter with a fake driver. Sanitizer runs cover the new modules.
Opt-in [live tests](../s7l2/docs/HARDWARE_IO_LIVE_TESTS.md) subsequently exercised
both audio backends, two-host virtual audio, local USB input and separate radio
providers, including failure injection. Their measured scope does not establish
full nominal-rate continuity, physical clock matching or RF coherence.

Target requirements for timing anchors, duplex evidence, loans/owned blocks,
queue telemetry, hardware performance and backend/device qualification remain
unimplemented or unqualified as detailed in the implementation guide. They are
not advertised as guarantees by the first implementation.

## 14. Backend-neutral processing and deterministic replay (0.3.0 H8)

In addition to these provider contracts, 0.3.0 requires explicit
separation of **physical provider**, **causal processing model**, and
**execution host**. Device callbacks and stream ownership belong to the
provider; pacing, bounded queues, cancellation and committed physical
outputs belong to the host; the model expresses causal computation and state.

Representative audio, SDR IQ and whole-frame video paths must support
recorded-input replay without editing the processing algorithm. Fixtures
retain available samples/frames, channel/layout/rate, position/segment,
continuity, source time/clock/uncertainty and honest unknown status.
Reference-output equality is assessed separately from physical latency and
jitter. Offline inspection/replay cannot implicitly activate devices or TX.
This supports future compiled models, but **no native compiler is claimed
in 0.3.0**. See [H8 work plan](../s7l2/docs/RELEASE_0.3.0_WORKPLAN.md#h8-bridge-qualification).


## Public references reviewed

- [PortAudio API overview](https://portaudio.com/docs/v19-doxydocs/api_overview.html)
  and [callback timing](https://portaudio.com/docs/v19-doxydocs/structPaStreamCallbackTimeInfo.html):
  device/stream abstractions, callback or blocking transfers and stream-local time.
- [PipeWire stream API](https://docs.pipewire.org/group__pw__stream.html):
  native stream lifecycle, buffers, timing and real-time callback constraints.
- [SoapySDR device API](https://pothosware.github.io/SoapySDR/doxygen/latest/classSoapySDR_1_1Device.html)
  and [stream flags](https://pothosware.github.io/SoapySDR/doxygen/latest/Constants_8h.html):
  capabilities, streaming, optional direct buffers and timestamp validity.
- [SoapyPlutoSDR settings](https://github.com/pothosware/SoapyPlutoSDR/blob/master/PlutoSDR_Settings.cpp):
  inspected channel advertisement; not a qualification of dual-channel support.
- [GNU Radio audio dependencies](https://github.com/gnuradio/gnuradio/blob/3d33e04a249b31ef32106d275b927a7c4cf86425/gr-audio/lib/CMakeLists.txt)
  and [IIO dependencies](https://github.com/gnuradio/gnuradio/blob/3d33e04a249b31ef32106d275b927a7c4cf86425/gr-iio/lib/CMakeLists.txt):
  inspected GNU Radio commit `3d33e04a249b31ef32106d275b927a7c4cf86425`;
  distinguish runtime adapters from reusable device libraries.

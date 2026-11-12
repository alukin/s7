# Sample-frame streams and bounded ownership

Status: **accepted by the maintainer, 2026-11-12; C++/Python/Java services implemented,
explicit scalar graph/Studio adapters implemented; automatic deployment pending**.
Approval covers the transport-only contract and ADR-025. Graph commit/failure
semantics and bulk execution remain a separate T4 decision. Audio/compression
extensions began as [follow-up research](S7_Audio_and_Frame_Compression_Research.md).
The maintainer subsequently authorized raw audio and independent lossless
compression implementation. The [C++ service guide](../s7l2/docs/FRAME_TRANSPORT.md)
records that implemented subset and its encoded-profile contract; neither profile
is advertised by RuntimeService yet.
The compatible managed-language ports and shared fixtures are documented in
the [interoperability guide](S7_Frame_Interoperability.md).
This is the concrete T2 contract from the
[transport improvement plan](../s7l2/docs/SDR_TRANSPORT_IMPROVEMENT_PLAN.md).
The existing [wire v1 contract](S7_Network_Wire_Protocol_v1.md) and accepted ADRs
remain authoritative for implemented scalar streams. Profile support must not
be advertised before implementation and conformance validation.

## Accepted direction

Adopt an explicitly advertised `sample_frames_v1` profile over the existing
ordered, replayable data transport, plus immutable owned frame-buffer leases.
Keep scalar sample evaluation as the computational reference. Implement the
profile/ownership service before introducing optimized graph execution.

**Causality first.** A packet is not a model step, a buffer is not a rate domain,
and a device or network clock does not define graph dependencies. Changing
packet/block size must not change model rate, Delay latency or state-update order.
No implicit resampling, format conversion or RF phase alignment is introduced.

## 1. Units and graph boundary

- A channel value is one real/complex numeric value in a declared existing type.
- A frame is one value from every declared channel at one declared acquisition
  or processing-grid position, in stable order. Physical simultaneity and RF
  coherence need their own provenance; grouping values never proves them.
- A block owns a contiguous range of frames and its frame-positioned metadata.
- A packet transports whole frames; packet boundaries need not equal processing
  block boundaries. A frame is never split across packets or partially accepted.

Recommend future `NetFrameSource`/`NetFrameSink` boundaries with C scalar ports
for C channels: one logical evaluation reads/publishes one declared-grid frame.
The initial profile does not require vector-valued graph ports or reinterpret
existing NetSource/NetSink. Bulk execution may optimize multiple logical samples
only after equivalence is proved against ordinary scalar execution, including
feedback, causal-memory publication, nested schemas and rate-domain boundaries.
That optimization is a separate T4 decision, not approval of a new scheduler.

## 2. Immutable stream descriptor

Proposed profile fields, in addition to the existing producer/consumer identity:

| Field | Proposed meaning |
| --- | --- |
| `sample_profile` | Literal `sample_frames_v1`; both endpoints must advertise support |
| `data_type` | Existing numeric type of one channel value; retain native sample width |
| `channels` | Ordered unique channel IDs, 1–64 channels for this initial profile |
| `wire_layout` | `frame_interleaved`: frame 0 channel 0..C−1, then frame 1; portable existing numeric encoding |
| `frame_rate` | Positive rational frames/s of the declared grid; not an oscillator measurement |
| `clock_domain` | Explicit source/model domain identifier and origin interpretation |
| `time_role` / `time_provenance` | SOURCE_TIME or MODEL_TIME, and unknown/device/synthetic provenance; transmission time remains separate |
| `position_provenance` | `software` or `hardware`; neither implies UTC synchronization |
| `max_frames_per_packet` | Positive frame bound; complete encoded frame must fit the global payload limit |
| `max_metadata_bytes` | At most 2048 bytes of frame events per packet, included in memory/validation limits |
| `max_events_per_packet` | At most 32 typed events; no arbitrary nested metadata objects |
| `resource_limits` | Explicit send-log outstanding-frame window, retained bytes/frames and timeouts; node-wide cap is local policy |

For this profile use `frame_rate`, not the legacy element-rate `sample_rate`
field. Reject contradictory timing/layout fields rather than guessing units.
Payload elements remain numeric channel values: `values = frames * channels`.
Validate products and limits before allocation. CI12 retains three bytes per
complex channel value, with explicit conversion from native driver storage.
The initial profile admits only types whose individual value encoding occupies
whole bytes. CI12 is included; scalar I12/U12 are deferred because odd channel
counts would otherwise require a separately defined bit-level frame layout.
Planar local memory is allowed behind a checked adapter; wire layout stays fixed.

Changes to type, channel layout, rate, clock interpretation, producer, processing
meaning or send-log limits require a new stream ID under wire v1's identity rule.
Revision fields must not become a loophole for changing stream meaning in place.
Route-only changes preserve identity and sequence state as today.

## 3. Position and metadata on wire

Keep wire v1's 16-byte packet header and per-packet sequence. Propose critical
extension **`0x8002 SAMPLE_FRAMES`**, version 1, with fixtures required before
advertising support. Every profile packet carries it. Its fixed portion is:

| Offset | Bytes | Value |
| ---: | ---: | --- |
| 0 | 2 | Profile version = 1 |
| 2 | 2 | Flags: `SEGMENT_START` (bit 0), `FAULT_ONLY` (bit 1); other bits rejected |
| 4 | 4 | Frame count, positive for data, zero only for `FAULT_ONLY` |
| 8 | 16 | Segment UUID, independent of stream ID and packet sequence |
| 24 | 8 | First frame index within this segment |
| 32 | 8 | Configuration revision, matching the committed descriptor |
| 40 | 8 | Timing/calibration interpretation revision |
| 48 | 4 | Event count |
| 52 | 4 | Event bytes following this fixed portion |

Multi-byte integers are little-endian; UUID bytes follow the existing stream-ID
convention. The TLV length includes the 56-byte fixed portion and event bytes.
Normal extension-area zero padding remains mandatory. Count, type, layout,
length and revision must agree before delivery. The global payload limit stays
256 KiB; metadata is bounded separately and counts toward local resource limits.

A new segment starts at frame zero with `SEGMENT_START` and a new UUID. All later
packets use the same segment with an exactly contiguous frame index; packet
sequence still follows the existing stream ordering/replay rules. Hardware
counter values can anchor this local segment grid without changing what
`first_frame` counts. Software counting is explicitly reported as such.

Events use `(frame_offset:u32, type:u16, value_bytes:u16, value, zero padding to
4 bytes)`, sorted by offset then type. Offsets address frames inside this packet.
Unknown event types with bit 15 set are critical and rejected; unknown noncritical
events may be ignored only where the descriptor permits that. Known event types
have fixed/bounded schemas and duplicate rules in shared fixtures.

Initial event catalog (the C++ service implements fault and clock-anchor events;
the remaining schemas are deferred):

- Mandatory fault event `0x8001`: offset zero in a FAULT_ONLY packet, 16-byte
  value `(code:u16, flags:u16, reserved_zero:u32, lost_frames:u64)`. Codes initially
  mean RX/source overflow (1) or source continuity lost (3); code 2 is reserved.
  Sink underflow is consumer-local as specified below. Flag bit 0 says an
  exact lost-frame count is known; otherwise the field is canonically zero and
  loss is explicitly unknown. No other flags are accepted.
- Clock anchor event `0x8002`: domain/role-qualified frame time, 16-byte value
  `(timestamp_ns:i64, uncertainty_ns:u64)`. UINT64_MAX uncertainty means no error
  bound is qualified. Emit only when an actual anchor is known under the stated
  provenance. Absence means source time is unknown, not timestamp zero.
- Native-counter anchor: native counter/grid mapping and validity; never inferred
  from a delivered-frame software count or a clear overflow latch.
- Clipping/quality: frame range/count and whether sample/timing validity is lost.
- Filter/processing delay: explicit rational frame mapping and interpretation ID.

Changes affecting layout, rate or interpretation require an explicit packet/stream
boundary and the identity rules above. A filter/resampler maps frame positions
and associated events deliberately; blindly forwarding old offsets is invalid.
Calibration/clock-model revisions may describe a new estimate of the same grid,
but must not silently move already published samples or claim RF phase validity.

Do not combine legacy TIMESTAMPS with profile packets in the first implementation.
Profile timing uses descriptor domains and sparse anchors; legacy scalar streams
retain their exact current timestamp rules. The final event schemas and timing
validity rules are an implementation gate, not unspecified extension freedom.

Native-counter, quality and processing-delay event schemas may be deferred from
the initial profile implementation; do not advertise their support prematurely.
Event IDs are in an independent namespace from header-extension IDs. Unknown
critical events are always rejected, including those not yet implemented.

## 4. Loss and recovery

Network replay and source continuity are independent. Continue consumption ACKs
only after the consumer boundary has taken every frame in a packet; receipt into
a socket/queue is not consumption. Do not strengthen this into a claim that all
downstream graph computation has committed or survived a process crash.

For the initial profile, acquisition overflow, missing frame position or device
playback underflow faults the affected stream with a typed source/sink fault.
Stop admitting affected future frames; record known lost-frame counts or explicitly
unknown counts. A producer's already sealed valid prefix may drain as below.
No silent dropping, hold-last-value or synthesized replacement samples. Ordered
packet/segment metadata must determine sample validity; asynchronous status
notifications alone cannot associate a fault precisely with data.

Propose an ordered terminal **FAULT_ONLY** packet rather than an asynchronous
status-only error. It has zero frames/payload, the next expected local frame
index and exactly one critical fault event. This is an explicit profile-only
exception to v1's nonempty numeric payload rule; legacy streams keep that rule.
The local index identifies the delivery boundary, not a claimed first lost ADC
sample. If hardware cannot locate an earlier loss, retain that uncertainty.

When the source faults, freeze new publication, append the terminal marker after
already sealed valid data, and drain that bounded prefix/marker in order. This
proposed `draining_fault` state differs from immediately discarding pending data.
Reserve one terminal-log slot and 256 encoded bytes per stream within its budget
so exhaustion cannot prevent recording the fault. A stalled network still has
no guaranteed fault-delivery latency; local status exposes the fault immediately.
Do not evict unconsumed samples to make room for the marker.

The consumer delivers the preceding valid prefix, then returns the terminal fault
without a sample for the marker, latches the stream fault and stops. It may ACK
the marker only after earlier frames were consumed and the terminal fault was
recorded at the boundary. No subsequent data/segment is accepted automatically.
Return prefix-frame counts together with a terminal condition where a caller's
read straddles this boundary. The first implementation is for continuous streams;
normal finite-stream completion needs a separately specified END marker and is
not represented as a fake loss fault.

The ordered marker is for **producer-detected** loss. A consumer detecting TX
device underflow faults its own boundary immediately, stops new device writes
and consumption ACKs, and reports `SINK_UNDERFLOW` with known/unknown counts in
local stream status. It must not inject into the producer's sequence space.
The producer eventually reaches the existing bounded backpressure/fault policy;
fast peer notification of the precise sink cause would require a separately
reviewed control message. This initial proposal does not invent one. No software
silently synthesizes samples; any device's autonomous underflow behavior must be
reported separately rather than claimed to have been prevented by the protocol.

Authorized recovery may start a new segment, retaining fault history. Require
that transition explicitly; a new segment never silently heals a fault. When
source-clock continuity cannot be preserved, establish a new clock-domain/stream
identity as required by the existing time/identity rules. Restart continuity and
process-crash replay guarantees remain unchanged.

An RX device continues acquiring while network publication may block: the driver
owner drains into its own bounded queue and faults when that budget is exhausted.
TX requires explicit device pacing, prefill and low-water limits. These budgets
are adapter configuration, not assertions that an ADC can be backpressured.

## 5. Ownership, resources and cancellation

Proposed service concepts, not final C++ signatures:

- `FrameBlockView`: borrowed immutable frame range and bounded metadata, valid
  only for the duration of a call. `publish_copy` copies accepted complete frames
  before returning; the caller may then reuse its input memory.
- `FrameBlockLease`: immutable reference-counted storage from a bounded owner
  pool. `publish_shared` retains accepted ranges; storage cannot be mutated or
  recycled until every reference, including replay retention, is released.
- Partial acceptance/consumption is an explicit number of whole frames. Return
  the accepted prefix and reason: budget/deadline, cancellation or stream fault.
  A timeout or fault must not be reported as complete success.

Account actual retained allocation capacity, not just active payload bytes. A
node-wide pool budget and per-stream quotas cover open buffers, send/replay logs,
receive queues, retained consumer leases and metadata. Shared allocation is
physically charged once, with explicit per-stream attribution; fan-out must not
create unbounded copies. Pool exhaustion is observable backpressure/fault behavior.
Do not expose raw DMA storage beyond its backend lease; copy into owned storage
where the backend cannot safely retain multiple buffers.

Cancellation wakes bounded pool and publication/receive waits. The owner joins
workers before destroying endpoints or driver state. Closing a stream must not
leave blocked operations waiting for ACKs from a closed peer. Reconnect retains
the same immutable data under the current send-log contract. This proposal does
not promise zero-copy TLS, driver-to-network zero-copy or unlimited queues.

An explicit receiver-credit protocol is deferred initially: use bounded local
queues plus the existing consumption ACK/send log. For admitted peers require
the receiver's reserved frame capacity to cover the producer's declared maximum
outstanding send-log frame window. Translate that window into local native
storage/metadata bytes (including CI12 decode expansion), and reserve the node's
aggregate budget across admitted streams and connection buffers before deployment.
This works because unconsumed frames cannot have been consumption-ACKed; they
remain inside the sender's bounded window. Validate that invariant and fault a
peer that exceeds its admitted limit before allocating more receiver storage.
Do not rely on pausing a multiplexed WebSocket reader to withhold sample credit
while unrelated ACKs need that same connection. If stress tests show these rules
cannot enforce an agreed budget with permitted peers, design credits separately;
do not introduce undocumented flow-control messages.

## 6. Graph failure boundary — separate T4 gate

The current runtime can restore published outputs after an exception, but does
not roll back arbitrary private state or external network effects. A bulk API
must not silently turn a whole block into one transaction or export unchecked
future logical samples. Preserve the accepted causal publication rules.

Before graph adapter implementation, specify how input leases, output staging,
consumption and step failure interact. Recommended safety rule: a failure after
irreversible source consumption/publication latches a boundary fault and requires
explicit recovery; retry must not silently advance to another frame. Any required
per-step commit/fault hook must be reviewed against ADR-021 and tested with a
failure in the middle of a block, feedback and partially progressed rate domains.
T3's transport-only frame service can be implemented/tested independently; this
proposal does not authorize changing NodeRuntime retry/rollback semantics.

## 7. Capability and validation gate

Advertise `NODE_INFO.data_protocol.sample_profiles: ["sample_frames_v1"]` and
critical extension `0x8002` in the existing extension list through node capabilities;
deployment refuses unsupported producer/consumer combinations before prepare.
Older scalar-only implementations remain scalar-only rather than interpreting
frames as sequential channels. Original wire framing need not be replaced.

Shared C++/Python/Java fixtures must cover multi-channel layout, CI12, whole-frame
limits, segment/revision changes, sparse timing anchors, unknown critical events,
integer/length overflow, missing events, source/sink faults and authorized recovery.
Implementations may advertise only their supported profile subset. Keep existing
control authorization, data security and legacy scalar conformance tests intact.

## Decision record

The maintainer accepted these initial choices on 2026-11-12, authorizing T3's
transport-only implementation. The initial socket-independent C++ services now
implement these rules; deployment/capability and graph integration remain pending:

1. Frame-interleaved sample profile over wire v1 with critical extension/capability;
   fixed channel identity and rate, new stream ID for meaning changes.
2. Immutable bounded owned leases plus explicit copy/whole-frame partial APIs;
   ACK/replay behavior retained, receiver credits deferred pending evidence.
3. Strict source/sink loss faults with explicit segment recovery; no synthesized
   samples or invented hardware timestamps.
4. Transport service first; scalar logical-frame semantics preserved and graph
   commit/failure/bulk execution reviewed separately before T4.

## Scalar adapter integration — 2026-11-12

The maintainer requested source/sink element-library and Studio integration.
`NetFrameSource` and `NetFrameSink` now use named scalar channel ports and the
existing runtime endpoints. One evaluation remains one logical frame; endpoint
packet batching does not perform bulk graph execution. On a failed rate-domain
sample the runtime notifies its elements; these new boundaries latch a fault
and refuse retry. Already consumed/published effects are not rolled back.
Legacy elements retain their retry behavior and previously completed rate domains
retain their progress. Restart requires a new graph/stream identity; legacy
RECOVER_STREAM cannot clear a frame boundary. Full details and current limits:
[frame elements](../s7l2/docs/FRAME_ELEMENTS.md). Automatic cross-node frame grouping,
profile negotiation and aggregate deployment admission remain follow-up work.

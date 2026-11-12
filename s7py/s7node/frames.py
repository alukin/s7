# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in sample-frame service, compatible with C++ s7::frames.

Payload APIs use portable numeric bytes (wire.encode_elements), avoiding Python
float conversion. This module does not enable runtime capability advertisement.
Managed objects/native codec overhead are outside the pool capacity counter.
"""
import struct
import threading
import time
import uuid
import zlib
from collections import deque
from dataclasses import dataclass, field, replace
from enum import IntEnum, Enum

from . import wire

try:
    import lz4.block as _lz4
except ImportError:
    _lz4 = None
try:
    import zstandard as _zstd
except ImportError:
    _zstd = None

MAX_MESSAGE_BYTES = wire.MAX_PAYLOAD_BYTES + 4096
MAX_EVENTS = 32
U64 = (1 << 64) - 1
FRAME_EXTENSION, ENCODING_EXTENSION = 0x8002, 0x8003


def require(condition, message):
    if not condition:
        raise wire.FormatError(message)


def uint(value, bits=64):
    return type(value) is int and 0 <= value < 1 << bits


class Encoding(IntEnum):
    RAW = 0
    LZ4 = 1
    ZSTD = 2


def encoding_available(encoding):
    return encoding == Encoding.RAW or encoding == Encoding.LZ4 and _lz4 is not None or encoding == Encoding.ZSTD and _zstd is not None


class Status(str, Enum):
    OK = "OK"
    BACKPRESSURE = "BACKPRESSURE"
    CANCELLED = "CANCELLED"
    CLOSED = "CLOSED"
    SOURCE_OVERFLOW = "SOURCE_OVERFLOW"
    CONTINUITY_LOST = "CONTINUITY_LOST"
    SINK_UNDERFLOW = "SINK_UNDERFLOW"
    MALFORMED = "MALFORMED"
    SEQUENCE_GAP = "SEQUENCE_GAP"


@dataclass(frozen=True)
class Descriptor:
    stream_id: bytes = field(default_factory=lambda: uuid.uuid4().bytes)
    data_type: wire.WireType = wire.TYPES["F32"]
    channels: tuple = ("mono",)
    frame_rate: tuple = (48000, 1)
    clock_domain: str = "synthetic"
    clock_origin: str = "model run origin; nanoseconds"
    time_role: int = wire.MODEL_TIME
    time_provenance: str = "synthetic"
    position_provenance: str = "software"
    max_frames_per_packet: int = 480
    window_packets: int = 32
    window_frames: int = 480 * 32
    configuration_revision: int = 0
    timing_revision: int = 0
    encoding: Encoding = Encoding.RAW

    def __post_init__(self):
        object.__setattr__(self, "stream_id", bytes(self.stream_id))
        object.__setattr__(self, "channels", tuple(self.channels))
        object.__setattr__(self, "frame_rate", tuple(self.frame_rate))

    @property
    def frame_bytes(self):
        return wire.payload_bytes(self.data_type, 1) * len(self.channels)

    def validate(self):
        require(self.data_type in wire.TYPES.values() and self.data_type.name not in ("I12", "U12"), "unsupported frame type")
        require(len(self.stream_id) == 16 and any(self.stream_id), "invalid stream identity")
        require(1 <= len(self.channels) <= 64 and len(set(self.channels)) == len(self.channels), "invalid channel count/identity")
        require(all(isinstance(c, str) and 0 < len(c.encode()) <= 128 for c in self.channels), "invalid channel name")
        require(len(self.frame_rate) == 2 and all(uint(v) and v for v in self.frame_rate), "invalid frame rate")
        require(0 < len(self.clock_domain.encode()) <= 128 and 0 < len(self.clock_origin.encode()) <= 256, "invalid clock domain/origin")
        require(self.time_role in (wire.MODEL_TIME, wire.SOURCE_TIME) and self.time_provenance in ("unknown", "device", "synthetic")
                and self.position_provenance in ("software", "hardware"), "invalid frame provenance")
        require(uint(self.max_frames_per_packet, 32) and 0 < self.max_frames_per_packet <= wire.MAX_PAYLOAD_BYTES // self.frame_bytes, "frame payload exceeds bound")
        require(uint(self.window_packets, 32) and 0 < self.window_packets <= 65536 and uint(self.window_frames)
                and self.max_frames_per_packet <= self.window_frames <= self.window_packets * self.max_frames_per_packet, "invalid frame window")
        require(uint(self.configuration_revision) and uint(self.timing_revision), "invalid frame revision")
        require(encoding_available(self.encoding), "frame encoding unavailable")


@dataclass(frozen=True)
class Event:
    offset: int
    type: int
    value: bytes

    def __post_init__(self):
        object.__setattr__(self, "value", bytes(self.value))
        require(uint(self.offset, 32) and uint(self.type, 16) and len(self.value) == 16, "invalid frame event")


def clock_anchor(offset, time_ns, uncertainty_ns=U64):
    return Event(offset, 0x8002, struct.pack("<qQ", time_ns, uncertainty_ns))


def source_fault(cause, lost=None):
    require(cause in (Status.SOURCE_OVERFLOW, Status.CONTINUITY_LOST), "not a producer loss fault")
    require(lost is None or uint(lost), "invalid loss count")
    return Event(0, 0x8001, struct.pack("<HHIQ", 1 if cause == Status.SOURCE_OVERFLOW else 3, int(lost is not None), 0, lost or 0))


def fault_details(event):
    require(len(event.value) == 16, "invalid fault length")
    code, flags, reserved, lost = struct.unpack("<HHIQ", event.value)
    require(code in (1, 3) and flags <= 1 and reserved == 0 and (flags or lost == 0), "invalid fault event")
    return (Status.SOURCE_OVERFLOW if code == 1 else Status.CONTINUITY_LOST), (lost if flags else None)


@dataclass(frozen=True)
class Info:
    frames: int
    segment: bytes
    first_frame: int = 0
    configuration_revision: int = 0
    timing_revision: int = 0
    segment_start: bool = False
    fault_only: bool = False
    events: tuple = ()

    def __post_init__(self):
        object.__setattr__(self, "segment", bytes(self.segment))
        object.__setattr__(self, "events", tuple(self.events))

    def sliced(self, offset, count):
        return replace(self, frames=count, first_frame=self.first_frame + offset, segment_start=self.segment_start and offset == 0,
                       events=tuple(replace(e, offset=e.offset - offset) for e in self.events if offset <= e.offset < offset + count))


def validate_info(d, info, size):
    require(len(info.segment) == 16 and any(info.segment), "zero/invalid segment")
    require(info.configuration_revision == d.configuration_revision and info.timing_revision == d.timing_revision, "frame revision mismatch")
    require(uint(info.frames, 32) and info.frames <= d.max_frames_per_packet and uint(info.first_frame)
            and info.frames <= U64 - info.first_frame, "frame count/index overflow")
    require(not info.segment_start or info.first_frame == 0, "segment does not start at zero")
    require(size == info.frames * d.frame_bytes, "frame payload length mismatch")
    require(len(info.events) <= MAX_EVENTS, "too many events")
    require(info.frames == 0 and len(info.events) == 1 if info.fault_only else info.frames > 0, "invalid terminal/data frame")
    previous = None
    for e in info.events:
        require(uint(e.offset, 32) and (e.offset < info.frames or info.fault_only and e.offset == 0) and len(e.value) == 16, "invalid event offset/length")
        require(previous is None or (e.offset, e.type) > previous, "duplicate/unsorted events")
        previous = (e.offset, e.type)
        if info.fault_only:
            require(e.type == 0x8001, "missing terminal fault")
            fault_details(e)
        else:
            require(e.type == 0x8002, "unsupported data event")


class BufferBudget:
    def __init__(self, capacity):
        if capacity < 0:
            raise ValueError("invalid buffer budget")
        self.capacity, self.reserved_bytes = capacity, 0
        self.lock = threading.Lock()

    def reserve(self, size):
        with self.lock:
            if size < 0 or size > self.capacity - self.reserved_bytes:
                raise BufferError("node frame buffer budget exhausted")
            self.reserved_bytes += size

    def release(self, size):
        with self.lock:
            self.reserved_bytes -= size


class Lease:
    """Explicit retain/close; bytes() exports an independent caller-owned copy."""
    def __init__(self, pool, slot, offset, size):
        self._pool, self._slot, self._offset, self._size = pool, slot, offset, size
        pool.refs[slot] += 1  # caller holds pool.condition

    def __bool__(self):
        return self._pool is not None

    @property
    def capacity_bytes(self):
        return self._pool.slot_bytes if self._pool else 0

    def bytes(self):
        pool = self._pool
        if not pool:
            raise ValueError("released lease")
        with pool.condition:
            if self._pool is not pool:
                raise ValueError("released lease")
            return bytes(pool.buffers[self._slot][self._offset:self._offset + self._size])

    def slice(self, offset, size):
        pool = self._pool
        if not pool or offset < 0 or size < 0 or offset + size > self._size:
            raise ValueError("invalid lease slice")
        with pool.condition:
            if self._pool is not pool:
                raise ValueError("released lease")
            return Lease(pool, self._slot, self._offset + offset, size)

    def retain(self):
        return self.slice(0, self._size)

    def close(self):
        pool = self._pool
        if pool:
            with pool.condition:
                if self._pool is not pool:
                    return
                pool.refs[self._slot] -= 1
                self._pool = None
                pool._release_if_closed()
                pool.condition.notify_all()

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class BufferPool:
    def __init__(self, slots, slot_bytes, budget=None):
        if slots <= 0 or slot_bytes <= 0:
            raise ValueError("invalid pool capacity")
        self.slots, self.slot_bytes, self.budget = slots, slot_bytes, budget
        self.capacity_bytes = slots * slot_bytes
        self._reserved = False
        if budget:
            budget.reserve(self.capacity_bytes)
            self._reserved = True
        self.condition = threading.Condition()
        self.closed = False
        self.buffers = [bytearray(slot_bytes) for _ in range(slots)]
        self.refs = [0] * slots

    def copy(self, payload):
        return self.wait_copy(payload, time.monotonic())

    def wait_copy(self, payload, deadline, cancelled=None):
        if len(payload) > self.slot_bytes:
            raise BufferError("block exceeds pool slot")
        with self.condition:
            while True:
                if self.closed or cancelled is not None and cancelled.is_set():
                    return None
                for slot, refs in enumerate(self.refs):
                    if refs == 0:
                        self.buffers[slot][:len(payload)] = payload
                        return Lease(self, slot, 0, len(payload))
                left = deadline - time.monotonic()
                if left <= 0:
                    return None
                self.condition.wait(min(left, 0.01) if cancelled else left)

    def close(self):
        with self.condition:
            self.closed = True
            self._release_if_closed()
            self.condition.notify_all()

    def _release_if_closed(self):
        if self.closed and not any(self.refs):
            self.buffers = []
            if self._reserved:
                self.budget.release(self.capacity_bytes)
                self._reserved = False

    @property
    def leased_slots(self):
        with self.condition:
            return sum(bool(n) for n in self.refs)

    def __del__(self):
        if getattr(self, "_reserved", False):
            self.budget.release(self.capacity_bytes)


@dataclass
class Packet:
    alias: int
    sequence: int
    info: Info
    payload: Lease | None

    def close(self):
        if self.payload:
            self.payload.close()


class Codec:
    def __init__(self, descriptor):
        descriptor.validate()
        self.descriptor = descriptor
        self.compressor = _zstd.ZstdCompressor(level=1) if descriptor.encoding == Encoding.ZSTD else None
        # backend_c passes this byte bound directly to Zstd. Independently
        # validate content_size before the provider can allocate output.
        self.decompressor = _zstd.ZstdDecompressor(max_window_size=wire.MAX_PAYLOAD_BYTES) if self.compressor else None

    def encode(self, alias, sequence, info, payload):
        d = self.descriptor
        validate_info(d, info, len(payload))
        require(uint(alias, 32) and alias and uint(sequence) and sequence != U64, "invalid alias/sequence")
        body, chosen = payload, Encoding.RAW
        encoded = not info.fault_only and d.encoding != Encoding.RAW
        if encoded:
            candidate = _lz4.compress(payload, store_size=False) if d.encoding == Encoding.LZ4 else self.compressor.compress(payload)
            if len(candidate) < len(payload):
                body, chosen = candidate, d.encoding
        value = struct.pack("<HHI16sQQQII", 1, int(info.segment_start) | int(info.fault_only) << 1, info.frames, info.segment,
                            info.first_frame, info.configuration_revision, info.timing_revision, len(info.events), 24 * len(info.events))
        value += b"".join(struct.pack("<IHH", e.offset, e.type, 16) + e.value for e in info.events)
        extensions = struct.pack("<HH", FRAME_EXTENSION, len(value)) + value
        if encoded:
            extensions += struct.pack("<HHHHII", ENCODING_EXTENSION, 12, 1, chosen, len(payload), zlib.crc32(payload))
        area = struct.pack("<H", len(extensions)) + extensions
        area += bytes(-len(area) % 8)
        return struct.pack("<BBHIQ", 1, 1, d.data_type.id, alias, sequence) + area + body

    def decode(self, message, pool, available_frames=U64):
        d = self.descriptor
        require(18 <= len(message) <= MAX_MESSAGE_BYTES, "invalid frame message length")
        version, flags, typ, alias, sequence, tlv = struct.unpack_from("<BBHIQH", message)
        require(version == 1 and flags == 1 and typ == d.data_type.id and alias and sequence != U64, "frame header mismatch")
        end, payload_start = 18 + tlv, 16 + (2 + tlv + 7) // 8 * 8
        require(tlv <= 56 + 2048 + 64 and payload_start <= len(message), "extension area exceeds bound")
        require(not any(message[end:payload_start]), "nonzero extension padding")
        offset, previous, info, encoding = 18, 0, None, None
        decoded_size, crc = 0, 0
        while offset < end:
            require(end - offset >= 4, "truncated extension")
            typ, length = struct.unpack_from("<HH", message, offset)
            require(typ > previous and length <= end - offset - 4, "duplicate/unsorted/truncated extension")
            value = memoryview(message)[offset + 4:offset + 4 + length]
            offset += 4 + length
            previous = typ
            if typ == FRAME_EXTENSION:
                require(length >= 56, "truncated frame extension")
                ver, flags, frames, segment, first, config, timing, count, event_bytes = struct.unpack_from("<HHI16sQQQII", value)
                require(ver == 1 and flags <= 3 and count <= MAX_EVENTS and event_bytes == count * 24 and length == 56 + count * 24, "invalid frame extension")
                events = []
                for n in range(count):
                    pos, kind, size = struct.unpack_from("<IHH", value, 56 + n * 24)
                    require(size == 16, "invalid event length")
                    events.append(Event(pos, kind, value[64 + n * 24:80 + n * 24]))
                info = Info(frames, segment, first, config, timing, bool(flags & 1), bool(flags & 2), tuple(events))
                decoded_size = frames * d.frame_bytes
            elif typ == ENCODING_EXTENSION:
                require(length == 12 and info is not None, "invalid encoding extension")
                ver, encoding, size, crc = struct.unpack("<HHII", value)
                require(ver == 1 and size == decoded_size, "decoded size mismatch")
            else:
                require(not typ & 0x8000, "unsupported critical extension")
        require(info is not None and decoded_size <= wire.MAX_PAYLOAD_BYTES, "missing/oversized frames")
        validate_info(d, info, decoded_size)
        require(info.frames <= available_frames, "receive frame window exceeded")
        require((encoding is not None) == (not info.fault_only and d.encoding != Encoding.RAW), "encoding profile mismatch")
        chosen = encoding if encoding is not None else Encoding.RAW
        require(chosen in (Encoding.RAW, d.encoding), "unnegotiated codec")
        body = message[payload_start:]
        require(len(body) <= wire.MAX_PAYLOAD_BYTES, "encoded payload exceeds bound")
        raw = body
        if chosen != Encoding.RAW:
            require(0 < len(body) < decoded_size, "noncanonical compressed block")
            try:
                if chosen == Encoding.LZ4:
                    raw = _lz4.decompress(body, uncompressed_size=decoded_size)
                else:
                    params = _zstd.get_frame_parameters(body)
                    require(params.content_size == decoded_size and params.dict_id == 0, "invalid Zstandard frame content/dictionary")
                    raw = self.decompressor.decompress(body, max_output_size=decoded_size, allow_extra_data=False)
            except (ValueError, _lz4.LZ4BlockError if _lz4 else ValueError, _zstd.ZstdError if _zstd else ValueError) as error:
                raise wire.FormatError("invalid compressed frame") from error
        require(len(raw) == decoded_size and (encoding is None or zlib.crc32(raw) == crc), "frame payload/checksum mismatch")
        lease = None if info.fault_only else pool.copy(raw)
        if not info.fault_only and lease is None:
            raise BufferError("receive frame pool exhausted")
        return Packet(alias, sequence, info, lease)


@dataclass(frozen=True)
class Result:
    frames: int = 0
    status: Status = Status.OK
    info: Info | None = None
    lost_frames: int | None = None
    payload: bytes = b""


@dataclass(frozen=True)
class FaultRecord:
    status: Status
    sequence: int
    first_frame: int
    segment: bytes
    lost_frames: int | None


class FrameSink:
    def __init__(self, descriptor, sequences, pool, alias=1):
        self.d, self.sequences, self.pool, self.alias = descriptor, sequences, pool, alias
        self.codec = Codec(descriptor)
        require(pool.slots >= descriptor.window_packets and pool.slot_bytes >= MAX_MESSAGE_BYTES and uint(alias, 32) and alias, "insufficient send pool/invalid alias")
        self.terminal_pool = BufferPool(1, 256, pool.budget)
        self.lock = threading.RLock()
        self.log, self.peers, self.history = deque(), {}, deque(maxlen=16)
        self.next_sequence = self.reservation = sequences.reserved_end(descriptor.stream_id)
        self.segment, self.frame, self.outstanding = uuid.uuid4().bytes, 0, 0
        self.state, self.cause = "active", Status.OK

    def _reserve(self):
        require(uint(self.next_sequence) and self.next_sequence != U64, "sequence exhausted")
        if self.next_sequence == self.reservation:
            require(self.next_sequence <= U64 - 65536, "sequence reservation overflow")
            end = self.next_sequence + 65536
            self.sequences.reserve(self.d.stream_id, end)
            self.reservation = end

    def _info(self, frames, events):
        return Info(frames, self.segment, self.frame, self.d.configuration_revision, self.d.timing_revision,
                    self.frame == 0, False, tuple(events))

    def _publish(self, payload, frames, events, retained=None):
        if self.state != "active":
            return Result(status=Status.CLOSED if self.state == "closed" else self.cause)
        info = self._info(frames, events)
        validate_info(self.d, info, len(payload))
        if len(self.log) >= self.d.window_packets or frames > self.d.window_frames - self.outstanding:
            return Result(status=Status.BACKPRESSURE)
        self._reserve()
        sealed = self.pool.copy(self.codec.encode(self.alias, self.next_sequence, info, payload))
        if sealed is None:
            return Result(status=Status.BACKPRESSURE)
        self.log.append((self.next_sequence, frames, sealed, retained.retain() if retained else None))
        self.next_sequence += 1
        self.frame += frames
        self.outstanding += frames
        return Result(frames)

    def publish_shared(self, lease, frames, events=(), cancelled=None):
        with self.lock:
            if cancelled is not None and cancelled.is_set():
                return Result(status=Status.CANCELLED)
            require(lease.capacity_bytes <= wire.MAX_PAYLOAD_BYTES, "shared allocation exceeds quota")
            return self._publish(lease.bytes(), frames, events, lease)

    def publish_copy(self, payload, frames, events=(), cancelled=None):
        """Interleaved portable numeric bytes; Python numbers use publish_elements."""
        require(uint(frames) and len(payload) == frames * self.d.frame_bytes, "invalid input frame length")
        events = tuple(events)
        require(len(events) <= MAX_EVENTS and all(e.type == 0x8002 and uint(e.offset, 32) and e.offset < frames for e in events), "invalid input event")
        require(all(events[i - 1].offset < events[i].offset for i in range(1, len(events))), "duplicate/unsorted input events")
        with self.lock:
            done = 0
            while done < frames:
                if cancelled is not None and cancelled.is_set():
                    return Result(done, Status.CANCELLED)
                count = min(frames - done, self.d.max_frames_per_packet)
                local = tuple(replace(e, offset=e.offset - done) for e in events if done <= e.offset < done + count)
                result = self._publish(payload[done * self.d.frame_bytes:(done + count) * self.d.frame_bytes], count, local)
                done += result.frames
                if result.status != Status.OK:
                    return replace(result, frames=done)
            return Result(done)

    def publish_elements(self, elements, events=(), cancelled=None):
        require(len(elements) % len(self.d.channels) == 0, "partial input frame")
        return self.publish_copy(wire.encode_elements(self.d.data_type, elements), len(elements) // len(self.d.channels), events, cancelled)

    def _trim(self):
        if not self.peers:
            return
        minimum = min(p[1] for p in self.peers.values())
        while self.log and self.log[0][0] < minimum:
            _, count, message, retained = self.log.popleft()
            self.outstanding -= count
            message.close()
            if retained:
                retained.close()
        if self.state == "draining_fault" and not self.log:
            self.state = "faulted"

    def resume(self, peer, next_expected):
        with self.lock:
            require(uint(next_expected) and next_expected <= self.next_sequence and isinstance(peer, str) and 0 < len(peer.encode()) <= 128
                    and (peer in self.peers or len(self.peers) < 64), "invalid resume/peer")
            oldest = self.log[0][0] if self.log else self.next_sequence
            entry = self.peers.setdefault(peer, [0, oldest, False])
            entry[0], entry[2] = max(next_expected, oldest), True
            self._trim()

    def detach(self, peer):
        with self.lock:
            if peer in self.peers:
                self.peers[peer][2] = False

    def acknowledge(self, peer, consumed_up_to):
        with self.lock:
            if peer in self.peers and uint(consumed_up_to) and consumed_up_to <= self.peers[peer][0]:
                self.peers[peer][1] = max(self.peers[peer][1], consumed_up_to)
                self._trim()

    def next_message(self, peer):
        with self.lock:
            entry = self.peers.get(peer)
            if self.state == "closed" or not entry or not entry[2] or not self.log or entry[0] >= self.next_sequence:
                return None
            index = entry[0] - self.log[0][0]
            if not 0 <= index < len(self.log):
                return None
            message = self.log[index][2].retain()
            entry[0] += 1
            return message

    def fault(self, cause, lost=None):
        event = source_fault(cause, lost)
        with self.lock:
            if self.state != "active":
                return
            info = replace(self._info(0, (event,)), fault_only=True)
            self._reserve()
            marker = self.terminal_pool.copy(self.codec.encode(self.alias, self.next_sequence, info, b""))
            require(marker is not None, "terminal slot still retained")
            self.log.append((self.next_sequence, 0, marker, None))
            self.history.append(FaultRecord(cause, self.next_sequence, self.frame, self.segment, lost))
            self.next_sequence += 1
            self.cause, self.state = cause, "draining_fault"

    def recover(self, segment):
        with self.lock:
            require(self.state == "faulted" and not self.log and self.terminal_pool.leased_slots == 0 and len(segment) == 16
                    and any(segment) and segment != self.segment, "invalid recovery boundary")
            self.segment, self.frame, self.cause, self.state = bytes(segment), 0, Status.OK, "active"

    def close(self):
        with self.lock:
            self.state = "closed"
            while self.log:
                _, _, message, retained = self.log.popleft()
                message.close()
                if retained:
                    retained.close()
            self.terminal_pool.close()


class FrameSource:
    def __init__(self, descriptor, pool, alias=1):
        self.d, self.pool, self.alias, self.codec = descriptor, pool, alias, Codec(descriptor)
        require(pool.slots >= descriptor.window_packets and pool.slot_bytes >= wire.MAX_PAYLOAD_BYTES and uint(alias, 32) and alias, "insufficient receive pool/invalid alias")
        self.lock = threading.RLock()
        self.queue, self.history = deque(), deque(maxlen=16)
        self.next_expected = self.consumed_up_to = self.frame = self.buffered = self.offset = 0
        self.segment = self.required_segment = self.lost_frames = None
        self.status, self.terminal, self.closed = Status.OK, False, False

    def _fault(self, cause, lost=None):
        self.status, self.lost_frames = cause, lost
        self.history.append(FaultRecord(cause, self.next_expected, self.frame, self.segment or bytes(16), lost))

    def on_message(self, message):
        with self.lock:
            if self.closed or self.status != Status.OK:
                return False
            packet = None
            try:
                require(18 <= len(message) <= MAX_MESSAGE_BYTES, "invalid frame message length")
                ver, flags, typ, alias, seq = struct.unpack_from("<BBHIQ", message)
                require(ver == 1 and flags == 1 and typ == self.d.data_type.id and alias == self.alias, "frame header/type/alias mismatch")
                if seq < self.next_expected:
                    return False
                if seq > self.next_expected:
                    self._fault(Status.SEQUENCE_GAP)
                    return False
                require(not self.terminal, "data follows terminal fault")
                budget = 0 if len(self.queue) >= self.d.window_packets else self.d.window_frames - self.buffered
                packet = self.codec.decode(message, self.pool, budget)
                info = packet.info
                require(len(self.queue) < self.d.window_packets + 1 and info.frames <= self.d.window_frames - self.buffered, "receive frame window exceeded")
                require(info.segment == self.segment and not info.segment_start if self.segment else
                        info.segment_start and info.first_frame == 0 and (self.required_segment is None or info.segment == self.required_segment), "unapproved initial/changed segment")
                require(info.first_frame == self.frame, "noncontiguous frame position")
                self.queue.append(packet)
                self.segment, self.terminal = info.segment, info.fault_only
                self.frame += info.frames
                self.buffered += info.frames
                self.next_expected += 1
                return True
            except (wire.FormatError, BufferError):
                if packet:
                    packet.close()
                self._fault(Status.MALFORMED)
                return False

    def front_packet(self):
        with self.lock:
            if self.closed or self.status != Status.OK or not self.queue or self.queue[0].info.fault_only:
                return None
            p = self.queue[0]
            info = p.info.sliced(self.offset, p.info.frames - self.offset)
            return Packet(p.alias, p.sequence, info, p.payload.slice(self.offset * self.d.frame_bytes, info.frames * self.d.frame_bytes))

    def consume(self, frames):
        require(uint(frames), "invalid consumption count")
        with self.lock:
            if self.closed or self.status != Status.OK:
                return Result(status=Status.CLOSED if self.closed else self.status, lost_frames=self.lost_frames)
            if not self.queue:
                return Result(status=Status.BACKPRESSURE)
            result = Result()
            if not self.queue[0].info.fault_only:
                packet = self.queue[0]
                count = min(frames, packet.info.frames - self.offset)
                result = Result(count, info=packet.info.sliced(self.offset, count))
                self.offset += count
                self.buffered -= count
                if self.offset == packet.info.frames:
                    self.consumed_up_to, self.offset = packet.sequence + 1, 0
                    self.queue.popleft().close()
            if self.queue and self.queue[0].info.fault_only:
                marker = self.queue.popleft()
                self.status, self.lost_frames = fault_details(marker.info.events[0])
                self.history.append(FaultRecord(self.status, marker.sequence, marker.info.first_frame, marker.info.segment, self.lost_frames))
                self.consumed_up_to = marker.sequence + 1
                result = replace(result, status=self.status, lost_frames=self.lost_frames, info=result.info if result.frames else marker.info)
            return result

    def receive_copy(self, max_frames):
        require(uint(max_frames), "invalid read count")
        with self.lock:
            packet = self.front_packet()
            payload = b""
            if packet:
                count = min(max_frames, packet.info.frames)
                payload = packet.payload.bytes()[:count * self.d.frame_bytes]
                packet.close()
            return replace(self.consume(max_frames), payload=payload)

    def sink_underflow(self, lost=None):
        require(lost is None or uint(lost), "invalid loss count")
        with self.lock:
            if self.status == Status.OK:
                self._fault(Status.SINK_UNDERFLOW, lost)

    def recover(self, next_sequence, segment):
        with self.lock:
            require(not self.closed and self.status != Status.OK and uint(next_sequence) and next_sequence >= self.next_expected
                    and len(segment) == 16 and any(segment) and segment != self.segment, "invalid recovery boundary")
            while self.queue:
                self.queue.popleft().close()
            self.next_expected = self.consumed_up_to = next_sequence
            self.frame = self.buffered = self.offset = 0
            self.segment, self.required_segment, self.lost_frames = None, bytes(segment), None
            self.status, self.terminal = Status.OK, False

    def close(self):
        with self.lock:
            self.closed = True
            self.status = Status.CLOSED
            while self.queue:
                self.queue.popleft().close()

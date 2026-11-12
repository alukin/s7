// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.math.BigInteger;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import s7.frames.FrameBuffers.Lease;
import s7.frames.FrameBuffers.Pool;
import s7.frames.FrameFormat.*;
import s7.stream.SequenceStore;
import s7.wire.WireFormat;

/** Nonblocking frame producer; socket owner authenticates peers and carries ACK/RESUME. */
public final class FrameSink implements AutoCloseable {
    private record Logged(long sequence, int frames, Lease message, Lease input) {}
    private static final class Peer { long sent, acknowledged; boolean attached; }
    private final Descriptor d;
    private final SequenceStore sequences;
    private final Pool pool, terminalPool;
    private final Codec codec;
    private final int alias;
    private final ArrayDeque<Logged> log = new ArrayDeque<>();
    private final Map<String, Peer> peers = new HashMap<>();
    private final ArrayDeque<FaultRecord> history = new ArrayDeque<>();
    private long next, reservation, frame, outstanding;
    private UUID segment = UUID.randomUUID();
    private String state = "active";
    private Status cause = Status.OK;
    public FrameSink(Descriptor d, SequenceStore sequences, Pool pool, int alias) {
        this.d = d; this.sequences = sequences; this.pool = pool; this.alias = alias; codec = new Codec(d);
        FrameFormat.require(pool.slots() >= d.windowPackets() && pool.slotBytes() >= FrameFormat.MAX_MESSAGE_BYTES && alias != 0, "invalid send pool/alias");
        terminalPool = new Pool(1, 256, pool.budget()); next = reservation = sequences.reservedEnd(WireFormat.parseStreamId(d.streamId().toString()));
    }
    private void reserve() {
        FrameFormat.require(next != -1, "sequence exhausted");
        if (next == reservation) {
            FrameFormat.require(Long.compareUnsigned(next, -1L - 65536) <= 0, "sequence reservation overflow");
            long end = next + 65536; sequences.reserve(WireFormat.parseStreamId(d.streamId().toString()), end); reservation = end;
        }
    }
    private Info info(int count, List<Event> events, boolean terminal) {
        return new Info(count, segment, frame, d.configurationRevision(), d.timingRevision(), frame == 0, terminal, events);
    }
    private Result publish(byte[] bytes, int count, List<Event> events, Lease retained) {
        if (!state.equals("active")) return Result.status(state.equals("closed") ? Status.CLOSED : cause);
        Info info = info(count, events, false); FrameFormat.validateInfo(d, info, bytes.length);
        if (log.size() >= d.windowPackets() || count > d.windowFrames() - outstanding) return Result.status(Status.BACKPRESSURE);
        reserve(); Lease sealed = pool.copy(codec.encode(alias, next, info, bytes));
        if (sealed == null) return Result.status(Status.BACKPRESSURE);
        log.add(new Logged(next, count, sealed, retained == null ? null : retained.retain()));
        next++; frame += count; outstanding += count;
        return new Result(count, Status.OK, null, null, new byte[0]);
    }
    public synchronized Result publishShared(Lease bytes, int frames, List<Event> events, BooleanSupplier cancelled) {
        if (cancelled.getAsBoolean()) return Result.status(Status.CANCELLED);
        FrameFormat.require(bytes.capacityBytes() <= WireFormat.MAX_PAYLOAD_BYTES, "shared allocation exceeds quota");
        return publish(bytes.bytes(), frames, List.copyOf(events), bytes);
    }
    /** Interleaved portable numeric bytes, avoiding Java float/NaN conversions. */
    public Result publishCopy(byte[] bytes, int frames) { return publishCopy(bytes, frames, List.of(), () -> false); }
    public synchronized Result publishCopy(byte[] bytes, int frames, List<Event> events, BooleanSupplier cancelled) {
        FrameFormat.require(frames >= 0 && bytes.length == (long) frames * d.frameBytes() && events.size() <= FrameFormat.MAX_EVENTS, "invalid frame input");
        int previous = -1;
        for (Event e : events) { FrameFormat.require(e.type() == 0x8002 && e.offset() > previous && e.offset() < frames, "invalid input events"); previous = e.offset(); }
        int done = 0;
        while (done < frames) {
            if (cancelled.getAsBoolean()) return new Result(done, Status.CANCELLED, null, null, new byte[0]);
            int count = Math.min(frames - done, d.maxFramesPerPacket()); List<Event> local = new ArrayList<>();
            for (Event e : events) if (e.offset() >= done && e.offset() - done < count) local.add(new Event(e.offset() - done, e.type(), e.value()));
            Result r = publish(Arrays.copyOfRange(bytes, done * d.frameBytes(), (done + count) * d.frameBytes()), count, local, null);
            done += r.frames(); if (r.status() != Status.OK) return new Result(done, r.status(), null, null, new byte[0]);
        }
        return new Result(done, Status.OK, null, null, new byte[0]);
    }
    public Result publishElements(List<BigInteger[]> elements) {
        FrameFormat.require(elements.size() % d.channels().size() == 0, "partial input frame");
        return publishCopy(WireFormat.encodeElements(d.type(), elements), elements.size() / d.channels().size());
    }
    private void trim() {
        if (peers.isEmpty()) return;
        long minimum = -1; for (Peer p : peers.values()) if (Long.compareUnsigned(p.acknowledged, minimum) < 0) minimum = p.acknowledged;
        while (!log.isEmpty() && Long.compareUnsigned(log.peek().sequence, minimum) < 0) {
            Logged item = log.remove(); outstanding -= item.frames; item.message.close(); if (item.input != null) item.input.close();
        }
        if (state.equals("draining_fault") && log.isEmpty()) state = "faulted";
    }
    public synchronized void resume(String peer, long nextExpected) {
        FrameFormat.require(Long.compareUnsigned(nextExpected, next) <= 0 && !peer.isEmpty() && peer.getBytes(java.nio.charset.StandardCharsets.UTF_8).length <= 128
                && (peers.containsKey(peer) || peers.size() < 64), "invalid resume/peer");
        long oldest = log.isEmpty() ? next : log.peek().sequence;
        Peer p = peers.get(peer);
        if (p == null) { p = new Peer(); p.acknowledged = oldest; peers.put(peer, p); }
        p.sent = Long.compareUnsigned(nextExpected, oldest) < 0 ? oldest : nextExpected; p.attached = true; trim();
    }
    public synchronized void detach(String peer) { Peer p = peers.get(peer); if (p != null) p.attached = false; }
    public synchronized void acknowledge(String peer, long consumedUpTo) {
        Peer p = peers.get(peer);
        if (p != null && Long.compareUnsigned(consumedUpTo, p.sent) <= 0) {
            if (Long.compareUnsigned(consumedUpTo, p.acknowledged) > 0) p.acknowledged = consumedUpTo; trim();
        }
    }
    public synchronized Lease nextMessage(String peer) {
        Peer p = peers.get(peer);
        if (state.equals("closed") || p == null || !p.attached || log.isEmpty() || Long.compareUnsigned(p.sent, next) >= 0) return null;
        long index = p.sent - log.peek().sequence; if (index < 0 || index >= log.size()) return null;
        int i = 0; for (Logged item : log) if (i++ == index) { Lease result = item.message.retain(); p.sent++; return result; }
        return null;
    }
    public synchronized void fault(Status cause, Long lostFrames) {
        Event event = FrameFormat.sourceFault(cause, lostFrames); if (!state.equals("active")) return;
        reserve(); Lease marker = terminalPool.copy(codec.encode(alias, next, info(0, List.of(event), true), new byte[0]));
        FrameFormat.require(marker != null, "terminal slot still retained");
        log.add(new Logged(next, 0, marker, null));
        if (history.size() == 16) history.remove(); history.add(new FaultRecord(cause, next, frame, segment, lostFrames));
        next++; this.cause = cause; state = "draining_fault";
    }
    public synchronized void recover(UUID newSegment) {
        FrameFormat.require(state.equals("faulted") && log.isEmpty() && terminalPool.leasedSlots() == 0 && !newSegment.equals(segment)
                && !newSegment.equals(new UUID(0, 0)), "invalid recovery boundary");
        segment = newSegment; frame = 0; state = "active"; cause = Status.OK;
    }
    public synchronized String state() { return state; }
    public synchronized int logDepth() { return log.size(); }
    public synchronized long nextSequence() { return next; }
    public synchronized List<FaultRecord> recentFaults() { return List.copyOf(history); }
    @Override public synchronized void close() {
        state = "closed";
        // Closing a managed handle deterministically releases its owned leases;
        // already exported leases remain independently valid.
        for (Logged item : log) { item.message.close(); if (item.input != null) item.input.close(); }
        log.clear(); terminalPool.close();
    }
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import s7.frames.FrameBuffers.Pool;
import s7.frames.FrameFormat.*;
import s7.wire.WireFormat;

/** Frame consumer: receipt and consumption positions remain distinct. */
public final class FrameSource implements AutoCloseable {
    private final Descriptor d;
    private final Pool pool;
    private final Codec codec;
    private final int alias;
    private final ArrayDeque<Packet> queue = new ArrayDeque<>();
    private final ArrayDeque<FaultRecord> history = new ArrayDeque<>();
    private long next, consumed, frame, buffered;
    private int offset;
    private UUID segment, requiredSegment;
    private Status status = Status.OK;
    private Long lost;
    private boolean terminal, closed;
    public FrameSource(Descriptor d, Pool pool, int alias) {
        this.d = d; this.pool = pool; this.alias = alias; codec = new Codec(d);
        FrameFormat.require(pool.slots() >= d.windowPackets() && pool.slotBytes() >= WireFormat.MAX_PAYLOAD_BYTES && alias != 0, "invalid receive pool/alias");
    }
    private void record(Status cause, long sequence, long position, UUID id, Long loss) {
        if (history.size() == 16) history.remove(); history.add(new FaultRecord(cause, sequence, position, id, loss));
    }
    private void fault(Status cause, Long loss) {
        status = cause; lost = loss; record(cause, next, frame, segment == null ? new UUID(0, 0) : segment, loss);
    }
    public synchronized boolean onMessage(byte[] message) {
        if (closed || status != Status.OK) return false;
        Packet packet = null;
        try {
            FrameFormat.require(message.length >= 18 && message.length <= FrameFormat.MAX_MESSAGE_BYTES, "invalid frame length");
            ByteBuffer b = ByteBuffer.wrap(message).order(ByteOrder.LITTLE_ENDIAN);
            FrameFormat.require(b.get() == 1 && b.get() == 1 && (b.getShort() & 65535) == d.type().id && b.getInt() == alias, "frame header/type/alias mismatch");
            long sequence = b.getLong(); if (Long.compareUnsigned(sequence, next) < 0) return false;
            if (Long.compareUnsigned(sequence, next) > 0) { fault(Status.SEQUENCE_GAP, null); return false; }
            FrameFormat.require(!terminal, "data follows terminal fault");
            long budget = queue.size() >= d.windowPackets() ? 0 : d.windowFrames() - buffered;
            packet = codec.decode(message, pool, budget); Info info = packet.info();
            FrameFormat.require(queue.size() < d.windowPackets() + 1 && info.frames() <= d.windowFrames() - buffered, "receive window exceeded");
            FrameFormat.require(segment == null ? info.segmentStart() && info.firstFrame() == 0 && (requiredSegment == null || requiredSegment.equals(info.segment()))
                    : segment.equals(info.segment()) && !info.segmentStart(), "unapproved segment transition");
            FrameFormat.require(info.firstFrame() == frame, "noncontiguous frame position");
            queue.add(packet); segment = info.segment(); terminal = info.faultOnly(); frame += info.frames(); buffered += info.frames(); next++; return true;
        } catch (WireFormat.FormatError | IllegalStateException ex) {
            if (packet != null) packet.close(); fault(Status.MALFORMED, null); return false;
        }
    }
    public synchronized Packet frontPacket() {
        if (closed || status != Status.OK || queue.isEmpty() || queue.peek().info().faultOnly()) return null;
        Packet p = queue.peek(); Info info = p.info().sliced(offset, p.info().frames() - offset);
        return new Packet(p.alias(), p.sequence(), info, p.payload().slice(offset * d.frameBytes(), info.frames() * d.frameBytes()));
    }
    public synchronized Result consume(int frames) {
        FrameFormat.require(frames >= 0, "invalid consumption count");
        if (closed || status != Status.OK) return new Result(0, closed ? Status.CLOSED : status, null, lost, new byte[0]);
        if (queue.isEmpty()) return Result.status(Status.BACKPRESSURE);
        int count = 0; Info info = null;
        if (!queue.peek().info().faultOnly()) {
            Packet packet = queue.peek(); count = Math.min(frames, packet.info().frames() - offset); info = packet.info().sliced(offset, count);
            offset += count; buffered -= count;
            if (offset == packet.info().frames()) { consumed = packet.sequence() + 1; queue.remove().close(); offset = 0; }
        }
        if (!queue.isEmpty() && queue.peek().info().faultOnly()) {
            Packet marker = queue.remove(); Fault details = FrameFormat.faultDetails(marker.info().events().get(0));
            status = details.status(); lost = details.lostFrames(); consumed = marker.sequence() + 1;
            record(status, marker.sequence(), marker.info().firstFrame(), marker.info().segment(), lost);
            if (count == 0) info = marker.info();
        }
        return new Result(count, status, info, lost, new byte[0]);
    }
    public synchronized Result receiveCopy(int maxFrames) {
        FrameFormat.require(maxFrames >= 0, "invalid read count"); byte[] payload = new byte[0];
        try (Packet p = frontPacket()) {
            if (p != null) payload = Arrays.copyOf(p.payload().bytes(), Math.min(maxFrames, p.info().frames()) * d.frameBytes());
        }
        Result r = consume(maxFrames); return new Result(r.frames(), r.status(), r.info(), r.lostFrames(), payload);
    }
    public synchronized void sinkUnderflow(Long loss) { if (status == Status.OK) fault(Status.SINK_UNDERFLOW, loss); }
    public synchronized void recover(long nextSequence, UUID newSegment) {
        FrameFormat.require(!closed && status != Status.OK && Long.compareUnsigned(nextSequence, next) >= 0 && !newSegment.equals(new UUID(0, 0))
                && !newSegment.equals(segment), "invalid recovery boundary");
        while (!queue.isEmpty()) queue.remove().close();
        next = consumed = nextSequence; frame = buffered = offset = 0; segment = null; requiredSegment = newSegment; lost = null; status = Status.OK; terminal = false;
    }
    public synchronized long nextExpected() { return next; }
    public synchronized long consumedUpTo() { return consumed; }
    public synchronized Status status() { return closed ? Status.CLOSED : status; }
    public synchronized List<FaultRecord> recentFaults() { return List.copyOf(history); }
    @Override public synchronized void close() { closed = true; while (!queue.isEmpty()) queue.remove().close(); }
}

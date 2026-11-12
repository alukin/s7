// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import s7.frames.FrameBuffers.*;
import s7.frames.FrameFormat.*;
import s7.json.Json;
import s7.stream.SequenceStore;
import s7.wire.WireFormat;
import s7.wire.WireType;

public final class FrameCheck {
    private static int checks, skipped;
    private FrameCheck() {}
    private static void check(boolean value, String error) { checks++; if (!value) throw new AssertionError(error); }
    private static void rejects(Runnable action) {
        boolean rejected = false; try { action.run(); } catch (RuntimeException ex) { rejected = true; }
        check(rejected, "invalid input accepted");
    }
    private static Descriptor descriptor(Encoding encoding, int packets) { return new Descriptor(encoding, WireType.I16, List.of("L", "R"), 8, packets); }
    private static void pump(FrameSink sink, FrameSource source) {
        for (Lease lease; (lease = sink.nextMessage("a")) != null;) { try (Lease message = lease) { check(source.onMessage(message.bytes()), "frame rejected"); } }
    }
    private static void transfer(Encoding encoding) {
        Descriptor d = descriptor(encoding, 2);
        try (Pool tx = new Pool(2, FrameFormat.MAX_MESSAGE_BYTES); Pool rx = new Pool(2, WireFormat.MAX_PAYLOAD_BYTES);
                FrameSink sink = new FrameSink(d, new SequenceStore.Memory(), tx, 1); FrameSource source = new FrameSource(d, rx, 1)) {
            sink.resume("a", 0); sink.resume("b", 0); byte[] input = new byte[64]; for (int i = 0; i < input.length; i++) input[i] = (byte) i;
            check(sink.publishCopy(input, 16, List.of(FrameFormat.clockAnchor(3, -123, -1), FrameFormat.clockAnchor(10, 456, 1)), () -> false).frames() == 16, "publication failed");
            byte[][] sent = new byte[2][];
            for (int i = 0; i < 2; i++) try (Lease message = sink.nextMessage("a")) { sent[i] = message.bytes(); check(source.onMessage(sent[i]), "frame rejected"); }
            check(source.nextExpected() == 2 && source.consumedUpTo() == 0, "receipt acknowledged consumption");
            check(!source.onMessage(sent[0]) && rx.leasedSlots() == 2, "duplicate allocated storage");
            sink.detach("a"); sink.resume("a", 2); check(sink.logDepth() == 2, "RESUME acknowledged receipt");
            try (Packet retained = source.frontPacket()) {
                Result first = source.receiveCopy(3); check(first.frames() == 3 && first.info().events().isEmpty() && source.consumedUpTo() == 0, "partial read ACK/event error");
                Result rest = source.receiveCopy(100), second = source.receiveCopy(100);
                check(rest.frames() == 5 && rest.info().firstFrame() == 3 && rest.info().events().get(0).offset() == 0, "event slicing failed");
                check(second.info().events().get(0).offset() == 2, "second packet event lost");
                byte[] output = new byte[64]; System.arraycopy(first.payload(), 0, output, 0, 12); System.arraycopy(rest.payload(), 0, output, 12, 20); System.arraycopy(second.payload(), 0, output, 32, 32);
                check(Arrays.equals(input, output), "sample bits changed");
                sink.acknowledge("a", 2); check(sink.logDepth() == 2, "slow consumer evicted");
                sink.detach("b"); sink.resume("b", 0);
                for (int i = 0; i < 2; i++) try (Lease message = sink.nextMessage("b")) { check(Arrays.equals(sent[i], message.bytes()), "replay changed bytes"); }
                sink.acknowledge("b", 2); check(sink.logDepth() == 0 && rx.leasedSlots() == 1, "ACK/retained lease accounting failed");
                check(retained.payload().bytes().length == 32, "retained payload corrupted");
            }
            check(rx.leasedSlots() == 0, "retained lease leaked");
            check(sink.publishCopy(input, 16, List.of(), () -> true).status() == Status.CANCELLED, "cancelled publication admitted");
        }
    }
    private static void faults(Encoding encoding) {
        Descriptor d = descriptor(encoding, 1);
        try (Pool tx = new Pool(1, FrameFormat.MAX_MESSAGE_BYTES); Pool rx = new Pool(1, WireFormat.MAX_PAYLOAD_BYTES);
                FrameSink sink = new FrameSink(d, new SequenceStore.Memory(), tx, 1); FrameSource source = new FrameSource(d, rx, 1)) {
            sink.resume("a", 0); Result result = sink.publishCopy(new byte[64], 16);
            check(result.frames() == 8 && result.status() == Status.BACKPRESSURE, "partial publication incorrect");
            sink.fault(Status.SOURCE_OVERFLOW, null); check(sink.logDepth() == 2, "reserved terminal missing");
            pump(sink, source); check(source.status() == Status.OK, "terminal fault preceded prefix");
            Result r = source.receiveCopy(100);
            check(r.frames() == 8 && r.status() == Status.SOURCE_OVERFLOW && r.lostFrames() == null && source.consumedUpTo() == 2, "terminal prefix/loss count incorrect");
            sink.acknowledge("a", 2); UUID segment = UUID.randomUUID(); sink.recover(segment); source.recover(2, segment);
            check(source.recentFaults().size() == 1 && sink.recentFaults().size() == 1, "recovery erased fault history");
            check(sink.publishCopy(new byte[4], 1).frames() == 1, "recovery did not restart"); pump(sink, source);
            Result next = source.receiveCopy(1); check(next.info().firstFrame() == 0 && next.info().segment().equals(segment), "new segment incorrect");
            source.sinkUnderflow(11L); check(source.receiveCopy(1).lostFrames().equals(11L), "sink underflow loss count lost");
        }
        try (Pool tx = new Pool(1, FrameFormat.MAX_MESSAGE_BYTES); Pool rx = new Pool(1, WireFormat.MAX_PAYLOAD_BYTES);
                FrameSink sink = new FrameSink(d, new SequenceStore.Memory(), tx, 1); FrameSource source = new FrameSource(d, rx, 1)) {
            sink.resume("a", 0); sink.fault(Status.CONTINUITY_LOST, -1L); pump(sink, source);
            check(source.consume(0).lostFrames() == -1L, "initial loss / unsigned count failed");
        }
    }
    private static void ownership() throws Exception {
        Budget budget = new Budget(16); Pool p = new Pool(1, 16, budget);
        Lease first = p.copy(new byte[]{1, 2, 3}), slice = first.slice(1, 1); first.close();
        check(p.copy(new byte[1]) == null && p.leasedSlots() == 1, "slice did not retain capacity");
        CompletableFuture<Lease> waiter = CompletableFuture.supplyAsync(() -> p.waitCopy(new byte[]{4}, System.nanoTime() + TimeUnit.SECONDS.toNanos(2), () -> false));
        slice.close(); Lease held = waiter.get(2, TimeUnit.SECONDS);
        check(held.bytes()[0] == 4, "release failed to wake waiter");
        AtomicBoolean stop = new AtomicBoolean();
        CompletableFuture<Lease> cancelled = CompletableFuture.supplyAsync(() -> p.waitCopy(new byte[1], System.nanoTime() + TimeUnit.SECONDS.toNanos(2), stop::get));
        stop.set(true); check(cancelled.get(2, TimeUnit.SECONDS) == null, "cancelled wait succeeded");
        CompletableFuture<Lease> closing = CompletableFuture.supplyAsync(() -> p.waitCopy(new byte[1], System.nanoTime() + TimeUnit.SECONDS.toNanos(2), () -> false));
        p.close(); check(closing.get(2, TimeUnit.SECONDS) == null, "pool close did not wake waiter");
        check(budget.reservedBytes() == 16 && held.bytes()[0] == 4, "pool close invalidated live lease");
        held.close(); check(budget.reservedBytes() == 0, "closed pool budget leaked");
        rejects(() -> new Pool(2, 16, budget));
        Descriptor d = descriptor(Encoding.RAW, 1);
        try (Pool input = new Pool(1, 32); Pool tx = new Pool(1, FrameFormat.MAX_MESSAGE_BYTES); Pool rx = new Pool(1, WireFormat.MAX_PAYLOAD_BYTES);
                FrameSink sink = new FrameSink(d, new SequenceStore.Memory(), tx, 1); FrameSource source = new FrameSource(d, rx, 1)) {
            sink.resume("a", 0);
            try (Lease bytes = input.copy(new byte[32])) { check(sink.publishShared(bytes, 8, List.of(), () -> false).frames() == 8, "shared publication failed"); }
            check(input.leasedSlots() == 1, "shared input released before ACK"); pump(sink, source);
            sink.resume("a", source.nextExpected()); check(input.leasedSlots() == 1, "RESUME released shared input");
            source.consume(8); sink.acknowledge("a", 1); check(input.leasedSlots() == 0, "shared input survived ACK");
        }
    }
    private static void concurrent() throws Exception {
        Descriptor d = descriptor(Encoding.RAW, 2);
        try (Pool tx = new Pool(2, FrameFormat.MAX_MESSAGE_BYTES); Pool rx = new Pool(2, WireFormat.MAX_PAYLOAD_BYTES);
                FrameSink sink = new FrameSink(d, new SequenceStore.Memory(), tx, 1); FrameSource source = new FrameSource(d, rx, 1)) {
            sink.resume("a", 0); long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            CompletableFuture<Void> writer = CompletableFuture.runAsync(() -> {
                for (int i = 0; i < 200; i++) while (sink.publishCopy(new byte[32], 8).frames() != 8) {
                    if (System.nanoTime() > deadline) throw new IllegalStateException("writer stalled"); Thread.yield();
                }
            });
            for (int i = 0; i < 200; i++) {
                Lease lease; while ((lease = sink.nextMessage("a")) == null) { if (System.nanoTime() > deadline) throw new AssertionError("reader stalled"); Thread.yield(); }
                try (Lease message = lease) { check(source.onMessage(message.bytes()), "concurrent frame rejected"); }
                Result r = source.receiveCopy(8); check(r.frames() == 8 && r.info().firstFrame() == i * 8L && Arrays.equals(r.payload(), new byte[32]), "concurrent sample/order mismatch");
                sink.acknowledge("a", source.consumedUpTo());
            }
            writer.get(5, TimeUnit.SECONDS); check(sink.logDepth() == 0, "concurrent log not drained");
        }
    }
    public static void main(String[] args) throws Exception {
        Map<?, ?> fixtures = (Map<?, ?>) Json.parse(Files.readString(Path.of(args.length == 0 ? "../s7l2/s7transport/tests/fixtures/frames_v1.json" : args[0])));
        for (String group : List.of("valid", "invalid")) for (Object item : (List<?>) fixtures.get(group)) {
            Map<?, ?> c = (Map<?, ?>) item;
            if (!FrameFormat.encodingAvailable(FrameInterop.descriptor(c).encoding())) { skipped++; continue; }
            if (group.equals("valid")) {
                check(FrameInterop.run(Map.of("op", "decode", "case", c)).get("status").equals("ok"), "valid fixture rejected: " + c.get("name"));
            } else rejects(() -> FrameInterop.run(Map.of("op", "decode", "case", c)));
        }
        for (Encoding encoding : Encoding.values()) {
            if (FrameFormat.encodingAvailable(encoding)) { transfer(encoding); faults(encoding); }
            else rejects(() -> new Codec(descriptor(encoding, 1)));
        }
        ownership(); concurrent();
        System.out.println(checks + " frame checks passed; " + skipped + " unavailable-codec fixtures skipped");
    }
}

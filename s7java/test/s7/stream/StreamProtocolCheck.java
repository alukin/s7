// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package s7.stream;

import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

import s7.wire.WireFormat;
import s7.wire.WireType;

/**
 * Lossless delivery rules with a simulated link: the scenarios and expectations of
 * s7transport/tests/stream_protocol_smoke.cpp. Usage: java -cp build s7.stream.StreamProtocolCheck
 */
public final class StreamProtocolCheck {
    private static final byte[] ID = WireFormat.parseStreamId("0335c8e1-a3ab-4e8c-8b17-dc8170f5e370");
    private static final long MS = Faults.MS;
    private static final long T0 = 1_000_000_000_000L;
    private static int checks;

    static void expect(boolean condition, String what) {
        checks++;
        if (!condition) {
            throw new AssertionError("Check failed: " + what);
        }
    }

    static StreamConfig config(WireType type, long perPacket, long logPackets) {
        StreamConfig c = new StreamConfig();
        c.streamId = ID;
        c.type = type;
        c.maxElementsPerPacket = perPacket;
        c.sendLogMaxPackets = logPackets;
        c.maxBatchLatencyMs = 10;
        return c;
    }

    static List<BigInteger[]> elements(WireType t, List<? extends Number> values) {
        return WireFormat.fromValues(t, values);
    }

    static List<Object> numbers(WireType t, List<BigInteger[]> items) {
        return WireFormat.values(t, items);
    }

    /** Moves messages from producer to consumer; a reset loses what is in flight. */
    static final class Link {
        final StreamProducer producer;
        final StreamConsumer consumer;
        final String key;
        final ArrayDeque<byte[]> inFlight = new ArrayDeque<>();

        Link(StreamProducer producer, StreamConsumer consumer, String key) {
            this.producer = producer;
            this.consumer = consumer;
            this.key = key;
        }

        void connect() {
            WireFormat.ControlMessage resume = consumer.resumeMessage();
            producer.resume(key, resume.streamAlias(), resume.nextExpected());
        }

        void reset() {
            inFlight.clear();
            producer.detach(key);
        }

        void pump(long now, int deliver) {
            byte[] message;
            while ((message = producer.nextMessage(key)) != null) {
                inFlight.add(message);
            }
            for (int n = 0; n < deliver && !inFlight.isEmpty(); n++) {
                consumer.onPacket(WireFormat.decodeDataPacket(inFlight.poll()), now, null);
            }
        }

        void acknowledge(long now) {
            WireFormat.ControlMessage ack = consumer.ackDue(now);
            if (ack != null) {
                WireFormat.ControlMessage decoded = WireFormat.decodeControlMessage(WireFormat.encodeControlMessage(ack));
                producer.acknowledge(key, decoded.nextExpected());
            }
        }
    }

    static List<Object> takeAll(StreamConsumer consumer) {
        return numbers(consumer.config.type, consumer.take((int) consumer.available(), null));
    }

    static void testConsumerPacketLimits() {
        for (WireType type : List.of(WireType.CI12, WireType.CI16, WireType.C32)) {
            for (int count : new int[]{1, 4}) {
                StreamConsumer consumer = new StreamConsumer(config(type, 4, 16), 1);
                var packet = new WireFormat.DataPacket(type, 1, 0, new byte[WireFormat.payloadBytes(type, count)]);
                consumer.onPacket(WireFormat.decodeDataPacket(WireFormat.encodeDataPacket(packet)), T0, null);
                expect(consumer.faultCode() == null && consumer.available() == count,
                        "partial and exact-limit packets accepted");
            }
            for (int size : new int[]{0, 1, WireFormat.payloadBytes(type, 5)}) {
                StreamConsumer consumer = new StreamConsumer(config(type, 4, 16), 1);
                consumer.fault(Faults.SEQUENCE_GAP);
                consumer.recover();
                consumer.onPacket(new WireFormat.DataPacket(type, 1, 99, new byte[size]), T0, null);
                expect(Faults.MALFORMED_PACKET.equals(consumer.faultCode()) && consumer.malformed() == 1,
                        "invalid length/oversized packet faults before decoding");
                expect(consumer.available() == 0 && consumer.packets() == 0 && consumer.elements() == 0
                                && consumer.nextExpected() == 0 && consumer.ackDue(T0 + 100 * MS) == null,
                        "rejected data changes neither delivery nor recovery/ACK position");
            }
        }
        StreamConsumer large = new StreamConsumer(config(WireType.CI16, 100000, 16), 1);
        large.onPacket(new WireFormat.DataPacket(WireType.CI16, 1, 0, new byte[WireFormat.MAX_PAYLOAD_BYTES + 4]), T0, null);
        expect(Faults.MALFORMED_PACKET.equals(large.faultCode()) && large.available() == 0,
                "global payload limit enforced for direct consumers");
        StreamConfig c = config(WireType.CI16, 4, 16);
        c.timestampRoles = List.of(WireFormat.SOURCE_TIME);
        StreamConsumer stamped = new StreamConsumer(c, 1);
        var packet = new WireFormat.DataPacket(WireType.CI16, 1, 0, new byte[8]);
        packet = new WireFormat.DataPacket(packet.type(), packet.streamAlias(), packet.sequence(),
                packet.payload(), List.of(new WireFormat.TimestampRecord(WireFormat.SOURCE_TIME, 0)), null);
        stamped.onPacket(packet, T0, null);
        expect(Faults.MALFORMED_PACKET.equals(stamped.faultCode()) && stamped.nextExpected() == 0,
                "explicit timestamp profile allows one element per packet");
    }

    public static void main(String[] args) throws Exception {
        testConsumerPacketLimits();
        WireType f64 = WireType.F64;

        // batching and delivery
        {
            StreamProducer producer = new StreamProducer(config(f64, 4, 16), new SequenceStore.Memory());
            StreamConsumer consumer = new StreamConsumer(config(f64, 4, 16), 7);
            Link link = new Link(producer, consumer, "consumer");
            link.connect();
            List<Double> values = new ArrayList<>();
            for (int v = 1; v <= 10; v++) {
                values.add((double) v);
            }
            expect(producer.append(elements(f64, values), T0, List.of()) == 10, "all elements accepted");
            expect(producer.packets() == 2, "two full packets sealed");
            expect(!producer.flush(T0 + 5 * MS, false), "partial batch waits for its latency");
            expect(producer.flush(T0 + 10 * MS, false), "partial batch flushed after max_batch_latency");
            link.pump(T0, Integer.MAX_VALUE);
            expect(takeAll(consumer).equals(new ArrayList<Object>(values)), "values arrive once and in order");
        }
        // reconnect replays without loss
        {
            WireType i32 = WireType.I32;
            StreamProducer producer = new StreamProducer(config(i32, 1, 64), new SequenceStore.Memory());
            StreamConsumer consumer = new StreamConsumer(config(i32, 1, 64), 3);
            Link link = new Link(producer, consumer, "consumer");
            link.connect();
            for (long v = 0; v < 6; v++) {
                producer.append(elements(i32, List.of(v)), T0, List.of());
            }
            link.pump(T0, 3); // packets 0..2 arrive, 3..5 are lost with the connection
            link.reset();
            expect(producer.sendLogDepth() == 6, "nothing acknowledged yet: all packets retained");
            link.connect();
            link.pump(T0, Integer.MAX_VALUE);
            expect(takeAll(consumer).equals(List.<Object>of(0L, 1L, 2L, 3L, 4L, 5L)), "replay fills the loss exactly");
            expect(consumer.duplicates() == 0, "resume does not resend delivered packets");
        }
        // backpressure and acknowledgements
        {
            StreamProducer producer = new StreamProducer(config(f64, 1, 3), new SequenceStore.Memory());
            StreamConsumer consumer = new StreamConsumer(config(f64, 1, 3), 1);
            Link link = new Link(producer, consumer, "consumer");
            link.connect();
            List<BigInteger[]> values = elements(f64, List.of(1.0, 2.0, 3.0, 4.0, 5.0));
            int taken = producer.append(values, T0, List.of());
            expect(taken == 3 && producer.logFull(), "a full send log stops new elements (backpressure)");
            link.pump(T0, Integer.MAX_VALUE);
            takeAll(consumer);
            link.acknowledge(T0);
            expect(producer.logFull(), "ACK waits for 64 packets or 50 ms");
            link.acknowledge(T0 + 50 * MS);
            expect(producer.sendLogDepth() == 0, "acknowledged packets leave the log");
            expect(producer.append(values.subList(taken, 5), T0, List.of()) == 2, "room again after the ACK");
        }
        // a restarted producer is a gap; recovery; a consumer ahead is state loss
        {
            Path dir = Files.createTempDirectory("s7java-seq");
            SequenceStore sequences = new SequenceStore.FileStore(dir);
            StreamConsumer consumer = new StreamConsumer(config(f64, 1, 64), 1);
            StreamProducer first = new StreamProducer(config(f64, 1, 64), sequences);
            Link link = new Link(first, consumer, "c");
            link.connect();
            first.append(elements(f64, List.of(1.0)), T0, List.of());
            link.pump(T0, Integer.MAX_VALUE);
            expect(Files.readString(dir.resolve("0335c8e1-a3ab-4e8c-8b17-dc8170f5e370.seq")).equals("65536\n"),
                    "the reservation is durable, in the C++ node's format");
            StreamProducer restarted = new StreamProducer(config(f64, 1, 64), sequences);
            expect(restarted.nextSequence() == Faults.SEQUENCE_RESERVATION, "continues beyond its reservation");
            link = new Link(restarted, consumer, "c");
            link.connect();
            expect(restarted.faultCode() == null && restarted.lostResumes() == 1, "a lost resume position is counted");
            restarted.append(elements(f64, List.of(2.0)), T0, List.of());
            link.pump(T0, Integer.MAX_VALUE);
            expect(Faults.SEQUENCE_GAP.equals(consumer.faultCode()), "the consumer faults on the gap");
            consumer.recover();
            restarted.append(elements(f64, List.of(2.0)), T0, List.of());
            link.pump(T0, Integer.MAX_VALUE);
            expect(consumer.faultCode() == null && takeAll(consumer).equals(List.<Object>of(1.0, 2.0)), "recovered consumer continues");
            StreamProducer behind = new StreamProducer(config(f64, 1, 64), sequences);
            behind.resume("ahead", 1, behind.nextSequence() + 10);
            expect(Faults.STREAM_STATE_LOST.equals(behind.faultCode()), "a resume beyond the producer is state loss");
        }
        // a reservation that cannot be recorded: nothing is sent from it, and the next seal tries again
        {
            int[] attempts = {0};
            long[] recorded = {0};
            SequenceStore failingOnce = new SequenceStore() {
                public long reservedEnd(byte[] streamId) {
                    return recorded[0];
                }

                public void reserve(byte[] streamId, long end) {
                    if (attempts[0]++ == 0) {
                        throw new java.io.UncheckedIOException(new java.io.IOException("disk full"));
                    }
                    recorded[0] = end;
                }
            };
            StreamProducer producer = new StreamProducer(config(f64, 1, 64), failingOnce);
            boolean threw = false;
            try {
                producer.append(elements(f64, List.of(1.0)), T0, List.of());
            } catch (java.io.UncheckedIOException ex) {
                threw = true;
            }
            expect(threw && producer.packets() == 0 && producer.sendLogDepth() == 0, "a failed reservation seals no packet");
            producer.flush(T0 + 10 * MS, true);
            expect(attempts[0] == 2 && recorded[0] == Faults.SEQUENCE_RESERVATION && producer.packets() == 1,
                    "the next seal reserves again before sending");
        }
        // sequence positions are uint64: the upper half of the range orders above the lower one
        {
            long high = Long.MIN_VALUE; // 2^63
            StreamConsumer fresh = new StreamConsumer(config(f64, 1, 64), 1);
            fresh.onPacket(new WireFormat.DataPacket(f64, 1, high, new byte[8]), T0, null);
            expect(Faults.SEQUENCE_GAP.equals(fresh.faultCode()) && fresh.duplicates() == 0,
                    "sequence 2^63 at a fresh consumer is a gap, not a duplicate");
            SequenceStore reserved = new SequenceStore.Memory();
            reserved.reserve(ID, high);
            StreamProducer producer = new StreamProducer(config(f64, 1, 64), reserved);
            producer.resume("c", 1, 0);
            expect(producer.faultCode() == null && producer.lostResumes() == 1, "RESUME(0) below a producer at 2^63 is a lost position, not state loss");
            producer.append(elements(f64, List.of(1.0)), T0, List.of());
            byte[] message = producer.nextMessage("c");
            expect(message != null && WireFormat.decodeDataPacket(message).sequence() == high
                    && reserved.reservedEnd(ID) == high + Faults.SEQUENCE_RESERVATION, "it reserves and sends from 2^63");
            producer.acknowledge("c", high + 1);
            expect(producer.sendLogDepth() == 0, "an ACK above 2^63 trims the log");
        }
        // one owner per state directory
        {
            Path state = Files.createTempDirectory("s7java-state");
            try (java.nio.channels.FileLock held = SequenceStore.FileStore.lock(state)) {
                boolean refused = false;
                try {
                    SequenceStore.FileStore.lock(state).close();
                } catch (IllegalStateException ex) {
                    refused = ex.getMessage().contains("in use");
                }
                expect(held.isValid() && refused, "a second owner of the state directory is refused");
            }
            SequenceStore.FileStore.lock(state).close();
            expect(true, "the directory can be taken again once released");
        }
        // soak: random batching and connection drops, every value exactly once and in order
        {
            Random random = new Random(12345);
            WireType i12 = WireType.I12;
            StreamProducer producer = new StreamProducer(config(i12, 7, 32), new SequenceStore.Memory());
            StreamConsumer consumer = new StreamConsumer(config(i12, 7, 32), 9);
            Link link = new Link(producer, consumer, "consumer");
            link.connect();
            long now = T0;
            List<Object> sent = new ArrayList<>();
            List<Object> received = new ArrayList<>();
            int resets = 0;
            while (sent.size() < 20000) {
                List<Long> chunk = new ArrayList<>();
                int size = 1 + random.nextInt(20);
                for (int i = 0; i < size; i++) {
                    chunk.add((long) (random.nextInt(4096) - 2048));
                }
                int offset = 0;
                while (offset < chunk.size()) {
                    offset += producer.append(elements(i12, chunk.subList(offset, chunk.size())), now, List.of());
                    if (offset < chunk.size()) {
                        link.pump(now, Integer.MAX_VALUE);
                        received.addAll(takeAll(consumer));
                        now += 60 * MS;
                        link.acknowledge(now);
                    }
                }
                sent.addAll(chunk);
                now += (random.nextInt(100) < 50 ? 1 : 12) * MS;
                producer.flush(now, false);
                link.pump(now, 1 + random.nextInt(20));
                if (random.nextInt(100) < 5) {
                    link.reset();
                    link.connect();
                    resets++;
                }
                received.addAll(takeAll(consumer));
                link.acknowledge(now);
            }
            producer.flush(now, true);
            link.pump(now, Integer.MAX_VALUE);
            received.addAll(takeAll(consumer));
            expect(consumer.faultCode() == null && producer.faultCode() == null, "no faults under reconnects");
            expect(resets > 50, "the connection dropped often");
            expect(received.equals(sent), "20000 I12 values exactly once and in order through reconnects");
        }
        // derived timestamps
        {
            StreamConfig c = config(f64, 4, 16);
            c.sampleRate = new long[]{48000, 1};
            c.timestampRoles = List.of(WireFormat.MODEL_TIME);
            StreamProducer producer = new StreamProducer(c, new SequenceStore.Memory());
            StreamConsumer consumer = new StreamConsumer(c, 1);
            Link link = new Link(producer, consumer, "consumer");
            link.connect();
            List<Double> ones = new ArrayList<>();
            for (int i = 0; i < 10; i++) {
                ones.add(1.0);
            }
            producer.append(elements(f64, ones), T0, List.of());
            producer.flush(T0, true);
            link.pump(T0, Integer.MAX_VALUE);
            List<WireFormat.TimestampRecord> times = new ArrayList<>();
            consumer.take(3, times);
            expect(times.equals(List.of(new WireFormat.TimestampRecord(WireFormat.MODEL_TIME, 0))), "element 0 at 0 ns");
            consumer.take(3, times);
            expect(times.equals(List.of(new WireFormat.TimestampRecord(WireFormat.MODEL_TIME, 62_500))), "element 3 at 3 / 48000 s");
            expect(Faults.elementTimeNs(new long[]{1, 3}, 1_000_000_000_000L) == Long.MAX_VALUE, "saturates");
            expect(Faults.elementTimeNs(new long[]{3, 1}, 1) == 333_333_333, "rounded toward zero");
        }
        System.out.println("stream protocol: " + checks + " checks pass");
    }
}

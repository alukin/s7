// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.io.ByteArrayOutputStream;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.zip.CRC32;
import s7.wire.WireFormat;
import s7.wire.WireType;
import s7.frames.FrameBuffers.Lease;
import s7.frames.FrameBuffers.Pool;

/** Opt-in frame profiles; scalar wire decoding and runtime capabilities are unchanged. */
public final class FrameFormat {
    public static final int MAX_MESSAGE_BYTES = WireFormat.MAX_PAYLOAD_BYTES + 4096;
    public static final int FRAME_EXTENSION = 0x8002, ENCODING_EXTENSION = 0x8003, MAX_EVENTS = 32;
    private FrameFormat() {}
    public enum Encoding { RAW, LZ4, ZSTD }
    public enum Status { OK, BACKPRESSURE, CANCELLED, CLOSED, SOURCE_OVERFLOW, CONTINUITY_LOST, SINK_UNDERFLOW, MALFORMED, SEQUENCE_GAP }
    public static void require(boolean condition, String error) { if (!condition) throw new WireFormat.FormatError(error); }
    private static boolean nonzero(UUID id) { return id.getMostSignificantBits() != 0 || id.getLeastSignificantBits() != 0; }
    public static record Descriptor(UUID streamId, WireType type, List<String> channels, long rateNumerator, long rateDenominator,
            String clockDomain, String clockOrigin, int timeRole, String timeProvenance, String positionProvenance,
            int maxFramesPerPacket, int windowPackets, long windowFrames, long configurationRevision, long timingRevision, Encoding encoding) {
        public Descriptor { channels = List.copyOf(channels); }
        public Descriptor(Encoding encoding, WireType type, List<String> channels, int frames, int packets) {
            this(UUID.randomUUID(), type, channels, 48000, 1, "synthetic", "model run origin; nanoseconds", WireFormat.MODEL_TIME,
                    "synthetic", "software", frames, packets, (long) frames * packets, 0, 0, encoding);
        }
        public int frameBytes() { return WireFormat.payloadBytes(type, 1) * channels.size(); }
        public void validate() {
            require(nonzero(streamId) && type != WireType.I12 && type != WireType.U12, "invalid identity/frame type");
            require(!channels.isEmpty() && channels.size() <= 64 && new HashSet<>(channels).size() == channels.size(), "invalid channel count/identity");
            for (String channel : channels) require(!channel.isEmpty() && channel.getBytes(StandardCharsets.UTF_8).length <= 128, "invalid channel name");
            require(rateNumerator != 0 && rateDenominator != 0 && !clockDomain.isEmpty() && clockDomain.getBytes(StandardCharsets.UTF_8).length <= 128
                    && !clockOrigin.isEmpty() && clockOrigin.getBytes(StandardCharsets.UTF_8).length <= 256, "invalid rate/clock definition");
            require((timeRole == WireFormat.MODEL_TIME || timeRole == WireFormat.SOURCE_TIME) && List.of("unknown", "device", "synthetic").contains(timeProvenance)
                    && List.of("software", "hardware").contains(positionProvenance), "invalid provenance");
            require(maxFramesPerPacket > 0 && maxFramesPerPacket <= WireFormat.MAX_PAYLOAD_BYTES / frameBytes(), "frame payload exceeds bound");
            require(windowPackets > 0 && windowPackets <= 65536 && windowFrames >= maxFramesPerPacket
                    && windowFrames <= (long) windowPackets * maxFramesPerPacket, "invalid frame window");
            require(encodingAvailable(encoding), "frame encoding unavailable");
        }
    }
    public static record Event(int offset, int type, byte[] value) {
        public Event { require(offset >= 0 && type >= 0 && type <= 65535 && value.length == 16, "invalid frame event"); value = value.clone(); }
        @Override public byte[] value() { return value.clone(); }
        @Override public boolean equals(Object other) { return other instanceof Event e && e.offset == offset && e.type == type && Arrays.equals(e.value, value); }
        @Override public int hashCode() { return 31 * (31 * offset + type) + Arrays.hashCode(value); }
    }
    public static Event clockAnchor(int offset, long timeNanos, long uncertaintyNanos) {
        return new Event(offset, 0x8002, ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN).putLong(timeNanos).putLong(uncertaintyNanos).array());
    }
    public static Event sourceFault(Status cause, Long lost) {
        require(cause == Status.SOURCE_OVERFLOW || cause == Status.CONTINUITY_LOST, "not a source loss fault");
        return new Event(0, 0x8001, ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN).putShort((short) (cause == Status.SOURCE_OVERFLOW ? 1 : 3))
                .putShort((short) (lost == null ? 0 : 1)).putInt(0).putLong(lost == null ? 0 : lost).array());
    }
    public static record Fault(Status status, Long lostFrames) {}
    public static Fault faultDetails(Event event) {
        require(event.value.length == 16, "invalid fault length"); ByteBuffer b = ByteBuffer.wrap(event.value).order(ByteOrder.LITTLE_ENDIAN);
        int code = b.getShort() & 65535, flags = b.getShort() & 65535, reserved = b.getInt(); long lost = b.getLong();
        require((code == 1 || code == 3) && flags <= 1 && reserved == 0 && (flags != 0 || lost == 0), "invalid source fault");
        return new Fault(code == 1 ? Status.SOURCE_OVERFLOW : Status.CONTINUITY_LOST, flags == 0 ? null : lost);
    }
    public static record Info(int frames, UUID segment, long firstFrame, long configurationRevision, long timingRevision,
            boolean segmentStart, boolean faultOnly, List<Event> events) {
        public Info { events = List.copyOf(events); }
        public Info sliced(int offset, int count) {
            List<Event> subset = new ArrayList<>();
            for (Event e : events) if (e.offset >= offset && e.offset - offset < count) subset.add(new Event(e.offset - offset, e.type, e.value));
            return new Info(count, segment, firstFrame + offset, configurationRevision, timingRevision, segmentStart && offset == 0, faultOnly, subset);
        }
    }
    public static void validateInfo(Descriptor d, Info info, long bytes) {
        require(nonzero(info.segment) && info.configurationRevision == d.configurationRevision && info.timingRevision == d.timingRevision, "segment/revision mismatch");
        require(info.frames >= 0 && info.frames <= d.maxFramesPerPacket && Long.compareUnsigned(info.firstFrame, -1L - info.frames) <= 0, "frame count/index overflow");
        require(!info.segmentStart || info.firstFrame == 0, "segment does not start at zero");
        require(bytes == (long) info.frames * d.frameBytes() && info.events.size() <= MAX_EVENTS, "frame payload/event count mismatch");
        require(info.faultOnly ? info.frames == 0 && info.events.size() == 1 : info.frames > 0, "invalid terminal/data frame");
        Event previous = null;
        for (Event e : info.events) {
            require(e.offset >= 0 && (e.offset < info.frames || info.faultOnly && e.offset == 0) && e.value.length == 16, "invalid event position/size");
            require(previous == null || e.offset > previous.offset || e.offset == previous.offset && e.type > previous.type, "duplicate/unsorted events");
            if (info.faultOnly) { require(e.type == 0x8001, "missing terminal fault"); faultDetails(e); }
            else require(e.type == 0x8002, "unsupported data event");
            previous = e;
        }
    }
    public static record Packet(int alias, long sequence, Info info, Lease payload) implements AutoCloseable {
        @Override public void close() { if (payload != null) payload.close(); }
    }
    public static record Result(int frames, Status status, Info info, Long lostFrames, byte[] payload) {
        public Result { payload = payload.clone(); }
        @Override public byte[] payload() { return payload.clone(); }
        public static Result status(Status status) { return new Result(0, status, null, null, new byte[0]); }
    }
    public static record FaultRecord(Status status, long sequence, long firstFrame, UUID segment, Long lostFrames) {}

    // Optional providers are loaded reflectively, keeping Java 21 compilation
    // and legacy nodes standard-library only. Use the public base classes for
    // reflection: implementations may be package-private.
    private static final class Compression {
        final Object encoder, decoder;
        final Method compress, decompress, contentSize, dictionary, frameSize;
        Compression(Encoding encoding) throws ReflectiveOperationException {
            if (encoding == Encoding.LZ4) {
                Class<?> factory = Class.forName("net.jpountz.lz4.LZ4Factory");
                Object f = factory.getMethod("fastestJavaInstance").invoke(null);
                encoder = factory.getMethod("fastCompressor").invoke(f); decoder = factory.getMethod("safeDecompressor").invoke(f);
                compress = Class.forName("net.jpountz.lz4.LZ4Compressor").getMethod("compress", byte[].class);
                decompress = Class.forName("net.jpountz.lz4.LZ4SafeDecompressor").getMethod("decompress", byte[].class, int.class);
                contentSize = dictionary = frameSize = null;
            } else {
                Class<?> zstd = Class.forName("com.github.luben.zstd.Zstd"); encoder = decoder = null;
                compress = zstd.getMethod("compress", byte[].class, int.class); decompress = zstd.getMethod("decompress", byte[].class, int.class);
                contentSize = zstd.getMethod("getFrameContentSize", byte[].class); dictionary = zstd.getMethod("getDictIdFromFrame", byte[].class);
                frameSize = zstd.getMethod("findFrameCompressedSize", byte[].class);
            }
        }
        byte[] compress(byte[] bytes) {
            try { return (byte[]) (encoder == null ? compress.invoke(null, bytes, 1) : compress.invoke(encoder, (Object) bytes)); }
            catch (ReflectiveOperationException ex) { throw new WireFormat.FormatError("frame compression failed: " + ex.getCause()); }
        }
        byte[] decompress(byte[] bytes, int expected) {
            try {
                if (contentSize != null) require((long) contentSize.invoke(null, (Object) bytes) == expected
                        && (long) dictionary.invoke(null, (Object) bytes) == 0 && (long) frameSize.invoke(null, (Object) bytes) == bytes.length,
                        "invalid Zstandard content/dictionary/trailer");
                return (byte[]) decompress.invoke(decoder, bytes, expected);
            } catch (ReflectiveOperationException ex) { throw new WireFormat.FormatError("invalid compressed frame: " + ex.getCause()); }
        }
    }
    public static boolean encodingAvailable(Encoding encoding) {
        if (encoding == Encoding.RAW) return true;
        if (encoding == null) return false;
        try { new Compression(encoding); return true; } catch (ReflectiveOperationException | LinkageError ex) { return false; }
    }
    private static long crc(byte[] bytes) { CRC32 crc = new CRC32(); crc.update(bytes); return crc.getValue(); }
    private static void put(ByteArrayOutputStream out, long value, int size) { for (int i = 0; i < size; i++) out.write((int) (value >>> (i * 8)) & 255); }
    private static long get(byte[] bytes, int offset, int size) {
        require(offset >= 0 && offset <= bytes.length - size, "truncated frame field");
        long value = 0; for (int i = 0; i < size; i++) value |= (bytes[offset + i] & 255L) << (i * 8); return value;
    }
    public static final class Codec {
        private final Descriptor d;
        private final Compression compression;
        public Codec(Descriptor d) {
            d.validate(); this.d = d;
            try { compression = d.encoding == Encoding.RAW ? null : new Compression(d.encoding); }
            catch (ReflectiveOperationException | LinkageError ex) { throw new IllegalArgumentException("frame encoding unavailable", ex); }
        }
        public synchronized byte[] encode(int alias, long sequence, Info info, byte[] payload) {
            validateInfo(d, info, payload.length); require(alias != 0 && sequence != -1, "invalid alias/sequence");
            int chosen = 0; byte[] body = payload;
            boolean encoded = !info.faultOnly && d.encoding != Encoding.RAW;
            if (encoded) { byte[] candidate = compression.compress(payload); if (candidate.length < payload.length) { body = candidate; chosen = d.encoding.ordinal(); } }
            ByteArrayOutputStream out = new ByteArrayOutputStream(MAX_MESSAGE_BYTES);
            put(out, 1, 1); put(out, 1, 1); put(out, d.type.id, 2); put(out, alias, 4); put(out, sequence, 8);
            put(out, 60 + 24 * info.events.size() + (encoded ? 16 : 0), 2);
            put(out, FRAME_EXTENSION, 2); put(out, 56 + 24 * info.events.size(), 2); put(out, 1, 2);
            put(out, (info.segmentStart ? 1 : 0) | (info.faultOnly ? 2 : 0), 2); put(out, info.frames, 4);
            out.writeBytes(WireFormat.parseStreamId(info.segment.toString())); put(out, info.firstFrame, 8);
            put(out, info.configurationRevision, 8); put(out, info.timingRevision, 8); put(out, info.events.size(), 4); put(out, 24 * info.events.size(), 4);
            for (Event e : info.events) { put(out, e.offset, 4); put(out, e.type, 2); put(out, 16, 2); out.writeBytes(e.value); }
            if (encoded) { put(out, ENCODING_EXTENSION, 2); put(out, 12, 2); put(out, 1, 2); put(out, chosen, 2); put(out, payload.length, 4); put(out, crc(payload), 4); }
            while ((out.size() - 16) % 8 != 0) out.write(0);
            out.writeBytes(body); return out.toByteArray();
        }
        public Packet decode(byte[] message, Pool pool) { return decode(message, pool, Long.MAX_VALUE); }
        public synchronized Packet decode(byte[] message, Pool pool, long availableFrames) {
            require(message.length >= 18 && message.length <= MAX_MESSAGE_BYTES, "invalid frame message length");
            int alias = (int) get(message, 4, 4); long sequence = get(message, 8, 8);
            require(message[0] == 1 && message[1] == 1 && get(message, 2, 2) == d.type.id && alias != 0 && sequence != -1, "frame header mismatch");
            int tlv = (int) get(message, 16, 2), end = 18 + tlv, payloadStart = 16 + (2 + tlv + 7) / 8 * 8;
            require(tlv <= 56 + 2048 + 64 && payloadStart <= message.length, "extension area exceeds bound");
            for (int i = end; i < payloadStart; i++) require(message[i] == 0, "nonzero extension padding");
            Info info = null; int encoding = -1, decoded = 0, previous = 0; long checksum = 0;
            for (int offset = 18; offset < end;) {
                require(end - offset >= 4, "truncated extension");
                int type = (int) get(message, offset, 2), length = (int) get(message, offset + 2, 2); offset += 4;
                require(type > previous && length <= end - offset, "duplicate/unsorted/truncated extension"); previous = type;
                if (type == FRAME_EXTENSION) {
                    require(length >= 56 && get(message, offset, 2) == 1, "invalid frame extension");
                    int flags = (int) get(message, offset + 2, 2); long frames = get(message, offset + 4, 4);
                    long count = get(message, offset + 48, 4);
                    require(flags <= 3 && frames <= d.maxFramesPerPacket && count <= MAX_EVENTS && get(message, offset + 52, 4) == count * 24
                            && length == 56 + count * 24, "invalid frame/event dimensions");
                    UUID segment = UUID.fromString(WireFormat.streamIdText(Arrays.copyOfRange(message, offset + 8, offset + 24)));
                    List<Event> events = new ArrayList<>();
                    for (int i = 0; i < count; i++) {
                        int base = offset + 56 + 24 * i;
                        require(get(message, base + 6, 2) == 16, "invalid event size");
                        events.add(new Event((int) get(message, base, 4), (int) get(message, base + 4, 2), Arrays.copyOfRange(message, base + 8, base + 24)));
                    }
                    info = new Info((int) frames, segment, get(message, offset + 24, 8), get(message, offset + 32, 8), get(message, offset + 40, 8),
                            (flags & 1) != 0, (flags & 2) != 0, events);
                    decoded = Math.toIntExact(frames * d.frameBytes());
                } else if (type == ENCODING_EXTENSION) {
                    require(length == 12 && get(message, offset, 2) == 1 && info != null && get(message, offset + 4, 4) == decoded, "invalid encoding extension");
                    encoding = (int) get(message, offset + 2, 2); checksum = get(message, offset + 8, 4);
                } else require((type & 0x8000) == 0, "unsupported critical extension");
                offset += length;
            }
            require(info != null && decoded <= WireFormat.MAX_PAYLOAD_BYTES, "missing/oversized frames");
            validateInfo(d, info, decoded); require(info.frames <= availableFrames, "receive frame window exceeded");
            require((encoding >= 0) == (!info.faultOnly && d.encoding != Encoding.RAW), "encoding profile mismatch");
            int chosen = encoding < 0 ? 0 : encoding; require(chosen == 0 || chosen == d.encoding.ordinal(), "unnegotiated codec");
            byte[] body = Arrays.copyOfRange(message, payloadStart, message.length); require(body.length <= WireFormat.MAX_PAYLOAD_BYTES, "encoded payload exceeds bound");
            byte[] raw = body;
            if (chosen != 0) { require(body.length > 0 && body.length < decoded, "noncanonical compressed block"); raw = compression.decompress(body, decoded); }
            require(raw.length == decoded && (encoding < 0 || crc(raw) == checksum), "payload/checksum mismatch");
            Lease lease = info.faultOnly ? null : pool.copy(raw);
            if (!info.faultOnly && lease == null) throw new IllegalStateException("receive pool exhausted");
            return new Packet(alias, sequence, info, lease);
        }
    }
}

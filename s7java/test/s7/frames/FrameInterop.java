// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package s7.frames;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import s7.frames.FrameFormat.*;
import s7.json.Json;
import s7.wire.WireFormat;
import s7.wire.WireType;

/** JSON-line test bridge, not an authenticated network service. */
public final class FrameInterop {
    private static final HexFormat HEX = HexFormat.of();
    private FrameInterop() {}
    static long unsigned(Map<?, ?> c, String name) { return Long.parseUnsignedLong((String) c.get(name)); }
    static int integer(Map<?, ?> c, String name) { return Math.toIntExact((Long) c.get(name)); }
    static Descriptor descriptor(Map<?, ?> c) {
        List<String> channels = new ArrayList<>(); for (Object item : (List<?>) c.get("channels")) channels.add((String) item);
        return new Descriptor(UUID.fromString("11111111-2222-4333-8444-555555555555"), WireType.parse((String) c.get("type")), channels,
                48000, 1, "synthetic", "model run origin; nanoseconds", WireFormat.MODEL_TIME, "synthetic", "software",
                integer(c, "max_frames"), integer(c, "window_packets"), (Long) c.get("window_frames"), unsigned(c, "configuration_revision"),
                unsigned(c, "timing_revision"), Encoding.values()[integer(c, "encoding")]);
    }
    static Info info(Map<?, ?> c) {
        Map<?, ?> i = (Map<?, ?>) c.get("info"); List<Event> events = new ArrayList<>();
        for (Object item : (List<?>) i.get("events")) {
            Map<?, ?> e = (Map<?, ?>) item;
            events.add(new Event(integer(e, "offset"), integer(e, "type"), HEX.parseHex((String) e.get("value_hex"))));
        }
        return new Info(integer(i, "frames"), UUID.fromString((String) i.get("segment")), unsigned(i, "first_frame"),
                unsigned(c, "configuration_revision"), unsigned(c, "timing_revision"), (Boolean) i.get("segment_start"), (Boolean) i.get("fault_only"), events);
    }
    static Map<String, Object> run(Map<?, ?> request) {
        Map<?, ?> c = (Map<?, ?>) request.get("case"); Descriptor d = descriptor(c);
        if (!FrameFormat.encodingAvailable(d.encoding())) return Map.of("status", "unavailable");
        Codec codec = new Codec(d);
        if (request.get("op").equals("encode")) return Map.of("status", "ok", "message_hex", HEX.formatHex(codec.encode((int) (long) (Long) c.get("alias"),
                unsigned(c, "sequence"), info(c), HEX.parseHex((String) c.get("payload_hex")))));
        try (FrameBuffers.Pool pool = new FrameBuffers.Pool(1, WireFormat.MAX_PAYLOAD_BYTES);
                Packet p = codec.decode(HEX.parseHex((String) c.get("message_hex")), pool)) {
            String payload = p.payload() == null ? "" : HEX.formatHex(p.payload().bytes());
            FrameFormat.require(Integer.toUnsignedLong(p.alias()) == (Long) c.get("alias") && p.sequence() == unsigned(c, "sequence")
                    && p.info().equals(info(c)) && payload.equals(c.get("payload_hex")), "fixture fields differ");
            return Map.of("status", "ok");
        }
    }
    public static void main(String[] args) throws Exception {
        BufferedReader input = new BufferedReader(new InputStreamReader(System.in, java.nio.charset.StandardCharsets.UTF_8));
        for (String line; (line = input.readLine()) != null;) {
            Map<String, Object> result;
            try { result = run((Map<?, ?>) Json.parse(line)); }
            catch (WireFormat.FormatError | IllegalArgumentException | IllegalStateException ex) { result = Map.of("status", "rejected", "error", ex.toString()); }
            System.out.println(Json.write(result));
        }
    }
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package s7.net;

import java.math.BigInteger;
import java.util.List;

import s7.json.Json;
import s7.stream.SequenceStore;
import s7.stream.StreamConfig;
import s7.stream.StreamProducer;
import s7.wire.WireFormat;
import s7.wire.WireType;

/**
 * The stream table's notes and clock rule, and the JSON reader and writer that load diagrams and
 * write status. Usage: java -ea -cp build s7.net.NetCheck
 */
public final class NetCheck {
    private static int checks;

    static void expect(boolean condition, String what) {
        checks++;
        if (!condition) {
            throw new AssertionError("Check failed: " + what);
        }
    }

    static StreamConfig config(String id) {
        StreamConfig c = new StreamConfig();
        c.streamId = WireFormat.parseStreamId(id);
        c.type = WireType.F64;
        c.maxElementsPerPacket = 1;
        c.sendLogMaxPackets = 16;
        c.maxBatchLatencyMs = 10;
        return c;
    }

    static void notes() {
        StreamTable table = new StreamTable(new SequenceStore.Memory());
        long handle = table.open(config("0335c8e1-a3ab-4e8c-8b17-dc8170f5e370"), StreamTable.CONSUMER);
        StreamTable.Entry entry = table.entries.get(handle);
        entry.noteLate();
        expect(entry.recentFaults.size() == 1 && "LATE".equals(entry.recentFaults.peekLast().get("code")), "the first late packet is noted");
        entry.noteLate();
        expect(entry.recentFaults.size() == 1, "a second one within a second is not");
    }

    static void clock() {
        StreamTable table = new StreamTable(new SequenceStore.Memory());
        StreamProducer unknown = table.entries.get(table.open(config("1335c8e1-a3ab-4e8c-8b17-dc8170f5e370"), StreamTable.PRODUCER)).producer;
        expect(!unknown.stampSendTime, "without a synchronized clock, no SEND_TIME");
        table.setClockUsable(true);
        StreamProducer stamped = table.entries.get(table.open(config("2335c8e1-a3ab-4e8c-8b17-dc8170f5e370"), StreamTable.PRODUCER)).producer;
        expect(stamped.stampSendTime, "with one, SEND_TIME");
    }

    static void json() {
        for (String text : new String[]{"٣", "３", "1.٣", "1e٣", "-٣"}) {
            boolean refused = false;
            try {
                Json.parse(text);
            } catch (IllegalArgumentException ex) {
                refused = true;
            }
            expect(refused, "non-ASCII digits are not a JSON number: " + text);
        }
        expect(Json.parse("123").equals(123L) && Json.parse("-0.5").equals(-0.5), "ASCII numbers still parse");
        double negativeZero = (Double) Json.parse(Json.write(-0.0));
        expect(Json.write(-0.0).equals("-0.0") && Double.doubleToRawLongBits(negativeZero) == Double.doubleToRawLongBits(-0.0),
                "-0.0 keeps its sign through write and parse");
        expect(Json.write(0.0).equals("0.0") && Json.write(3.0).equals("3.0"), "integral reals stay reals");
        expect(Json.parse("18446744073709551615").equals(new BigInteger("18446744073709551615")), "large integers are exact");
        expect(Json.write(List.of(1L, 2.5)).equals("[1,2.5]"), "integers and reals stay apart");
    }

    public static void main(String[] args) {
        notes();
        clock();
        json();
        System.out.println("net: " + checks + " checks pass");
    }
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package s7.node;

import java.util.List;
import java.util.Map;

import s7.json.Json;
import s7.net.StreamTable;
import s7.net.Tls;
import s7.stream.SequenceStore;

/**
 * The node's part of a deployed diagram and the peer identity rules: what s7java accepts and
 * refuses before any connection is made. Usage: java -ea -cp build s7.node.NodeCheck
 */
public final class NodeCheck {
    private static final String P = "11111111-2222-3333-4444-555555555555";
    private static final String Q = "33333333-4444-5555-6666-777777777777";
    private static int checks;

    static void expect(boolean condition, String what) {
        checks++;
        if (!condition) {
            throw new AssertionError("Check failed: " + what);
        }
    }

    /** Runs {@code body} and expects an IllegalArgumentException whose message contains {@code part}. */
    static void refused(Runnable body, String part, String what) {
        try {
            body.run();
        } catch (IllegalArgumentException ex) {
            expect(ex.getMessage().contains(part), what + ": message '" + ex.getMessage() + "' contains '" + part + "'");
            return;
        }
        expect(false, what + ": refused");
    }

    /** A stream from count on P to out on Q; {@code producerNode} is P's runtime_nodes entry. */
    @SuppressWarnings("unchecked")
    static Map<String, Object> diagram(String producerNode) {
        return (Map<String, Object>) Json.parse("""
                {"document_kind": "s7.diagram",
                 "runtime_nodes": [%s, {"node_uuid": "%s", "endpoint": "wss://127.0.0.1:2"}],
                 "schema": {"name": "d", "elements": [
                   {"id": "count", "type": "Constant", "params": {"value": 1}, "node": "%s"},
                   {"id": "out", "type": "Gain", "params": {"factor": 1}, "node": "%s"}],
                  "connections": [{"from_element": "count", "from_port": 0, "to_element": "out", "to_port": 0}]},
                 "dt": 0.001,
                 "streams": [{"stream_id": "4fa82c76-77bf-4cc6-8aa7-b2cdd52cfa9f",
                   "producer": {"node_uuid": "%s", "element": "count", "port": 0},
                   "consumers": [{"node_uuid": "%s", "element": "out", "port": 0}],
                   "data_type": "F64", "max_elements_per_packet": 1024, "max_batch_latency_ms": 10,
                   "send_log": {"max_packets": 4096, "max_block_ms": 2000}, "max_wait_ms": 2000,
                   "sample_rate": {"numerator": 1000, "denominator": 1},
                   "timestamps": [{"role": "MODEL_TIME", "clock_domain": "model"}]}]}
                """.formatted(producerNode, Q, P, Q, P, Q));
    }

    static String node(String fields) {
        return "{\"node_uuid\": \"" + P + "\", " + fields + "}";
    }

    static void dataPeers() {
        NodePart mtls = new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\", "
                + "\"data_endpoint\": \"wss://127.0.0.1:3/data\", \"data_security\": \"mtls\"")), Q);
        expect(mtls.dataPeers.size() == 1 && mtls.dataPeers.get(0).url().equals("wss://127.0.0.1:3/data"),
                "mtls data_endpoint is the producer's data peer");
        NodePart shared = new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\"")), Q);
        expect(shared.dataPeers.get(0).url().equals("wss://127.0.0.1:1"), "without data_endpoint, the endpoint");

        refused(() -> new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\", \"data_endpoint\": \"ws://127.0.0.1:3/data\", "
                + "\"data_security\": \"mtls\"")), Q), "conflicts", "ws:// data_endpoint with mtls");
        refused(() -> new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\", \"data_endpoint\": \"ws://127.0.0.1:3/data\", "
                + "\"data_security\": \"trusted_plaintext\"")), Q), "not supported", "trusted_plaintext");
        refused(() -> new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\", \"data_endpoint\": \"wss://127.0.0.1:3/data\", "
                + "\"data_security\": \"none\"")), Q), "must be", "unknown data_security");
        refused(() -> new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\", \"data_endpoint\": \"wss://127.0.0.1:3/data\"")), Q),
                "both", "data_endpoint without data_security");
        refused(() -> new NodePart(diagram(node("\"endpoint\": \"ws://127.0.0.1:1\"")), Q), "not wss://", "plain ws:// endpoint");
        // The producer's own part has no data peer, so its runtime_nodes entry is not used.
        expect(new NodePart(diagram(node("\"endpoint\": \"ws://127.0.0.1:1\"")), P).dataPeers.isEmpty(), "producer part: no peers");
    }

    static List<?> uri(String value) {
        return List.of(6, value);
    }

    static void identities() {
        String uuid = "11111111-2222-3333-4444-555555555555";
        String prefix = "spiffe://s7/node/";
        expect(Tls.nodeUuidOf(List.of(uri(prefix + uuid))).equals(uuid), "one node URI");
        expect(Tls.nodeUuidOf(List.of(List.of(2, "node.example"), uri("spiffe://s7/studio/x"), uri(prefix + uuid))).equals(uuid),
                "other names are ignored");
        expect(Tls.nodeUuidOf(List.of(uri(prefix + uuid), uri(prefix + uuid))).isEmpty(), "the same node URI twice: none");
        expect(Tls.nodeUuidOf(List.of(uri(prefix + uuid), uri(prefix + "not-a-uuid"))).isEmpty(), "a second, malformed node URI: none");
        expect(Tls.nodeUuidOf(List.of(uri(prefix + uuid.toUpperCase()))).equals(uuid.toUpperCase()), "kept as written, compared exactly");
        expect(Tls.nodeUuidOf(List.of(List.of(2, "x"))).isEmpty(), "no node URI: none");
        expect(Tls.nodeUuidOf(null).isEmpty(), "no names: none");
    }

    @SuppressWarnings("unchecked")
    static Graph graph(String elements, String connections) {
        Map<String, Object> schema = (Map<String, Object>) Json.parse("{\"elements\": [" + elements + "], \"connections\": [" + connections + "]}");
        return new Graph(schema, new StreamTable(new SequenceStore.Memory()));
    }

    static final String C = "{\"id\": \"c\", \"type\": \"Constant\", \"params\": {\"value\": 2}}";
    static final String G = "{\"id\": \"g\", \"type\": \"Gain\", \"params\": {\"factor\": 3}}";

    static String wire(String from, String fromPort, String to, String toPort) {
        return "{\"from_element\": \"" + from + "\"" + (fromPort != null ? ", \"from_port\": " + fromPort : "")
                + ", \"to_element\": \"" + to + "\"" + (toPort != null ? ", \"to_port\": " + toPort : "") + "}";
    }

    static void graphs() throws InterruptedException {
        Graph ok = graph(C + ", " + G, wire("c", "0", "g", null));
        ok.step();
        expect(ok.sample("g") == 6.0, "omitted to_port is port 0");

        refused(() -> graph(C + ", " + C.replace("2}", "5}") + ", " + G, wire("c", "0", "g", "0")), "duplicate element id 'c'",
                "two elements with one id");
        refused(() -> graph(C + ", " + G, wire("c", "7", "g", "0")), "output port 7", "from_port beyond the outputs");
        refused(() -> graph(C + ", " + G, wire("c", "0", "g", "4294967296")), "input port 4294967296", "to_port beyond int");
        refused(() -> graph(C + ", " + G, wire("c", "-1", "g", "0")), "non-negative integer", "negative from_port");
        refused(() -> graph(C + ", " + G, wire("c", "0", "g", "0.5")), "non-negative integer", "fractional to_port");
        refused(() -> graph(C + ", " + G.replace("\"factor\": 3", "\"factor\": 3, \"type\": \"F32\""), wire("c", "0", "g", "0")),
                "F64 only", "an element assigned F32");
        refused(() -> graph("{\"id\": \"a\", \"type\": \"Accumulator\", \"params\": {\"accumulator_type\": \"I32\"}}, " + C,
                wire("c", "0", "a", "0")), "F64 only", "an integer accumulator");
        refused(() -> graph(C + ", {\"id\": \"k\", \"type\": \"Comparator\", \"params\": {\"operation\": \"lte\"}}",
                wire("c", "0", "k", "0") + ", " + wire("c", "0", "k", "1")), "Comparator operation", "unknown Comparator operation, when built");

        for (String dt : new String[]{"0", "-0.001", "1e-10", "\"0.001\"", "null"}) {
            Map<String, Object> d = new java.util.HashMap<>(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\"")));
            d.put("dt", Json.parse(dt));
            refused(() -> new NodePart(d, P), "dt", "dt " + dt);
        }
        expect(new NodePart(diagram(node("\"endpoint\": \"wss://127.0.0.1:1\"")), P).stepNs == 1_000_000L, "dt 0.001 is 1 ms");
    }

    public static void main(String[] args) throws InterruptedException {
        dataPeers();
        identities();
        graphs();
        System.out.println("node: " + checks + " checks pass");
    }
}

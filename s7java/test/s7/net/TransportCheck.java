// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package s7.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigInteger;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BooleanSupplier;

import javax.net.ssl.SSLContext;

import s7.stream.SequenceStore;
import s7.stream.StreamConfig;
import s7.wire.WireFormat;
import s7.wire.WireType;

/**
 * DataEndpoints over mTLS on loopback, with the repository's test certificates: delivery through
 * dropped connections, who may consume, certificate identity, waiting and backpressure (the cases
 * of s7py/tests/test_transport.py). Usage: java -ea -cp build s7.net.TransportCheck CERT_DIR
 */
public final class TransportCheck {
    static final String P = "11111111-2222-3333-4444-555555555555"; // s7_runtime_node
    static final String Q = "33333333-4444-5555-6666-777777777777"; // s7_runtime_node_b
    private static int checks;
    private static Path certs;

    static void expect(boolean condition, String what) {
        checks++;
        if (!condition) {
            throw new AssertionError("Check failed: " + what);
        }
    }

    static boolean await(BooleanSupplier condition, long ms) throws InterruptedException {
        long deadline = System.nanoTime() + ms * 1_000_000;
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return true;
            }
            Thread.sleep(10);
        }
        return condition.getAsBoolean();
    }

    static SSLContext tls(String name) throws Exception {
        return Tls.context(certs.resolve(name + ".cert.pem"), certs.resolve(name + ".key.pem"), certs.resolve("s7_root_ca.cert.pem"));
    }

    static StreamConfig config(String id, long perPacket, long logPackets) {
        StreamConfig c = new StreamConfig();
        c.streamId = WireFormat.parseStreamId(id);
        c.type = WireType.F64;
        c.maxElementsPerPacket = perPacket;
        c.sendLogMaxPackets = logPackets;
        c.maxBatchLatencyMs = 2;
        c.producerNode = P;
        c.consumerNodes = new ArrayList<>(List.of(Q));
        c.maxWaitMs = 5000; // longer than the 1 s reconnect delay
        return c;
    }

    static List<BigInteger[]> one(double value) {
        List<BigInteger[]> items = new ArrayList<>();
        items.add(new BigInteger[]{new BigInteger(Long.toUnsignedString(Double.doubleToRawLongBits(value)))});
        return items;
    }

    static double value(BigInteger[] element) {
        return Double.longBitsToDouble(element[0].longValue());
    }

    static boolean logged(DataEndpoint endpoint, String part) {
        synchronized (endpoint) {
            return endpoint.errors.stream().anyMatch(e -> e.contains(part));
        }
    }

    /** A node: its table and /data endpoint on a free loopback port. */
    record Node(StreamTable table, DataEndpoint endpoint, int port) implements AutoCloseable {
        static Node start(String cert, String uuid) throws Exception {
            StreamTable table = new StreamTable(new SequenceStore.Memory());
            DataEndpoint endpoint = new DataEndpoint(table, uuid, tls(cert), message -> { });
            return new Node(table, endpoint, endpoint.listen("127.0.0.1", 0));
        }

        String url() {
            return "wss://127.0.0.1:" + port + "/data";
        }

        @Override
        public void close() {
            endpoint.close();
        }
    }

    /** Forwards TCP to a port and resets (RST) every connection on drop(), as chaos_proxy.py. */
    static final class Proxy implements AutoCloseable {
        final ServerSocket server;
        final Set<Socket> open = ConcurrentHashMap.newKeySet();

        Proxy(int target) throws IOException {
            server = new ServerSocket(0, 50, InetAddress.getLoopbackAddress());
            Thread accept = new Thread(() -> {
                while (!server.isClosed()) {
                    try {
                        Socket client = server.accept();
                        Socket upstream = new Socket(InetAddress.getLoopbackAddress(), target);
                        open.add(client);
                        open.add(upstream);
                        pipe(client, upstream);
                        pipe(upstream, client);
                    } catch (IOException ex) {
                        return;
                    }
                }
            });
            accept.setDaemon(true);
            accept.start();
        }

        void pipe(Socket from, Socket to) {
            Thread t = new Thread(() -> {
                byte[] buffer = new byte[65536];
                try (InputStream in = from.getInputStream(); OutputStream out = to.getOutputStream()) {
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                } catch (IOException ignored) {
                    // dropped
                }
            });
            t.setDaemon(true);
            t.start();
        }

        int port() {
            return server.getLocalPort();
        }

        void drop() throws IOException {
            for (Socket s : List.copyOf(open)) {
                s.setSoLinger(true, 0);
                s.close();
                open.remove(s);
            }
        }

        @Override
        public void close() throws IOException {
            server.close();
            drop();
        }
    }

    static void deliveryThroughDrops() throws Exception {
        String id = "4fa82c76-77bf-4cc6-8aa7-b2cdd52cfa9f";
        try (Node producer = Node.start("s7_runtime_node", P); Node consumer = Node.start("s7_runtime_node_b", Q);
             Proxy proxy = new Proxy(producer.port())) {
            long out = producer.table.open(config(id, 4, 4096), StreamTable.PRODUCER);
            long in = consumer.table.open(config(id, 4, 4096), StreamTable.CONSUMER);
            consumer.endpoint.setDataPeers(List.of(new DataEndpoint.DataPeer("wss://127.0.0.1:" + proxy.port() + "/data", P)));
            int total = 3000;
            Thread publisher = new Thread(() -> {
                try {
                    for (int v = 1; v <= total; v++) {
                        producer.table.publish(out, one(v), List.of());
                        if (v % 10 == 0) {
                            Thread.sleep(1);
                        }
                    }
                } catch (InterruptedException ignored) {
                    // stopped
                }
            });
            publisher.start();
            List<Double> received = new ArrayList<>();
            int drops = 0;
            String fault = null;
            while (received.size() < total && fault == null) {
                StreamTable.Received r = consumer.table.receive(in, 1);
                fault = r.fault();
                r.elements().forEach(e -> received.add(value(e)));
                if (received.size() % 700 == 0 && drops < 3) {
                    proxy.drop();
                    drops++;
                }
            }
            publisher.join();
            expect(fault == null, "no fault while delivering: " + fault);
            boolean inOrder = received.size() == total;
            for (int i = 0; inOrder && i < total; i++) {
                inOrder = received.get(i) == i + 1;
            }
            expect(drops == 3 && inOrder, "3000 values once and in order through 3 dropped connections");
        }
    }

    static void onlyConfiguredConsumers() throws Exception {
        String id = "5fa82c76-77bf-4cc6-8aa7-b2cdd52cfa9f";
        // The Studio certificate has no node identity: it is not one of the stream's consumer nodes.
        try (Node producer = Node.start("s7_runtime_node", P); Node stranger = Node.start("s7_studio", "")) {
            long out = producer.table.open(config(id, 1, 64), StreamTable.PRODUCER);
            stranger.table.open(config(id, 1, 64), StreamTable.CONSUMER);
            stranger.endpoint.setDataPeers(List.of(new DataEndpoint.DataPeer(producer.url(), P)));
            expect(await(() -> logged(producer.endpoint, "not a configured consumer"), 5000), "the producer refuses an unconfigured consumer");
            expect(producer.table.entries.get(out).unauthorized > 0, "and counts it as unauthorized");
        }
    }

    static void identityMismatch() throws Exception {
        try (Node producer = Node.start("s7_runtime_node", P); Node consumer = Node.start("s7_runtime_node_b", Q)) {
            // The diagram says the node at this URL is Q, but its certificate says P.
            consumer.endpoint.setDataPeers(List.of(new DataEndpoint.DataPeer(producer.url(), Q)));
            expect(await(() -> logged(consumer.endpoint, "certificate identity mismatch"), 5000), "a peer whose certificate is another node is refused");
        }
    }

    static void waitsThenUnderruns() throws Exception {
        int closedPort;
        try (ServerSocket nobody = new ServerSocket(0)) {
            closedPort = nobody.getLocalPort(); // free again once closed: nobody listens there
        }
        try (Node consumer = Node.start("s7_runtime_node_b", Q)) {
            StreamConfig c = config("6fa82c76-77bf-4cc6-8aa7-b2cdd52cfa9f", 1, 64);
            c.maxWaitMs = 200;
            long in = consumer.table.open(c, StreamTable.CONSUMER);
            consumer.endpoint.setDataPeers(List.of(new DataEndpoint.DataPeer("wss://127.0.0.1:" + closedPort + "/data", P)));
            long start = System.nanoTime();
            StreamTable.Received r = consumer.table.receive(in, 1);
            long waited = (System.nanoTime() - start) / 1_000_000;
            expect("UNDERRUN".equals(r.fault()) && r.elements().isEmpty() && waited >= 190, "no producer: waits max_wait_ms, then UNDERRUN (" + waited + " ms)");
        }
    }

    static void backpressureOverflows() throws Exception {
        try (Node producer = Node.start("s7_runtime_node", P)) {
            StreamConfig c = config("7fa82c76-77bf-4cc6-8aa7-b2cdd52cfa9f", 1, 2);
            c.maxBlockMs = 100;
            long out = producer.table.open(c, StreamTable.PRODUCER);
            expect(producer.table.publish(out, one(1), List.of()) == null && producer.table.publish(out, one(2), List.of()) == null,
                    "two packets fit the send log");
            long start = System.nanoTime();
            String fault = producer.table.publish(out, one(3), List.of());
            long blocked = (System.nanoTime() - start) / 1_000_000;
            expect("SEND_LOG_OVERFLOW".equals(fault) && blocked >= 95, "a full log blocks max_block_ms, then SEND_LOG_OVERFLOW (" + blocked + " ms)");
        }
    }

    public static void main(String[] args) throws Exception {
        certs = Path.of(args.length > 0 ? args[0] : "../s7l2/s7runtime/examples/certs");
        deliveryThroughDrops();
        onlyConfiguredConsumers();
        identityMismatch();
        waitsThenUnderruns();
        backpressureOverflows();
        System.out.println("transport: " + checks + " checks pass");
    }
}

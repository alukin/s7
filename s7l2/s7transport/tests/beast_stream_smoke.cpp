// Streams over real WebSocket connections (BeastRuntimeNetEndpoint): both
// directions, lossless resume across dropped TCP connections, two consumers
// with their own mTLS identities, malformed input and the inbound limit.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <s7/transport.hpp>

namespace {

namespace transport = s7::transport;
namespace stream = s7::stream;
namespace wire = s7::wire;
using boost::asio::ip::tcp;
using std::chrono::milliseconds;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

bool wait_for(const std::function<bool()>& condition, const milliseconds timeout = milliseconds(5000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return condition();
}

stream::StreamConfig config(const char* id)
{
    stream::StreamConfig c;
    c.stream_id = wire::parse_stream_id(id);
    c.type = s7::SignalType::F64;
    c.max_elements_per_packet = 64;
    c.max_batch_latency = milliseconds(5);
    c.send_log_max_packets = 256;
    c.max_block = milliseconds(15000);
    c.max_wait = milliseconds(15000);
    return c;
}

// Publishes 0..count-1 in chunks while the consumer reads them back; checks
// every value arrives exactly once and in order.
void transfer(transport::IRuntimeNetEndpoint& producer_endpoint, const transport::StreamHandle producer,
              const std::vector<std::pair<transport::IRuntimeNetEndpoint*, transport::StreamHandle>>& consumers,
              const std::size_t count, const std::function<void(std::size_t)>& on_progress = {})
{
    constexpr std::size_t kChunk = 100;
    std::vector<std::thread> readers;
    std::vector<std::string> failures(consumers.size());
    for (std::size_t c = 0; c < consumers.size(); ++c) {
        readers.emplace_back([&, c] {
            auto [endpoint, handle] = consumers[c];
            std::vector<double> chunk(kChunk);
            for (std::size_t base = 0; base < count; base += kChunk) {
                const auto result = endpoint->receive(handle, chunk.data(), chunk.size());
                if (result.fault != stream::FaultCode::None || result.received != kChunk) {
                    failures[c] = "consumer " + std::to_string(c) + " faulted at " + std::to_string(base) + ": " +
                                  stream::to_string(result.fault);
                    return;
                }
                for (std::size_t i = 0; i < kChunk; ++i) {
                    if (chunk[i] != static_cast<double>(base + i)) {
                        failures[c] = "consumer " + std::to_string(c) + " got " + std::to_string(chunk[i]) + " at " +
                                      std::to_string(base + i);
                        return;
                    }
                }
            }
        });
    }
    std::vector<double> chunk(kChunk);
    std::string publish_failure;
    for (std::size_t base = 0; base < count; base += kChunk) {
        for (std::size_t i = 0; i < kChunk; ++i) {
            chunk[i] = static_cast<double>(base + i);
        }
        const auto result = producer_endpoint.publish(producer, chunk.data(), chunk.size());
        if (result.fault != stream::FaultCode::None) {
            publish_failure = "publish faulted at " + std::to_string(base) + ": " + stream::to_string(result.fault);
            break;
        }
        if (on_progress) {
            on_progress(base + kChunk);
        }
    }
    for (auto& reader : readers) {
        reader.join();
    }
    expect(publish_failure.empty(), publish_failure);
    for (const auto& failure : failures) {
        expect(failure.empty(), failure);
    }
}

// A TCP forwarder whose connections the test can cut.
class SeverableProxy {
public:
    explicit SeverableProxy(const std::uint16_t target_port)
        : acceptor_(io_, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
        , target_(boost::asio::ip::make_address("127.0.0.1"), target_port)
    {
        accept();
        thread_ = std::thread([this] { io_.run(); });
    }
    ~SeverableProxy()
    {
        boost::asio::post(io_, [this] {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
            close_all();
        });
        work_.reset();
        thread_.join();
    }
    // Drops every forwarded connection (and what is in flight on it).
    void sever()
    {
        boost::asio::post(io_, [this] { close_all(); });
        ++severed_;
    }
    [[nodiscard]] int severed() const { return severed_; }
    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

private:
    struct Session {
        tcp::socket a;
        tcp::socket b;
        std::array<std::uint8_t, 16384> ab{};
        std::array<std::uint8_t, 16384> ba{};
        explicit Session(boost::asio::io_context& io) : a(io), b(io) {}
    };

    void accept()
    {
        auto session = std::make_shared<Session>(io_);
        acceptor_.async_accept(session->a, [this, session](const boost::system::error_code& ec) {
            if (ec) {
                return;
            }
            session->b.async_connect(target_, [this, session](const boost::system::error_code& connect_ec) {
                if (connect_ec) {
                    return;
                }
                sessions_.push_back(session);
                forward(session, session->a, session->b, session->ab);
                forward(session, session->b, session->a, session->ba);
            });
            accept();
        });
    }

    template <std::size_t N>
    void forward(const std::shared_ptr<Session>& session, tcp::socket& from, tcp::socket& to, std::array<std::uint8_t, N>& buffer)
    {
        from.async_read_some(boost::asio::buffer(buffer), [this, session, &from, &to, &buffer](const boost::system::error_code& ec, std::size_t n) {
            if (ec) {
                close(*session);
                return;
            }
            boost::asio::async_write(to, boost::asio::buffer(buffer, n), [this, session, &from, &to, &buffer](const boost::system::error_code& write_ec, std::size_t) {
                if (write_ec) {
                    close(*session);
                    return;
                }
                forward(session, from, to, buffer);
            });
        });
    }

    static void close(Session& session)
    {
        boost::system::error_code ignored;
        session.a.close(ignored);
        session.b.close(ignored);
    }

    void close_all()
    {
        for (const auto& session : sessions_) {
            close(*session);
        }
        sessions_.clear();
    }

    boost::asio::io_context io_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_{io_.get_executor()};
    tcp::acceptor acceptor_;
    tcp::endpoint target_;
    std::vector<std::shared_ptr<Session>> sessions_;
    std::thread thread_;
    std::atomic<int> severed_{0};
};

void test_both_directions()
{
    transport::BeastRuntimeNetEndpoint server;
    transport::BeastRuntimeNetEndpoint client;
    const auto port = server.configure_server(0);
    client.configure_client("ws://127.0.0.1:" + std::to_string(port));

    // server -> client
    const auto down = config("1b0c4f7e-8d2a-4e6b-9a3c-5f1e2d3c4b5a");
    const auto down_producer = server.open_producer(down);
    const auto down_consumer = client.open_consumer(down);
    transfer(server, down_producer, {{&client, down_consumer}}, 20000);

    // client -> server
    const auto up = config("2c1d5a8f-9e3b-4f7c-8b4d-6a2f3e4d5c6b");
    const auto up_consumer = server.open_consumer(up);
    const auto up_producer = client.open_producer(up);
    transfer(client, up_producer, {{&server, up_consumer}}, 20000);

    const auto stats = client.stats();
    expect(stats.data_connections == 1 && stats.control_connections == 1, "client keeps one data and one control connection");
    for (const auto& status : server.stream_statuses()) {
        expect(status.fault == stream::FaultCode::None, "no stream faults");
    }
}

void test_resume_across_dropped_connections()
{
    transport::BeastRuntimeNetEndpoint server;
    SeverableProxy proxy(server.configure_server(0));
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client("ws://127.0.0.1:" + std::to_string(proxy.port()));

    const auto c = config("3d2e6b9a-0f4c-4a8d-9c5e-7b3a4f5e6d7c");
    const auto producer = server.open_producer(c);
    const auto consumer = client.open_consumer(c);
    // Cut the TCP connections three times while data flows; the client
    // reconnects and RESUMEs, the producer replays from its send log.
    std::size_t next_cut = 15000;
    transfer(server, producer, {{&client, consumer}}, 60000, [&](const std::size_t published) {
        if (published >= next_cut && proxy.severed() < 3) {
            proxy.sever();
            next_cut += 15000;
        }
    });
    expect(proxy.severed() == 3, "connections were cut three times");
    const auto statuses = server.stream_statuses();
    expect(statuses.size() == 1 && statuses.front().replayed > 0, "the producer replayed after reconnects");
    expect(statuses.front().fault == stream::FaultCode::None, "no fault across reconnects");
}

void test_two_mtls_consumers()
{
    const auto certs = std::filesystem::path(S7_TEST_CERT_DIR);
    auto tls = [&](const char* name) {
        transport::TlsConfig t;
        t.enabled = true;
        t.cert_file = (certs / (std::string(name) + ".cert.pem")).string();
        t.key_file = (certs / (std::string(name) + ".key.pem")).string();
        t.ca_file = (certs / "s7_root_ca.cert.pem").string();
        return t;
    };
    transport::BeastRuntimeNetEndpoint server;
    const auto url = "wss://127.0.0.1:" + std::to_string(server.configure_server(0, tls("s7_runtime_node")));
    // Two consumers with distinct certificates: the producer tracks each one.
    transport::BeastRuntimeNetEndpoint first;
    transport::BeastRuntimeNetEndpoint second;
    first.configure_client(url, tls("s7_runtime_node"));
    second.configure_client(url, tls("s7_studio"));

    const auto c = config("4e3f7c0b-1a5d-4b9e-8d6f-8c4b5a6f7e8d");
    const auto producer = server.open_producer(c);
    const auto a = first.open_consumer(c);
    const auto b = second.open_consumer(c);
    transfer(server, producer, {{&first, a}, {&second, b}}, 10000);
    expect(server.stats().data_connections == 2, "two data connections");
}

// A minimal WebSocket client for hostile input.
struct RawClient {
    boost::asio::io_context io;
    boost::beast::websocket::stream<tcp::socket> ws{io};

    explicit RawClient(const std::uint16_t port, const char* path = "/data")
    {
        ws.next_layer().connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));
        ws.handshake("127.0.0.1", path);
        ws.binary(true);
    }
};

void test_malformed_message_closes_connection()
{
    transport::BeastRuntimeNetEndpoint server;
    RawClient raw(server.configure_server(0));
    const std::vector<std::uint8_t> garbage{0x01, 0x00, 0x05};
    raw.ws.write(boost::asio::buffer(garbage));
    boost::beast::flat_buffer buffer;
    boost::system::error_code ec;
    raw.ws.read(buffer, ec);
    expect(static_cast<bool>(ec), "the server closes a connection that sends a malformed message");
    expect(wait_for([&] { return server.stats().malformed_messages == 1; }), "malformed message counted");
    const auto errors = server.take_transport_errors();
    expect(!errors.empty() && errors.front().find("malformed") != std::string::npos, "malformed message reported");
}

void test_inbound_limit()
{
    transport::BeastRuntimeNetEndpoint server(transport::TransportLimits{.max_inbound_connections = 2});
    const auto port = server.configure_server(0);
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client("ws://127.0.0.1:" + std::to_string(port)); // control + data: the limit
    expect(wait_for([&] { return server.stats().data_connections + server.stats().control_connections == 2; }),
           "client connections registered");
    bool rejected = false;
    try {
        RawClient extra(port);
    } catch (const boost::system::system_error&) {
        rejected = true;
    }
    expect(rejected, "a connection beyond the inbound limit is refused");
    expect(server.stats().connections_rejected == 1, "rejection counted");
}

// A redeployed graph closes its stream and the new graph opens it again: the
// stream continues where it was, on both ends.
void test_reopen_continues_stream()
{
    transport::BeastRuntimeNetEndpoint server;
    const auto port = server.configure_server(0);
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client("ws://127.0.0.1:" + std::to_string(port));
    const auto c = config("6a5b9e2d-3c7f-4d1a-8f0b-0e6d7c8b9a0f");

    auto producer = server.open_producer(c);
    auto consumer = client.open_consumer(c);
    std::vector<double> values(200);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>(i);
    }
    expect(server.publish(producer, values.data(), 100).fault == stream::FaultCode::None, "first half published");
    std::vector<double> got(50);
    expect(client.receive(consumer, got.data(), got.size()).received == 50, "first values received");

    // Both graphs are replaced; 50 values are still buffered at the consumer.
    server.close_stream(producer);
    client.close_stream(consumer);
    expect(server.stream_statuses().front().state == "closed", "a closed stream is reported");
    const auto previous = producer;
    producer = server.open_producer(c);
    expect(producer.value == previous.value, "reopening continues the same stream");
    bool opened_twice = false;
    try {
        static_cast<void>(server.open_producer(c));
    } catch (const std::invalid_argument&) {
        opened_twice = true;
    }
    expect(opened_twice, "an open stream cannot be opened twice");
    consumer = client.open_consumer(c);
    expect(server.publish(producer, values.data() + 100, 100).fault == stream::FaultCode::None, "second half published");
    std::vector<double> rest(150);
    expect(client.receive(consumer, rest.data(), rest.size()).received == 150, "the rest received");
    got.insert(got.end(), rest.begin(), rest.end());
    expect(got == values, "values continue across reopening, none lost or repeated");
}

// Two nodes consuming from each other have two connections (each one's
// client connection and the other's data peer connection); every consumer
// still receives over exactly one of them, in order.
void test_mutual_streams_over_two_connections()
{
    transport::BeastRuntimeNetEndpoint a;
    transport::BeastRuntimeNetEndpoint b;
    const auto port_a = a.configure_server(0);
    const auto port_b = b.configure_server(0);
    b.configure_client("ws://127.0.0.1:" + std::to_string(port_a));
    a.set_data_peers({{"ws://127.0.0.1:" + std::to_string(port_b), {}}});
    expect(wait_for([&] { return a.stats().data_connections == 2 && b.stats().data_connections == 2; }),
           "two data connections between the nodes");

    const auto x = config("7b6c0f3e-4d8a-4e2b-9a1c-1f7e8d9c0b1a");
    const auto y = config("8c7d1a4f-5e9b-4f3c-8b2d-2a8f9e0d1c2b");
    const auto x_producer = a.open_producer(x);
    const auto x_consumer = b.open_consumer(x);
    const auto y_producer = b.open_producer(y);
    const auto y_consumer = a.open_consumer(y);
    std::exception_ptr other_failure;
    std::thread other([&] {
        try {
            transfer(b, y_producer, {{&a, y_consumer}}, 20000);
        } catch (...) {
            other_failure = std::current_exception();
        }
    });
    try {
        transfer(a, x_producer, {{&b, x_consumer}}, 20000);
    } catch (...) {
        other.join();
        throw;
    }
    other.join();
    if (other_failure) {
        std::rethrow_exception(other_failure);
    }
    for (const auto* endpoint : {&a, &b}) {
        for (const auto& status : endpoint->stream_statuses()) {
            expect(status.fault == stream::FaultCode::None, "no faults with two connections");
            expect(status.duplicates_discarded < 2000, "the stream is not sent on both connections");
        }
    }
}

// A data peer that is down when configured is connected once it comes up.
void test_data_peer_retried()
{
    std::uint16_t port = 0;
    {
        transport::BeastRuntimeNetEndpoint probe;
        port = probe.configure_server(0); // a free port, released again
    }
    transport::BeastRuntimeNetEndpoint consumer_node;
    consumer_node.set_data_peers({{"ws://127.0.0.1:" + std::to_string(port), {}}});
    const auto c = config("9d8e2b5a-6f0c-4a4d-8c3e-3b9a0f1e2d3c");
    const auto consumer = consumer_node.open_consumer(c);
    std::this_thread::sleep_for(milliseconds(300));
    transport::BeastRuntimeNetEndpoint producer_node;
    producer_node.configure_server(port);
    const auto producer = producer_node.open_producer(c);
    transfer(producer_node, producer, {{&consumer_node, consumer}}, 1000);
    consumer_node.set_data_peers({});
    expect(wait_for([&] { return consumer_node.stats().data_connections == 0; }), "a removed data peer is disconnected");
}

// The address of a non-loopback IPv4 interface of this host, if any.
std::optional<std::string> external_ipv4()
{
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return std::nullopt;
    }
    std::optional<std::string> found;
    for (auto* entry = list; entry && !found; entry = entry->ifa_next) {
        if (entry->ifa_addr && entry->ifa_addr->sa_family == AF_INET && !(entry->ifa_flags & IFF_LOOPBACK) && (entry->ifa_flags & IFF_UP)) {
            char text[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr, text, sizeof(text));
            found = text;
        }
    }
    freeifaddrs(list);
    return found;
}

bool can_connect(const std::string& address, const std::uint16_t port)
{
    boost::asio::io_context io;
    tcp::socket socket(io);
    boost::system::error_code ec;
    socket.connect(tcp::endpoint(boost::asio::ip::make_address(address), port), ec);
    return !ec;
}

transport::TlsConfig tls_for(const char* name)
{
    const auto certs = std::filesystem::path(S7_TEST_CERT_DIR);
    transport::TlsConfig t;
    t.enabled = true;
    t.cert_file = (certs / (std::string(name) + ".cert.pem")).string();
    t.key_file = (certs / (std::string(name) + ".key.pem")).string();
    t.ca_file = (certs / "s7_root_ca.cert.pem").string();
    return t;
}

// Peer identity comes from the verified certificate, not from the URL.
void test_node_identity()
{
    constexpr const char* kNodeA = "11111111-2222-3333-4444-555555555555";
    constexpr const char* kNodeB = "33333333-4444-5555-6666-777777777777";
    transport::BeastRuntimeNetEndpoint node_a;
    const auto port = node_a.configure_server(0, tls_for("s7_runtime_node"));
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client("wss://127.0.0.1:" + std::to_string(port), tls_for("s7_studio"));
    const auto certificate = client.server_certificate();
    expect(certificate && transport::node_uuid_of(*certificate) == std::string(kNodeA), "the server presents node A's identity");
    expect(transport::check_node_identity(certificate, kNodeA) == transport::NodeIdentity::Verified, "claimed A: verified");
    expect(transport::check_node_identity(certificate, kNodeB) == transport::NodeIdentity::Mismatch, "claimed B: mismatch");
    expect(transport::check_node_identity(std::nullopt, kNodeA) == transport::NodeIdentity::Unverified, "no certificate: unverified");

    // A data peer whose certificate is another node is refused.
    transport::BeastRuntimeNetEndpoint consumer;
    consumer.set_data_peers({{"wss://127.0.0.1:" + std::to_string(port), kNodeB}}, tls_for("s7_runtime_node_b"));
    std::vector<std::string> errors;
    expect(wait_for([&] {
               for (auto& error : consumer.take_transport_errors()) {
                   errors.push_back(std::move(error));
               }
               return !errors.empty();
           }),
           "the mismatch is reported");
    expect(errors.front().find("identity mismatch") != std::string::npos, "reported as identity mismatch: " + errors.front());
    expect(consumer.stats().data_connections == 0, "no data connection to the wrong node");
    consumer.set_data_peers({{"wss://127.0.0.1:" + std::to_string(port), kNodeA}}, tls_for("s7_runtime_node_b"));
    expect(wait_for([&] { return consumer.stats().data_connections == 1; }), "the right node is accepted");
}

// A plain WebSocket server listens on loopback unless an address is given.
void test_plain_server_binds_loopback()
{
    transport::BeastRuntimeNetEndpoint plain;
    const auto port = plain.configure_server(0);
    expect(can_connect("127.0.0.1", port), "reachable on loopback");
    const auto external = external_ipv4();
    if (!external) {
        std::cout << "no non-loopback interface: external reachability not checked\n";
        return;
    }
    expect(!can_connect(*external, port), "a default plain server is not reachable on " + *external);
    transport::BeastRuntimeNetEndpoint exposed;
    const auto exposed_port = exposed.configure_server(0, {}, "0.0.0.0");
    expect(can_connect(*external, exposed_port), "an explicit bind address exposes it");
}

} // namespace

int main()
try {
    test_both_directions();
    test_resume_across_dropped_connections();
    test_two_mtls_consumers();
    test_malformed_message_closes_connection();
    test_inbound_limit();
    test_reopen_continues_stream();
    test_mutual_streams_over_two_connections();
    test_data_peer_retried();
    test_node_identity();
    test_plain_server_binds_loopback();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7transport beast stream smoke: " << ex.what() << '\n';
    return 1;
}

// Lossless delivery rules of the stream protocol, with a simulated link.
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <s7/stream_protocol.hpp>

namespace {

namespace stream = s7::stream;
namespace wire = s7::wire;
using s7::SignalType;
using std::chrono::milliseconds;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

stream::StreamConfig config(SignalType type, std::uint32_t per_packet, std::uint32_t log_packets)
{
    stream::StreamConfig c;
    c.stream_id = wire::parse_stream_id("0335c8e1-a3ab-4e8c-8b17-dc8170f5e370");
    c.type = type;
    c.max_elements_per_packet = per_packet;
    c.send_log_max_packets = log_packets;
    c.max_batch_latency = milliseconds(10);
    return c;
}

// Moves messages from producer to consumer; a reset loses what is in flight.
struct Link {
    stream::StreamProducer& producer;
    stream::StreamConsumer& consumer;
    std::string key{"consumer"};
    std::deque<std::vector<std::uint8_t>> in_flight;

    void connect()
    {
        const auto resume = consumer.resume_message();
        producer.resume(key, resume.stream_alias, resume.next_expected);
    }
    void reset()
    {
        in_flight.clear();
        producer.detach(key);
    }
    // Producer sends what it has; up to `deliver` messages arrive.
    void pump(stream::Clock::time_point now, std::size_t deliver = SIZE_MAX)
    {
        while (auto message = producer.next_message(key)) {
            in_flight.push_back(std::move(*message));
        }
        for (std::size_t n = 0; n < deliver && !in_flight.empty(); ++n) {
            consumer.on_packet(wire::decode_data_packet(in_flight.front()), now);
            in_flight.pop_front();
        }
    }
    void acknowledge(stream::Clock::time_point now)
    {
        if (const auto ack = consumer.ack_due(now)) {
            const auto bytes = wire::encode_control_message(*ack);
            const auto decoded = wire::decode_control_message(bytes);
            producer.acknowledge(key, decoded.next_expected);
        }
    }
};

std::vector<double> take_all(stream::StreamConsumer& consumer)
{
    std::vector<double> values(consumer.available());
    expect(consumer.take(values.data(), values.size()) == values.size(), "take everything available");
    return values;
}

void test_batching_and_delivery()
{
    stream::MemorySequenceStore sequences;
    stream::StreamProducer producer(config(SignalType::F64, 4, 16), sequences);
    stream::StreamConsumer consumer(config(SignalType::F64, 4, 16), 7);
    Link link{producer, consumer};
    link.connect();

    const auto t0 = stream::Clock::now();
    std::vector<double> values{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    expect(producer.append(values.data(), values.size(), t0) == 10, "all elements accepted");
    expect(producer.stats().packets == 2, "two full packets sealed");
    expect(!producer.flush(t0 + milliseconds(5)), "partial batch waits for its latency");
    expect(producer.flush(t0 + milliseconds(10)), "partial batch flushed after max_batch_latency");
    link.pump(t0);
    expect(take_all(consumer) == values, "values arrive once and in order");

    // A duplicate (e.g. after replay) is discarded.
    wire::DataPacket again{SignalType::F64, 7, 0, {}, {}};
    const double one = 1;
    wire::encode_elements(SignalType::F64, &one, 1, again.payload);
    consumer.on_packet(again, t0);
    expect(consumer.stats().duplicates_discarded == 1 && consumer.available() == 0, "duplicate discarded");
    expect(consumer.fault_code() == stream::FaultCode::None, "duplicates are not faults");
}

void test_gap_and_type_mismatch()
{
    stream::StreamConsumer consumer(config(SignalType::F64, 4, 16), 1);
    wire::DataPacket packet{SignalType::F64, 1, 1, {}, {}};
    const double value = 1;
    wire::encode_elements(SignalType::F64, &value, 1, packet.payload);
    consumer.on_packet(packet, stream::Clock::now());
    expect(consumer.fault_code() == stream::FaultCode::SequenceGap, "a gap faults the stream");

    stream::StreamConsumer typed(config(SignalType::F64, 4, 16), 1);
    wire::DataPacket other{SignalType::F32, 1, 0, {}, {}};
    const float f = 1;
    wire::encode_elements(SignalType::F32, &f, 1, other.payload);
    typed.on_packet(other, stream::Clock::now());
    expect(typed.fault_code() == stream::FaultCode::TypeMismatch, "a packet of another type faults the stream");
}

void test_reconnect_replays_without_loss()
{
    stream::MemorySequenceStore sequences;
    stream::StreamProducer producer(config(SignalType::I32, 1, 64), sequences);
    stream::StreamConsumer consumer(config(SignalType::I32, 1, 64), 3);
    Link link{producer, consumer};
    link.connect();
    const auto now = stream::Clock::now();

    for (std::int32_t v = 0; v < 6; ++v) {
        producer.append(&v, 1, now);
    }
    link.pump(now, 3); // packets 0..2 arrive, 3..5 are lost with the connection
    link.reset();
    expect(producer.stats().send_log_depth == 6, "nothing acknowledged yet: all packets retained");
    link.connect(); // RESUME from next_expected = 3
    link.pump(now);
    std::vector<std::int32_t> got(consumer.available());
    consumer.take(got.data(), got.size());
    expect(got == std::vector<std::int32_t>{0, 1, 2, 3, 4, 5}, "replay fills the loss exactly");
    expect(consumer.stats().duplicates_discarded == 0, "resume does not resend delivered packets");
}

void test_backpressure_and_acks()
{
    stream::MemorySequenceStore sequences;
    stream::StreamProducer producer(config(SignalType::F64, 1, 3), sequences);
    stream::StreamConsumer consumer(config(SignalType::F64, 1, 3), 1);
    Link link{producer, consumer};
    link.connect();
    const auto now = stream::Clock::now();

    std::vector<double> values{1, 2, 3, 4, 5};
    const auto taken = producer.append(values.data(), values.size(), now);
    expect(taken == 3 && producer.log_full(), "a full send log stops new elements (backpressure)");
    link.pump(now);
    take_all(consumer);
    link.acknowledge(now); // nothing yet: neither 64 packets nor 50 ms
    expect(producer.log_full(), "ACK waits for 64 packets or 50 ms");
    link.acknowledge(now + milliseconds(50));
    expect(producer.stats().send_log_depth == 0, "acknowledged packets leave the log");
    expect(producer.append(values.data() + taken, 2, now) == 2, "room again after the ACK");
}

void test_retention_and_two_consumers()
{
    stream::MemorySequenceStore sequences;
    stream::StreamProducer producer(config(SignalType::F64, 1, 64), sequences);
    stream::StreamConsumer fast(config(SignalType::F64, 1, 64), 1);
    stream::StreamConsumer slow(config(SignalType::F64, 1, 64), 2);
    Link a{producer, fast, "fast"};
    Link b{producer, slow, "slow"};
    a.connect();
    b.connect();
    const auto now = stream::Clock::now();
    for (int i = 0; i < 10; ++i) {
        const double v = i;
        producer.append(&v, 1, now);
    }
    a.pump(now);
    take_all(fast);
    a.acknowledge(now + milliseconds(50));
    expect(producer.stats().send_log_depth == 10, "the log keeps what the slow consumer has not acknowledged");

    b.pump(now, 4);
    b.reset(); // slow consumer disconnects; its packets stay
    b.connect();
    b.pump(now);
    expect(take_all(slow).size() == 10, "slow consumer resumes without loss");
    b.acknowledge(now + milliseconds(50));
    expect(producer.stats().send_log_depth == 0, "log trimmed once both acknowledged");
}

void test_producer_restart_is_a_gap()
{
    const auto dir = std::filesystem::temp_directory_path() / ("s7-seq-test-" + std::to_string(std::random_device{}()));
    stream::FileSequenceStore sequences(dir);
    stream::StreamConsumer consumer(config(SignalType::F64, 1, 64), 1);
    const auto now = stream::Clock::now();
    {
        stream::StreamProducer first(config(SignalType::F64, 1, 64), sequences);
        Link link{first, consumer};
        link.connect();
        const double v = 1;
        first.append(&v, 1, now);
        link.pump(now);
    }
    stream::StreamProducer restarted(config(SignalType::F64, 1, 64), sequences);
    expect(restarted.next_sequence() == stream::kSequenceReservation, "a restarted producer continues beyond its reservation");
    const auto resume = consumer.resume_message();
    restarted.resume("consumer", resume.stream_alias, resume.next_expected);
    expect(restarted.fault_code() == stream::FaultCode::StreamStateLost, "resuming into a restarted producer is state loss");

    // A brand-new consumer cannot start at 0 either: those packets are gone.
    stream::StreamProducer again(config(SignalType::F64, 1, 64), sequences);
    again.resume("fresh", 1, 0);
    expect(again.fault_code() == stream::FaultCode::StreamStateLost, "position 0 is gone after a restart");
    std::filesystem::remove_all(dir);
}

// Random batching and random connection drops: every value exactly once, in order.
void test_soak_i12()
{
    std::mt19937 random(12345);
    stream::MemorySequenceStore sequences;
    auto c = config(SignalType::I12, 7, 32);
    stream::StreamProducer producer(c, sequences);
    stream::StreamConsumer consumer(c, 9);
    Link link{producer, consumer};
    link.connect();
    auto now = stream::Clock::now();

    std::vector<std::int16_t> sent;
    std::vector<std::int16_t> received;
    std::uniform_int_distribution<int> value(-2048, 2047);
    std::uniform_int_distribution<int> burst(1, 20);
    std::uniform_int_distribution<int> chance(0, 99);
    while (sent.size() < 20000) {
        std::vector<std::int16_t> chunk(static_cast<std::size_t>(burst(random)));
        for (auto& v : chunk) {
            v = static_cast<std::int16_t>(value(random));
        }
        std::size_t offset = 0;
        while (offset < chunk.size()) {
            offset += producer.append(chunk.data() + offset, chunk.size() - offset, now);
            if (offset < chunk.size()) {
                // blocked: let the consumer catch up
                link.pump(now);
                std::vector<std::int16_t> out(consumer.available());
                consumer.take(out.data(), out.size());
                received.insert(received.end(), out.begin(), out.end());
                now += milliseconds(60);
                link.acknowledge(now);
            }
        }
        sent.insert(sent.end(), chunk.begin(), chunk.end());
        now += milliseconds(chance(random) < 50 ? 1 : 12);
        producer.flush(now);
        link.pump(now, static_cast<std::size_t>(burst(random)));
        if (chance(random) < 5) {
            link.reset();
            link.connect();
        }
        std::vector<std::int16_t> out(consumer.available());
        consumer.take(out.data(), out.size());
        received.insert(received.end(), out.begin(), out.end());
        link.acknowledge(now);
    }
    producer.flush(now, true);
    link.pump(now);
    std::vector<std::int16_t> out(consumer.available());
    consumer.take(out.data(), out.size());
    received.insert(received.end(), out.begin(), out.end());

    expect(consumer.fault_code() == stream::FaultCode::None && producer.fault_code() == stream::FaultCode::None,
           "no faults under reconnects");
    expect(received == sent, "20000 I12 values delivered exactly once and in order through reconnects (got " +
                                 std::to_string(received.size()) + ")");
}

} // namespace

int main()
try {
    test_batching_and_delivery();
    test_gap_and_type_mismatch();
    test_reconnect_replays_without_loss();
    test_backpressure_and_acks();
    test_retention_and_two_consumers();
    test_producer_restart_is_a_gap();
    test_soak_i12();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7transport stream protocol smoke: " << ex.what() << '\n';
    return 1;
}

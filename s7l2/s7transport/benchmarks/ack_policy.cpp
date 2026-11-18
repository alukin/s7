// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Virtual-time protocol spike: same producer/consumer, only ACK policy varies.
// Not an endpoint/scheduler implementation or a throughput benchmark.
#include <s7/stream_protocol.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace json = boost::json;
using namespace s7;
auto run(bool immediate, unsigned window, unsigned packet, unsigned consumer_period, unsigned tick_ms) -> json::object
{
    stream::StreamConfig config;
    config.stream_id = wire::parse_stream_id("11111111-2222-4333-8444-555555555555");
    config.send_log_max_packets = window;
    config.max_elements_per_packet = packet;
    config.max_batch_latency = std::chrono::milliseconds(5);
    stream::MemorySequenceStore sequences;
    stream::StreamProducer producer(config, sequences);
    stream::StreamConsumer consumer(config, 7);
    const auto resume = consumer.resume_message();
    producer.resume("peer", resume.stream_alias, resume.next_expected);
    constexpr unsigned target = 127; // exercises a final partial four-value batch
    unsigned produced = 0, taken = 0, blocked = 0, acknowledgements = 0;
    std::size_t max_log = 0, max_buffer = 0;
    for (unsigned tick = 0; tick < 100000; ++tick) {
        const auto now = stream::Clock::time_point{} + std::chrono::seconds(1) + std::chrono::milliseconds(tick * tick_ms);
        if (produced < target) {
            const double value = produced;
            const auto accepted = producer.append(&value, 1, now);
            produced += static_cast<unsigned>(accepted);
            blocked += accepted == 0;
        }
        static_cast<void>(producer.flush(now));
        while (auto message = producer.next_message("peer"))
            consumer.on_packet(wire::decode_data_packet(*message), now);
        max_log = std::max(max_log, producer.stats().send_log_depth);
        max_buffer = std::max(max_buffer, consumer.available());
        if (tick % consumer_period == 0 && consumer.available()) {
            double value = -1;
            if (consumer.take(&value, 1) != 1 || value != taken) throw std::runtime_error("reference/order mismatch");
            ++taken;
        }
        const auto ack = immediate ? consumer.ack_now(now) : consumer.ack_due(now);
        if (ack) {
            if (ack->next_expected > producer.stats().packets) throw std::runtime_error("ACK exceeds sent prefix");
            producer.acknowledge("peer", ack->next_expected);
            ++acknowledgements;
        }
        if (max_log > window || max_buffer > window * packet) throw std::runtime_error("resource bound exceeded");
        if (consumer.fault_code() != stream::FaultCode::None) throw std::runtime_error("protocol fault");
        if (taken == target && producer.stats().send_log_depth == 0) {
            return {{"policy", immediate ? "immediate_consumption" : "timer_50ms_or_64_packets"},
                {"window_packets", window}, {"packet_values", packet}, {"consumer_opportunity_period", consumer_period},
                {"io_tick_ms", tick_ms}, {"values_checked", taken}, {"logical_elapsed_ms", tick * tick_ms},
                {"backpressure_ticks", blocked}, {"acks", acknowledgements},
                {"max_log_packets", max_log}, {"max_buffered_values", max_buffer}};
        }
    }
    throw std::runtime_error("no progress with independent protocol ticks");
}

int main()
try {
    json::array runs;
    for (const bool immediate : {false, true})
        for (const unsigned window : {1U, 8U})
            for (const unsigned packet : {1U, 4U})
                for (const unsigned consumer_period : {1U, 7U})
                    for (const unsigned tick_ms : {1U, 5U})
                        runs.emplace_back(run(immediate, window, packet, consumer_period, tick_ms));
    std::cout << json::serialize(json::object{{"engine", "production_stream_protocol_virtual_time"}, {"runs", std::move(runs)}}) << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "ACK policy spike: " << error.what() << '\n';
    return 1;
}

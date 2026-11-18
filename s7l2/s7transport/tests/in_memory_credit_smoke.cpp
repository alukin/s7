// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/transport.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace s7;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void test_ack_policy()
{
    stream::StreamConfig config;
    stream::StreamConsumer consumer(config, 7);
    const auto now = stream::Clock::now();
    check(!consumer.ack_now(now), "empty stream ACKed");
    wire::DataPacket packet{};
    packet.type = SignalType::F64;
    packet.stream_alias = 7;
    const double inputs[]{1, 2};
    wire::encode_elements(SignalType::F64, inputs, 2, packet.payload);
    consumer.on_packet(packet, now);
    check(!consumer.ack_now(now), "receipt ACKed before consumption");
    double value = 0;
    check(consumer.take(&value, 1) == 1 && value == 1, "partial consumption");
    check(!consumer.ack_now(now), "partial packet ACKed");
    check(consumer.take(&value, 1) == 1 && value == 2, "remaining consumption");
    const auto ack = consumer.ack_now(now);
    check(ack && ack->next_expected == 1, "complete consumed packet not ACKed");
    check(!consumer.ack_now(now), "duplicate ACK without new consumption");
    packet.sequence = 1;
    consumer.on_packet(packet, now);
    double values[2];
    check(consumer.take(values, 2) == 2, "second packet consumption");
    check(!consumer.ack_due(now + stream::kAckInterval / 2), "network timer policy ACKed early");
    const auto due = consumer.ack_due(now + stream::kAckInterval);
    check(due && due->next_expected == 2, "network timer policy did not ACK at deadline");
    for (unsigned sequence = 2; sequence < 66; ++sequence) {
        packet.sequence = sequence;
        consumer.on_packet(packet, now + stream::kAckInterval);
        check(consumer.take(values, 2) == 2, "count-trigger packet consumption");
        const auto counted = consumer.ack_due(now + stream::kAckInterval);
        if (sequence < 65) check(!counted, "network count policy ACKed before 64 packets");
        else check(counted && counted->next_expected == 66, "network count policy missed 64 packets");
    }
}

int main()
try {
    test_ack_policy();
    transport::InMemoryRuntimeNetEndpoint endpoint;
    stream::StreamConfig config;
    config.stream_id = wire::parse_stream_id("11111111-2222-4333-8444-555555555555");
    config.max_elements_per_packet = config.send_log_max_packets = 1;
    config.max_block = std::chrono::milliseconds(100);
    const auto producer = endpoint.open_producer(config);
    const auto consumer = endpoint.open_consumer(config);
    for (unsigned index = 0; index < 128; ++index) {
        const double input = index;
        const auto sent = endpoint.publish(producer, &input, 1);
        check(sent.accepted == 1 && sent.fault == stream::FaultCode::None, "one-packet producer lost progress");
        double output = -1;
        const auto received = endpoint.receive(consumer, &output, 1);
        check(received.received == 1 && received.fault == stream::FaultCode::None && output == input,
              "in-memory value/order mismatch");
        for (const auto& status : endpoint.stream_statuses())
            if (status.role == transport::StreamRole::Producer)
                check(status.send_log_depth == 0, "consumed packet still retains the sole local credit");
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "in-memory credit smoke: " << error.what() << '\n';
    return 1;
}

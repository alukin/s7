// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_port.hpp"
#include "s7/core.hpp"

#include <array>
#include <iostream>
#include <type_traits>
#include <utility>

namespace {
using s7::ArrayDescriptor;
using s7::ArrayElement;
using s7::ArrayInputBinding;
using s7::ArrayMemoryBudget;
using s7::ArrayOutputSlot;
using s7::ArrayPool;
using s7::SignalType;

static_assert(!std::is_copy_constructible_v<ArrayOutputSlot>);
static_assert(!std::is_move_constructible_v<ArrayOutputSlot>);
static_assert(!std::is_trivially_copyable_v<ArrayOutputSlot>);

constexpr std::array<std::int64_t, 2> extents{2, 3};
const ArrayDescriptor shape(SignalType::F32, extents);
const ArrayDescriptor wrong_shape(SignalType::F32, std::array<std::int64_t, 2>{3, 2});

void require(const bool condition, const std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}

template <typename Error, typename Fn>
void rejects(Fn operation, std::string_view reason)
{
    try { operation(); }
    catch (const Error&) { return; }
    throw std::runtime_error(std::string(reason));
}

ArrayPool::ReadLease frame(ArrayPool& pool, float sample)
{
    auto write = pool.try_acquire().value();
    std::ranges::fill(write.values<SignalType::F32>(), sample);
    return std::move(write).seal();
}

class Producer final : public ArrayElement {
public:
    Producer() : ArrayElement("internal_array_producer")
    {
        register_output(0, "scale", SignalType::F64, &scale_);
        frame_ = &register_array_output("frame", shape);
        register_output(2, "offset", SignalType::F64, &offset_);
        register_array_output("other", shape);
    }

    void set_frame(ArrayPool::ReadLease&& value) { static_cast<void>(frame_->exchange(std::move(value))); }
    void step() override {}

private:
    double scale_{2.0};
    double offset_{3.0};
    ArrayOutputSlot* frame_{};
};

struct WireTrace {
    const void* source{};
    std::size_t reads{};
    std::size_t materialized_bytes{};

    void read(std::span<const std::byte> bytes)
    {
        ++reads;
        if (bytes.data() != source) materialized_bytes += bytes.size();
    }
};

class Consumer final : public ArrayElement {
public:
    explicit Consumer(const ArrayDescriptor& descriptor = shape, WireTrace* trace = nullptr)
        : ArrayElement("internal_array_consumer"), trace_(trace)
    {
        register_input(0, "scale", SignalType::F64, nullptr);
        register_array_input("frame", descriptor);
        register_input(2, "offset", SignalType::F64, nullptr);
        register_output(0, "result", SignalType::F64, &result);
    }

    std::span<const float> samples() const { return array_input(1).values<SignalType::F32>(); }
    void step() override
    {
        const auto input = samples();
        if (trace_) trace_->read(std::as_bytes(input));
        result = read_input<double>(0) * input.front() + read_input<double>(2);
    }
    double result{};

private:
    WireTrace* trace_;
};

class Source final : public ArrayElement {
public:
    explicit Source(WireTrace& trace)
        : ArrayElement("array_source"), trace_(trace),
          pool_(shape, 2, 0, std::make_shared<ArrayMemoryBudget>(2 * shape.byte_size())),
          output_(register_array_output("frame", shape))
    {}

    void step() override
    {
        auto value = frame(pool_, ++sample_);
        trace_.source = value.view().bytes().data();
        static_cast<void>(output_.exchange(std::move(value)));
    }

private:
    WireTrace& trace_;
    ArrayPool pool_;
    ArrayOutputSlot& output_;
    float sample_{};
};

class Forward final : public ArrayElement {
public:
    explicit Forward(WireTrace& trace)
        : ArrayElement("array_forward"), trace_(trace), output_(register_array_output("frame", shape))
    {
        register_array_input("frame", shape);
    }

    void step() override
    {
        const auto& input = array_input(0);
        trace_.read(input.view().bytes());
        static_cast<void>(output_.exchange(input.share()));
    }

private:
    WireTrace& trace_;
    ArrayOutputSlot& output_;
};

void check_slot_exchange()
{
    const auto bytes = shape.byte_size();
    const auto budget = std::make_shared<ArrayMemoryBudget>(bytes * 4);
    ArrayPool pool(shape, 2, 1, budget);
    ArrayPool other(wrong_shape, 1, 0, budget);
    auto retention = pool.make_retention(1, bytes);
    ArrayOutputSlot slot(shape);
    ArrayOutputSlot mismatch(wrong_shape);
    ArrayInputBinding input(shape);
    rejects<std::logic_error>([&] { static_cast<void>(input.view()); }, "unbound array input");
    input.bind(slot);
    rejects<std::logic_error>([&] { static_cast<void>(input.view()); }, "empty producer slot");

    auto first = frame(pool, 1);
    const auto* first_address = first.view().bytes().data();
    require(!slot.exchange(std::move(first)) && !first && input.view().bytes().data() == first_address,
            "input borrows the slot's original payload");
    const auto held = slot.try_retain(retention).value();
    auto next = frame(pool, 2);
    const auto* next_address = next.view().bytes().data();
    auto previous = slot.exchange(std::move(next));
    require(!next && previous.view().bytes().data() == first_address &&
            input.view().bytes().data() == next_address && next_address != first_address &&
            input.values<SignalType::F32>().front() == 2 &&
            held.values<SignalType::F32>().front() == 1,
            "slot replacement changes borrowed input while retaining the old payload");
    previous = {};
    auto bad = frame(other, 9);
    rejects<std::invalid_argument>([&] { static_cast<void>(slot.exchange(std::move(bad))); }, "lease shape mismatch");
    rejects<std::invalid_argument>([&] { input.bind(mismatch); }, "producer shape mismatch");
    rejects<std::invalid_argument>([&] { static_cast<void>(input.values<SignalType::I32>()); }, "typed read rejects numeric mismatch");
    require(bad && input.view().bytes().data() == next_address, "a failed exchange or binding preserves ownership and source");
    static_cast<void>(slot.exchange({}));
    rejects<std::logic_error>([&] { static_cast<void>(input.view()); }, "cleared producer slot");
    require(held.values<SignalType::F32>().front() == 1, "clearing a slot leaves retained reads valid");
}

void check_mixed_ports()
{
    Producer producer;
    Consumer consumer;
    const auto& ports = producer.output_ports();
    const auto& frame_slot = producer.array_output(1);
    require(ports.size() == 4 && !ports[0].array_descriptor && !ports[1].value_ptr &&
            ports[1].array_descriptor == &frame_slot.descriptor() && !ports[2].array_descriptor &&
            ports[3].array_descriptor == &producer.array_output(3).descriptor(),
            "mixed port positions and slot descriptor addresses stay stable");
    rejects<std::logic_error>([&] { static_cast<void>(ports[1].as<float>()); }, "scalar view of array port");
    rejects<std::invalid_argument>([&] { consumer.bind_array_input(0, frame_slot); }, "array binding to scalar input");
    rejects<std::invalid_argument>([&] { static_cast<void>(producer.array_output(2)); }, "array slot for scalar output");
    const auto raw = producer.outputs();
    rejects<std::runtime_error>([&] { consumer.bind_inputs(std::array{raw[0], raw[0], raw[2]}); },
                                "raw pointer on array input");
    consumer.bind_inputs(std::array<s7::InputBinding, 3>{raw[0], nullptr, raw[2]});
    consumer.bind_array_input(1, frame_slot);
    auto budget = std::make_shared<ArrayMemoryBudget>(shape.byte_size() * 2);
    ArrayPool pool(shape, 2, 0, budget);
    producer.set_frame(frame(pool, 5));
    consumer.step();
    require(consumer.result == 13 &&
            static_cast<const void*>(consumer.samples().data()) == frame_slot.view().bytes().data(),
            "scalar bindings and typed borrowed array inputs work in the same element");
    producer.set_frame(frame(pool, 7));
    consumer.step();
    require(consumer.result == 17, "consumer follows slot replacement without rebinding");
}

void check_execution_guard()
{
    s7::Schema schema("mixed_factory");
    schema.set_rate(s7::Rate::from_hz(100));
    schema.add_element("producer", [] { return std::make_unique<Producer>(); });
    rejects<std::invalid_argument>([&] { static_cast<void>(s7::Compiler::compile(schema)); },
                                   "compiler must keep array ports out of scalar runtime");
    s7::ExecutableGraph graph;
    graph.elements.emplace("producer", std::make_unique<Producer>());
    graph.causal_execution.push_back(graph.elements.at("producer").get());
    rejects<std::invalid_argument>([&] { s7::NodeRuntime runtime(std::move(graph)); },
                                   "manually submitted graph must not snapshot an array port as scalar bytes");
}

void check_compiled_fanout()
{
    WireTrace trace;
    s7::Schema schema("compiled_fanout");
    schema.set_rate(s7::Rate::from_hz(100));
    // Declare the consumers first to make scheduling depend on the wires.
    schema.add_element("left", [&] { return std::make_unique<Consumer>(shape, &trace); });
    schema.add_element("right", [&] { return std::make_unique<Consumer>(shape, &trace); });
    schema.add_element("forward", [&] { return std::make_unique<Forward>(trace); });
    schema.add_element("source", [&] { return std::make_unique<Source>(trace); });
    schema.add_element("scale", [] { return std::make_unique<s7::Constant>(2.0); });
    schema.add_element("offset", [] { return std::make_unique<s7::Constant>(3.0); });
    schema.connect("source", 0, "forward", 0);
    for (const auto* reader : {"left", "right"}) {
        schema.connect("forward", 0, reader, 1);
        schema.connect("scale", 0, reader, 0);
        schema.connect("offset", 0, reader, 2);
    }
    s7::NodeRuntime runtime(s7::Compiler::compile_experimental_arrays(schema));
    const void* previous = nullptr;
    for (int step = 1; step <= 4; ++step) {
        runtime.step();
        require(trace.source != previous, "the two consumers follow each freshly sealed payload");
        require(runtime.real_output_of("left") == 2 * step + 3 &&
                runtime.real_output_of("right") == 2 * step + 3, "mixed inputs evaluate in causal order");
        previous = trace.source;
    }
    require(trace.reads == 12 && trace.materialized_bytes == 0, "all three wires reuse the source payload");
    rejects<std::invalid_argument>([&] { static_cast<void>(runtime.output_of<SignalType::F32>("forward")); },
                                   "scalar observation cannot read a graph array candidate");
}

void check_compiled_shape_validation()
{
    for (const auto& descriptor : {wrong_shape, ArrayDescriptor(SignalType::I32, extents)}) {
        WireTrace trace;
        s7::Schema schema("wrong_signature");
        schema.set_rate(s7::Rate::from_hz(100));
        schema.add_element("source", [&] { return std::make_unique<Source>(trace); });
        schema.add_element("consumer", [&] { return std::make_unique<Consumer>(descriptor); });
        schema.add_element("scalar", [] { return std::make_unique<s7::Constant>(1.0); });
        schema.connect("source", 0, "consumer", 1);
        schema.connect("scalar", 0, "consumer", 0);
        schema.connect("scalar", 0, "consumer", 2);
        try { static_cast<void>(s7::Compiler::compile_experimental_arrays(schema)); }
        catch (const std::invalid_argument& error) {
            if (std::string_view(error.what()).contains("Array port mismatch")) continue;
            throw;
        }
        throw std::runtime_error("compiled wire accepted a mismatched array signature");
    }
}
} // namespace

int main() try {
    check_slot_exchange();
    check_mixed_ports();
    check_execution_guard();
    check_compiled_fanout();
    check_compiled_shape_validation();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_port.hpp"
#include "s7/core.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace {
using namespace s7;

constexpr std::array<std::int64_t, 1> extents{4};
const ArrayDescriptor shape(SignalType::F32, extents);
const auto fast = Rate::from_hz(100);
const auto slow = Rate::from_hz(50);

enum class Fault { None, FirstOutput, Late, PoolExhausted, Capture };

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void expect_fault(NodeRuntime& runtime, Fault fault)
{
    try { runtime.step(); }
    catch (Fault actual) {
        require(actual == fault, "wrong fault propagated");
        return;
    }
    throw std::runtime_error("expected fault did not propagate");
}

class Source final : public ArrayElement {
public:
    explicit Source(const Fault& fault)
        : ArrayElement("ArrayRollbackSource"), fault_(fault),
          pool_(shape, 4, 1, std::make_shared<ArrayMemoryBudget>(5 * shape.byte_size())),
          first_(register_array_output("first", shape)), second_(register_array_output("second", shape))
    {
        register_output(2, "position", SignalType::F64, &position_);
    }

    void step() override
    {
        ++position_;
        publish(first_, static_cast<float>(position_));
        if (fault_ == Fault::FirstOutput) throw fault_;
        publish(second_, -static_cast<float>(position_));
    }

    ArrayPool::Retention retention() { return pool_.make_retention(1, shape.byte_size()); }
    ArrayPool::WriteLease occupy_execution_slot() { return pool_.try_acquire().value(); }

private:
    void publish(ArrayOutputSlot& output, float start)
    {
        auto writable = pool_.try_acquire();
        if (!writable) {
            require(writable.error() == ArrayPoolError::Exhausted, "unexpected pool error");
            throw Fault::PoolExhausted;
        }
        for (auto& value : writable->values<SignalType::F32>()) value = start++;
        static_cast<void>(output.exchange(std::move(*writable).seal()));
    }

    const Fault& fault_;
    ArrayPool pool_;
    ArrayOutputSlot& first_;
    ArrayOutputSlot& second_;
    double position_{};
};

struct WireTrace {
    const std::byte* first{};
    const std::byte* second{};
};

class Forward final : public ArrayElement {
public:
    Forward(const Fault& fault, WireTrace& trace)
        : ArrayElement("RollbackForward"), fault_(fault), trace_(trace),
          output_(register_array_output("frame", shape))
    {
        register_array_input("first", shape);
        register_array_input("second", shape);
        register_output(1, "sum", SignalType::F64, &sum_);
    }

    void step() override
    {
        const auto& first = array_input(0);
        const auto& second = array_input(1);
        trace_ = {first.view().bytes().data(), second.view().bytes().data()};
        sum_ = first.values<SignalType::F32>().back() - second.values<SignalType::F32>().front();
        static_cast<void>(output_.exchange(first.share()));
        if (fault_ == Fault::Late) throw fault_;
    }

private:
    const Fault& fault_;
    WireTrace& trace_;
    ArrayOutputSlot& output_;
    double sum_{};
};

ExecutableGraph direct_graph(const Fault& fault, WireTrace& trace)
{
    Schema schema("array_rollback");
    schema.set_rate(fast);
    schema.add_element("source", [&] { return std::make_unique<Source>(fault); });
    schema.add_element("forward", [&] { return std::make_unique<Forward>(fault, trace); });
    schema.connect("source", 0, "forward", 0);
    schema.connect("source", 1, "forward", 1);
    return Compiler::compile_experimental_arrays(schema);
}

void check_values(const ArrayOutputSlot& slot, float start)
{
    const auto view = slot.view();
    const auto values = std::span(reinterpret_cast<const float*>(view.bytes().data()), shape.element_count());
    for (const auto value : values) require(value == start++, "published array values differ from oracle");
}

void check_direct()
{
    Fault fault = Fault::Late;
    WireTrace trace;
    auto graph = direct_graph(fault, trace);
    auto& source = static_cast<Source&>(*graph.elements.at("source"));
    const auto& first = source.array_output(0);
    const auto& second = source.array_output(1);
    const auto& forwarded = static_cast<const Forward&>(*graph.elements.at("forward")).array_output(0);
    auto retention = source.retention();
    NodeRuntime runtime(std::move(graph));

    expect_fault(runtime, Fault::Late);
    const auto initial = runtime.try_retain_array_output("source", 0, retention);
    require(runtime.step_count() == 0 && runtime.real_output_of("source", 2) == 0 &&
                !initial && initial.error() == ArrayPoolError::InvalidLease, "first fault published a candidate");

    fault = Fault::None;
    runtime.step();
    const auto held = runtime.try_retain_array_output("forward", 0, retention).value();
    const auto* held_address = held.lease.view().bytes().data();

    for (int position = 2; position <= 4; ++position) {
        const auto* first_address = first.view().bytes().data();
        const auto* second_address = second.view().bytes().data();
        const auto check_fault = [&](Fault expected) {
            expect_fault(runtime, expected);
            require(runtime.step_count() == static_cast<std::size_t>(position - 1) &&
                        runtime.real_output_of("source", 2) == position - 1 &&
                        runtime.real_output_of("forward", 1) == 2 * (position - 1) + 3,
                    "fault advanced mixed scalar state");
            require(first.view().bytes().data() == first_address && second.view().bytes().data() == second_address &&
                        forwarded.view().bytes().data() == first_address, "rollback replaced a published buffer");
            check_values(first, static_cast<float>(position - 1));
            check_values(second, -static_cast<float>(position - 1));
        };

        fault = Fault::FirstOutput;
        check_fault(fault);
        fault = Fault::Late;
        check_fault(fault);
        require(trace.first != first_address && trace.second != second_address, "late fault missed the new candidates");
        fault = Fault::None;
        {
            auto occupied = source.occupy_execution_slot();
            check_fault(Fault::PoolExhausted);
        }
        const auto skipped = runtime.try_retain_array_output("source", 1, retention);
        require(!skipped && skipped.error() == ArrayPoolError::Exhausted, "optional admission exceeded its budget");
        runtime.step();
        require(runtime.step_count() == static_cast<std::size_t>(position) &&
                    runtime.real_output_of("source", 2) == position &&
                    runtime.real_output_of("forward", 1) == 2 * position + 3, "retry differs from the scalar oracle");
        require(first.view().bytes().data() == trace.first && second.view().bytes().data() == trace.second &&
                    forwarded.view().bytes().data() == trace.first, "successful retry copied a candidate");
        check_values(first, static_cast<float>(position));
        check_values(second, -static_cast<float>(position));
        require(held.domain_step == 1 && held.lease.view().bytes().data() == held_address,
                "retained payload identity changed during retries");
        float expected = 1.0f;
        for (auto sample : held.lease.values<SignalType::F32>()) {
            require(sample == expected++, "retained payload changed during retries");
        }
    }
}

class Boundary final : public Element, public RateChangingElement {
public:
    explicit Boundary(const Fault& fault) : Element("RollbackCapture"), fault_(fault)
    {
        register_input(0, "in", SignalType::F64, nullptr);
        register_output(0, "out", SignalType::F64, &output_);
    }

    void adopt_input_rate(const Rate&) override {}
    std::optional<Rate> declared_rate() const override { return slow; }
    bool changes_rate() const override { return true; }

    void capture_input() override
    {
        captured_ = read_input<double>(0);
        ++captures_;
        if (fault_ == Fault::Capture) throw fault_;
    }

    void step() override { output_ = captured_; }
    std::size_t captures() const { return captures_; }

private:
    const Fault& fault_;
    double captured_{};
    double output_{};
    std::size_t captures_{};
};

class Later final : public Element {
public:
    Later(const Fault& fault, bool linked) : Element("LaterDomain"), fault_(fault)
    {
        if (linked) register_input(0, "in", SignalType::F64, nullptr);
        register_output(0, "out", SignalType::F64, &position_);
    }

    std::optional<Rate> declared_rate() const override { return slow; }
    void step() override
    {
        position_ = input_count() ? read_input<double>(0) : position_ + 1.0;
        if (fault_ == Fault::Late) throw fault_;
    }

private:
    const Fault& fault_;
    double position_{};
};

ExecutableGraph rate_graph(const Fault& fault, bool clocked)
{
    Schema schema("array_rate_rollback");
    schema.set_rate(fast);
    schema.add_element("source", [&] { return std::make_unique<Source>(fault); });
    schema.add_element("later", [&] { return std::make_unique<Later>(fault, clocked); });
    if (clocked) {
        schema.add_element("bridge", [&] { return std::make_unique<Boundary>(fault); });
        schema.connect("source", 2, "bridge", 0);
        schema.connect("bridge", 0, "later", 0);
    }
    return Compiler::compile_experimental_arrays(schema);
}

void check_prefix(bool clocked)
{
    Fault fault = Fault::None;
    auto graph = rate_graph(fault, clocked);
    auto retention = static_cast<Source&>(*graph.elements.at("source")).retention();
    const auto* bridge = clocked ? static_cast<const Boundary*>(graph.elements.at("bridge").get()) : nullptr;
    NodeRuntime runtime(std::move(graph));
    const auto never = Fault::None;
    NodeRuntime reference(rate_graph(never, clocked));
    if (clocked) {
        runtime.set_model_time_rate(slow);
        reference.set_model_time_rate(slow);
    }
    runtime.step();
    reference.step();
    const auto before_fault = runtime.step_count();
    const auto previous_later = runtime.real_output_of("later");
    reference.step();
    const auto expected_fast_step = clocked ? 2 * reference.step_count() - 1 : reference.step_count();
    fault = Fault::Late;
    const std::byte* committed{};
    for (int attempt = 0; attempt < 3; ++attempt) {
        expect_fault(runtime, fault);
        const auto sample = runtime.try_retain_array_output("source", 0, retention).value();
        require(sample.domain_step == expected_fast_step && sample.rate == fast &&
                    sample.lease.values<SignalType::F32>().front() == static_cast<float>(reference.real_output_of("source", 2)),
                "retry reran the committed prefix");
        require(!bridge || bridge->captures() == expected_fast_step, "retry recaptured the upstream sample");
        require(runtime.step_count() == before_fault &&
                    runtime.real_output_of("later") == previous_later, "failed domain advanced");
        const auto* address = sample.lease.view().bytes().data();
        require(!committed || committed == address, "retry replaced the committed prefix payload");
        committed = address;
    }
    fault = Fault::None;
    runtime.step();
    require(runtime.step_count() == reference.step_count() &&
                runtime.real_output_of("source", 2) == reference.real_output_of("source", 2) &&
                runtime.real_output_of("later") == reference.real_output_of("later"),
            "recovered domains differ from the unfaulted run");
}

void check_capture_fault()
{
    Fault fault = Fault::Capture;
    auto graph = rate_graph(fault, true);
    auto retention = static_cast<Source&>(*graph.elements.at("source")).retention();
    const auto& bridge = static_cast<const Boundary&>(*graph.elements.at("bridge"));
    NodeRuntime runtime(std::move(graph));
    runtime.set_model_time_rate(fast);
    expect_fault(runtime, fault);
    const auto committed = runtime.try_retain_array_output("source", 0, retention).value();
    fault = Fault::None;
    expect_fault(runtime, Fault::Capture);
    require(committed.domain_step == 1 && runtime.step_count() == 0 && bridge.captures() == 1 &&
                runtime.real_output_of("source", 2) == 1.0 && runtime.real_output_of("later") == 0.0,
            "failed post-commit delivery replayed work or let the receiver advance");
}
} // namespace

int main() try {
    check_direct();
    check_prefix(false);
    check_prefix(true);
    check_capture_fault();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} catch (Fault) {
    std::cerr << "unexpected injected fault\n";
    return 1;
}

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

namespace {
using namespace s7;

constexpr std::array<std::int64_t, 1> extents{3};
const ArrayDescriptor shape(SignalType::F32, extents);
constexpr std::uint64_t source_id = 42;

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

ArrayPool::ReadLease make_value(ArrayPool& pool, float value)
{
    auto writer = pool.try_acquire().value();
    std::ranges::fill(writer.values<SignalType::F32>(), value);
    return std::move(writer).seal();
}

class TaggedSource final : public ArrayElement {
public:
    TaggedSource(const ArrayPool::ReadLease& frame, const ArraySourceOccurrence& occurrence)
        : ArrayElement("TaggedSource"), frame_(frame), occurrence_(occurrence),
          output_(register_array_output("out", shape))
    {}

    void step() override { static_cast<void>(output_.exchange({frame_, occurrence_})); }

private:
    const ArrayPool::ReadLease& frame_;
    const ArraySourceOccurrence& occurrence_;
    ArrayOutputSlot& output_;
};

struct LateFault {};

class FaultingMemory final : public Element {
public:
    explicit FaultingMemory(const bool& fail) : Element("FaultingMemory", true, Feedthrough::None), fail_(fail) {}
    void step() override { if (fail_) throw LateFault{}; }

private:
    const bool& fail_;
};

void check_provenance()
{
    auto budget = std::make_shared<ArrayMemoryBudget>(5 * shape.byte_size());
    ArrayPool initializer(shape, 1, 1, budget);
    ArrayPool upstream(shape, 1, 2, budget);
    const auto initial = make_value(initializer, -1.0f);
    const auto payload = make_value(upstream, 18.0f);
    auto initializer_reader = initializer.make_retention(1, shape.byte_size());
    auto upstream_reader = upstream.make_retention(2, 2 * shape.byte_size());
    const auto* payload_address = payload.view().bytes().data();
    ArraySourceOccurrence occurrence{source_id, 0, 1};
    bool fail = false;
    Schema schema("vector_delay_provenance");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("source", [&] { return std::make_unique<TaggedSource>(payload, occurrence); });
    schema.add_element("delay", [&] { return std::make_unique<VectorDelay>(shape, initial); });
    schema.add_element("fault", [&] { return std::make_unique<FaultingMemory>(fail); });
    schema.connect("source", 0, "delay", 0);
    NodeRuntime runtime(Compiler::compile_experimental_arrays(schema));

    runtime.step();
    const auto held_initializer = runtime.try_retain_array_output("delay", 0, initializer_reader).value();
    require(held_initializer.domain_step == 1 && !held_initializer.value.origin &&
                held_initializer.value.lease.view().bytes().data() == initial.view().bytes().data(),
            "first commit must publish the initializer with absent origin");

    occurrence.sequence = 2;
    fail = true;
    bool threw = false;
    try { runtime.step(); }
    catch (const LateFault&) { threw = true; }
    const auto premature = runtime.try_retain_array_output("delay", 0, upstream_reader);
    require(threw && runtime.step_count() == 1 &&
                !premature && premature.error() == ArrayPoolError::InvalidLease,
            "failed evaluation published the previous staging");
    fail = false;
    runtime.step();
    const auto wrong_pool = runtime.try_retain_array_output("delay", 0, initializer_reader);
    require(!wrong_pool && wrong_pool.error() == ArrayPoolError::InvalidLease,
            "upstream payload must require admission from its own pool");
    auto first = runtime.try_retain_array_output("delay", 0, upstream_reader).value();
    require(first.domain_step == 2 && first.value.origin == ArraySourceOccurrence{source_id, 0, 1} &&
                first.value.lease.view().bytes().data() == payload_address, "retry published the wrong occurrence");

    occurrence = {source_id, 1, 1};
    runtime.step();
    const auto second = runtime.try_retain_array_output("delay", 0, upstream_reader).value();
    require(second.domain_step == 3 && second.value.origin == ArraySourceOccurrence{source_id, 0, 2} &&
                second.value.lease.view().bytes().data() == payload_address &&
                first.value.origin == ArraySourceOccurrence{source_id, 0, 1},
            "logical occurrences sharing a payload lost their independent origins");
    require(held_initializer.value.lease.values<SignalType::F32>().front() == -1.0f,
            "initializer retention changed after the pool switch");
    first.value = {};
    runtime.step();
    const auto epoch_change = runtime.try_retain_array_output("delay", 0, upstream_reader).value();
    require(epoch_change.domain_step == 4 && epoch_change.value.origin == occurrence,
            "the delay did not preserve the source epoch change");
}

class Increment final : public ArrayElement {
public:
    explicit Increment(ArrayPool& pool)
        : ArrayElement("IncrementArray"), pool_(pool), output_(register_array_output("out", shape))
    {
        register_array_input("in", shape);
    }

    void step() override
    {
        auto writer = pool_.try_acquire().value();
        const auto input = array_input(0).values<SignalType::F32>();
        std::ranges::transform(input, writer.values<SignalType::F32>().begin(), [](float value) { return value + 1; });
        static_cast<void>(output_.exchange({std::move(writer).seal()}));
    }

private:
    ArrayPool& pool_;
    ArrayOutputSlot& output_;
};

void check_feedback()
{
    auto budget = std::make_shared<ArrayMemoryBudget>(6 * shape.byte_size());
    ArrayPool initializer(shape, 1, 1, budget);
    ArrayPool feedback(shape, 3, 1, budget);
    auto first_admission = initializer.make_retention(1, shape.byte_size());
    auto later_admission = feedback.make_retention(1, shape.byte_size());

    Schema schema("vector_delay_feedback");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("delay", [&] { return std::make_unique<VectorDelay>(shape, make_value(initializer, 3)); });
    schema.add_element("increment", [&] { return std::make_unique<Increment>(feedback); });
    schema.connect("delay", 0, "increment", 0);
    schema.connect("increment", 0, "delay", 0);
    NodeRuntime runtime(Compiler::compile_experimental_arrays(schema));
    for (std::uint64_t step = 1; step <= 8; ++step) {
        runtime.step();
        const auto sample = runtime.try_retain_array_output("delay", 0, step == 1 ? first_admission : later_admission).value();
        require(sample.domain_step == step &&
                    std::ranges::all_of(sample.value.lease.values<SignalType::F32>(),
                                        [step](float value) { return value == static_cast<float>(step + 2); }),
                "feedback does not match the full-array recurrence");
    }

    Schema direct("invalid_array_cycle");
    direct.set_rate(Rate::from_hz(100));
    direct.add_element("increment", [&] { return std::make_unique<Increment>(feedback); });
    direct.connect("increment", 0, "increment", 0);
    try { static_cast<void>(Compiler::compile_experimental_arrays(direct)); }
    catch (const std::invalid_argument& error) {
        require(std::string_view(error.what()).contains("Cycle detected without causal memory"), "expected direct cycle validation error");
        return;
    }
    throw std::runtime_error("direct-only array cycle compiled");
}
} // namespace

int main() try {
    check_provenance();
    check_feedback();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

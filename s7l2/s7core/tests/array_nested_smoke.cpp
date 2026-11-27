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

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Error, typename Operation>
void rejects(Operation operation, std::string_view message)
{
    try { operation(); }
    catch (const Error& error) {
        require(std::string_view(error.what()).contains(message), error.what());
        return;
    }
    throw std::runtime_error("Expected rejection.");
}

class Forward final : public ArrayElement {
public:
    Forward() : ArrayElement("ForwardArray"), output_(register_array_output("out", shape))
    {
        register_array_input("in", shape);
    }

    void step() override { static_cast<void>(output_.exchange(array_input(0).share())); }

private:
    ArrayOutputSlot& output_;
};

class Source final : public ArrayElement {
public:
    explicit Source(const ArrayPool::ReadLease& frame)
        : ArrayElement("ArrayFixtureSource"), frame_(frame), output_(register_array_output("out", shape))
    {}

    void step() override { static_cast<void>(output_.exchange({frame_, ArraySourceOccurrence{7, 0, 1}})); }

private:
    const ArrayPool::ReadLease& frame_;
    ArrayOutputSlot& output_;
};

class Increment final : public ArrayElement {
public:
    explicit Increment(ArrayPool& pool)
        : ArrayElement("IncrementArray"), pool_(pool), output_(register_array_output("out", shape))
    {
        register_array_input("in", shape);
    }

    void step() override
    {
        const auto input = array_input(0).values<SignalType::F32>();
        auto writer = pool_.try_acquire().value();
        std::ranges::transform(input, writer.values<SignalType::F32>().begin(), [](float value) { return value + 1; });
        static_cast<void>(output_.exchange({std::move(writer).seal(),
                                          ArraySourceOccurrence{7, 0, static_cast<std::uint64_t>(input.front())}}));
    }

private:
    ArrayPool& pool_;
    ArrayOutputSlot& output_;
};

class Fixture {
    std::shared_ptr<ArrayMemoryBudget> budget_ = std::make_shared<ArrayMemoryBudget>(6 * shape.byte_size());

public:
    Schema graph(bool nested, bool delayed = true)
    {
        Schema schema("feedback");
        schema.set_rate(Rate::from_hz(100));
        schema.add_element("increment", [this] { return std::make_unique<Increment>(feedback); });
        if (nested) {
            schema.add_element("outer", [this, delayed] {
                auto outer = std::make_unique<Schema>("outer");
                outer->add_element("inner", [this, delayed] { return inner(delayed, shape); });
                outer->expose_input({"in", shape.type(), "inner", 0, shape});
                outer->expose_output({"out", shape.type(), "inner", 0, shape});
                return outer;
            });
            schema.connect("increment", 0, "outer", 0);
            schema.connect("outer", 0, "increment", 0);
        } else {
            add_chain(schema, delayed);
            schema.connect("increment", 0, "forward", 0);
            schema.connect(delayed ? "delay" : "forward", 0, "increment", 0);
        }
        return schema;
    }

    std::unique_ptr<Schema> inner(bool delayed, const ArrayDescriptor& boundary)
    {
        auto schema = std::make_unique<Schema>("inner");
        add_chain(*schema, delayed);
        schema->expose_input({"in", boundary.type(), "forward", 0, boundary});
        schema->expose_output({"out", shape.type(), delayed ? "delay" : "forward", 0, shape});
        return schema;
    }

    ArrayPool initializer{shape, 1, 1, budget_};
    ArrayPool feedback{shape, 3, 1, budget_};

private:
    void add_chain(Schema& schema, bool delayed)
    {
        schema.add_element("forward", [] { return std::make_unique<Forward>(); });
        if (delayed) {
            schema.add_element("delay", [this] {
                auto writer = initializer.try_acquire().value();
                std::ranges::fill(writer.values<SignalType::F32>(), 1.0f);
                return std::make_unique<VectorDelay>(shape, std::move(writer).seal());
            });
            schema.connect("forward", 0, "delay", 0);
        }
    }

};

CommittedArraySample sample(NodeRuntime& runtime, const std::string& id, const ArrayPool::Retention& reader)
{
    return runtime.try_retain_array_output(id, 0, reader).value();
}

void check_aliases()
{
    Fixture inline_fixture;
    Fixture nested_fixture;
    auto inline_initial = inline_fixture.initializer.make_retention(1, shape.byte_size());
    auto inline_feedback = inline_fixture.feedback.make_retention(1, shape.byte_size());
    auto nested_initial = nested_fixture.initializer.make_retention(1, shape.byte_size());
    auto nested_feedback = nested_fixture.feedback.make_retention(1, shape.byte_size());
    auto compiled = Compiler::compile_experimental_arrays(nested_fixture.graph(true));
    require(compiled.elements.size() == 3 && compiled.output_aliases.at("outer").at(0).first == "outer/inner/delay" &&
                compiled.causal_execution.size() == 2 && compiled.memory_execution.size() == 1,
            "a boundary port survived compilation or changed the schedule");
    NodeRuntime nested(std::move(compiled));
    NodeRuntime inlined(Compiler::compile_experimental_arrays(inline_fixture.graph(false)));
    for (std::uint64_t step = 1; step <= 8; ++step) {
        nested.step();
        inlined.step();
        const auto& admission = step == 1 ? nested_initial : nested_feedback;
        const auto expected = sample(inlined, "delay", step == 1 ? inline_initial : inline_feedback);
        auto actual = sample(nested, "outer", admission);
        require(actual.domain_step == step && actual.rate == expected.rate &&
                    actual.value.origin == expected.value.origin &&
                    std::ranges::equal(actual.value.lease.values<SignalType::F32>(),
                                       expected.value.lease.values<SignalType::F32>()) &&
                    actual.value.lease.values<SignalType::F32>().front() == static_cast<float>(step),
                "nested feedback differs from the inlined recurrence");
        const auto* address = actual.value.lease.view().bytes().data();
        // Each read has a separate admission ticket. Release it before
        // reading another alias through the same one-ticket reader.
        actual.value = {};
        for (const auto* id : {"outer/inner", "outer/inner/delay"}) {
            const auto aliased = sample(nested, id, admission);
            require(aliased.value.lease.view().bytes().data() == address &&
                        aliased.domain_step == step && aliased.value.origin == expected.value.origin,
                    "nested aliases did not retain the original publication");
        }
    }
    rejects<std::invalid_argument>([&] { static_cast<void>(nested.output_of<SignalType::F32>("outer")); },
                                   "Array output is unavailable through scalar observation");
}

void check_direct_alias()
{
    Fixture fixture;
    auto writer = fixture.initializer.try_acquire().value();
    std::ranges::fill(writer.values<SignalType::F32>(), 9.0f);
    const auto frame = std::move(writer).seal();
    auto admission = fixture.initializer.make_retention(1, shape.byte_size());
    Schema schema("direct_nested");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("block", [&] { return fixture.inner(false, shape); });
    schema.add_element("source", [&] { return std::make_unique<Source>(frame); });
    schema.connect("source", 0, "block", 0);
    NodeRuntime runtime(Compiler::compile_experimental_arrays(schema));
    runtime.step();
    const auto result = sample(runtime, "block", admission);
    require(result.domain_step == 1 && result.value.origin == ArraySourceOccurrence{7, 0, 1} &&
                result.value.lease.view().bytes().data() == frame.view().bytes().data() &&
                result.value.lease.values<SignalType::F32>().front() == 9.0f,
            "nested direct forwarding changed the payload, provenance or timing");
}

void check_rejections()
{
    Fixture fixture;
    auto standalone = fixture.inner(false, shape);
    rejects<std::logic_error>([&] { standalone->bind_inputs({}); }, "requires a parent graph");
    rejects<std::logic_error>([&] { standalone->step(); }, "requires a parent graph");
    rejects<std::invalid_argument>([&] { static_cast<void>(Compiler::compile(fixture.graph(true))); },
                                   "Array execution is not available");
    rejects<std::invalid_argument>([&] { static_cast<void>(Compiler::compile_experimental_arrays(fixture.graph(true, false))); },
                                   "Cycle detected without causal memory");

    for (const auto& boundary : {ArrayDescriptor(SignalType::F32, std::array<std::int64_t, 1>{4}),
                                 ArrayDescriptor(SignalType::I32, extents)}) {
        Schema schema("bad_boundary");
        schema.set_rate(Rate::from_hz(100));
        schema.add_element("block", [&] { return fixture.inner(false, boundary); });
        schema.connect("block", 0, "block", 0);
        rejects<std::invalid_argument>([&] { static_cast<void>(Compiler::compile_experimental_arrays(schema)); },
                                       "Array port mismatch");
    }
}
} // namespace

int main() try {
    check_aliases();
    check_direct_alias();
    check_rejections();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

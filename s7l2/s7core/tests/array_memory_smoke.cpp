// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_port.hpp"
#include "s7/core.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace {
using namespace s7;

constexpr std::array<std::int64_t, 1> extents{2};
const ArrayDescriptor shape(SignalType::F32, extents);

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

enum class Fault { None, First, Last };

class Memory final : public ArrayElement {
public:
    Memory(float initial, float increment, Fault site, const Fault& fault, bool initialize)
        : ArrayElement("ArrayMemoryFixture", true, Feedthrough::None),
          fault_(fault), site_(site), increment_(increment),
          pool_(shape, 3, 1, std::make_shared<ArrayMemoryBudget>(4 * shape.byte_size())),
          array_(register_array_output("array", shape)), scalar_(initial)
    {
        register_array_input("array", shape);
        register_input(1, "scalar", SignalType::F64, nullptr);
        register_output(1, "scalar", SignalType::F64, &scalar_);
        if (initialize) write(initial);
    }

    void step() override
    {
        const auto values = array_input(0).values<SignalType::F32>();
        const auto input = read_input<double>(1);
        require(values[0] == input && values[1] == -input, "array staging and scalar staging disagree");
        scalar_ = input + increment_;
        write(static_cast<float>(scalar_));
        if (fault_ == site_) throw site_;
    }

    ArrayPool::Retention retention() { return pool_.make_retention(1, shape.byte_size()); }

private:
    void write(float value)
    {
        auto writable = pool_.try_acquire().value();
        auto data = writable.values<SignalType::F32>();
        data[0] = value;
        data[1] = -value;
        static_cast<void>(array_.exchange(std::move(writable).seal()));
    }

    const Fault& fault_;
    Fault site_;
    float increment_;
    ArrayPool pool_;
    ArrayOutputSlot& array_;
    double scalar_;
};

class Probe final : public ArrayElement {
public:
    Probe() : ArrayElement("ArrayMemoryProbe")
    {
        register_array_input("first", shape);
        register_array_input("last", shape);
        register_output(0, "difference", SignalType::F64, &difference_);
    }

    void step() override
    {
        difference_ = array_input(0).values<SignalType::F32>().front() -
                      array_input(1).values<SignalType::F32>().front();
    }

private:
    double difference_{};
};

ExecutableGraph graph(const Fault& fault, bool initialize = true)
{
    Schema schema("array_memory");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("first", [&] { return std::make_unique<Memory>(10.0f, 1.0f, Fault::First, fault, initialize); });
    schema.add_element("last", [&] { return std::make_unique<Memory>(20.0f, 2.0f, Fault::Last, fault, true); });
    schema.add_element("probe", [] { return std::make_unique<Probe>(); });
    schema.connect("first", 0, "last", 0);
    schema.connect("first", 1, "last", 1);
    schema.connect("last", 0, "first", 0);
    schema.connect("last", 1, "first", 1);
    schema.connect("first", 0, "probe", 0);
    schema.connect("last", 0, "probe", 1);
    return Compiler::compile_experimental_arrays(schema);
}

void expect_fault(NodeRuntime& runtime, Fault site)
{
    try { runtime.step(); }
    catch (Fault actual) {
        require(actual == site, "unexpected failing memory element");
        return;
    }
    throw std::runtime_error("expected memory fault");
}

void check_memory()
{
    Fault fault = Fault::None;
    auto compiled = graph(fault);
    auto& first = static_cast<Memory&>(*compiled.elements.at("first"));
    auto& last = static_cast<Memory&>(*compiled.elements.at("last"));
    auto first_admission = first.retention();
    auto last_admission = last.retention();
    NodeRuntime runtime(std::move(compiled));
    float expected_first = 10.0f;
    float expected_last = 20.0f;
    float previous_first = expected_first;
    float previous_last = expected_last;
    double previous_probe{};
    for (std::uint64_t position = 1; position <= 5; ++position) {
        {
            auto retained = runtime.try_retain_array_output("first", 0, first_admission);
            const auto* first_address = first.array_output(0).view().bytes().data();
            const auto* last_address = last.array_output(0).view().bytes().data();
            for (const auto site : {Fault::First, Fault::Last, Fault::Last}) {
                fault = site;
                expect_fault(runtime, site);
                require(runtime.step_count() == position - 1, "memory fault advanced the domain");
                require(runtime.real_output_of("first", 1) == previous_first &&
                            runtime.real_output_of("last", 1) == previous_last &&
                            runtime.real_output_of("probe") == previous_probe, "memory fault changed scalar outputs");
                require(first.array_output(0).view().bytes().data() == first_address &&
                            last.array_output(0).view().bytes().data() == last_address, "memory fault changed published buffers");
                if (position == 1) {
                    require(!retained && retained.error() == ArrayPoolError::InvalidLease,
                            "initializer was externally visible before successful evaluation");
                } else {
                    require(retained && retained->domain_step == position - 1 &&
                                retained->lease.values<SignalType::F32>().front() == previous_first,
                            "memory fault changed retained data");
                }
            }
        }
        fault = Fault::None;
        runtime.step();
        const auto published_first = runtime.try_retain_array_output("first", 0, first_admission).value();
        const auto published_last = runtime.try_retain_array_output("last", 0, last_admission).value();
        require(published_first.domain_step == position && published_last.domain_step == position &&
                    published_first.lease.values<SignalType::F32>().front() == expected_first &&
                    published_last.lease.values<SignalType::F32>().front() == expected_last,
                "published arrays differ from the independent recurrence");
        previous_probe = expected_first - expected_last;
        require(runtime.real_output_of("probe") == previous_probe, "direct probe read next memory staging");
        previous_first = expected_first;
        previous_last = expected_last;
        expected_first = previous_last + 1.0f;
        expected_last = previous_first + 2.0f;
    }
}

void check_missing_initializer()
{
    const auto fault = Fault::None;
    try { NodeRuntime runtime(graph(fault, false)); }
    catch (const std::invalid_argument& error) {
        require(std::string_view(error.what()).contains("requires an initial value"), "unexpected preflight error");
        return;
    }
    throw std::runtime_error("runtime accepted uninitialized array memory");
}
} // namespace

int main() try {
    check_memory();
    check_missing_initializer();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} catch (Fault) {
    std::cerr << "unexpected injected memory fault\n";
    return 1;
}

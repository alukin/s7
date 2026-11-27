// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <new>

#if defined(__ELF__)
namespace {
thread_local bool count_allocations{};
thread_local std::size_t allocations{};

void* checked_allocation(void* buffer)
{
    if (!buffer) throw std::bad_alloc();
    if (count_allocations) ++allocations;
    return buffer;
}
} // namespace

void* operator new(std::size_t size) { return checked_allocation(std::malloc(size == 0 ? 1 : size)); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    void* buffer = nullptr;
    if (posix_memalign(&buffer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) != 0) {
        throw std::bad_alloc();
    }
    return checked_allocation(buffer);
}

void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
#endif

namespace {
using namespace s7;

constexpr std::array<std::int64_t, 1> extent{8};
const ArrayDescriptor shape(SignalType::F32, extent);

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct InjectedFault {};

struct Trace {
    const std::byte* produced{};
    std::size_t generated_bytes{};
    std::size_t forwarded_bytes{};
    std::size_t materialized_bytes{};

    void read(const DenseCpuArrayView& view)
    {
        forwarded_bytes += view.bytes().size();
        if (view.bytes().data() != produced) materialized_bytes += view.bytes().size();
    }
};

class Source final : public ArrayElement {
public:
    explicit Source(Trace& trace)
        : ArrayElement("InternalDeterministicVector"), trace_(trace),
          pool_(shape, 2, 0, std::make_shared<ArrayMemoryBudget>(2 * shape.byte_size())),
          out_(register_array_output("out", shape))
    {
        register_output(1, "sequence", SignalType::F64, &sequence_);
    }

    void step() override
    {
        auto writer = pool_.try_acquire().value();
        ++sequence_;
        auto data = writer.values<SignalType::F32>();
        for (std::size_t index = 0; index < data.size(); ++index) data[index] = static_cast<float>(sequence_ + 3 * index);
        auto frame = std::move(writer).seal();
        trace_.produced = frame.view().bytes().data();
        trace_.generated_bytes += shape.byte_size();
        static_cast<void>(out_.exchange({std::move(frame), ArraySourceOccurrence{1, 0, static_cast<std::uint64_t>(sequence_)}}));
    }

private:
    Trace& trace_;
    ArrayPool pool_;
    ArrayOutputSlot& out_;
    double sequence_{};
};

class Forward final : public ArrayElement {
public:
    explicit Forward(Trace& trace) : ArrayElement("InternalVectorForward"), trace_(trace),
                                    out_(register_array_output("out", shape))
    {
        register_array_input("in", shape);
    }

    void step() override
    {
        trace_.read(array_input(0).view());
        static_cast<void>(out_.exchange(array_input(0).share()));
    }

private:
    Trace& trace_;
    ArrayOutputSlot& out_;
};

class Reader final : public ArrayElement {
public:
    Reader(Trace& trace, bool& fault) : ArrayElement("InternalVectorReader"), trace_(trace), fault_(fault)
    {
        register_array_input("in", shape);
        register_output(0, "sum", SignalType::F64, &sum_);
    }

    void step() override
    {
        trace_.read(array_input(0).view());
        sum_ = 0;
        for (const auto sample : array_input(0).values<SignalType::F32>()) sum_ += sample;
        if (fault_) throw InjectedFault{};
    }

private:
    Trace& trace_;
    const bool& fault_;
    double sum_{};
};

void check_fixture()
{
    Trace trace;
    bool fail = false;
    Schema schema("internal_vector_fixture");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("source", [&] { return std::make_unique<Source>(trace); });
    schema.add_element("forward", [&] { return std::make_unique<Forward>(trace); });
    schema.add_element("reader", [&] { return std::make_unique<Reader>(trace, fail); });
    schema.connect("source", 0, "forward", 0);
    schema.connect("forward", 0, "reader", 0);
#if defined(__ELF__)
    count_allocations = true;
    auto graph = Compiler::compile_experimental_arrays(schema);
    count_allocations = false;
    require(allocations > 0, "allocation counter did not detect setup");
#else
    auto graph = Compiler::compile_experimental_arrays(schema);
#endif
    NodeRuntime runtime(std::move(graph));
    runtime.step();
    fail = true;
    bool threw = false;
    try { runtime.step(); }
    catch (const InjectedFault&) { threw = true; }
    require(threw && runtime.step_count() == 1 && runtime.real_output_of("source", 1) == 1.0 &&
                runtime.real_output_of("reader") == 92.0, "late fault changed published state");
    fail = false;
    constexpr std::size_t measured_steps = 20'000;
    for (std::size_t index = 0; index < measured_steps; ++index) {
#if defined(__ELF__)
        count_allocations = true;
        allocations = 0;
#endif
        runtime.step();
#if defined(__ELF__)
        count_allocations = false;
        require(allocations == 0, "warmed step allocated on the stepping thread");
#endif
        const auto sequence = static_cast<double>(index + 2);
        require(runtime.real_output_of("source", 1) == sequence &&
                    runtime.real_output_of("reader") == 8.0 * sequence + 84.0,
                "committed output differs from the independent sequence oracle");
    }
    require(trace.generated_bytes == (measured_steps + 2) * shape.byte_size() &&
                trace.forwarded_bytes == 2 * trace.generated_bytes && trace.materialized_bytes == 0,
            "the fixture copied a payload while forwarding or reading");
}
} // namespace

int main() try {
    check_fixture();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

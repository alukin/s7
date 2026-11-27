// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace {
using namespace s7;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Operation>
void rejects(Operation operation, std::string_view reason)
{
    try { operation(); }
    catch (const std::invalid_argument& error) {
        if (std::string_view(error.what()).contains(reason)) return;
        throw;
    }
    throw std::runtime_error("expected the vector adapter configuration to be rejected");
}

template <SignalType Type>
void check_round_trip(const std::array<signal_storage_t<Type>, 3>& components)
{
    constexpr auto width = sizeof(signal_storage_t<Type>) * 3;
    auto budget = std::make_shared<ArrayMemoryBudget>(3 * width);
    Schema schema("scalar_vector_round_trip");
    schema.set_rate(Rate::from_hz(100));
    schema.add_element("pack", [&] { return std::make_unique<PackVectorT<Type>>(3, 2, 1, budget); });
    schema.add_element("unpack", [] { return std::make_unique<UnpackVectorT<Type>>(3); });
    for (std::size_t port = 0; port < components.size(); ++port) {
        const auto id = "component" + std::to_string(port);
        schema.add_element(id, [value = components[port]] { return std::make_unique<ConstantT<Type>>(value); });
        schema.connect(id, 0, "pack", port);
    }
    schema.connect("pack", 0, "unpack", 0);
    auto graph = Compiler::compile_experimental_arrays(schema);
    auto& pack = static_cast<PackVectorT<Type>&>(*graph.elements.at("pack"));
    auto reader = pack.make_retention(1, width);
    NodeRuntime runtime(std::move(graph));
    for (std::uint64_t step = 1; step <= 5; ++step) {
        runtime.step();
        const auto packed = runtime.try_retain_array_output("pack", 0, reader).value();
        require(packed.domain_step == step && !packed.value.origin &&
                    std::ranges::equal(packed.value.lease.template values<Type>(), components),
                "packed component order or provenance differs");
        for (std::size_t port = 0; port < components.size(); ++port) {
            require(runtime.output_of<Type>("unpack", port) == components[port], "unpacked scalar value differs");
        }
    }
}

class PinnedScalar final : public Element {
public:
    explicit PinnedScalar(int hz) : Element("PinnedScalar"), rate_(Rate::from_hz(hz))
    {
        register_output(0, port_names::out, SignalType::F32, &value_);
    }

    std::optional<Rate> declared_rate() const override { return rate_; }
    void step() override {}

private:
    Rate rate_;
    float value_{};
};

void check_errors()
{
    auto budget = std::make_shared<ArrayMemoryBudget>(64);
    rejects([&] { PackVectorT<SignalType::F32> pack(1, 2, 0, budget); }, "at least two components");
    rejects([&] { PackVectorT<SignalType::F32> pack(2, 1, 0, budget); }, "at least two execution slots");
    rejects([] { UnpackVectorT<SignalType::F32> unpack(1); }, "at least two components");
    for (const bool wrong_type : {false, true}) {
        Schema schema("invalid_pack_unpack");
        schema.set_rate(Rate::from_hz(100));
        schema.add_element("source", [] { return std::make_unique<ConstantT<SignalType::F32>>(1.0f); });
        schema.add_element("pack", [&] { return std::make_unique<PackVectorT<SignalType::F32>>(2, 2, 0, budget); });
        if (wrong_type) schema.add_element("unpack", [] { return std::make_unique<UnpackVectorT<SignalType::F64>>(2); });
        else schema.add_element("unpack", [] { return std::make_unique<UnpackVectorT<SignalType::F32>>(3); });
        schema.connect("source", 0, "pack", 0);
        schema.connect("source", 0, "pack", 1);
        schema.connect("pack", 0, "unpack", 0);
        rejects([&] { static_cast<void>(Compiler::compile_experimental_arrays(schema)); }, "Array port mismatch");
    }
    Schema rates("different_rates");
    rates.add_element("fast", [] { return std::make_unique<PinnedScalar>(100); });
    rates.add_element("slow", [] { return std::make_unique<PinnedScalar>(50); });
    rates.add_element("pack", [&] { return std::make_unique<PackVectorT<SignalType::F32>>(2, 2, 0, budget); });
    rates.connect("fast", 0, "pack", 0);
    rates.connect("slow", 0, "pack", 1);
    rejects([&] { static_cast<void>(Compiler::compile_experimental_arrays(rates)); }, "Rate conflict");
}
} // namespace

int main() try {
    check_round_trip<SignalType::F32>({1.25f, -3.5f, 0.25f});
    check_round_trip<SignalType::C32>({std::complex<float>{2, 3}, {4, -7}, {-1, 9}});
    check_errors();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

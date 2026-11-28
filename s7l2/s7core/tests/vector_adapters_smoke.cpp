// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"
#include "s7/validation.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
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
void check_saved_graph(const char* filename)
{
    std::ifstream input(filename);
    require(input.is_open(), "cannot open the saved array graph");
    const std::string json(std::istreambuf_iterator<char>{input}, {});
    auto& registry = ElementRegistry::instance();
    register_builtin_elements(registry);
    const auto definition = boost::json::parse(json);
    auto schema = SchemaLibraryLoader::build_schema(definition, registry);
    const auto validation = Validator::validate(schema);
    if (!validation.ok()) throw std::runtime_error(validation.errors.front());
    require(!detail::check_schema(schema).errors.empty(), "distributed placement accepted local array wires");

    auto graph = Compiler::compile(schema);
    require(graph.elements.at("pack")->input_count() == 3 && graph.elements.at("unpack")->output_count() == 3,
            "registered vector adapter width differs from saved parameters");
    NodeRuntime runtime(std::move(graph));
    const std::array<float, 3> expected{3, -6, 14};
    for (int step = 0; step < 10; ++step) {
        runtime.step();
        for (std::size_t port = 0; port < expected.size(); ++port) {
            require(runtime.output_of<SignalType::F32>("unpack", port) == expected[port] &&
                        runtime.output_of<SignalType::F32>("reference_components", port) == expected[port],
                    "saved vector graph differs from the independent elementwise oracle");
        }
        require(runtime.output_of<SignalType::U64>("reference", 1) == 3, "mixed scalar status port differs");
    }
}
// Registered VectorDelay, VectorAdd and VectorMultiply from saved JSON: a
// vector accumulator loop closed by VectorDelay, and an elementwise product,
// against scalar oracles.
void check_registered_vector_arithmetic()
{
    auto& registry = ElementRegistry::instance();
    register_builtin_elements(registry);
    const auto definition = boost::json::parse(R"({
        "name": "vector_arithmetic", "dt": 0.01,
        "elements": [
            {"id": "step", "type": "VectorConstant", "params": {"type": "F64", "values": [1, -2, 0.5]}},
            {"id": "sum", "type": "VectorAdd", "params": {"type": "F64", "components": 3}},
            {"id": "memory", "type": "VectorDelay", "params": {"type": "F64", "components": 3, "initial_values": [10, 0, -1]}},
            {"id": "square", "type": "VectorMultiply", "params": {"type": "F64", "components": 3}},
            {"id": "total", "type": "UnpackVector", "params": {"type": "F64", "components": 3}},
            {"id": "squared", "type": "UnpackVector", "params": {"type": "F64", "components": 3}}
        ],
        "connections": [
            {"from_element": "step", "from_port": 0, "to_element": "sum", "to_port": 0},
            {"from_element": "memory", "from_port": 0, "to_element": "sum", "to_port": 1},
            {"from_element": "sum", "from_port": 0, "to_element": "memory", "to_port": 0},
            {"from_element": "sum", "from_port": 0, "to_element": "square", "to_port": 0},
            {"from_element": "sum", "from_port": 0, "to_element": "square", "to_port": 1},
            {"from_element": "sum", "from_port": 0, "to_element": "total", "to_port": 0},
            {"from_element": "square", "from_port": 0, "to_element": "squared", "to_port": 0}
        ]})");
    auto schema = SchemaLibraryLoader::build_schema(definition, registry);
    const auto validation = Validator::validate(schema);
    if (!validation.ok()) throw std::runtime_error(validation.errors.front());
    NodeRuntime runtime(Compiler::compile(schema));
    std::array<double, 3> total{10, 0, -1};
    const std::array<double, 3> step{1, -2, 0.5};
    for (int n = 0; n < 6; ++n) {
        runtime.step();
        for (std::size_t i = 0; i < 3; ++i) {
            total[i] += step[i];
            require(runtime.output_of<SignalType::F64>("total", i) == total[i], "vector accumulator loop differs from the scalar oracle");
            require(runtime.output_of<SignalType::F64>("squared", i) == total[i] * total[i], "VectorMultiply differs from the scalar oracle");
        }
    }
    auto bad = definition;
    bad.as_object()["elements"].as_array()[2].as_object()["params"].as_object()["initial_values"] = boost::json::array{1, 2};
    rejects([&] { static_cast<void>(Compiler::compile(SchemaLibraryLoader::build_schema(bad, registry))); }, "initial_values");
    auto integer = definition;
    integer.as_object()["elements"].as_array()[1].as_object()["params"].as_object()["type"] = "I32";
    rejects([&] { static_cast<void>(Compiler::compile(SchemaLibraryLoader::build_schema(integer, registry))); }, "supports F32, F64, C32 and C64");
}
} // namespace

int main(int argc, char** argv) try {
    check_registered_vector_arithmetic();
    check_round_trip<SignalType::F32>({1.25f, -3.5f, 0.25f});
    check_round_trip<SignalType::C32>({std::complex<float>{2, 3}, {4, -7}, {-1, 9}});
    check_errors();
    require(argc == 2, "pass the path of the saved array graph");
    check_saved_graph(argv[1]);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

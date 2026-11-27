// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace {
using s7::PortSpec;
using s7::SignalType;

void require(bool condition, std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}

PortSpec port(SignalType type, std::initializer_list<std::int64_t> extents = {})
{
    PortSpec result{"value", "", type};
    if (extents.size() != 0) result.array.emplace(type, std::span(extents.begin(), extents.size()));
    return result;
}

void add_array(s7::Schema& schema, std::string id, std::vector<PortSpec> inputs, std::vector<PortSpec> outputs,
               std::vector<s7::TypeVariableSpec> variables = {})
{
    s7::ElementMetadata metadata;
    metadata.type_name = "ArrayFixture";
    metadata.stateless = true;
    metadata.input_ports = std::move(inputs);
    metadata.output_ports = std::move(outputs);
    metadata.type_variables = std::move(variables);
    if (!metadata.type_variables.empty()) {
        s7::ParameterSpec pin;
        pin.name = "type";
        pin.type = "type";
        pin.default_value = "F64";
        metadata.parameters.push_back(std::move(pin));
    }
    const s7::ElementDescriptor descriptor(metadata.type_name, [](const auto&) -> std::unique_ptr<s7::Element> {
        throw std::runtime_error("Array declaration unexpectedly created an executable element.");
    }, metadata);
    schema.add_element(s7::make_element_spec(std::move(id), descriptor, {}));
}

void rejects(const s7::Schema& schema, std::string_view reason, bool valid_declaration = false)
{
    const auto validation = s7::Validator::validate(schema);
    if (valid_declaration) {
        require(validation.ok(), validation.ok() ? "" : validation.errors.front());
        require(!s7::detail::check_schema_structure(schema).errors.empty(),
                "deployment preflight must reject declaration-only arrays");
    } else {
        if (!std::ranges::any_of(validation.errors, [&](const auto& error) { return error.contains(reason); })) {
            throw std::runtime_error(validation.ok() ? "Expected a validation failure." : validation.errors.front());
        }
    }
    try {
        static_cast<void>(s7::Compiler::compile(schema));
    } catch (const std::invalid_argument& error) {
        require(std::string_view(error.what()).contains(reason), error.what());
        return;
    }
    throw std::runtime_error("The compiler accepted a declaration-only array graph.");
}

void check_mixed_types()
{
    s7::Schema schema("mixed_types");
    schema.set_rate(s7::Rate::from_hz(100));
    schema.add_element("scalar", [] { return std::make_unique<s7::ConstantT<SignalType::F32>>(1.0F); });
    auto generic_scalar = port(SignalType::F64);
    generic_scalar.type_rule = "T";
    const auto vector = port(SignalType::C32, {8});
    add_array(schema, "mixed", {generic_scalar}, {generic_scalar, vector},
              {{"T", {SignalType::F32, SignalType::F64}}});
    add_array(schema, "vector_sink", {vector}, {});
    schema.connect("scalar", 0, "mixed", 0);
    schema.connect("mixed", 1, "vector_sink", 0);
    const auto inferred = s7::TypeResolver::resolve(schema);
    require(inferred.ok() && inferred.instances.at(1)->input_ports()[0].type == SignalType::F32 &&
            inferred.instances.at(1)->output_ports()[0].type == SignalType::F32,
            "scalar pins of a mixed element still use numeric inference");
    rejects(schema, "Array execution is not available", true);
}

void check_pair(const PortSpec& output, const PortSpec& input, std::string_view mismatch = {})
{
    s7::Schema schema("array_ports");
    schema.set_rate(s7::Rate::from_hz(100));
    if (output.array) add_array(schema, "source", {}, {output});
    else schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    if (input.array) add_array(schema, "sink", {input}, {});
    else schema.add_element("sink", [] { return std::make_unique<s7::Gain>(1.0); });
    schema.connect("source", 0, "sink", 0);
    rejects(schema, mismatch.empty() ? "Array execution is not available" : mismatch, mismatch.empty());
}

template <typename Integrator>
void check_trial(std::string_view error)
{
    s7::Schema schema("trial");
    schema.set_rate(s7::Rate::from_hz(100));
    schema.add_element("state", [] { return std::make_unique<Integrator>(0.01); });
    add_array(schema, "derivative", {port(SignalType::F64)}, {port(SignalType::F64), port(SignalType::F64, {3})});
    schema.connect("state", 0, "derivative", 0);
    schema.connect("derivative", 0, "state", 0);
    rejects(schema, error);
}

void check_cycle()
{
    s7::Schema schema("cycle");
    schema.set_rate(s7::Rate::from_hz(100));
    const auto vector = port(SignalType::F32, {8});
    add_array(schema, "direct", {vector}, {vector});
    schema.connect("direct", 0, "direct", 0);
    rejects(schema, "Cycle detected without causal memory");
}
} // namespace

int main() try {
    check_pair(port(SignalType::F32, {8}), port(SignalType::F32, {8}));
    check_pair(port(SignalType::C32, {8}), port(SignalType::C32, {8}));
    check_pair(port(SignalType::U8, {2, 4, 3}), port(SignalType::U8, {2, 4, 3}));
    check_pair(port(SignalType::F32, {8}), port(SignalType::C32, {8}), "F32[8]");
    check_pair(port(SignalType::U8, {2, 4, 3}), port(SignalType::U8, {24}), "U8[24]");
    check_pair(port(SignalType::F64, {2, 4}), port(SignalType::F64, {4, 2}), "F64[4,2]");
    check_pair(port(SignalType::F64, {1}), port(SignalType::F64), "(scalar)");
    check_pair(port(SignalType::F64), port(SignalType::F64, {1}), "(scalar)");
    check_cycle();
    check_mixed_types();
    check_trial<s7::TrapezoidIntegrator>("Implicit TrapezoidIntegrator trial cannot replay");
    check_trial<s7::GaussLegendreIntegrator>("Coupled integration trial cannot replay");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

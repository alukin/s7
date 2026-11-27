// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_elements.hpp"
#include "s7/element_registry.hpp"
#include "s7/metadata_json.hpp"
#include "s7/signal_values.hpp"
#include <limits>

namespace s7::detail {
namespace {
constexpr int max_vector_length = 1 << 20;

ArrayDescriptor descriptor(const ElementCreationContext& ctx)
{
    return ArrayDescriptor(parse_signal_type(ctx.get_string("type")), std::array{ctx.get_int("components")});
}

std::shared_ptr<ArrayMemoryBudget> budget(const ArrayDescriptor& shape, std::size_t slots)
{
    if (shape.byte_size() > std::numeric_limits<std::size_t>::max() / slots) {
        throw std::length_error("Vector buffer reservation overflows size_t.");
    }
    return std::make_shared<ArrayMemoryBudget>(shape.byte_size() * slots);
}
} // namespace

void register_builtin_array_elements(ElementRegistry& registry, const ElementMetadataLibrary& metadata)
{
    registry.register_element(ElementDescriptor("VectorConstant", [](const ElementCreationContext& ctx) {
        const auto& values = ctx.params.at("values").as_array();
        if (values.empty() || values.size() > max_vector_length) {
            throw std::invalid_argument("VectorConstant requires 1 to 1048576 values.");
        }
        return visit_signal_type(parse_signal_type(ctx.get_string("type")), [&]<SignalType Type>() -> std::unique_ptr<Element> {
            std::vector<signal_storage_t<Type>> data;
            data.reserve(values.size());
            for (const auto& value : values) data.push_back(signal_value_from_json<Type>(value, "VectorConstant component"));
            const ArrayDescriptor shape(Type, std::array{static_cast<std::int64_t>(data.size())});
            return std::make_unique<VectorConstantT<Type>>(data, 0, budget(shape, 1));
        });
    }, metadata.at("VectorConstant"), true));

    registry.register_element(ElementDescriptor("VectorPassThrough", [](const ElementCreationContext& ctx) {
        return std::make_unique<VectorPassThrough>(descriptor(ctx));
    }, metadata.at("VectorPassThrough"), true));

    registry.register_element(ElementDescriptor("VectorGain", [](const ElementCreationContext& ctx) {
        const auto shape = descriptor(ctx);
        const auto slots = static_cast<std::size_t>(ctx.get_int("execution_slots"));
        return visit_signal_type(shape.type(), [&]<SignalType Type>() -> std::unique_ptr<Element> {
            if constexpr (Type == SignalType::F32 || Type == SignalType::F64 ||
                          Type == SignalType::C32 || Type == SignalType::C64) {
                const auto value = signal_value_from_json<Type>(ctx.params.at("factor"), "VectorGain factor");
                return std::make_unique<VectorGainT<Type>>(shape, value, slots, budget(shape, slots));
            } else {
                throw std::invalid_argument("VectorGain supports F32, F64, C32 and C64.");
            }
        });
    }, metadata.at("VectorGain"), true));

    registry.register_element(ElementDescriptor("PackVector", [](const ElementCreationContext& ctx) {
        const auto shape = descriptor(ctx);
        const auto slots = static_cast<std::size_t>(ctx.get_int("execution_slots"));
        return visit_signal_type(shape.type(), [&]<SignalType Type>() -> std::unique_ptr<Element> {
            return std::make_unique<PackVectorT<Type>>(ctx.get_int("components"), slots, 0, budget(shape, slots));
        });
    }, metadata.at("PackVector"), true));

    registry.register_element(ElementDescriptor("UnpackVector", [](const ElementCreationContext& ctx) {
        return visit_signal_type(parse_signal_type(ctx.get_string("type")), [&]<SignalType Type>() -> std::unique_ptr<Element> {
            return std::make_unique<UnpackVectorT<Type>>(ctx.get_int("components"));
        });
    }, metadata.at("UnpackVector"), true));
}

} // namespace s7::detail

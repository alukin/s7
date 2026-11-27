// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_descriptor.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using s7::ArrayDescriptor;
using s7::DenseCpuArrayView;
using s7::SignalType;

static_assert(!std::is_constructible_v<DenseCpuArrayView, ArrayDescriptor&&, std::span<const std::byte>>);

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Exception, typename Function>
void require_error(Function&& function, const std::string_view message)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void check_numeric_types()
{
    const std::array<std::int64_t, 1> two{2};
    for (const auto type : s7::kAllSignalTypes) {
        const ArrayDescriptor scalar(type);
        const ArrayDescriptor vector(type, two);
        s7::visit_signal_type(type, [&]<SignalType T>() {
            using Storage = s7::signal_storage_t<T>;
            std::array<Storage, 2> storage{};
            const DenseCpuArrayView view(vector, std::as_bytes(std::span(storage)));
            require(scalar.type() == type && scalar.extents().empty() &&
                    scalar.element_count() == 1 && scalar.byte_size() == sizeof(Storage),
                    "rank-zero descriptor matches native scalar storage");
            require(vector.rank() == 1 && vector.element_count() == 2 &&
                    vector.element_alignment() == alignof(Storage) && view.stride_bytes(0) == sizeof(Storage),
                    "rank-one descriptor matches numeric type storage");
        });
    }
    const ArrayDescriptor scalar(SignalType::F64);
    const ArrayDescriptor one(SignalType::F64, std::array<std::int64_t, 1>{1});
    require(scalar != one && scalar.byte_size() == one.byte_size(),
            "one-element array is distinct from a native scalar");
    require(one != ArrayDescriptor(SignalType::I64, std::array<std::int64_t, 1>{1}),
            "equal storage sizes do not make numeric types equal");
}

void check_shapes()
{
    const ArrayDescriptor grayscale(SignalType::U8, std::array<std::int64_t, 2>{2, 4});
    const ArrayDescriptor rgb(SignalType::U8, std::array<std::int64_t, 3>{2, 4, 3});
    const std::array<std::byte, 24> image{};
    const DenseCpuArrayView gray_view(grayscale, image);
    const DenseCpuArrayView rgb_view(rgb, image);
    require(gray_view.bytes().size() == 8 && gray_view.stride_bytes(0) == 4 &&
            gray_view.stride_bytes(1) == 1, "compact grayscale layout");
    require(rgb_view.bytes().size() == 24 && rgb_view.stride_bytes(0) == 12 &&
            rgb_view.stride_bytes(1) == 3 && rgb_view.stride_bytes(2) == 1, "compact RGB layout");

    const ArrayDescriptor field(SignalType::F64, std::array<std::int64_t, 2>{2, 4});
    const ArrayDescriptor vector_field(SignalType::F64, std::array<std::int64_t, 4>{2, 3, 4, 3});
    const std::array<double, 72> values{};
    const DenseCpuArrayView field_view(field, std::as_bytes(std::span(values)));
    const DenseCpuArrayView vector_field_view(vector_field, std::as_bytes(std::span(values)));
    require(field_view.bytes().size() == 8 * sizeof(double) &&
            field_view.stride_bytes(0) == 4 * sizeof(double), "rank-two spatial scalar field");
    require(vector_field.extents()[2] == 4 && vector_field.element_count() == 72 &&
            vector_field_view.bytes().size() == sizeof(values) &&
            vector_field_view.stride_bytes(0) == 36 * sizeof(double) &&
            vector_field_view.stride_bytes(1) == 12 * sizeof(double) &&
            vector_field_view.stride_bytes(2) == 3 * sizeof(double) &&
            vector_field_view.stride_bytes(3) == sizeof(double), "rank-four spatial vector field");

    const ArrayDescriptor higher_rank(SignalType::F64, std::array<std::int64_t, 5>{1, 2, 1, 3, 1});
    require(higher_rank.rank() == 5 && higher_rank.element_count() == 6, "rank is not tied to a domain");
}

void check_invalid_descriptors()
{
    constexpr auto large = static_cast<std::int64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    require_error<std::invalid_argument>([] {
        static_cast<void>(ArrayDescriptor(static_cast<SignalType>(0xffff)));
    }, "invalid numeric type must throw");
    require_error<std::invalid_argument>([] {
        ArrayDescriptor(SignalType::U8, std::array<std::int64_t, 2>{3, 0});
    }, "zero extent must throw");
    require_error<std::invalid_argument>([] {
        ArrayDescriptor(SignalType::U8, std::array<std::int64_t, 1>{-1});
    }, "negative extent must throw");
    require_error<std::overflow_error>([] {
        ArrayDescriptor(SignalType::U8, std::array<std::int64_t, 2>{large, 3});
    }, "overflowing extent product must throw before payload allocation");
    require_error<std::overflow_error>([] {
        ArrayDescriptor(SignalType::F64, std::array<std::int64_t, 1>{large});
    }, "overflowing byte count must throw before payload allocation");
}

void check_view_bounds()
{
    const ArrayDescriptor descriptor(SignalType::F64, std::array<std::int64_t, 2>{2, 3});
    const std::array<double, 7> values{};
    const auto backing = std::as_bytes(std::span(values));
    const DenseCpuArrayView view(descriptor, backing, sizeof(double));
    require(&view.descriptor() == &descriptor && view.bytes().data() == backing.data() + sizeof(double) &&
            view.bytes().size() == 6 * sizeof(double), "aligned nonzero offset ending at backing boundary");
    require_error<std::out_of_range>([&] {
        DenseCpuArrayView(descriptor, backing.first(backing.size() - 1), sizeof(double));
    }, "one-byte-short backing must throw");
    require_error<std::out_of_range>([&] {
        DenseCpuArrayView(descriptor, backing, std::numeric_limits<std::size_t>::max());
    }, "out-of-range offset must throw without wrapping");
    require_error<std::invalid_argument>([&] {
        DenseCpuArrayView(descriptor, backing, 1);
    }, "misaligned offset must throw");
    require_error<std::invalid_argument>([&] {
        DenseCpuArrayView(descriptor, backing.subspan(1));
    }, "actual backing address must be aligned");
    const ArrayDescriptor scalar(SignalType::F64);
    require_error<std::invalid_argument>([&] {
        DenseCpuArrayView(scalar, backing);
    }, "rank zero uses native storage");
    require_error<std::out_of_range>([&] {
        static_cast<void>(view.stride_bytes(descriptor.rank()));
    }, "invalid stride axis must throw");
}

} // namespace

int main() try {
    check_numeric_types();
    check_shapes();
    check_invalid_descriptors();
    check_view_bounds();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

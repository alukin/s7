// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_elements.hpp"

#include <array>
#include <limits>
#include <utility>

namespace s7 {
namespace {
ArrayDescriptor frame_shape(std::int64_t width, std::int64_t height, ImagePixelFormat format)
{
    if (width < 1 || height < 1 || width > 2048 || height > 2048) {
        throw std::invalid_argument("TestPatternFrame dimensions must lie in [1, 2048].");
    }
    switch (format) {
    case ImagePixelFormat::Gray8: return ArrayDescriptor(SignalType::U8, std::array{height, width});
    case ImagePixelFormat::RGB8: return ArrayDescriptor(SignalType::U8, std::array{height, width, std::int64_t{3}});
    }
    throw std::invalid_argument("TestPatternFrame received an unsupported pixel format.");
}
} // namespace

ArrayDescriptor detail::vector_adapter_shape(SignalType type, std::int64_t components)
{
    if (components < 2) throw std::invalid_argument("A vector adapter requires at least two components.");
    return ArrayDescriptor(type, std::array{components});
}

VectorDelay::VectorDelay(ArrayDescriptor descriptor, ArrayPool::ReadLease initial)
    : ArrayElement("VectorDelay", true, Feedthrough::None),
      output_(register_array_output(port_names::out, descriptor))
{
    if (!initial) throw std::invalid_argument("VectorDelay requires an initialized array.");
    register_array_input(port_names::in, std::move(descriptor));
    static_cast<void>(output_.exchange({std::move(initial)}));
}

void VectorDelay::step()
{
    static_cast<void>(output_.exchange(array_input(0).share()));
}

VectorPassThrough::VectorPassThrough(ArrayDescriptor descriptor)
    : ArrayElement("VectorPassThrough"), output_(register_array_output(port_names::out, descriptor))
{
    register_array_input(port_names::in, std::move(descriptor));
}

void VectorPassThrough::step()
{
    static_cast<void>(output_.exchange(array_input(0).share()));
}

TestPatternFrame::TestPatternFrame(std::int64_t width, std::int64_t height, ImagePixelFormat format, std::uint64_t source_id)
    : ArrayElement("TestPatternFrame"), format_(format), source_id_(source_id), descriptor_(frame_shape(width, height, format)),
      pool_(descriptor_, 2, 1, std::make_shared<ArrayMemoryBudget>(3 * descriptor_.byte_size())),
      output_(register_array_output(port_names::out, descriptor_))
{
    register_output(1, "sequence", SignalType::U64, &sequence_);
}

void TestPatternFrame::step()
{
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("TestPatternFrame sequence exhausted.");
    }
    auto writer = pool_.try_acquire();
    if (!writer) throw std::runtime_error("TestPatternFrame execution pool cannot supply an output.");
    const auto next_sequence = sequence_ + 1;
    const auto shape = descriptor_.extents();
    const auto bar_column = (next_sequence - 1) % shape[1];
    auto pixels = writer->values<SignalType::U8>();
    std::size_t offset = 0;
    for (std::size_t row = 0; row < shape[0]; ++row) {
        for (std::size_t column = 0; column < shape[1]; ++column) {
            const auto gray = static_cast<std::uint8_t>((row / 2 + column / 2) % 2 ? 208 : 32);
            const bool bar = column == bar_column;
            pixels[offset++] = bar ? 255 : gray;
            if (format_ == ImagePixelFormat::RGB8) {
                pixels[offset++] = bar ? 0 : gray;
                pixels[offset++] = bar ? 0 : gray;
            }
        }
    }
    static_cast<void>(output_.exchange({std::move(*writer).seal(), ArraySourceOccurrence{source_id_, 0, next_sequence},
                                        {}, {}, ArrayImage{format_}}));
    sequence_ = next_sequence;
}

ArrayPool::Retention TestPatternFrame::make_retention()
{
    return pool_.make_retention(1, descriptor_.byte_size());
}

} // namespace s7

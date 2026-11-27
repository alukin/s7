// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_descriptor.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace s7 {

ArrayDescriptor::ArrayDescriptor(const SignalType type, const std::span<const std::int64_t> extents)
    : type_(type)
{
    if (std::ranges::find(kAllSignalTypes, type) == kAllSignalTypes.end()) {
        throw std::invalid_argument("Unknown array signal type.");
    }
    const auto [size, alignment] = visit_signal_type(type, []<SignalType T>() {
        return std::pair{sizeof(signal_storage_t<T>), alignof(signal_storage_t<T>)};
    });
    element_size_ = size;
    element_alignment_ = alignment;

    // Establish the invariant before allocating the descriptor's extent list.
    std::size_t bytes = element_size_;
    for (const auto extent : extents) {
        if (extent <= 0 || !std::in_range<std::size_t>(extent)) {
            throw std::invalid_argument("Array extents must be positive and fit in size_t.");
        }
        const auto count = static_cast<std::size_t>(extent);
        if (count > std::numeric_limits<std::size_t>::max() / bytes) {
            throw std::overflow_error("Array byte size overflows size_t.");
        }
        bytes *= count;
    }
    extents_.assign(extents.begin(), extents.end());
}

ArrayDescriptor ArrayDescriptor::mjpeg_packet(const std::size_t capacity, const MjpegPacketFormat format)
{
    if (capacity < 4 || capacity > MjpegPacketFormat::max_capacity)
        throw std::invalid_argument("MJPEG packet capacity must be between 4 bytes and 32 MiB.");
    if (format.width < 1 || format.width > MjpegPacketFormat::max_dimension ||
        format.height < 1 || format.height > MjpegPacketFormat::max_dimension ||
        std::int64_t{format.width} * format.height > MjpegPacketFormat::max_pixels)
        throw std::invalid_argument("MJPEG dimensions exceed the supported bounds.");
    ArrayDescriptor descriptor(SignalType::U8, std::array{static_cast<std::int64_t>(capacity)});
    descriptor.mjpeg_ = format;
    return descriptor;
}

std::size_t ArrayDescriptor::element_count() const noexcept
{
    std::size_t count = 1;
    for (const auto extent : extents_) {
        count *= extent;
    }
    return count;
}

DenseCpuArrayView::DenseCpuArrayView(const ArrayDescriptor& descriptor,
                                     const std::span<const std::byte> backing,
                                     const std::size_t byte_offset)
    : descriptor_(&descriptor)
{
    if (descriptor.rank() == 0) {
        throw std::invalid_argument("A dense array view requires at least one axis.");
    }
    const auto size = descriptor.byte_size();
    if (byte_offset > backing.size() || size > backing.size() - byte_offset) {
        throw std::out_of_range("Dense array view exceeds backing storage.");
    }
    bytes_ = backing.subspan(byte_offset, size);
    if (reinterpret_cast<std::uintptr_t>(bytes_.data()) % descriptor.element_alignment() != 0) {
        throw std::invalid_argument("Dense array view is misaligned for its signal type.");
    }
}

std::size_t DenseCpuArrayView::stride_bytes(const std::size_t axis) const
{
    const auto extents = descriptor_->extents();
    if (axis >= extents.size()) {
        throw std::out_of_range("Dense array axis is out of range.");
    }
    std::size_t stride = bytes_.size();
    for (const auto extent : extents.first(axis + 1)) {
        stride /= extent;
    }
    return stride;
}

} // namespace s7

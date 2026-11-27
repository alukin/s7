// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_DESCRIPTOR_HPP
#define S7CORE_INCLUDE_S7_ARRAY_DESCRIPTOR_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "s7/signal_types.hpp"

namespace s7 {

struct MjpegPacketFormat {
    static constexpr int max_dimension = 4096;
    static constexpr std::int64_t max_pixels = 1 << 23;
    static constexpr std::size_t max_capacity = 32U << 20;
    int width;
    int height;
    bool operator==(const MjpegPacketFormat&) const = default;
};

/// Fixed numeric type and extents, independent of storage and domain metadata.
/// An empty extent list denotes one native scalar.
class ArrayDescriptor {
public:
    explicit ArrayDescriptor(SignalType type, std::span<const std::int64_t> extents = {});
    [[nodiscard]] static ArrayDescriptor mjpeg_packet(std::size_t capacity, MjpegPacketFormat format);
    [[nodiscard]] const std::optional<MjpegPacketFormat>& mjpeg() const noexcept { return mjpeg_; }

    [[nodiscard]] SignalType type() const noexcept { return type_; }
    [[nodiscard]] std::span<const std::size_t> extents() const noexcept { return extents_; }
    [[nodiscard]] std::size_t rank() const noexcept { return extents_.size(); }
    [[nodiscard]] std::size_t element_count() const noexcept;
    [[nodiscard]] std::size_t element_alignment() const noexcept { return element_alignment_; }
    [[nodiscard]] std::size_t byte_size() const noexcept { return element_count() * element_size_; }

    friend bool operator==(const ArrayDescriptor&, const ArrayDescriptor&) = default;

private:
    std::vector<std::size_t> extents_;
    SignalType type_;
    std::size_t element_size_;
    std::size_t element_alignment_;
    std::optional<MjpegPacketFormat> mjpeg_;
};

/// Borrowed read-only view of an aligned, tightly packed row-major CPU array.
/// The backing storage and descriptor must outlive the view and the descriptor
/// must remain unchanged while borrowed. Owning leases are a separate contract.
class DenseCpuArrayView {
public:
    DenseCpuArrayView(const ArrayDescriptor& descriptor, std::span<const std::byte> backing,
                      std::size_t byte_offset = 0);
    DenseCpuArrayView(const ArrayDescriptor&&, std::span<const std::byte>, std::size_t = 0) = delete;

    [[nodiscard]] const ArrayDescriptor& descriptor() const noexcept { return *descriptor_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t stride_bytes(std::size_t axis) const;

private:
    const ArrayDescriptor* descriptor_;
    std::span<const std::byte> bytes_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_DESCRIPTOR_HPP

// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP
#define S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP

#include "s7/array_pool.hpp"

#include <cstdint>
#include <optional>

namespace s7 {

// Source IDs are unique within a run. The source changes epoch before it
// reuses a sequence. These fields describe the occurrence, not its buffer.
struct ArraySourceOccurrence {
    std::uint64_t source_id{};
    std::uint64_t epoch{};
    std::uint64_t sequence{};

    bool operator==(const ArraySourceOccurrence&) const = default;
};

struct ArrayValue {
    ArrayPool::ReadLease lease;
    std::optional<ArraySourceOccurrence> origin{};

    explicit operator bool() const noexcept { return static_cast<bool>(lease); }
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP

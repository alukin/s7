#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace s7studio::observation {

// Joins N independently delivered coordinate streams into points of one
// runtime step: a point exists only once every coordinate of that step has
// arrived, so X/Y(/Z) always come from the same simulation sample, never from
// whichever samples happen to arrive next to each other. Coordinates may
// arrive in any order. A step still incomplete after kMaxPendingSteps newer
// steps started is discarded and counted (the mailbox dropped part of it).
template <std::size_t N>
class StepAligner {
public:
    using Point = std::array<double, N>;
    static constexpr std::size_t kMaxPendingSteps = 8;

    auto add(const std::int64_t step, const std::size_t axis, const double value) -> std::optional<Point>
    {
        auto& pending = pending_[step];
        pending.values[axis] = value;
        pending.present.set(axis);
        if (pending.present.all()) {
            const auto point = pending.values;
            pending_.erase(step);
            return point;
        }
        while (pending_.size() > kMaxPendingSteps) {
            pending_.erase(pending_.begin());
            ++discardedSteps_;
        }
        return std::nullopt;
    }

    void clear()
    {
        pending_.clear();
        discardedSteps_ = 0;
    }

    [[nodiscard]] auto discardedSteps() const -> std::uint64_t { return discardedSteps_; }

private:
    struct Pending {
        Point values{};
        std::bitset<N> present;
    };
    std::map<std::int64_t, Pending> pending_;
    std::uint64_t discardedSteps_ = 0;
};

} // namespace s7studio::observation

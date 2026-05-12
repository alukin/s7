//
// s7probe.hpp — data-capture Probe element
//
// Probe is a sink element that reads N real-valued inputs every tick and
// stores the samples in per-channel ring buffers.  The ring buffer is
// thread-safe: tick() writes from the compute thread; readChannel() reads
// from any other thread (e.g. the GUI thread via a QTimer).
//
// When the buffer is full, the oldest sample is silently overwritten
// (circular / oscilloscope semantics).
//
// An optional IElementObserver can also be attached for push-mode
// notification (e.g. headless file logging).
//

#pragma once

#include "s7.hpp"

#include <algorithm>
#include <mutex>
#include <type_traits>
#include <vector>

namespace s7 {

template<typename T>
class TypedProbe : public Element
{
    static_assert(std::is_same_v<T, Real> || std::is_same_v<T, Complex>,
                  "TypedProbe supports only Real or Complex signal types");

public:
    static constexpr std::size_t kDefaultCapacity = 8192;

    TypedProbe() : TypedProbe("", 2) {}
    explicit TypedProbe(int channel_count, std::size_t buffer_capacity = kDefaultCapacity)
        : TypedProbe("", channel_count, buffer_capacity)
    {
    }

    TypedProbe(const std::string& id, int channel_count, std::size_t buffer_capacity = kDefaultCapacity)
        : Element(id)
        , channel_count_(channel_count > 0 ? channel_count : 1)
        , capacity_(buffer_capacity > 0 ? buffer_capacity : kDefaultCapacity)
    {
        setTraits(/*stateful=*/true);

        channels_.resize(channel_count_);
        for (auto& ch : channels_)
        {
            ch.data.assign(capacity_, T{});
        }

        inputs_.reserve(channel_count_);
        for (int i = 0; i < channel_count_; ++i)
        {
            inputs_.push_back(addInput<T>());
        }
    }

    void tick() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int ch = 0; ch < channel_count_; ++ch)
        {
            T value{};
            if (inputs_[ch]->isConnected())
            {
                value = inputs_[ch]->value();
            }
            auto& buf = channels_[ch];
            buf.data[buf.write_pos] = value;
            buf.write_pos = (buf.write_pos + 1) % capacity_;
            if (buf.count < capacity_)
            {
                ++buf.count;
            }
        }

        const std::size_t half_capacity = capacity_ / 2;
        if (!half_full_event_sent_ && half_capacity > 0 && !channels_.empty() && channels_.front().count >= half_capacity)
        {
            half_full_event_sent_ = true;
            notifyObserver(ElementEvent{
                .topic = "probe.buffer.half_full",
                .severity = ElementEventSeverity::Warning,
                .message = "Probe ring buffer reached 50% capacity"
            });
        }
    }

    [[nodiscard]] auto channelCount() const -> int
    {
        return channel_count_;
    }

    [[nodiscard]] auto capacity() const -> std::size_t
    {
        return capacity_;
    }

    [[nodiscard]] auto readChannel(int channel, std::size_t maxSamples) const -> std::vector<T>
    {
        if (channel < 0 || channel >= channel_count_)
        {
            return {};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto& buf = channels_[channel];
        const std::size_t n = std::min(maxSamples, buf.count);
        if (n == 0)
        {
            return {};
        }

        std::vector<T> result(n);
        const std::size_t start = (buf.write_pos + capacity_ - n) % capacity_;
        for (std::size_t i = 0; i < n; ++i)
        {
            result[i] = buf.data[(start + i) % capacity_];
        }
        return result;
    }

    [[nodiscard]] auto sampleCount(int channel) const -> std::size_t
    {
        if (channel < 0 || channel >= channel_count_)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return channels_[channel].count;
    }

    void clearBuffers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& ch : channels_)
        {
            ch.write_pos = 0;
            ch.count = 0;
        }
        half_full_event_sent_ = false;
    }

private:
    struct ChannelBuffer {
        std::vector<T> data;
        std::size_t write_pos = 0;
        std::size_t count = 0;
    };

    int channel_count_;
    std::size_t capacity_;
    bool half_full_event_sent_ = false;
    mutable std::mutex mutex_;
    std::vector<ChannelBuffer> channels_;
    std::vector<std::shared_ptr<TypedInput<T>>> inputs_;
};

using RealProbe = TypedProbe<Real>;
using ComplexProbe = TypedProbe<Complex>;
using Probe = RealProbe;

} // namespace s7

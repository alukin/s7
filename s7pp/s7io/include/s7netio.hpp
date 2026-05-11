//
// s7netio.hpp — Network I/O elements (WebSocket sink/source API)
//
// v0 uses a local Beast WebSocket server/client pair so the same element
// interface can be wired now and later pointed at a remote backend.
//

#pragma once

#include "s7.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s7 {

struct NetIOConfig {
    std::string endpoint = "ws://127.0.0.1:5577/s7/default";
    uint32_t num_channels = 1;
    uint32_t queue_capacity = 4096;
    bool zero_on_underrun = true;
};

class RealNetSink final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    RealNetSink(const std::string& id, const NetIOConfig& cfg);
    ~RealNetSink() override;

    void tick() override;

    [[nodiscard]] auto pushedSamples() const -> uint64_t { return pushed_samples_; }
    [[nodiscard]] auto droppedSamples() const -> uint64_t { return dropped_samples_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::shared_ptr<TypedInput<Real>>> inputs_;

    uint64_t pushed_samples_ = 0;
    uint64_t dropped_samples_ = 0;
};

class RealNetSource final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    RealNetSource(const std::string& id, const NetIOConfig& cfg);
    ~RealNetSource() override;

    void tick() override;

    [[nodiscard]] auto pulledSamples() const -> uint64_t { return pulled_samples_; }
    [[nodiscard]] auto underruns() const -> uint64_t { return underruns_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<Real> values_;

    uint64_t pulled_samples_ = 0;
    uint64_t underruns_ = 0;
};

class ComplexNetSink final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ComplexNetSink(const std::string& id, const NetIOConfig& cfg);
    ~ComplexNetSink() override;

    void tick() override;

    [[nodiscard]] auto pushedSamples() const -> uint64_t { return pushed_samples_; }
    [[nodiscard]] auto droppedSamples() const -> uint64_t { return dropped_samples_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::shared_ptr<TypedInput<Complex>>> inputs_;

    uint64_t pushed_samples_ = 0;
    uint64_t dropped_samples_ = 0;
};

class ComplexNetSource final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ComplexNetSource(const std::string& id, const NetIOConfig& cfg);
    ~ComplexNetSource() override;

    void tick() override;

    [[nodiscard]] auto pulledSamples() const -> uint64_t { return pulled_samples_; }
    [[nodiscard]] auto underruns() const -> uint64_t { return underruns_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<Complex> values_;

    uint64_t pulled_samples_ = 0;
    uint64_t underruns_ = 0;
};

} // namespace s7


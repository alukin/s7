//
// s7netio.hpp — Network I/O elements (RSocket-oriented sink/source API)
//
// v0 transport backend is in-process channel routing so the same element
// interface can be wired now and switched to socket transport later.
//

#pragma once

#include "s7.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s7 {

struct NetIOConfig {
    std::string endpoint = "rsocket://127.0.0.1:5577/s7/default";
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

    uint64_t pushedSamples() const { return pushed_samples_; }
    uint64_t droppedSamples() const { return dropped_samples_; }

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

    uint64_t pulledSamples() const { return pulled_samples_; }
    uint64_t underruns() const { return underruns_; }

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

    uint64_t pushedSamples() const { return pushed_samples_; }
    uint64_t droppedSamples() const { return dropped_samples_; }

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

    uint64_t pulledSamples() const { return pulled_samples_; }
    uint64_t underruns() const { return underruns_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<Complex> values_;

    uint64_t pulled_samples_ = 0;
    uint64_t underruns_ = 0;
};

} // namespace s7


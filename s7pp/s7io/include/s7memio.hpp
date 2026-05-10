//
// s7memio.hpp — Shared-memory I/O elements
//
// Supports:
// - real and complex stream classes
// - selectable scalar encoding for SDR-oriented formats
// - single-producer/single-consumer ring in POSIX shared memory
// - Linux baseline
//

#pragma once

#include "s7.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s7 {

enum class MemScalarEncoding : uint8_t {
    F32 = 0,
    S16 = 1,
    S8 = 2,
    U8 = 3,
};

enum class MemSignalDomain : uint8_t {
    REAL = 0,
    COMPLEX = 1,
};

struct MemIOConfig {
    std::string shm_name = "/s7_mem_stream";
    uint32_t num_channels = 1;
    uint32_t block_size = 64;        // samples per channel per ring slot
    uint32_t capacity_blocks = 64;   // number of slots in ring
    double sample_rate_hz = 48000.0;
    MemScalarEncoding encoding = MemScalarEncoding::F32;
    bool create_if_missing = true;   // sink should create; source can attach/create
    bool unlink_on_destroy = false;  // useful for tests
    bool zero_on_underrun = true;    // source policy when no data available
};

class RealMemSink final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    RealMemSink(const std::string& id, const MemIOConfig& cfg);
    ~RealMemSink() override;

    void tick() override;

    uint64_t pushedBlocks() const { return pushed_blocks_; }
    uint64_t droppedBlocks() const { return dropped_blocks_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<std::shared_ptr<TypedInput<Real>>> inputs_;
    std::vector<uint8_t> staging_; // encoded interleaved block bytes
    uint32_t staging_pos_ = 0;

    uint64_t pushed_blocks_ = 0;
    uint64_t dropped_blocks_ = 0;
};

class RealMemSource final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    RealMemSource(const std::string& id, const MemIOConfig& cfg);
    ~RealMemSource() override;

    void tick() override;

    uint64_t pulledBlocks() const { return pulled_blocks_; }
    uint64_t underruns() const { return underruns_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<Real> outputs_values_;
    std::vector<uint8_t> block_buffer_;
    uint32_t block_pos_ = 0;

    MemIOConfig cfg_;

    uint64_t pulled_blocks_ = 0;
    uint64_t underruns_ = 0;
};

class ComplexMemSink final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ComplexMemSink(const std::string& id, const MemIOConfig& cfg);
    ~ComplexMemSink() override;

    void tick() override;

    uint64_t pushedBlocks() const { return pushed_blocks_; }
    uint64_t droppedBlocks() const { return dropped_blocks_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<std::shared_ptr<TypedInput<Complex>>> inputs_;
    std::vector<uint8_t> staging_;
    uint32_t staging_pos_ = 0;

    uint64_t pushed_blocks_ = 0;
    uint64_t dropped_blocks_ = 0;
};

class ComplexMemSource final : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ComplexMemSource(const std::string& id, const MemIOConfig& cfg);
    ~ComplexMemSource() override;

    void tick() override;

    uint64_t pulledBlocks() const { return pulled_blocks_; }
    uint64_t underruns() const { return underruns_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<Complex> outputs_values_;
    std::vector<uint8_t> block_buffer_;
    uint32_t block_pos_ = 0;

    MemIOConfig cfg_;

    uint64_t pulled_blocks_ = 0;
    uint64_t underruns_ = 0;
};

} // namespace s7


// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7OBSERVE_INCLUDE_S7_OBSERVE_HPP
#define S7OBSERVE_INCLUDE_S7_OBSERVE_HPP

#include <array>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "s7/core.hpp"

namespace s7 {

struct ProbePoint {
    std::string element_id;
    std::size_t port{};
};

// The value of one output after step `step`.
//
// Time: `step` orders samples; `model_time_ns` is the model time of that step
// when the runtime has a model-time base. `model_time_seconds` avoids ns
// quantization for numerical recording; both derive directly from step/rate,
// otherwise unavailable. Local observation has no source or processing
// timestamps: those belong to data-stream packets (wire protocol section 6)
// and become observable only through network elements that expose them.
//
// Type: `type` is the port's SignalType and the value is kept in its native
// storage. Read it exactly with as<Type>() (as_real() / as_complex() for F64 /
// C64). to_display_double() is an explicit, lossy conversion for display only.
struct ObservationSample {
    std::size_t step{};
    std::optional<std::int64_t> model_time_ns;
    std::optional<double> model_time_seconds;
    std::string element_id;
    std::size_t port{};
    SignalType type{SignalType::F64};
    std::array<std::byte, 32> storage{}; // the value's bytes, signal_type_info(type).storage_bytes long

    template <SignalType Type>
    [[nodiscard]] signal_storage_t<Type> as() const {
        if (type != Type) {
            throw std::invalid_argument("ObservationSample has type " + std::string(signal_type_name(type)) +
                                        ", not " + std::string(signal_type_name(Type)) + ".");
        }
        signal_storage_t<Type> value{};
        std::memcpy(&value, storage.data(), sizeof(value));
        return value;
    }

    [[nodiscard]] RealValue as_real() const { return as<SignalType::F64>(); }
    [[nodiscard]] ComplexValue as_complex() const { return as<SignalType::C64>(); }
    [[nodiscard]] bool is_complex() const { return is_complex_signal_type(type); }

    // Any real (non-complex) value as a double, for plots and logs. Rounds
    // values that a double cannot hold exactly (64/128-bit integers, F128).
    [[nodiscard]] double to_display_double() const;
};

// Values of several outputs taken between two steps: every sample belongs to
// the step `step`.
struct ObservationSnapshot {
    std::size_t step{};
    std::optional<std::int64_t> model_time_ns;
    std::optional<double> model_time_seconds;
    std::vector<ObservationSample> samples;
};

class CompiledProbes;

// Reads a runtime's outputs without changing it.
//
// Threading contract: observation runs between steps on the thread that steps
// the runtime, so it never races with step() and a snapshot is coherent. Calls
// from any other thread (once the runtime has stepped) throw std::logic_error.
// Code that observes from another thread must hand samples over itself, as
// Studio's local runner does with its sample mailbox.
class ObservationService {
public:
    explicit ObservationService(const NodeRuntime& runtime);

    [[nodiscard]] ObservationSample sample_output(const std::string& element_id, std::size_t port = 0) const;
    // Experimental direct arrays. Samples carry the position of their rate
    // domain. The admission bounds how many leases a reader may keep.
    [[nodiscard]] std::expected<CommittedArraySample, ArrayPoolError> try_retain_array_output(
        const ProbePoint& probe, const ArrayPool::Retention& retention) const;
    [[nodiscard]] ObservationSnapshot snapshot(const std::vector<ProbePoint>& probes) const;
    // Resolves probes once, for reading on every step without lookups or
    // allocation. Unknown outputs throw std::out_of_range here, not later.
    [[nodiscard]] CompiledProbes compile(const std::vector<ProbePoint>& probes) const;

private:
    void require_stepping_thread() const;
    [[nodiscard]] ObservationSample read(const std::string& element_id, std::size_t port) const;

    const NodeRuntime* runtime_;
};

// Probes resolved to their output storage (ObservationService::compile).
// Reading copies values directly from that storage: no id lookups, strings or
// allocation per step. The storage addresses stay valid for the runtime's
// lifetime, so a CompiledProbes must not outlive the runtime it was compiled
// from. The same threading contract applies: read between steps, on the
// stepping thread.
class CompiledProbes {
public:
    struct Point {
        const void* value{};
        SignalType type{SignalType::F64};
        // Explicit, lossy conversion for display; null for complex types.
        double (*display)(const void*){};
    };

    [[nodiscard]] std::size_t size() const { return points_.size(); }
    [[nodiscard]] const Point& point(std::size_t index) const { return points_.at(index); }
    // True when every probe has a real (non-complex) type.
    [[nodiscard]] bool displayable() const { return displayable_; }

    // Step and model time of the values read now (see ObservationSample).
    [[nodiscard]] std::size_t step() const { return runtime_->step_count(); }
    [[nodiscard]] std::optional<double> model_time_seconds() const { return runtime_->model_time_seconds(); }

    // Writes probe i's display value to out[i], for every probe. Requires
    // displayable() and out.size() == size().
    void read_display(std::span<double> out) const;
    // Copies probe `index`'s native storage (storage_bytes of its type).
    void read(std::size_t index, void* destination) const;

private:
    friend class ObservationService;
    CompiledProbes(const NodeRuntime& runtime, std::vector<Point> points);
    void require_stepping_thread() const;

    const NodeRuntime* runtime_;
    std::vector<Point> points_;
    bool displayable_{true};
};

} // namespace s7

#endif // S7OBSERVE_INCLUDE_S7_OBSERVE_HPP

#ifndef S7OBSERVE_INCLUDE_S7_OBSERVE_HPP
#define S7OBSERVE_INCLUDE_S7_OBSERVE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
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
// when the runtime has a model-time base (NodeRuntime::set_model_time_step_ns),
// otherwise unavailable. Local observation has no source or processing
// timestamps: those belong to data-stream packets (wire protocol section 6)
// and become observable only through network elements that expose them.
//
// Type: `type` is the port's SignalType. DOUBLE and the integer types are read
// with as_real() (integer types hold exact integers, see signal_type_info()) or
// as_integer(); COMPLEX with as_complex().
struct ObservationSample {
    std::size_t step{};
    std::optional<std::int64_t> model_time_ns;
    std::string element_id;
    std::size_t port{};
    SignalType type{SignalType::DOUBLE};
    RealValue real_value{};
    ComplexValue complex_value{};

    [[nodiscard]] bool is_complex() const {
        return is_complex_signal_type(type);
    }

    [[nodiscard]] RealValue as_real() const {
        if (is_complex()) {
            throw std::invalid_argument("ObservationSample does not contain a real-domain value.");
        }
        return real_value;
    }

    [[nodiscard]] std::int64_t as_integer() const {
        if (!signal_type_info(type).is_integer) {
            throw std::invalid_argument("ObservationSample does not contain an integer value.");
        }
        return static_cast<std::int64_t>(real_value);
    }

    [[nodiscard]] ComplexValue as_complex() const {
        if (!is_complex()) {
            throw std::invalid_argument("ObservationSample does not contain a complex value.");
        }
        return complex_value;
    }
};

// Values of several outputs taken between two steps: every sample belongs to
// the step `step`.
struct ObservationSnapshot {
    std::size_t step{};
    std::optional<std::int64_t> model_time_ns;
    std::vector<ObservationSample> samples;
};

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
    [[nodiscard]] ObservationSnapshot snapshot(const std::vector<ProbePoint>& probes) const;

private:
    void require_stepping_thread() const;
    [[nodiscard]] ObservationSample read(const std::string& element_id, std::size_t port) const;

    const NodeRuntime* runtime_;
};

} // namespace s7

#endif // S7OBSERVE_INCLUDE_S7_OBSERVE_HPP

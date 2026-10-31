// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_RUNTIME_HPP
#define S7CORE_INCLUDE_S7_RUNTIME_HPP

#include "s7/graph.hpp"
#include "s7/rate.hpp"
#include "s7/trapezoid.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace s7 {

// Not thread-safe: step() and every read of outputs or step_count() must run on
// one thread at a time (normally the thread that steps the runtime).
class NodeRuntime {
public:
    explicit NodeRuntime(ExecutableGraph executable_graph);

    void step();
    // The thread that last called step() (default-constructed before the first step).
    [[nodiscard]] std::thread::id stepping_thread() const;

    // Model time advanced by one step, in nanoseconds. Unset means the graph
    // has no model-time base (steps are only ordered, not timed).
    void set_model_time_step_ns(std::optional<std::int64_t> step_ns);
    // Local rational model-time base; rejects conflicting declared element rates.
    // Does not change scheduling or element dt.
    void set_model_time_rate(std::optional<Rate> rate);
    // Absolute step/rate, rounded only once to ns; throws on int64 overflow.
    [[nodiscard]] std::optional<std::int64_t> model_time_ns() const;
    [[nodiscard]] std::optional<double> model_time_seconds() const;
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] const std::string& schema_instance_uuid() const;
    [[nodiscard]] const std::string& element_instance_uuid_of(const std::string& element_id) const;
    [[nodiscard]] const std::string& element_id_of_instance_uuid(const std::string& element_instance_uuid) const;
    [[nodiscard]] SignalType output_type_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] SignalType output_type_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    // Typed output read; throws std::invalid_argument if the port has another type.
    template <SignalType Type>
    [[nodiscard]] signal_storage_t<Type> output_of(const std::string& element_id, std::size_t port = 0) const
    {
        require_output_type(element_id, port, Type);
        return *static_cast<const signal_storage_t<Type>*>(output_binding_of(element_id, port));
    }
    // Address of an output's storage, valid between steps on the stepping thread.
    [[nodiscard]] OutputBinding output_binding_of(const std::string& element_id, std::size_t port) const;
    // F64 / C64 shorthands for output_of<F64> / output_of<C64>.
    [[nodiscard]] RealValue real_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] RealValue real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] std::size_t step_count() const;

private:
    // Output of a causal-memory element. Its step() writes the next step's
    // value; the runtime stages it and publishes it at the start of the next
    // step, so all outputs read after step() belong to the same step.
    // Raw copies of the output's storage (at most 32 bytes: CI128/C128).
    // staged[staged_index_] holds the values to publish next; a step stages
    // into the other buffer and switches only after every memory element and
    // coupled trial succeeded, so a failed step keeps the earlier values.
    struct MemoryOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> snapshot{};
        std::array<std::byte, 32> before_publish{};
        std::array<std::array<std::byte, 32>, 2> staged{};
    };

    struct MemoryOutputRange {
        std::size_t begin{};
        std::size_t end{};
    };

    // Output of a causal-order element, snapshotted before causal_order runs
    // so a throwing element leaves no torn step: either every causal output
    // reflects this step, or (on exception) none of them do.
    struct CausalOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> snapshot{};
    };

    struct TrapezoidStage {
        TrapezoidIntegration* integration{};
        MemoryOutputRange outputs;
    };
    // Temporary trial storage, restored before any ordinary memory step or
    // observation. Solver failure restores it before propagating the error.
    struct TrialOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> base{};
    };
    void stage_trapezoids();
    // Restores every published output to its pre-step value after a failed step.
    void roll_back_step(bool published_memory);

    void require_output_type(const std::string& element_id, std::size_t port, SignalType expected) const;

    ExecutableGraph graph_;
    std::vector<MemoryOutput> memory_outputs_;
    std::vector<MemoryOutputRange> memory_output_ranges_;
    std::vector<CausalOutput> causal_outputs_;
    std::vector<TrapezoidStage> trapezoids_;
    std::vector<TrialOutput> trial_outputs_;
    std::vector<bool> implicit_memory_;
    bool memory_outputs_staged_{};
    std::size_t staged_index_{};
    std::atomic<std::thread::id> stepping_thread_{};
    std::optional<Rate> model_time_rate_;
    std::size_t step_count_{};
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RUNTIME_HPP

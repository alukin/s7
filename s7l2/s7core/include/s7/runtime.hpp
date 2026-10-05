#ifndef S7CORE_INCLUDE_S7_RUNTIME_HPP
#define S7CORE_INCLUDE_S7_RUNTIME_HPP

#include "s7/graph.hpp"

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
    // step_count() * step, or nullopt without a model-time base.
    [[nodiscard]] std::optional<std::int64_t> model_time_ns() const;
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
    struct MemoryOutput {
        void* value_ptr{};
        std::size_t bytes{};
        std::array<std::byte, 32> snapshot{};
        std::array<std::byte, 32> staged{};
    };

    struct MemoryOutputRange {
        std::size_t begin{};
        std::size_t end{};
    };

    void require_output_type(const std::string& element_id, std::size_t port, SignalType expected) const;

    ExecutableGraph graph_;
    std::vector<MemoryOutput> memory_outputs_;
    std::vector<MemoryOutputRange> memory_output_ranges_;
    bool memory_outputs_staged_{};
    std::atomic<std::thread::id> stepping_thread_{};
    std::optional<std::int64_t> model_time_step_ns_;
    std::size_t step_count_{};
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RUNTIME_HPP

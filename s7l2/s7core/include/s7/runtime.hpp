#ifndef S7CORE_INCLUDE_S7_RUNTIME_HPP
#define S7CORE_INCLUDE_S7_RUNTIME_HPP

#include "s7/graph.hpp"

#include <atomic>
#include <cstddef>
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
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] const std::string& schema_instance_uuid() const;
    [[nodiscard]] const std::string& element_instance_uuid_of(const std::string& element_id) const;
    [[nodiscard]] const std::string& element_id_of_instance_uuid(const std::string& element_instance_uuid) const;
    [[nodiscard]] SignalType output_type_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] SignalType output_type_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] RealValue real_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] RealValue real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port = 0) const;
    [[nodiscard]] std::size_t step_count() const;

private:
    // Output of a causal-memory element. Its step() writes the next step's
    // value; the runtime stages it and publishes it at the start of the next
    // step, so all outputs read after step() belong to the same step.
    struct MemoryOutput {
        void* value_ptr{};
        SignalType type{SignalType::DOUBLE};
        RealValue snapshot_real{};
        RealValue staged_real{};
        ComplexValue snapshot_complex{};
        ComplexValue staged_complex{};
    };

    struct MemoryOutputRange {
        std::size_t begin{};
        std::size_t end{};
    };

    [[nodiscard]] OutputBinding output_binding_of(const std::string& element_id, std::size_t port) const;

    ExecutableGraph graph_;
    std::vector<MemoryOutput> memory_outputs_;
    std::vector<MemoryOutputRange> memory_output_ranges_;
    bool memory_outputs_staged_{};
    std::atomic<std::thread::id> stepping_thread_{};
    std::size_t step_count_{};
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RUNTIME_HPP

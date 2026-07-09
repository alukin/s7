#ifndef S7CORE_INCLUDE_S7_RUNTIME_HPP
#define S7CORE_INCLUDE_S7_RUNTIME_HPP

#include "s7/graph.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {

class NodeRuntime {
public:
    explicit NodeRuntime(ExecutableGraph executable_graph);

    void step();
    [[nodiscard]] SignalType output_type_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] RealValue real_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] ComplexValue complex_output_of(const std::string& element_id, std::size_t port = 0) const;
    [[nodiscard]] std::size_t step_count() const;

private:
    [[nodiscard]] OutputBinding output_binding_of(const std::string& element_id, std::size_t port) const;

    ExecutableGraph graph_;
    std::size_t step_count_{};
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RUNTIME_HPP
